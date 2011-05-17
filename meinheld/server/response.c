#include "response.h"
#include "log.h"
#include "util.h"

#define CRLF "\r\n"
#define DELIM ": "

#define MSG_500 ("HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/html\r\nServer:  " SERVER "\r\n\r\n<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p>The server encountered an internal error and was unable to complete your request.  Either the server is overloaded or there is an error in the application.</p></body></html>")

#define MSG_503 ("HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n<html><head><title>Service Unavailable</title></head><body><p>Service Unavailable.</p></body></html>")

#define MSG_400 ("HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n<html><head><title>Bad Request</title></head><body><p>Bad Request.</p></body></html>")

#define MSG_408 ("HTTP/1.0 408 Request Timeout\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n<html><head><title>Request Timeout</title></head><body><p>Request Timeout.</p></body></html>")

#define MSG_411 ("HTTP/1.0 411 Length Required\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n<html><head><title>Length Required</title></head><body><p>Length Required.</p></body></html>")

#define MSG_413 ("HTTP/1.0 413 Request Entity Too Large\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n<html><head><title>Request Entity Too Large</title></head><body><p>Request Entity Too Large.</p></body></html>")

#define MSG_417 ("HTTP/1.1 417 Expectation Failed\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n<html><head><title>Expectation Failed</title></head><body><p>Expectation Failed.</p></body></html>")

ResponseObject *start_response = NULL;

static inline int 
blocking_write(client_t *client, char *data, size_t len)
{
    size_t r = 0, send_len = len;
    while ( (int)len > 0 ){
        if (len < send_len){
             send_len = len;
        }
        Py_BEGIN_ALLOW_THREADS
        r = write(client->fd, data, send_len);
        Py_END_ALLOW_THREADS
        switch(r){
            case 0:
                return 1;
                break;
            case -1:
                if (errno == EAGAIN || errno == EWOULDBLOCK) { /* try again later */
                    usleep(500);
                    break;
                }else{
                    // fatal error
                    //close
        
                    if(errno == EPIPE){
                        // Connection reset by peer 
                        client->keep_alive = 0;
                        client->status_code = 500;
                        client->header_done = 1;
                        client->response_closed = 1;
                        
                    }else{
                        PyErr_SetFromErrno(PyExc_IOError);
                        write_error_log(__FILE__, __LINE__);
                        client->keep_alive = 0;
                    }
                    return -1;
                }
            default:
                data += (int)r;
                len -= r;
                client->content_length += r;
        }
    }
    return 1;
}

void
send_error_page(client_t *client)
{
    shutdown(client->fd, SHUT_RD);
    if(client->header_done || client->response_closed){
        //already sended response data
        //close connection
        return;
    }

    int status = client->bad_request_code;
    int r = status < 0 ? status * -1:status;
    client->status_code = r;

#ifdef DEBUG
    printf("send_error_page status_code %d client %p \n", status, client);
#endif

    switch(r){
        case 400:
            blocking_write(client, MSG_400, sizeof(MSG_400) -1);
            break;
        case 408:
            blocking_write(client, MSG_400, sizeof(MSG_408) -1);
            break;
        case 411:
            blocking_write(client, MSG_411, sizeof(MSG_411) -1);
            break;
        case 413:
            blocking_write(client, MSG_413, sizeof(MSG_413) -1);
            break;
        case 417:
            blocking_write(client, MSG_417, sizeof(MSG_417) -1);
            break;
        case 503:
            blocking_write(client, MSG_503, sizeof(MSG_503) -1);
            break;
        default:
            //Internal Server Error
            blocking_write(client, MSG_500, sizeof(MSG_500) -1);
            break;
    }
    client->keep_alive = 0;
    client->header_done = 1;
    client->response_closed = 1;
}



static inline write_bucket *
new_write_bucket(int fd, int cnt){

    write_bucket *bucket;
    bucket = PyMem_Malloc(sizeof(write_bucket));
    memset(bucket, 0, sizeof(write_bucket));
    
    bucket->fd = fd;
    bucket->iov = (iovec_t *)PyMem_Malloc(sizeof(iovec_t) * cnt);
    bucket->iov_size = cnt;
    return bucket;
}

static inline void
free_write_bucket(write_bucket *bucket)
{
    PyMem_Free(bucket->iov);
    PyMem_Free(bucket);
}


static inline void
set2bucket(write_bucket *bucket, char *buf, size_t len)
{
    bucket->iov[bucket->iov_cnt].iov_base = buf;
    bucket->iov[bucket->iov_cnt].iov_len = len;
    bucket->iov_cnt++;
    bucket->total += len;
    bucket->total_size += len;
}

static inline void
set_chunked_data(write_bucket *bucket, char *lendata, size_t lenlen, char *data, size_t datalen)
{
    set2bucket(bucket, lendata, lenlen);
    set2bucket(bucket, CRLF, 2);
    set2bucket(bucket, data, datalen);
    set2bucket(bucket, CRLF, 2);
}

static inline void
set_last_chunked_data(write_bucket *bucket)
{
    set2bucket(bucket, "0", 1);
    set2bucket(bucket, CRLF, 2);
    set2bucket(bucket, CRLF, 2);
}


static inline void
add_header(write_bucket *bucket, char *key, size_t keylen, char *val, size_t vallen)
{
    set2bucket(bucket, key, keylen);
    set2bucket(bucket, DELIM, 2);
    set2bucket(bucket, val, vallen);
    set2bucket(bucket, CRLF, 2);
}


static inline int 
writev_bucket(write_bucket *data)
{
    size_t w;
    register int i = 0;
    Py_BEGIN_ALLOW_THREADS
    w = writev(data->fd, data->iov, data->iov_cnt);
    Py_END_ALLOW_THREADS
    if(w == -1){
        //error
        if (errno == EAGAIN || errno == EWOULDBLOCK) { 
            // try again later
            return 0;
        }else{
            //ERROR
            PyErr_SetFromErrno(PyExc_IOError);
            write_error_log(__FILE__, __LINE__); 
            return -1;
        }
    }if(w == 0){
        data->sended = 1;
        return 1;
    }else{
        if(data->total > w){
            for(; i < data->iov_cnt;i++){
                if(w > data->iov[i].iov_len){
                    //already write
                    w -= data->iov[i].iov_len;
                    data->iov[i].iov_len = 0;
                }else{
                    data->iov[i].iov_base += w;
                    data->iov[i].iov_len = data->iov[i].iov_len - w;
                    break;
                }
            }
            data->total = data->total -w;
#ifdef DEBUG
            printf("writev_bucket write %d progeress %d/%d \n", w, data->total, data->total_size);
#endif
            //resume
            // again later
            return writev_bucket(data);
        }
        data->sended = 1;
    }
    data->sended = 1;
    return 1;
}

static inline int 
get_len(PyObject *v)
{
	Py_ssize_t res;
	res = PyObject_Size(v);
	if (res < 0 && PyErr_Occurred()){
		PyErr_Clear();
        return 0;
    }
	return (int)res;
}

static inline void
set_content_length(client_t *client, write_bucket *bucket, char *data, size_t datalen )
{
    PyObject *header, *length;
    char *value;
    Py_ssize_t valuelen;

    if(client->headers && !client->content_length_set){
        if (get_len(client->response) == 1) {
            client->content_length_set = 1;
#ifdef DEBUG
            printf("set content_length %d \n", datalen);
#endif
            length = PyString_FromFormat("%zu", datalen);

            header = Py_BuildValue("(sO)", "Content-Length", length);
            Py_DECREF(length);

            PyList_Append(client->headers, header);
            Py_DECREF(header); 
            PyString_AsStringAndSize(length, &value, &valuelen);
            add_header(bucket, "Content-Length", 14, value, valuelen);
        }
    }
}

static inline int
write_headers(client_t *client, char *data, size_t datalen)
{
    if(client->header_done){
        return 1;
    }
    write_bucket *bucket; 
    register uint32_t i = 0, hlen = 0;
    register PyObject *headers = NULL;
    register PyObject *object = NULL;
    char *name = NULL;
    Py_ssize_t namelen;
    char *value = NULL;
    Py_ssize_t valuelen;

    if(client->headers){
        headers = PySequence_Fast(client->headers, "header must be list");
        hlen = PySequence_Fast_GET_SIZE(headers);
        Py_DECREF(headers);
    }
    bucket = new_write_bucket(client->fd, (hlen * 4) + 40 );
    
    object = client->http_status;
    if(object){
        PyString_AsStringAndSize(object, &value, &valuelen);
    
        //write status code
        set2bucket(bucket, value, valuelen);

        add_header(bucket, "Server", 6,  SERVER, sizeof(SERVER) -1);
        cache_time_update();
        add_header(bucket, "Date", 4, (char *)http_time, 29);
    }

    //write header
    if(client->headers){
        for (i = 0; i < hlen; i++) {
            PyObject *tuple = NULL;

            PyObject *object1 = NULL;
            PyObject *object2 = NULL;

            tuple = PySequence_Fast_GET_ITEM(headers, i);

            if (!PyTuple_Check(tuple)) {
                PyErr_Format(PyExc_TypeError, "list of tuple values "
                             "expected, value of type %.200s found",
                             tuple->ob_type->tp_name);
                goto error;
            }


            if (PySequence_Fast_GET_SIZE(tuple) != 2) {
                PyErr_Format(PyExc_ValueError, "tuple of length 2 "
                             "expected, length is %d",
                             (int)PyTuple_Size(tuple));
                goto error;
            }
            
            object1 = PyTuple_GET_ITEM(tuple, 0);
            object2 = PyTuple_GET_ITEM(tuple, 1);
            
            if (PyString_Check(object1)) {
                PyString_AsStringAndSize(object1, &name, &namelen);
            }else {
                PyErr_Format(PyExc_TypeError, "expected byte string object "
                             "for header name, value of type %.200s "
                             "found", object1->ob_type->tp_name);
                goto error;
            }

            if (PyString_Check(object2)) {
                PyString_AsStringAndSize(object2, &value, &valuelen);
            }else {
                PyErr_Format(PyExc_TypeError, "expected byte string object "
                             "for header value, value of type %.200s "
                             "found", object2->ob_type->tp_name);
                goto error;
            }

            if (strchr(name, ':') != 0) {
                PyErr_Format(PyExc_ValueError, "header name may not contains ':'"
                             "response header with name '%s' and value '%s'",
                             name, value);
                goto error;
            }

            if (strchr(name, '\n') != 0 || strchr(value, '\n') != 0) {
                PyErr_Format(PyExc_ValueError, "embedded newline in "
                             "response header with name '%s' and value '%s'",
                             name, value);
                goto error;
            }

            if (!strcasecmp(name, "Server") || !strcasecmp(name, "Date")) {
                continue;
            }
            
            if (!strcasecmp(name, "Content-Length")) {
                char *v = value;
                long l = 0;

                errno = 0;
                l = strtol(v, &v, 10);
                if (*v || errno == ERANGE || l < 0) {
                    PyErr_SetString(PyExc_ValueError,
                                    "invalid content length");
                    goto error;
                }

                client->content_length_set = 1;
                client->content_length = l;
            }
            add_header(bucket, name, namelen, value, valuelen);
        }
        
    }
    //header done 
    
    // check content_length_set
    if(data && !client->content_length_set && client->http->http_minor == 1){
        //Transfer-Encoding chunked
        add_header(bucket, "Transfer-Encoding", 17, "chunked", 7);
        client->chunked_response = 1;
    }

    if(client->keep_alive == 1){
        //Keep-Alive
        add_header(bucket, "Connection", 10, "Keep-Alive", 10);
    }else{
        add_header(bucket, "Connection", 10, "close", 5);
    }

    set2bucket(bucket, CRLF, 2);
    
    if(data){
        if(client->chunked_response){
            char lendata[32];
            int i = 0;
            i = snprintf(lendata, 32, "%zx", datalen);
#ifdef DEBUG
            printf("Transfer-Encoding chunk_size %s \n", lendata);
#endif
            set_chunked_data(bucket, lendata, i, data, datalen);  
        }else{
            set2bucket(bucket, data, datalen);
        }
    }
    client->bucket = bucket;
    int ret = writev_bucket(bucket);
    if(ret != 0){
        client->header_done = 1;
        if(ret > 0 && data){
            client->write_bytes += datalen;
        }
        // clear
        free_write_bucket(bucket);
        client->bucket = NULL;
    }
    return ret;
error:
    if (PyErr_Occurred()){ 
        write_error_log(__FILE__, __LINE__);
    }
    if(bucket){
        free_write_bucket(bucket);
        client->bucket = NULL;
    }
    return -1;
}

static inline int
write_sendfile(int out_fd, int in_fd, int offset, size_t count)
{
    int size = (int)count;
    int res;
#ifdef linux
    /*
    if (size == 0) {
        struct stat info;
#ifdef DEBUG
        printf("call fstat \n");
#endif
        if (fstat(in_fd, &info) == -1){
            PyErr_SetFromErrno(PyExc_IOError);
            write_error_log(__FILE__, __LINE__); 
            return -1;
        }

        size = info.st_size - lseek(in_fd, 0, SEEK_CUR);
    }*/
    Py_BEGIN_ALLOW_THREADS
    res = sendfile(out_fd, in_fd, NULL, size);
    Py_END_ALLOW_THREADS
    return res;
#elif defined(__FreeBSD__)
    off_t len;
    Py_BEGIN_ALLOW_THREADS
    res = sendfile(in_fd, out_fd, offset, 0, NULL, &len, 0);
    Py_END_ALLOW_THREADS
    if (res == 0) {
        return len;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { 
            return len; 
        }
        return -1;
    }
#elif defined(__APPLE__) 
    off_t len;
    Py_BEGIN_ALLOW_THREADS
    res = sendfile(in_fd, out_fd, offset, &len, NULL, 0);
    Py_END_ALLOW_THREADS
    if (res == 0) {
        return len;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { 
            return len; 
        }
        return -1;
    }
#endif
}

inline void 
close_response(client_t *client)
{
    if(!client->response_closed){ 
        //send all response
        //closing reponse object
        if (client->response && PyObject_HasAttrString(client->response, "close")) {
            PyObject *close = NULL;
            PyObject *args = NULL;
            PyObject *data = NULL;
            
            close = PyObject_GetAttrString(client->response, "close");

            args = Py_BuildValue("()");
            data = PyEval_CallObject(close, args);

            Py_DECREF(args);
            Py_XDECREF(data);
            Py_DECREF(close);
            if (PyErr_Occurred()){
                PyErr_Clear();
            }
        }

        client->response_closed = 1;
    }

}


static inline int
processs_sendfile(register client_t *client)
{
    PyObject *filelike = NULL;
    FileWrapperObject *filewrap = NULL;
    int in_fd, ret;

    filewrap = (FileWrapperObject *)client->response;
    filelike = filewrap->filelike;

    in_fd = PyObject_AsFileDescriptor(filelike);
    if (in_fd == -1) {
        PyErr_Clear();
        return 0;
    }

    while(client->content_length > client->write_bytes){
        ret = write_sendfile(client->fd, in_fd, client->write_bytes, client->content_length);
#ifdef DEBUG
        printf("processs_sendfile send %d \n", ret);
#endif
        switch (ret) {
            case 0: 
                break;
            case -1: /* error */
                if (errno == EAGAIN || errno == EWOULDBLOCK) { /* try again later */
                    //next 
#ifdef DEBUG
                    printf("processs_sendfile EAGAIN %d \n", ret);
#endif
                    return 0;
                } else { /* fatal error */
                    client->keep_alive = 0;
                    client->bad_request_code = 500;
                    client->status_code = 500;
                    //close
                    return -1;
                }
            default:
                client->write_bytes += ret;
                
        }
    }
    close_response(client);
    //all send
    return 1;
}

static inline int
processs_write(register client_t *client)
{
    register PyObject *iterator = NULL;
    register PyObject *item;
    char *buf;
    Py_ssize_t buflen;
    register write_bucket *bucket;
    int ret;

    iterator = client->response_iter;
    if(iterator != NULL){
        while((item =  PyIter_Next(iterator))){
            if(PyString_Check(item)){
                PyString_AsStringAndSize(item, &buf, &buflen);
                //write
                if(client->chunked_response){
                    bucket = new_write_bucket(client->fd, 4);
                    
                    char lendata[32];
                    int i = 0;
                    i = snprintf(lendata, 32, "%zx", buflen);
#ifdef DEBUG
                    printf("Transfer-Encoding chunk_size %s \n", lendata);
#endif
                    set_chunked_data(bucket, lendata, i, buf, buflen);  
                }else{
                    bucket = new_write_bucket(client->fd, 1);
                    set2bucket(bucket, buf, buflen);
                }
                ret = writev_bucket(bucket);
                if(ret <= 0){
                    client->bucket = bucket;
                    Py_DECREF(item);
                    return ret;
                }

                free_write_bucket(bucket);
                //mark
                client->write_bytes += buflen;
                //check write_bytes/content_length
                if(client->content_length_set){
                    if(client->content_length <= client->write_bytes){
                        // all done
                        Py_DECREF(item);
                        break;
                    }
                }
            }else{ 
                PyErr_SetString(PyExc_TypeError, "response item must be a string");
                Py_DECREF(item);
                if (PyErr_Occurred()){ 
                    write_error_log(__FILE__, __LINE__);
                    return -1;
                }
            }
            Py_DECREF(item);
        }

        if(client->chunked_response){
            bucket = new_write_bucket(client->fd, 3);
            set_last_chunked_data(bucket);
            writev_bucket(bucket);
            free_write_bucket(bucket);
        }
        close_response(client);
    }
    return 1;
}


inline int 
process_body(client_t *client)
{
    int ret;
    write_bucket *bucket;
    if(client->bucket){
        bucket = (write_bucket *)client->bucket;
        //retry send 
        ret = writev_bucket(bucket);
    
        if(ret != 0){
            client->write_bytes += bucket->total_size;
            //free
            free_write_bucket(bucket);
            client->bucket = NULL;
        }else{
            return 0;
        }

    }

    if (CheckFileWrapper(client->response)) {
        ret = processs_sendfile(client);
    }else{
        ret = processs_write(client);
    }

    return ret;
}

static inline int
start_response_file(client_t *client)
{
    PyObject *filelike;
    FileWrapperObject *filewrap;
    int ret,in_fd, size;
    struct stat info;

    filewrap = (FileWrapperObject *)client->response;
    filelike = filewrap->filelike;

    in_fd = PyObject_AsFileDescriptor(filelike);
    if (in_fd == -1) {
        PyErr_Clear();
#ifdef DEBUG
        printf("can't get fd \n");
#endif
        return -1;
    }
    ret = write_headers(client, NULL, 0);
    if(!client->content_length_set){
        if (fstat(in_fd, &info) == -1){
            PyErr_SetFromErrno(PyExc_IOError);
            write_error_log(__FILE__, __LINE__); 
            return -1;
        }

        size = info.st_size;
        client->content_length_set = 1;
        client->content_length = size;
    }
    return ret;

}

static inline int
start_response_write(client_t *client)
{
    PyObject *iterator;
    PyObject *item;
    char *buf;
    Py_ssize_t buflen;
    
    iterator = PyObject_GetIter(client->response);
    if (PyErr_Occurred()){ 
        write_error_log(__FILE__, __LINE__);
        return -1;
    }
    client->response_iter = iterator;

    item =  PyIter_Next(iterator);
    if(item != NULL && PyString_Check(item)){

        //write string only
        buf = PyString_AS_STRING(item);
        buflen = PyString_GET_SIZE(item);

#ifdef DEBUG
        printf("start_response_write status_code %d buflen %d \n", client->status_code, buflen);
#endif
        Py_DECREF(item);
        return write_headers(client, buf, buflen);
    }else{
        if (item == NULL && !PyErr_Occurred()){ 
            //Stap Iteration
            return write_headers(client, NULL, 0);
        }else{
            PyErr_SetString(PyExc_TypeError, "response item must be a string");
            Py_XDECREF(item);
            if (PyErr_Occurred()){ 
                write_error_log(__FILE__, __LINE__);
                return -1;
            }
        }
        
    }
    return -1;
}

inline int
response_start(client_t *client)
{
    int ret ;
    if(client->status_code == 304){
        return write_headers(client, NULL, 0);
    }
    enable_cork(client);
    if (CheckFileWrapper(client->response)) {
#ifdef DEBUG
        printf("use sendfile \n");
#endif 
        ret = start_response_file(client);
        if(ret > 0){
            // sended header 
            ret = processs_sendfile(client);
        }
    }else{
        ret = start_response_write(client);
#ifdef DEBUG
        printf("start_response_write status_code %d ret = %d \n", client->status_code, ret);
#endif 
        if(ret > 0){
            // sended header 
            ret = processs_write(client);
        }
    }
    return ret;
}

inline void
setup_start_response(void)
{
    start_response = PyObject_NEW(ResponseObject, &ResponseObjectType);
}

inline void
clear_start_response(void)
{
    Py_DECREF(start_response);
}


inline PyObject *  
create_start_response(client_t *cli)
{
    start_response->cli = cli;
    return (PyObject *)start_response;
}

static void
ResponseObject_dealloc(ResponseObject* self)
{
    self->cli = NULL;
    PyObject_DEL(self);
}


static PyObject *
ResponseObject_call(PyObject *obj, PyObject *args, PyObject *kw)
{
    
    PyObject *status = NULL, *headers = NULL, *exc_info = NULL ;
    char *status_line = NULL;
    char *status_code = NULL;

    ResponseObject *self = NULL;
    self = (ResponseObject *)obj;
    
    if (!PyArg_ParseTuple(args, "SO|O:start_response", &status, &headers, &exc_info))
        return NULL; 

    if (!PyString_Check(status)) {
        PyErr_Format(PyExc_TypeError, "expected byte string object for "
                     "status, value of type %.200s found",
                     status->ob_type->tp_name);
        return NULL;
    }

    if (!PyList_Check(headers)) {
        PyErr_SetString(PyExc_TypeError, "response headers must be a list");
        return NULL;
    }

    if (exc_info && exc_info != Py_None) {
        PyObject *type = NULL;
        PyObject *value = NULL;
        PyObject *traceback = NULL;

        if (!PyArg_ParseTuple(exc_info, "OOO", &type,
                              &value, &traceback)) {
            return NULL;
        }

        Py_INCREF(type);
        Py_INCREF(value);
        Py_INCREF(traceback);
        //raise 
        PyErr_Restore(type, value, traceback);
        return NULL;
    }
    
    char buf[PyString_GET_SIZE(status)];
    status_line = buf;
    strcpy(status_line, PyString_AS_STRING(status));

    status_code = strsep((char **)&status_line, " ");

    errno = 0;
    int int_code = strtol(status_code, &status_code, 10);

    if (*status_code || errno == ERANGE) {
        PyErr_SetString(PyExc_TypeError, "status value is not an integer");
        return NULL;
    }

    if (!*status_line) {
        PyErr_SetString(PyExc_ValueError, "status message was not supplied");
        return NULL;
    }

    if (int_code < 100 || int_code > 999) {
        PyErr_SetString(PyExc_ValueError, "status code is invalid");
        return NULL;
    }

    self->cli->status_code = int_code;

    Py_XDECREF(self->cli->headers);
    self->cli->headers = headers;
    Py_INCREF(self->cli->headers);
    
    Py_XDECREF(self->cli->http_status);

    if(self->cli->http->http_minor == 1){
        self->cli->http_status =  PyString_FromFormat("HTTP/1.1 %s\r\n", PyString_AS_STRING(status));
    }else{
        self->cli->http_status =  PyString_FromFormat("HTTP/1.0 %s\r\n", PyString_AS_STRING(status));
    }

    Py_RETURN_NONE;
}

static PyObject *
FileWrapperObject_new(PyObject *self, PyObject *filelike, size_t blksize)
{
    FileWrapperObject *f;
    f = PyObject_NEW(FileWrapperObject, &FileWrapperType);
    if(f == NULL)
        return NULL;

    f->filelike = filelike;
    Py_INCREF(f->filelike);
    return (PyObject *)f;
}

static PyObject * 
FileWrapperObject_iter(PyObject *o)
{
    FileWrapperObject *self = (FileWrapperObject *)o;
    PyObject *iterator = PyObject_GetIter(self->filelike);
    if (iterator == NULL) {
        PyErr_SetString(PyExc_TypeError, "file-like object must be a iterable object");
        return NULL;
    }

#ifdef DEBUG
    printf("use FileWrapperObject_iter \n");
#endif 
    return (PyObject *)iterator;
}

static void
FileWrapperObject_dealloc(FileWrapperObject* self)
{
    Py_XDECREF(self->filelike);
    PyObject_DEL(self);
}

static PyObject *
FileWrapperObject_close(FileWrapperObject *self, PyObject *args)
{
    PyObject *method = NULL;
    PyObject *result = NULL;

    method = PyObject_GetAttrString(self->filelike, "close");

    if (method) {
        result = PyEval_CallObject(method, (PyObject *)NULL);
        if (!result)
            PyErr_Clear();
        Py_DECREF(method);
    }

    Py_XDECREF(result);
    Py_RETURN_NONE;
}

inline PyObject *
file_wrapper(PyObject *self, PyObject *args)
{
    PyObject *filelike = NULL;
    size_t blksize = 0;
    //PyObject *result = NULL;

    if (!PyArg_ParseTuple(args, "O|l:file_wrapper", &filelike, &blksize))
        return NULL;

    return FileWrapperObject_new(self, filelike, blksize);
}

inline int 
CheckFileWrapper(PyObject *obj)
{
    FileWrapperObject *f;
    PyObject *filelike;
    int in_fd;
    if (obj->ob_type != &FileWrapperType){
        return 0;
    }

    f = (FileWrapperObject *)obj;
    filelike = f->filelike;

    in_fd = PyObject_AsFileDescriptor(filelike);
    if (in_fd == -1) {
        PyErr_Clear();
        return 0;
    }

    return 1;
}

static PyMethodDef FileWrapperObject_method[] = {
    { "close",      (PyCFunction)FileWrapperObject_close, METH_VARARGS, 0 },
    { NULL, NULL}
};

PyTypeObject ResponseObjectType = {
	PyObject_HEAD_INIT(NULL)
    0,
    "meinheld.start_response",             /*tp_name*/
    sizeof(ResponseObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)ResponseObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    ResponseObject_call,                         /*tp_call*/
    0, /*ResponseObject_str*/                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "wsgi start_response ",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    0,             /* tp_methods */
    0,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
};

PyTypeObject FileWrapperType = {
	PyObject_HEAD_INIT(&PyType_Type)
    0,
    "meinheld.file_wrapper",             /*tp_name*/
    sizeof(FileWrapperObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)FileWrapperObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "wsgi file_wrapper",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    FileWrapperObject_iter,		               /* tp_iter */
    0,		               /* tp_iternext */
    FileWrapperObject_method,             /* tp_methods */
    0,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
};

