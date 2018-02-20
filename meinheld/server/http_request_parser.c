#include "http_request_parser.h"
#include "http_parser.h"
#include "server.h"
#include "response.h"
#include "input.h"
#include "util.h"

#define MAXFREELIST 1024

/**
 * environ spec.
 *
 * REQUEST_METHOD
 *   The HTTP request method, such as "GET" or "POST". This cannot ever be an empty string, and so is always required.
 *
 * SCRIPT_NAME
 *   The initial portion of the request URL's "path" that corresponds to the application object,
 *   so that the application knows its virtual "location".
 *   This may be an empty string, if the application corresponds to the "root" of the server.
 *
 * PATH_INFO
 *   The remainder of the request URL's "path", designating the virtual "location" of the request's target within the application.
 *   This may be an empty string, if the request URL targets the application root and does not have a trailing slash.
 *
 * QUERY_STRING
 *   The portion of the request URL that follows the "?", if any. May be empty or absent.
 *
 * CONTENT_TYPE
 *   The contents of any Content-Type fields in the HTTP request. May be empty or absent.
 *
 * CONTENT_LENGTH
 *   The contents of any Content-Length fields in the HTTP request. May be empty or absent.
 *
 * SERVER_NAME, SERVER_PORT
 *    When combined with SCRIPT_NAME and PATH_INFO, these variables can be used to complete the URL. Note, however, that HTTP_HOST,
 *    if present, should be used in preference to SERVER_NAME for reconstructing the request URL.
 *    See the URL Reconstruction section below for more detail.
 *    SERVER_NAME and SERVER_PORT can never be empty strings, and so are always required.
 *
 * SERVER_PROTOCOL
 *   The version of the protocol the client used to send the request.
 *   Typically this will be something like "HTTP/1.0" or "HTTP/1.1" and may be used by the application to determine
 *   how to treat any HTTP request headers.
 *   (This variable should probably be called REQUEST_PROTOCOL, since it denotes the protocol used in the request,
 *   and is not necessarily the protocol that will be used in the server's response.
 *   However, for compatibility with CGI we have to keep the existing name.)
 *
 * HTTP_ Variables
 *   Variables corresponding to the client-supplied HTTP request headers (i.e., variables whose names begin with "HTTP_").
 *   The presence or absence of these variables should correspond with the presence or absence of the appropriate
 *   HTTP header in the request.
 *
 */

static int prefix_len;

static PyObject *empty_string;

static PyObject *version_key;
static PyObject *version_val;
static PyObject *scheme_key;
static PyObject *scheme_val;
static PyObject *errors_key;
static PyObject *errors_val;
static PyObject *multithread_key;
static PyObject *multithread_val;
static PyObject *multiprocess_key;
static PyObject *multiprocess_val;
static PyObject *run_once_key;
static PyObject *run_once_val;
static PyObject *file_wrapper_key;
static PyObject *file_wrapper_val;
static PyObject *wsgi_input_key;

static PyObject *script_key;
static PyObject *server_name_key;
static PyObject *server_name_val;
static PyObject *server_port_key;
static PyObject *server_port_val;
static PyObject *remote_addr_key;
static PyObject *remote_port_key;

static PyObject *server_protocol_key;
static PyObject *path_info_key;
static PyObject *query_string_key;
static PyObject *request_method_key;
static PyObject *client_key;

static PyObject *content_type_key;
static PyObject *content_length_key;
static PyObject *h_content_type_key;
static PyObject *h_content_length_key;

static PyObject *server_protocol_val10;
static PyObject *server_protocol_val11;

static PyObject *http_method_delete;
static PyObject *http_method_get;
static PyObject *http_method_head;
static PyObject *http_method_post;
static PyObject *http_method_put;
static PyObject *http_method_patch;
static PyObject *http_method_connect;
static PyObject *http_method_options;
static PyObject *http_method_trace;
static PyObject *http_method_copy;
static PyObject *http_method_lock;
static PyObject *http_method_mkcol;
static PyObject *http_method_move;
static PyObject *http_method_propfind;
static PyObject *http_method_proppatch;
static PyObject *http_method_unlock;
static PyObject *http_method_report;
static PyObject *http_method_mkactivity;
static PyObject *http_method_checkout;
static PyObject *http_method_merge;

static http_parser *http_parser_free_list[MAXFREELIST];
static int numfree = 0;

void
parser_list_fill(void)
{
    http_parser *p;

    while (numfree < MAXFREELIST) {
        p = (http_parser*)PyMem_Malloc(sizeof(http_parser));
        http_parser_free_list[numfree++] = p;
    }
}

void
parser_list_clear(void)
{
    http_parser *p;

    while (numfree) {
        p = http_parser_free_list[--numfree];
        PyMem_Free(p);
    }
}

static http_parser*
alloc_parser(void)
{
    http_parser *p;
    if (numfree) {
        p = http_parser_free_list[--numfree];
        GDEBUG("use pooled %p", p);
    }else{
        p = (http_parser*)PyMem_Malloc(sizeof(http_parser));
        GDEBUG("alloc %p", p);
    }
    memset(p, 0, sizeof(http_parser));
    return p;
}

void
dealloc_parser(http_parser *p)
{
    if (numfree < MAXFREELIST){
        http_parser_free_list[numfree++] = p;
        GDEBUG("back to pool %p", p);
    }else{
        PyMem_Free(p);
    }
}

PyObject*
new_environ(client_t *client)
{
    PyObject *object, *environ;

    environ = PyDict_New();
    PyDict_SetItem(environ, version_key, version_val);
    PyDict_SetItem(environ, scheme_key, scheme_val);
    PyDict_SetItem(environ, errors_key, errors_val);
    PyDict_SetItem(environ, multithread_key, multithread_val);
    PyDict_SetItem(environ, multiprocess_key, multiprocess_val);
    PyDict_SetItem(environ, run_once_key, run_once_val);
    PyDict_SetItem(environ, script_key, empty_string);
    PyDict_SetItem(environ, server_name_key, server_name_val);
    PyDict_SetItem(environ, server_port_key, server_port_val);
    PyDict_SetItem(environ, file_wrapper_key, file_wrapper_val);

    object = NATIVE_FROMSTRING(client->remote_addr);
    PyDict_SetItem(environ, remote_addr_key, object);
    Py_DECREF(object);

    object = NATIVE_FROMFORMAT("%d", client->remote_port);
    PyDict_SetItem(environ, remote_port_key, object);
    Py_DECREF(object);
    return environ;
}

static int
hex2int(int i)
{
    i = toupper(i);
    i = i - '0';
    if(i > 9){
        i = i - 'A' + '9' + 1;
    }
    return i;
}

static int
urldecode(char *buf, int len)
{
    int c, c1;
    char *s0, *t;
    t = s0 = buf;
    while(len >0){
        c = *buf++;
        if(c == '%' && len > 2){
            c = *buf++;
            c1 = c;
            c = *buf++;
            c = hex2int(c1) * 16 + hex2int(c);
            len -= 2;
        }
        *t++ = c;
        len--;
    }
    *t = 0;
    return t - s0;
}

static PyObject*
concat_string(PyObject *o, const char *buf, size_t len)
{
    PyObject *ret;
    size_t l;
    char *dest, *origin;
    
    l = PyBytes_GET_SIZE(o);

    ret = PyBytes_FromStringAndSize((char*)0, l + len);
    if(ret == NULL){
        return ret;
    }
    dest = PyBytes_AS_STRING(ret);
    origin = PyBytes_AS_STRING(o);
    memcpy(dest, origin , l);
    memcpy(dest + l, buf , len);
    Py_DECREF(o);
    return ret;
}

static int
replace_env_key(PyObject* dict, PyObject* old_key, PyObject* new_key)
{
    int ret = 1;

    PyObject* value = PyDict_GetItem(dict, old_key);
    if(value) {
        Py_INCREF(value);
        ret = PyDict_DelItem(dict, old_key);
        if(ret == -1){
            return ret;
        }
        ret = PyDict_SetItem(dict, new_key, value);
        Py_DECREF(value);
    }
    return ret;
}


static int
set_query(PyObject *env, char *buf, int len)
{
    int c, ret, slen = 0;
    char *s0;
    PyObject *obj;
#ifdef PY3
    char *c2;
    PyObject *v;
#endif
    s0 = buf;
    while(len > 0){
        c = *buf++;
        if(c == '#'){
            slen++;
            break;
        }
        len--;
        slen++;
    }

    if(slen > 1){
        obj = PyBytes_FromStringAndSize(s0, slen -1);
        /* DEBUG("query:%.*s", len, PyBytes_AS_STRING(obj)); */
        if(unlikely(obj == NULL)){
            return -1;
        }
        
#ifdef PY3
        //TODO CHECK ERROR 
        c2 = PyBytes_AS_STRING(obj);
        v = PyUnicode_DecodeLatin1(c2, strlen(c2), NULL);
        ret = PyDict_SetItem(env, query_string_key, v);
        Py_DECREF(v);
#else
        ret = PyDict_SetItem(env, query_string_key, obj);
#endif
        Py_DECREF(obj);
        
        if(unlikely(ret == -1)){
            return -1;
        }
    }
    
    return 1; 
}


static int
set_path(PyObject *env, char *buf, int len)
{
    int c, c1, slen;
    char *s0, *t;
    PyObject *obj;

    t = s0 = buf;
    while(len > 0){
        c = *buf++;
        if(c == '%' && len > 2){
            c = *buf++;
            c1 = c;
            c = *buf++;
            c = hex2int(c1) * 16 + hex2int(c);
            len -= 2;
        }else if(c == '?'){
            //stop
            if(set_query(env, buf, len) == -1){
                //Error
                return -1;
            }
            break;
        }else if(c == '#'){
            //stop 
            //ignore fragment
            break;
        }
        *t++ = c;
        len--;
    }
    //*t = 0;
    slen = t - s0;
    slen = urldecode(s0, slen);

#ifdef PY3
    obj = PyUnicode_DecodeLatin1(s0, slen, "strict");
#else
    obj = PyBytes_FromStringAndSize(s0, slen);
#endif
    if (likely(obj != NULL)) {
        PyDict_SetItem(env, path_info_key, obj);
        Py_DECREF(obj);
        return slen;
    } else {
        return -1;
    }
}

static PyObject*
get_http_header_key(const char *s, int len)
{
    PyObject *obj;
    char *dest;
    char c;

    obj = PyBytes_FromStringAndSize(NULL, len + prefix_len);
    dest = (char*)PyBytes_AS_STRING(obj);

    *dest++ = 'H';
    *dest++ = 'T';
    *dest++ = 'T';
    *dest++ = 'P';
    *dest++ = '_';

    while(len--) {
        c = *s++;
        if(c == '-'){
            *dest++ = '_';
        }else if(c >= 'a' && c <= 'z'){
            *dest++ = c - ('a'-'A');
        }else{
            *dest++ = c;
        }
    }

    return obj;
}

static void
key_upper(char *s, const char *key, size_t len)
{
    int i = 0;
    int c;
    for (i = 0; i < len; i++) {
        c = key[i];
        if(c == '-'){
            s[i] = '_';
        }else{
            if(islower(c)){
                s[i] = toupper(c);
            }else{
                s[i] = c;
            }
        }
    }
}

static int
write_body2file(request *req, const char *buffer, size_t buffer_len)
{

    FILE *tmp = (FILE *)req->body;
    fwrite(buffer, 1, buffer_len, tmp);
    req->body_readed += buffer_len;
    DEBUG("write_body2file %d bytes", (int)buffer_len);
    return req->body_readed;

}

static int
write_body2mem(request *req, const char *buf, size_t buf_len)
{
    buffer_t *body = (buffer_t*)req->body;
    write2buf(body, buf, buf_len);

    req->body_readed += buf_len;
    DEBUG("write_body2mem %d bytes", (int)buf_len);
    return req->body_readed;
}

static int
write_body(request *req, const char *buffer, size_t buffer_len)
{

    if(req->body_type == BODY_TYPE_TMPFILE){
        return write_body2file(req, buffer, buffer_len);
    }else{
        return write_body2mem(req, buffer, buffer_len);
    }
}

static client_t *
get_client(http_parser *p)
{
    return (client_t *)p->data;
}

static request *
get_current_request(http_parser *p)
{
    client_t *client =  (client_t *)p->data;
    return client->current_req;
}

static int
message_begin_cb(http_parser *p)
{
    request *req = NULL;
    PyObject *environ = NULL;
    client_t *client = get_client(p);

    DEBUG("message_begin_cb");

    req = new_request();
    if(req == NULL){
        return -1;
    }
    req->start_msec = current_msec;
    client->current_req = req;
    environ = new_environ(client);
    client->complete = 0;
    /* client->bad_request_code = 0; */
    /* client->body_type = BODY_TYPE_NONE; */
    /* client->body_readed = 0; */
    /* client->body_length = 0; */
    req->environ = environ;
    push_request(client->request_queue, client->current_req);
    return 0;
}


static int
header_field_cb(http_parser *p, const char *buf, size_t len)
{
    request *req = get_current_request(p);
    PyObject *env = NULL, *obj = NULL;
#ifdef PY3
    char *c1, *c2;
    PyObject *f, *v;
#endif
    /* DEBUG("field key:%.*s", (int)len, buf); */

    if(req->last_header_element != FIELD){
        env = req->environ;
        if(LIMIT_REQUEST_FIELDS <= req->num_headers){
            req->bad_request_code = 400;
            return -1;
        }
#ifdef PY3
        //TODO CHECK ERROR 
        c1 = PyBytes_AS_STRING(req->field);
        f = PyUnicode_DecodeLatin1(c1, strlen(c1), NULL);
        c2 = PyBytes_AS_STRING(req->value);
        v = PyUnicode_DecodeLatin1(c2, strlen(c2), NULL);
        PyDict_SetItem(env, f, v);
        Py_DECREF(f);
        Py_DECREF(v);
#else
        PyDict_SetItem(env, req->field, req->value);
#endif
        Py_DECREF(req->field);
        Py_DECREF(req->value);
        req->field = NULL;
        req->value = NULL;
        req->num_headers++;
    }

    if(likely(req->field == NULL)){
        obj = get_http_header_key(buf, len);
    }else{
        char temp[len];
        key_upper(temp, buf, len);
        obj = concat_string(req->field, temp, len);
    }

    if(unlikely(obj == NULL)){
        req->bad_request_code = 500;
        return -1;
    }
    if(unlikely(PyBytes_GET_SIZE(obj) > LIMIT_REQUEST_FIELD_SIZE)){
        req->bad_request_code = 400;
        return -1;
    }

    req->field = obj;
    req->last_header_element = FIELD;
    return 0;
}

static int
header_value_cb(http_parser *p, const char *buf, size_t len)
{
    request *req = get_current_request(p);
    PyObject *obj;

    /* DEBUG("field value:%.*s", (int)len, buf); */
    if(likely(req->value== NULL)){
        obj = PyBytes_FromStringAndSize(buf, len);
    }else{
        obj = concat_string(req->value, buf, len);
    }

    if(unlikely(obj == NULL)){
        req->bad_request_code = 500;
        return -1; 
    }
    if(unlikely(PyBytes_GET_SIZE(obj) > LIMIT_REQUEST_FIELD_SIZE)){
        req->bad_request_code = 400;
        return -1;
    }

    req->value = obj;
    req->last_header_element = VALUE;
    return 0;
}

static int
url_cb(http_parser *p, const char *buf, size_t len)
{
    request *req = get_current_request(p);
    buffer_result ret = MEMORY_ERROR;

    if(unlikely(req->path)){
        ret = write2buf(req->path, buf, len);
    }else{
        req->path = new_buffer(1024, LIMIT_PATH);
        ret = write2buf(req->path, buf, len);
    }
    switch(ret){
        case MEMORY_ERROR:
            req->bad_request_code = 500;
            return -1;
        case LIMIT_OVER:
            req->bad_request_code = 400;
            return -1;
        default:
            break;
    }


    return 0;
}


static int
body_cb(http_parser *p, const char *buf, size_t len)
{
    request *req = get_current_request(p);
    DEBUG("body_cb");

    if(max_content_length < req->body_readed + len){

        DEBUG("set request code %d", 413);
        req->bad_request_code = 413;
        return -1;
    }
    if(req->body_type == BODY_TYPE_NONE){
        if(req->body_length == 0 && !(p->flags & F_CHUNKED)){
            //Length Required
            DEBUG("set request code %d", 411);
            req->bad_request_code = 411;
            return -1;
        }
        if(req->body_length > client_body_buffer_size){
            //large size request
            FILE *tmp = tmpfile();
            if(tmp < 0){
                req->bad_request_code = 500;
                return -1;
            }

            req->body = tmp;
            req->body_type = BODY_TYPE_TMPFILE;
            DEBUG("BODY_TYPE_TMPFILE");
        }else{
            //default memory stream
            DEBUG("client->body_length %d", req->body_length);
            req->body = new_buffer(req->body_length, 0);
            req->body_type = BODY_TYPE_BUFFER;
            DEBUG("BODY_TYPE_BUFFER");
        }
    }
    write_body(req, buf, len);
    return 0;
}

int
headers_complete_cb(http_parser *p)
{
    PyObject *obj;
    int ret;
    uint64_t content_length = 0;

    client_t *client = get_client(p);
    request *req = client->current_req;
    PyObject *env = req->environ;
    
    DEBUG("should keep alive %d", http_should_keep_alive(p));
    client->keep_alive = http_should_keep_alive(p);

    if(p->content_length != ULLONG_MAX){
        content_length = p->content_length;
        if(max_content_length < content_length){
            RDEBUG("max_content_length over %d/%d", (int)content_length, (int)max_content_length);
            DEBUG("set request code %d", 413);
            req->bad_request_code = 413;
            return -1;
        }
    }

    if(p->http_major == 1 && p->http_minor == 1){
        obj = server_protocol_val11;
    }else{
        obj = server_protocol_val10;
    }

    ret = PyDict_SetItem(env, server_protocol_key, obj);
    if(unlikely(ret == -1)){
        return -1;
    }

    if(likely(req->path)){
        ret = set_path(env, req->path->buf, req->path->len);
        free_buffer(req->path);
        if(unlikely(ret == -1)){
           //TODO Error 
           return -1;
        }
    }else{
        ret = PyDict_SetItem(env, path_info_key, empty_string);
        if(ret == -1){
            return -1;
        }
    }
    req->path = NULL;

    //Last header
    if(likely(req->field && req->value)){

#ifdef PY3
        //TODO CHECK ERROR 
        char *c1 = PyBytes_AS_STRING(req->field);
        PyObject *f = PyUnicode_DecodeLatin1(c1, strlen(c1), NULL);
        char *c2 = PyBytes_AS_STRING(req->value);
        PyObject *v = PyUnicode_DecodeLatin1(c2, strlen(c2), NULL);
        PyDict_SetItem(env, f, v);
        Py_DECREF(f);
        Py_DECREF(v);
#else
        PyDict_SetItem(env, req->field, req->value);
#endif
        Py_DECREF(req->field);
        Py_DECREF(req->value);
        req->field = NULL;
        req->value = NULL;
        if(unlikely(ret == -1)){
            return -1;
        }
    }
    ret = replace_env_key(env, h_content_type_key, content_type_key);
    if(unlikely(ret == -1)){
        return -1;
    }
    ret = replace_env_key(env, h_content_length_key, content_length_key);
    if(unlikely(ret == -1)){
        return -1;
    }

    switch(p->method){
        case HTTP_DELETE:
            obj = http_method_delete;
            break;
        case HTTP_GET:
            obj = http_method_get;
            break;
        case HTTP_HEAD:
            obj = http_method_head;
            break;
        case HTTP_POST:
            obj = http_method_post;
            break;
        case HTTP_PUT:
            obj = http_method_put;
            break;
        case HTTP_PATCH:
            obj = http_method_patch;
            break;
        case HTTP_CONNECT:
            obj = http_method_connect;
            break;
        case HTTP_OPTIONS:
            obj = http_method_options;
            break;
        case  HTTP_TRACE:
            obj = http_method_trace;
            break;
        case HTTP_COPY:
            obj = http_method_copy;
            break;
        case HTTP_LOCK:
            obj = http_method_lock;
            break;
        case HTTP_MKCOL:
            obj = http_method_mkcol;
            break;
        case HTTP_MOVE:
            obj = http_method_move;
            break;
        case HTTP_PROPFIND:
            obj = http_method_propfind;
            break;
        case HTTP_PROPPATCH:
            obj = http_method_proppatch;
            break;
        case HTTP_UNLOCK:
            obj = http_method_unlock;
            break;
        case HTTP_REPORT:
            obj = http_method_report;
            break;
        case HTTP_MKACTIVITY:
            obj = http_method_mkactivity;
            break;
        case HTTP_CHECKOUT:
            obj = http_method_checkout;
            break;
        case HTTP_MERGE:
            obj = http_method_merge;
            break;
        default:
            obj = http_method_get;
            break;
    }

    ret = PyDict_SetItem(env, request_method_key, obj);
    if(unlikely(ret == -1)){
        return -1;
    }
    req->body_length = content_length;
    /* client->current_req = NULL; */

    //keep client data
    obj = ClientObject_New(client);
    if(unlikely(obj == NULL)){
        return -1;
    }
    ret = PyDict_SetItem(env, client_key, obj);
    Py_DECREF(obj);
    if(unlikely(ret == -1)){
        return -1;
    }

    DEBUG("fin headers_complete_cb");
    return 0;
}

int
message_complete_cb(http_parser *p)
{
    client_t *client = get_client(p);
    DEBUG("message_complete_cb");
    client->complete = 1;
    client->upgrade = p->upgrade;

    /* request *req = client->request_queue->tail; */
    /* req->body = client->body; */
    /* req->body_type = client->body_type; */

    return 0;
}

static http_parser_settings settings =
  {.on_message_begin = message_begin_cb
  ,.on_header_field = header_field_cb
  ,.on_header_value = header_value_cb
  ,.on_url = url_cb
  ,.on_body = body_cb
  ,.on_headers_complete = headers_complete_cb
  ,.on_message_complete = message_complete_cb
  };


static PyMethodDef method = {"file_wrapper", (PyCFunction)file_wrapper, METH_VARARGS, 0};

int
init_parser(client_t *cli, const char *name, const short port)
{

    cli->http_parser = alloc_parser();
    /* cli->http_parser = (http_parser*)PyMem_Malloc(sizeof(http_parser)); */
    if(cli->http_parser == NULL){
        return -1;
    }
    /* memset(cli->http_parser, 0, sizeof(http_parser)); */
    http_parser_init(cli->http_parser, HTTP_REQUEST);
    cli->http_parser->data = cli;

    return 0;
}

size_t
execute_parse(client_t *cli, const char *data, size_t len)
{
    cli->complete = 0;
    return http_parser_execute(cli->http_parser, &settings, data, len);
}


int
parser_finish(client_t *cli)
{
    return cli->complete;
}

void
setup_static_env(char *name, int port)
{
    prefix_len = strlen("HTTP_");

    empty_string = NATIVE_FROMSTRING("");

    version_val = Py_BuildValue("(ii)", 1, 0);
    version_key = NATIVE_FROMSTRING("wsgi.version");

    scheme_val = NATIVE_FROMSTRING("http");
    scheme_key = NATIVE_FROMSTRING("wsgi.url_scheme");

    errors_val = PySys_GetObject("stderr");
    errors_key = NATIVE_FROMSTRING("wsgi.errors");

    multithread_val = PyBool_FromLong(0);
    multithread_key = NATIVE_FROMSTRING("wsgi.multithread");

    multiprocess_val = PyBool_FromLong(1);
    multiprocess_key = NATIVE_FROMSTRING("wsgi.multiprocess");

    run_once_val = PyBool_FromLong(0);
    run_once_key = NATIVE_FROMSTRING("wsgi.run_once");

    file_wrapper_val = PyCFunction_New(&method, NULL);
    file_wrapper_key = NATIVE_FROMSTRING("wsgi.file_wrapper");

    wsgi_input_key = NATIVE_FROMSTRING("wsgi.input");
    
    script_key = NATIVE_FROMSTRING("SCRIPT_NAME");

    server_name_val = NATIVE_FROMSTRING(name);
    server_name_key = NATIVE_FROMSTRING("SERVER_NAME");

    server_port_val = NATIVE_FROMFORMAT("%d", port);
    server_port_key = NATIVE_FROMSTRING("SERVER_PORT");

    remote_addr_key = NATIVE_FROMSTRING("REMOTE_ADDR");
    remote_port_key = NATIVE_FROMSTRING("REMOTE_PORT");

    server_protocol_key = NATIVE_FROMSTRING("SERVER_PROTOCOL");
    path_info_key = NATIVE_FROMSTRING("PATH_INFO");
    query_string_key = NATIVE_FROMSTRING("QUERY_STRING");
    request_method_key = NATIVE_FROMSTRING("REQUEST_METHOD");
    client_key = NATIVE_FROMSTRING("meinheld.client");

    content_type_key = NATIVE_FROMSTRING("CONTENT_TYPE");
    content_length_key = NATIVE_FROMSTRING("CONTENT_LENGTH");

    h_content_type_key = NATIVE_FROMSTRING("HTTP_CONTENT_TYPE");
    h_content_length_key = NATIVE_FROMSTRING("HTTP_CONTENT_LENGTH");

    server_protocol_val10 = NATIVE_FROMSTRING("HTTP/1.0");
    server_protocol_val11 = NATIVE_FROMSTRING("HTTP/1.1");

    http_method_delete = NATIVE_FROMSTRING("DELETE");
    http_method_get = NATIVE_FROMSTRING("GET");
    http_method_head = NATIVE_FROMSTRING("HEAD");
    http_method_post = NATIVE_FROMSTRING("POST");
    http_method_put = NATIVE_FROMSTRING("PUT");
    http_method_patch = NATIVE_FROMSTRING("PATCH");
    http_method_connect = NATIVE_FROMSTRING("CONNECT");
    http_method_options = NATIVE_FROMSTRING("OPTIONS");
    http_method_trace = NATIVE_FROMSTRING("TRACE");
    http_method_copy = NATIVE_FROMSTRING("COPY");
    http_method_lock = NATIVE_FROMSTRING("LOCK");
    http_method_mkcol = NATIVE_FROMSTRING("MKCOL");
    http_method_move = NATIVE_FROMSTRING("MOVE");
    http_method_propfind= NATIVE_FROMSTRING("PROPFIND");
    http_method_proppatch = NATIVE_FROMSTRING("PROPPATCH");
    http_method_unlock = NATIVE_FROMSTRING("UNLOCK");
    http_method_report = NATIVE_FROMSTRING("REPORT");
    http_method_mkactivity = NATIVE_FROMSTRING("MKACTIVITY");
    http_method_checkout = NATIVE_FROMSTRING("CHECKOUT");
    http_method_merge = NATIVE_FROMSTRING("MERGE");

    //PycString_IMPORT;
}

void
clear_static_env(void)
{
    DEBUG("clear_static_env");
    Py_DECREF(empty_string);

    Py_DECREF(version_key);
    Py_DECREF(version_val);
    Py_DECREF(scheme_key);
    Py_DECREF(scheme_val);
    Py_DECREF(errors_key);
    /* Py_DECREF(errors_val); */
    Py_DECREF(multithread_key);
    Py_DECREF(multithread_val);
    Py_DECREF(multiprocess_key);
    Py_DECREF(multiprocess_val);
    Py_DECREF(run_once_key);
    Py_DECREF(run_once_val);
    Py_DECREF(file_wrapper_key);
    Py_DECREF(file_wrapper_val);
    Py_DECREF(wsgi_input_key);

    Py_DECREF(script_key);
    Py_DECREF(server_name_key);
    Py_DECREF(server_name_val);
    Py_DECREF(server_port_key);
    Py_DECREF(server_port_val);
    Py_DECREF(remote_addr_key);
    Py_DECREF(remote_port_key);

    Py_DECREF(server_protocol_key);
    Py_DECREF(path_info_key);
    Py_DECREF(query_string_key);
    Py_DECREF(request_method_key);
    Py_DECREF(client_key);

    Py_DECREF(content_type_key);
    Py_DECREF(content_length_key);
    Py_DECREF(h_content_type_key);
    Py_DECREF(h_content_length_key);

    Py_DECREF(server_protocol_val10);
    Py_DECREF(server_protocol_val11);

    Py_DECREF(http_method_delete);
    Py_DECREF(http_method_get);
    Py_DECREF(http_method_head);
    Py_DECREF(http_method_post);
    Py_DECREF(http_method_put);
    Py_DECREF(http_method_patch);
    Py_DECREF(http_method_connect);
    Py_DECREF(http_method_options);
    Py_DECREF(http_method_trace);
    Py_DECREF(http_method_copy);
    Py_DECREF(http_method_lock);
    Py_DECREF(http_method_mkcol);
    Py_DECREF(http_method_move);
    Py_DECREF(http_method_propfind);
    Py_DECREF(http_method_proppatch);
    Py_DECREF(http_method_unlock);
    Py_DECREF(http_method_report);
    Py_DECREF(http_method_mkactivity);
    Py_DECREF(http_method_checkout);
    Py_DECREF(http_method_merge);

}

