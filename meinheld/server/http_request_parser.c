#include "http_request_parser.h"
#include "server.h"
#include "response.h"
#include "input.h"


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


static PyObject*
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

    object = PyString_FromString(client->remote_addr);
    PyDict_SetItem(environ, remote_addr_key, object);
    Py_DECREF(object);

    object = PyString_FromFormat("%d", client->remote_port);
    PyDict_SetItem(environ, remote_port_key, object);
    Py_DECREF(object);
    return environ;
}

static PyObject*
concat_string(PyObject *o, const char *buf, size_t len)
{
    PyObject *ret;
    size_t l;
    char *dest, *origin;
    
    l = PyString_GET_SIZE(o);

    ret = PyString_FromStringAndSize((char*)0, l + len);
    if(ret == NULL){
        return ret;
    }
    dest = PyString_AS_STRING(ret);
    origin = PyString_AS_STRING(o);
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
set_query(PyObject *env, char *buf, int len)
{
    int c, ret, slen = 0;
    char *s0;
    PyObject *obj;

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
        obj = PyString_FromStringAndSize(s0, slen -1);
        DEBUG("query:%.*s", len, PyString_AS_STRING(obj));
        if(unlikely(obj == NULL)){
            return -1;
        }
        
        ret = PyDict_SetItem(env, query_string_key, obj);
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
    int c, c1;
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
    obj = PyString_FromStringAndSize(s0, t - s0);
    DEBUG("path:%.*s", t-s0, PyString_AS_STRING(obj));
    
    if(likely(obj != NULL)){
        PyDict_SetItem(env, path_info_key, obj);
        Py_DECREF(obj);
        return t - s0;
    }else{
        return -1;
    }
    
}

static PyObject*
get_http_header_key(const char *s, int len)
{
    PyObject *obj;
    char *dest;
    char c;

    obj = PyString_FromStringAndSize(NULL, len + prefix_len);
    dest = (char*)PyString_AS_STRING(obj);

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
write_body2file(client_t *client, const char *buffer, size_t buffer_len)
{
    FILE *tmp = (FILE *)client->body;
    fwrite(buffer, 1, buffer_len, tmp);
    client->body_readed += buffer_len;
    DEBUG("write_body2file %d bytes", (int)buffer_len);
    return client->body_readed;

}

static int
write_body2mem(client_t *client, const char *buf, size_t buf_len)
{
    buffer *body = (buffer *)client->body;
    write2buf(body, buf, buf_len);

    client->body_readed += buf_len;
    DEBUG("write_body2mem %d bytes", (int)buf_len);
    return client->body_readed;
}

static int
write_body(client_t *cli, const char *buffer, size_t buffer_len)
{
    if(cli->body_type == BODY_TYPE_TMPFILE){
        return write_body2file(cli, buffer, buffer_len);
    }else{
        return write_body2mem(cli, buffer, buffer_len);
    }
}

static client_t *
get_client(http_parser *p)
{
    return (client_t *)p->data;
}

static int
message_begin_cb(http_parser *p)
{
    DEBUG("message_begin_cb");

    client_t *client = get_client(p);

    client->req = new_request();
    client->environ = new_environ(client);
    client->complete = 0;
    client->bad_request_code = 0;
    client->body_type = BODY_TYPE_NONE;
    client->body_readed = 0;
    client->body_length = 0;
    client->req->env = client->environ;
    push_request(client->request_queue, client->req);
    return 0;
}


static int
header_field_cb(http_parser *p, const char *buf, size_t len)
{
    client_t *client = get_client(p);
    request *req = client->req;
    PyObject *env = NULL, *obj;
    
    DEBUG("field:%.*s", len, buf);

    if(req->last_header_element != FIELD){
        env = req->env;
        if(LIMIT_REQUEST_FIELDS <= req->num_headers){
            client->bad_request_code = 400;
            return -1;
        }
        PyDict_SetItem(env, req->field, req->value);
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
        client->bad_request_code = 500;
        return -1;
    }
    if(unlikely(Py_SIZE(obj) > LIMIT_REQUEST_FIELD_SIZE)){
        client->bad_request_code = 400;
        return -1;
    }

    req->field = obj;
    req->last_header_element = FIELD;
    return 0;
}

static int
header_value_cb(http_parser *p, const char *buf, size_t len)
{
    client_t *client = get_client(p);
    request *req = client->req;
    PyObject *obj;

    DEBUG("value:%.*s", len, buf);
    if(likely(req->value== NULL)){
        obj = PyString_FromStringAndSize(buf, len);
    }else{
        obj = concat_string(req->value, buf, len);
    }

    if(unlikely(obj == NULL)){
        client->bad_request_code = 500;
        return -1; 
    }
    if(unlikely(Py_SIZE(obj) > LIMIT_REQUEST_FIELD_SIZE)){
        client->bad_request_code = 400;
        return -1;
    }

    req->value = obj;
    req->last_header_element = VALUE;
    return 0;
}

static int
url_cb(http_parser *p, const char *buf, size_t len)
{
    client_t *client = get_client(p);
    request *req = client->req;
    buffer_result ret = MEMORY_ERROR;

    if(unlikely(req->path)){
        ret = write2buf(req->path, buf, len);
    }else{
        req->path = new_buffer(1024, LIMIT_PATH);
        ret = write2buf(req->path, buf, len);
    }
    switch(ret){
        case MEMORY_ERROR:
            client->bad_request_code = 500;
            return -1;
        case LIMIT_OVER:
            client->bad_request_code = 400;
            return -1;
        default:
            break;
    }


    return 0;
}


static int
body_cb(http_parser *p, const char *buf, size_t len)
{
    DEBUG("body_cb");
    client_t *client = get_client(p);

    DEBUG("content_length:%lu", (unsigned long)p->content_length);

    if(max_content_length < client->body_readed + len){

        client->bad_request_code = 413;
        return -1;
    }
    if(client->body_type == BODY_TYPE_NONE){
        if(client->body_length == 0){
            //Length Required
            client->bad_request_code = 411;
            return -1;
        }
        if(client->body_length > client_body_buffer_size){
            //large size request
            FILE *tmp = tmpfile();
            if(tmp < 0){
                client->bad_request_code = 500;
                return -1;
            }
            client->body = tmp;
            client->body_type = BODY_TYPE_TMPFILE;
            DEBUG("BODY_TYPE_TMPFILE");
        }else{
            //default memory stream
            DEBUG("client->body_length %d", client->body_length);
            client->body = new_buffer(client->body_length, 0);
            client->body_type = BODY_TYPE_BUFFER;
            DEBUG("BODY_TYPE_BUFFER");
        }
    }
    write_body(client, buf, len);
    return 0;
}

int
headers_complete_cb(http_parser *p)
{
    PyObject *obj;
    int ret;

    client_t *client = get_client(p);
    request *req = client->req;
    PyObject *env = client->environ;

    if(max_content_length < p->content_length){

        client->bad_request_code = 413;
        return -1;
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
        ret = PyDict_SetItem(env, req->field, req->value);
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
    //free_request(req);
    client->req = NULL;
    client->body_length = p->content_length;
    
    obj = InputObject_New(client);
    if(unlikely(obj == NULL)){
        return -1;
    }
    ret = PyDict_SetItem(env, wsgi_input_key, obj);
    Py_DECREF(obj);
    if(unlikely(ret == -1)){
        return -1;
    }

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
    DEBUG("message_complete_cb");
    client_t *client = get_client(p);
    client->complete = 1;

    request *req = client->request_queue->tail;
    req->body = client->body;
    req->body_type = client->body_type;

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

    cli->http = (http_parser*)PyMem_Malloc(sizeof(http_parser));
    memset(cli->http, 0, sizeof(http_parser));

    http_parser_init(cli->http, HTTP_REQUEST);
    cli->http->data = cli;
    return 0;
}

size_t
execute_parse(client_t *cli, const char *data, size_t len)
{
    size_t ret = http_parser_execute(cli->http, &settings, data, len);
    //check new protocol
    cli->upgrade = cli->http->upgrade;

    return ret;
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

    empty_string = PyString_FromString("");

    version_val = Py_BuildValue("(ii)", 1, 0);
    version_key = PyString_FromString("wsgi.version");

    scheme_val = PyString_FromString("http");
    scheme_key = PyString_FromString("wsgi.url_scheme");

    errors_val = PySys_GetObject("stderr");
    errors_key = PyString_FromString("wsgi.errors");

    multithread_val = PyBool_FromLong(0);
    multithread_key = PyString_FromString("wsgi.multithread");

    multiprocess_val = PyBool_FromLong(1);
    multiprocess_key = PyString_FromString("wsgi.multiprocess");

    run_once_val = PyBool_FromLong(0);
    run_once_key = PyString_FromString("wsgi.run_once");

    file_wrapper_val = PyCFunction_New(&method, NULL);
    file_wrapper_key = PyString_FromString("wsgi.file_wrapper");

    wsgi_input_key = PyString_FromString("wsgi.input");
    
    script_key = PyString_FromString("SCRIPT_NAME");

    server_name_val = PyString_FromString(name);
    server_name_key = PyString_FromString("SERVER_NAME");

    server_port_val = PyString_FromFormat("%d", port);
    server_port_key = PyString_FromString("SERVER_PORT");

    remote_addr_key = PyString_FromString("REMOTE_ADDR");
    remote_port_key = PyString_FromString("REMOTE_PORT");

    server_protocol_key = PyString_FromString("SERVER_PROTOCOL");
    path_info_key = PyString_FromString("PATH_INFO");
    query_string_key = PyString_FromString("QUERY_STRING");
    request_method_key = PyString_FromString("REQUEST_METHOD");
    client_key = PyString_FromString("meinheld.client");

    content_type_key = PyString_FromString("CONTENT_TYPE");
    content_length_key = PyString_FromString("CONTENT_LENGTH");

    h_content_type_key = PyString_FromString("HTTP_CONTENT_TYPE");
    h_content_length_key = PyString_FromString("HTTP_CONTENT_LENGTH");

    server_protocol_val10 = PyString_FromString("HTTP/1.0");
    server_protocol_val11 = PyString_FromString("HTTP/1.1");

    http_method_delete = PyString_FromStringAndSize("DELETE", 6);
    http_method_get = PyString_FromStringAndSize("GET", 3);
    http_method_head = PyString_FromStringAndSize("HEAD", 4);
    http_method_post = PyString_FromStringAndSize("POST", 4);
    http_method_put = PyString_FromStringAndSize("PUT", 3);
    http_method_connect = PyString_FromStringAndSize("CONNECT", 7);
    http_method_options = PyString_FromStringAndSize("OPTIONS", 7);
    http_method_trace = PyString_FromStringAndSize("TRACE", 5);
    http_method_copy = PyString_FromStringAndSize("COPY", 4);
    http_method_lock = PyString_FromStringAndSize("LOCK", 4);
    http_method_mkcol = PyString_FromStringAndSize("MKCOL", 5);
    http_method_move = PyString_FromStringAndSize("MOVE", 4);
    http_method_propfind= PyString_FromStringAndSize("PROPFIND", 8);
    http_method_proppatch = PyString_FromStringAndSize("PROPPATCH", 9);
    http_method_unlock = PyString_FromStringAndSize("UNLOCK", 6);
    http_method_report = PyString_FromStringAndSize("REPORT", 6);
    http_method_mkactivity = PyString_FromStringAndSize("MKACTIVITY", 10);
    http_method_checkout = PyString_FromStringAndSize("CHECKOUT", 8);
    http_method_merge = PyString_FromStringAndSize("MERGE", 5);

    //PycString_IMPORT;
}

void
clear_static_env(void)
{
    Py_DECREF(empty_string);

    Py_DECREF(version_key);
    Py_DECREF(version_val);
    Py_DECREF(scheme_key);
    Py_DECREF(scheme_val);
    Py_DECREF(errors_key);
    Py_DECREF(errors_val);
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
    //Py_DECREF(request_uri_key);
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

