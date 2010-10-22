#include "http_request_parser.h"
#include "response.h"
#include "client.h"


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

static PyObject *script_key;
static PyObject *script_val;
static PyObject *server_name_key;
static PyObject *server_name_val;
static PyObject *server_port_key;
static PyObject *server_port_val;
static PyObject *remote_addr_key;
static PyObject *remote_port_key;

static PyObject *server_protocol_key;
static PyObject *path_info_key;
static PyObject *request_uri_key;
static PyObject *query_string_key;
static PyObject *fragment_key;
static PyObject *request_method_key;
static PyObject *client_key;

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

static inline PyObject * 
new_environ(client_t *client)
{
    register PyObject *object, *environ;

    environ = PyDict_New();
    PyDict_SetItem(environ, version_key, version_val);
    PyDict_SetItem(environ, scheme_key, scheme_val);
    PyDict_SetItem(environ, errors_key, errors_val);
    PyDict_SetItem(environ, multithread_key, multithread_val);
    PyDict_SetItem(environ, multiprocess_key, multiprocess_val);
    PyDict_SetItem(environ, run_once_key, run_once_val);
    PyDict_SetItem(environ, script_key, script_val);
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

static inline void
key_upper(char *s, const char *key, size_t len)
{
    int i = 0;
    register int c;
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

static inline int
write_body2file(client_t *client, const char *buffer, size_t buffer_len)
{
    FILE *tmp = (FILE *)client->body;
    fwrite(buffer, 1, buffer_len, tmp);
    client->body_readed += buffer_len;
#ifdef DEBUG
    printf("write_body2file %d bytes \n", buffer_len);
#endif
    return client->body_readed;

}

static inline int
write_body2mem(client_t *client, const char *buf, size_t buf_len)
{
    buffer *body = (buffer *)client->body;
    write2buf(body, buf, buf_len);

    client->body_readed += buf_len;
#ifdef DEBUG
    printf("write_body2mem %d bytes \n", buf_len);
#endif
    return client->body_readed;
}

static inline int
write_body(client_t *cli, const char *buffer, size_t buffer_len)
{
    if(cli->body_type == BODY_TYPE_TMPFILE){
        return write_body2file(cli, buffer, buffer_len);
    }else{
        return write_body2mem(cli, buffer, buffer_len);
    }
}

typedef enum{
    CONTENT_TYPE,
    CONTENT_LENGTH,
    OTHER
} wsgi_header_type;

static inline wsgi_header_type
check_header_type(const char *buf)
{
    if(*buf++ != 'C'){
        return OTHER;
    }
    if(*buf++ != 'O'){
        return OTHER;
    }
    if(*buf++ != 'N'){
        return OTHER;
    }
    if(*buf++ != 'T'){
        return OTHER;
    }
    if(*buf++ != 'E'){
        return OTHER;
    }
    if(*buf++ != 'N'){
        return OTHER;
    }
    if(*buf++ != 'T'){
        return OTHER;
    }
    if(*buf++ != '_'){
        return OTHER;
    }
    char c = *buf++;
    if(c == 'L'){
        return CONTENT_LENGTH;
    }else if(c == 'T'){
        return CONTENT_TYPE;
    }
    return OTHER;
}



static inline client_t *
get_client(http_parser *p)
{
    return (client_t *)p->data;
}

int
message_begin_cb(http_parser *p)
{
#ifdef DEBUG
    printf("message_begin_cb \n");
#endif

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

int
header_field_cb (http_parser *p, const char *buf, size_t len, char partial)
{
    uint32_t i;
    register header *h;
    client_t *client = get_client(p);
    register request *req = client->req;
    char temp[len];

    buffer_result ret = MEMORY_ERROR;
    if (req->last_header_element != FIELD){
        if(LIMIT_REQUEST_FIELDS <= req->num_headers){
            client->bad_request_code = 400;
            return -1;
        }
        req->num_headers++;
    }
    i = req->num_headers;
    h = req->headers[i];

    key_upper(temp, buf, len);
    if(h){
        ret = write2buf(h->field, temp, len);
    }else{
        req->headers[i] = h = new_header(128, LIMIT_REQUEST_FIELD_SIZE, 2048, LIMIT_REQUEST_FIELD_SIZE);
        wsgi_header_type type = check_header_type(temp);
        if(type == OTHER){
            ret = write2buf(h->field, "HTTP_", 5);
        }
        ret = write2buf(h->field, temp, len);
        //printf("%s \n", getString(h->field));

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

    req->last_header_element = FIELD;
    return 0;
}

int
header_value_cb (http_parser *p, const char *buf, size_t len, char partial)
{
    uint32_t i;
    register header *h;
    client_t *client = get_client(p);
    register request *req = client->req;
    
    buffer_result ret = MEMORY_ERROR;
    i = req->num_headers;
    h = req->headers[i];

    if(h){
        ret = write2buf(h->value, buf, len);
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
    req->last_header_element = VALUE;
    return 0;
}

int
request_path_cb (http_parser *p, const char *buf, size_t len, char partial)
{
    client_t *client = get_client(p);
    register request *req = client->req;
    buffer_result ret = MEMORY_ERROR;

    if(req->path){
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

int
request_uri_cb (http_parser *p, const char *buf, size_t len, char partial)
{
    client_t *client = get_client(p);
    register request *req = client->req;
    buffer_result ret = MEMORY_ERROR;

    if(req->uri){
        ret = write2buf(req->uri, buf, len);
    }else{
        req->uri = new_buffer(1024, LIMIT_URI);
        ret = write2buf(req->uri, buf, len);
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

int
query_string_cb (http_parser *p, const char *buf, size_t len, char partial)
{
    client_t *client = get_client(p);
    register request *req = client->req;
    buffer_result ret = MEMORY_ERROR;

    if(req->query_string){
        ret = write2buf(req->query_string, buf, len);
    }else{
        req->query_string = new_buffer(1024*2, LIMIT_QUERY_STRING);
        ret = write2buf(req->query_string, buf, len);
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

int
fragment_cb (http_parser *p, const char *buf, size_t len, char partial)
{
    client_t *client = get_client(p);
    register request *req = client->req;
    buffer_result ret = MEMORY_ERROR;

    if(req->fragment){
        ret = write2buf(req->fragment, buf, len);
    }else{
        req->fragment = new_buffer(1024, LIMIT_FRAGMENT);
        ret = write2buf(req->fragment, buf, len);
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


int
body_cb (http_parser *p, const char *buf, size_t len, char partial)
{
#ifdef DEBUG
    printf("body_cb \n");
#endif
    client_t *client = get_client(p);
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
#ifdef DEBUG
            printf("BODY_TYPE_TMPFILE \n");
#endif
        }else{
            //default memory stream
#ifdef DEBUG
            printf("client->body_length %d \n", client->body_length);
#endif
            client->body = new_buffer(client->body_length, 0);
            client->body_type = BODY_TYPE_BUFFER;
#ifdef DEBUG
            printf("BODY_TYPE_BUFFER \n");
#endif
        }
    }
    write_body(client, buf, len);
    return 0;
}

int
headers_complete_cb (http_parser *p)
{
    register PyObject *obj, *key;
    client_t *client = get_client(p);
    request *req = client->req;
    register PyObject *env = client->environ;
    register uint32_t i = 0; 
    register header *h;

    if(max_content_length < p->content_length){

        client->bad_request_code = 413;
        return -1;
    }
    

    if(p->http_major == 1 && p->http_minor == 1){
        obj = server_protocol_val11;
    }else{
        obj = server_protocol_val10;
    }
    PyDict_SetItem(env, server_protocol_key, obj);
    
    if(req->path){
        obj = getPyStringAndDecode(req->path);
        PyDict_SetItem(env, path_info_key, obj);
        Py_DECREF(obj);
        req->path = NULL;
    }
    if(req->uri){
        obj = getPyString(req->uri); 
        PyDict_SetItem(env, request_uri_key, obj);
        Py_DECREF(obj);
        req->uri = NULL;
    }
    if(req->query_string){
        obj = getPyString(req->query_string); 
        PyDict_SetItem(env, query_string_key, obj);
        Py_DECREF(obj);
        req->query_string = NULL;
    }
    if(req->fragment){
        obj = getPyString(req->fragment); 
        PyDict_SetItem(env, fragment_key, obj);
        Py_DECREF(obj);
        req->fragment = NULL;
    }
    for(i = 0; i < req->num_headers+1; i++){
        h = req->headers[i];
        if(h){
            key = getPyString(h->field);
            obj = getPyString(h->value);
            PyDict_SetItem(env, key, obj);
            Py_DECREF(key);
            Py_DECREF(obj);
            free_header(h);
            req->headers[i] = NULL;
        }
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
    
    PyDict_SetItem(env, request_method_key, obj);

    //free_request(req);
    client->req = NULL;
    client->body_length = p->content_length;
    
    //keep client data
    obj = ClientObject_New(client);
    PyDict_SetItem(env, client_key, obj);
    Py_DECREF(obj);

#ifdef DEBUG
    printf("headers_complete_cb \n");
#endif
    return 0;
}

int
message_complete_cb (http_parser *p)
{
#ifdef DEBUG
    printf("message_complete_cb \n");
#endif
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
  ,.on_path = request_path_cb
  ,.on_url = request_uri_cb
  ,.on_fragment = fragment_cb
  ,.on_query_string = query_string_cb
  ,.on_body = body_cb
  ,.on_headers_complete = headers_complete_cb
  ,.on_message_complete = message_complete_cb
  };


static PyMethodDef method = {"file_wrapper", (PyCFunction)file_wrapper, METH_VARARGS, 0};

inline int
init_parser(client_t *cli, const char *name, const short port)
{

    cli->http = (http_parser*)PyMem_Malloc(sizeof(http_parser));
    memset(cli->http, 0, sizeof(http_parser));

    http_parser_init(cli->http, HTTP_REQUEST);
    cli->http->data = cli;
    return 0;
}

inline size_t
execute_parse(client_t *cli, const char *data, size_t len)
{
    size_t ret = http_parser_execute(cli->http, &settings, data, len);
    //check new protocol
    cli->upgrade = cli->http->upgrade;

    return ret;
}


inline int
parser_finish(client_t *cli)
{
    return cli->complete;
}

inline void
setup_static_env(char *name, int port)
{

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

    script_val = PyString_FromString("");
    script_key = PyString_FromString("SCRIPT_NAME");
    
    server_name_val = PyString_FromString(name);
    server_name_key = PyString_FromString("SERVER_NAME");
    
    server_port_val = PyString_FromFormat("%d", port);
    server_port_key = PyString_FromString("SERVER_PORT");

    remote_addr_key = PyString_FromString("REMOTE_ADDR");
    remote_port_key = PyString_FromString("REMOTE_PORT");
    
    server_protocol_key = PyString_FromString("SERVER_PROTOCOL");
    path_info_key = PyString_FromString("PATH_INFO");
    request_uri_key = PyString_FromString("REQUEST_URI");
    query_string_key = PyString_FromString("QUERY_STRING");
    fragment_key = PyString_FromString("HTTP_FRAGMENT");
    request_method_key = PyString_FromString("REQUEST_METHOD");
    client_key = PyString_FromString("meinheld.client");
    
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

inline void
clear_static_env(void)
{
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
                 
    Py_DECREF(script_key);
    Py_DECREF(script_val);
    Py_DECREF(server_name_key);
    Py_DECREF(server_name_val);
    Py_DECREF(server_port_key);
    Py_DECREF(server_port_val);
    Py_DECREF(remote_addr_key);
    Py_DECREF(remote_port_key);

    Py_DECREF(server_protocol_key);
    Py_DECREF(path_info_key);
    Py_DECREF(request_uri_key);
    Py_DECREF(query_string_key);
    Py_DECREF(fragment_key);
    Py_DECREF(request_method_key);
    Py_DECREF(client_key);

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

