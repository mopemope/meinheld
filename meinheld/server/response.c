#include "response.h"
#include "log.h"
#include "util.h"
#include "meinheld.h"

#define CRLF "\r\n"
#define DELIM ": "

#define H_MSG_500 "HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/html\r\nServer:  " SERVER "\r\n\r\n"

#define H_MSG_503 "HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n"

#define H_MSG_400 "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n"

#define H_MSG_408 "HTTP/1.0 408 Request Timeout\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n"

#define H_MSG_411 "HTTP/1.0 411 Length Required\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n"

#define H_MSG_413 "HTTP/1.0 413 Request Entity Too Large\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n"

#define H_MSG_417 "HTTP/1.1 417 Expectation Failed\r\nContent-Type: text/html\r\nServer: " SERVER "\r\n\r\n"


#define MSG_500 H_MSG_500 "<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p>The server encountered an internal error and was unable to complete your request.  Either the server is overloaded or there is an error in the application.</p></body></html>"

#define MSG_503 H_MSG_503 "<html><head><title>Service Unavailable</title></head><body><p>Service Unavailable.</p></body></html>"

#define MSG_400 H_MSG_400 "<html><head><title>Bad Request</title></head><body><p>Bad Request.</p></body></html>"

#define MSG_408 H_MSG_408 "<html><head><title>Request Timeout</title></head><body><p>Request Timeout.</p></body></html>"

#define MSG_411 H_MSG_411 "<html><head><title>Length Required</title></head><body><p>Length Required.</p></body></html>"

#define MSG_413 H_MSG_413 "<html><head><title>Request Entity Too Large</title></head><body><p>Request Entity Too Large.</p></body></html>"

#define MSG_417 H_MSG_417 "<html><head><title>Expectation Failed</title></head><body><p>Expectation Failed.</p></body></html>"

ResponseObject *start_response = NULL;

static PyObject*
wsgi_to_bytes(PyObject *value)
{
    PyObject *result = NULL;

#ifdef PY3
    if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError, "expected unicode object, value "
                     "of type %.200s found", value->ob_type->tp_name);
        return NULL;
    }

    result = PyUnicode_AsLatin1String(value);

    if (!result) {
        PyErr_SetString(PyExc_ValueError, "unicode object contains non "
                        "latin-1 characters");
        return NULL;
    }
#else
    if (!PyBytes_Check(value)) {
        PyErr_Format(PyExc_TypeError, "expected byte string object, "
                     "value of type %.200s found", value->ob_type->tp_name);
        return NULL;
    }

    Py_INCREF(value);
    result = value;
#endif

    return result;
}

static int
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
                    usleep(200);
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
                        /* write_error_log(__FILE__, __LINE__); */
                        call_error_logger();
                        client->keep_alive = 0;
                    }
                    return -1;
                }
            default:
                data += (int)r;
                len -= r;
                client->write_bytes += r;
        }
    }
    return 1;
}

void
send_error_page(client_t *client)
{
    shutdown(client->fd, SHUT_RD);
    if(client->header_done || client->response_closed){
        // already sended response data
        // close connection
        return;
    }

    /* int status = client->bad_request_code; */
    /* int r = status < 0 ? status * -1:status; */
    /* client->status_code = client->bad_request_code; */

    DEBUG("send_error_page status_code %d client %p", client->status_code, client);

    switch(client->status_code){
        case 400:
            blocking_write(client, MSG_400, sizeof(MSG_400) -1);
            client->write_bytes -= sizeof(H_MSG_400) -1;
            break;
        case 408:
            blocking_write(client, MSG_408, sizeof(MSG_408) -1);
            client->write_bytes -= sizeof(H_MSG_408) -1;
            break;
        case 411:
            blocking_write(client, MSG_411, sizeof(MSG_411) -1);
            client->write_bytes -= sizeof(H_MSG_411) -1;
            break;
        case 413:
            blocking_write(client, MSG_413, sizeof(MSG_413) -1);
            client->write_bytes -= sizeof(H_MSG_413) -1;
            break;
        case 417:
            blocking_write(client, MSG_417, sizeof(MSG_417) -1);
            client->write_bytes -= sizeof(H_MSG_417) -1;
            break;
        case 503:
            blocking_write(client, MSG_503, sizeof(MSG_503) -1);
            client->write_bytes -= sizeof(H_MSG_503) -1;
            break;
        default:
            //Internal Server Error
            blocking_write(client, MSG_500, sizeof(MSG_500) -1);
            client->write_bytes -= sizeof(H_MSG_500) -1;
            break;
    }
    client->keep_alive = 0;
    client->header_done = 1;
    client->response_closed = 1;
}



static write_bucket *
new_write_bucket(int fd, int cnt)
{

    write_bucket *bucket;
    iovec_t *iov;

    bucket = PyMem_Malloc(sizeof(write_bucket));
    if(bucket == NULL){
        return NULL;
    }
    memset(bucket, 0, sizeof(write_bucket));

    bucket->fd = fd;
    iov = (iovec_t *)PyMem_Malloc(sizeof(iovec_t) * cnt);
    if(iov == NULL){
        PyMem_Free(bucket);
        return NULL;
    }
    memset(iov, 0, sizeof(iovec_t));
    bucket->iov = iov;
    bucket->iov_size = cnt;
    GDEBUG("allocate %p", bucket);
    return bucket;
}

static void
free_write_bucket(write_bucket *bucket)
{
    GDEBUG("free %p", bucket);
    Py_CLEAR(bucket->temp1);
    Py_CLEAR(bucket->chunk_data);
    PyMem_Free(bucket->iov);
    PyMem_Free(bucket);
}


static void
set2bucket(write_bucket *bucket, char *buf, size_t len)
{
    bucket->iov[bucket->iov_cnt].iov_base = buf;
    bucket->iov[bucket->iov_cnt].iov_len = len;
    bucket->iov_cnt++;
    bucket->total += len;
    bucket->total_size += len;
}

static void
set_chunked_data(write_bucket *bucket, char *lendata, size_t lenlen, char *data, size_t datalen)
{
    set2bucket(bucket, lendata, lenlen);
    set2bucket(bucket, CRLF, 2);
    set2bucket(bucket, data, datalen);
    set2bucket(bucket, CRLF, 2);
}

static void
set_last_chunked_data(write_bucket *bucket)
{
    set2bucket(bucket, "0", 1);
    set2bucket(bucket, CRLF, 2);
    set2bucket(bucket, CRLF, 2);
}


static void
add_header(write_bucket *bucket, char *key, size_t keylen, char *val, size_t vallen)
{
    set2bucket(bucket, key, keylen);
    set2bucket(bucket, DELIM, 2);
    set2bucket(bucket, val, vallen);
    set2bucket(bucket, CRLF, 2);
}

#ifdef DEVELOP
static void
writev_log(write_bucket *data)
{
    int i = 0;
    char *c;
    size_t len;
    for(; i < data->iov_cnt; i++){
        c = data->iov[i].iov_base;
        len = data->iov[i].iov_len;
        printf("%.*s", (int)len, c); 
    }
}
#endif

static response_status 
writev_bucket(write_bucket *data)
{
    size_t w;
    int i = 0;
    Py_BEGIN_ALLOW_THREADS
#ifdef DEVELOP
    BDEBUG("\nwritev_bucket fd:%d", data->fd);
    printf("\x1B[34m");
    writev_log(data);
    printf("\x1B[0m\n");
#endif
    w = writev(data->fd, data->iov, data->iov_cnt);
    BDEBUG("writev fd:%d ret:%d total_size:%d", data->fd, (int)w, data->total);
    Py_END_ALLOW_THREADS
    if(w == -1){
        //error
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            BDEBUG("try again later");
            return STATUS_SUSPEND;
        }else{
            //ERROR
            PyErr_SetFromErrno(PyExc_IOError);
            /* write_error_log(__FILE__, __LINE__); */
            call_error_logger();
            return STATUS_ERROR;
        }
    }if(w == 0){
        data->sended = 1;
        return STATUS_OK;
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
            data->total = data->total - w;
            BDEBUG("writev_bucket write %d progress %d/%d", (int)w, data->total, data->total_size);
            //resume
            // again later
            return writev_bucket(data);
        }
        data->sended = 1;
    }
    data->sended = 1;
    return STATUS_OK;
}

static int
set_file_content_length(client_t *client, write_bucket *bucket)
{
    struct stat info;
    int in_fd, ret = 0;
    size_t size = 0;
    Py_ssize_t valuelen = 0;
    FileWrapperObject *filewrap = NULL;
    PyObject *filelike = NULL, *length = NULL;
    char *value = NULL; 

    filewrap = (FileWrapperObject *)client->response;
    filelike = filewrap->filelike;

    in_fd = PyObject_AsFileDescriptor(filelike);
    if (in_fd == -1) {
        call_error_logger();
        return -1;
    }
    if (fstat(in_fd, &info) == -1){
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }

    size = info.st_size;
    client->content_length_set = 1;
    client->content_length = size;
    DEBUG("set content length:%" PRIu64 , size);
    length = PyBytes_FromFormat("%zu", size);
    if (length == NULL) {
        return -1;
    }

    PyBytes_AsStringAndSize(length, &value, &valuelen);
    
    add_header(bucket, "Content-Length", 14, value, valuelen);
    ret = PyList_Append(bucket->temp1, length);
    if (ret == -1) {
        return -1;
    }
    Py_DECREF(length);
    return 1;
}

/*
static int
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

static void
set_content_length(client_t *client, write_bucket *bucket, char *data, size_t datalen )
{
    PyObject *header, *length;
    char *value;
    Py_ssize_t valuelen;

    if(client->headers && !client->content_length_set){
        if (get_len(client->response) == 1) {
            client->content_length_set = 1;
            DEBUG("set content_length %d", (int)datalen);
            //length = PyBytes_FromFormat("%zu", datalen);

            header = Py_BuildValue("(sO)", "Content-Length", length);
            Py_DECREF(length);

            PyList_Append(client->headers, header);
            Py_DECREF(header); 
            PyBytes_AsStringAndSize(length, &value, &valuelen);
            add_header(bucket, "Content-Length", 14, value, valuelen);
        }
    }
}
*/

static int
add_all_headers(write_bucket *bucket, PyObject *fast_headers, int hlen, client_t *client)
{
    int i;
    PyObject *tuple = NULL;
    PyObject *obj1 = NULL, *obj2 = NULL;
    PyObject *bytes1 = NULL, *bytes2 = NULL;
    char *name = NULL, *value = NULL;
    Py_ssize_t namelen, valuelen;
#ifdef PY3
    int o;
#endif

    if(likely(fast_headers != NULL)){
        for (i = 0; i < hlen; i++) {

            tuple = PySequence_Fast_GET_ITEM(fast_headers, i);

            if (unlikely( !PyTuple_Check(tuple))) {
                PyErr_Format(PyExc_TypeError, "list of tuple values "
                             "expected, value of type %.200s found",
                             tuple->ob_type->tp_name);
                goto error;
            }


            if (unlikely(PyTuple_GET_SIZE(tuple) != 2)) {
                PyErr_Format(PyExc_ValueError, "tuple of length 2 "
                             "expected, length is %d",
                             (int)PyTuple_Size(tuple));
                goto error;
            }

            obj1 = PyTuple_GET_ITEM(tuple, 0);
            obj2 = PyTuple_GET_ITEM(tuple, 1);
            if(unlikely(!obj1)){
                goto error;
            }
            if(unlikely(!obj2)){
                goto error;
            }
            bytes1 = wsgi_to_bytes(obj1);
            if(unlikely(PyBytes_AsStringAndSize(bytes1, &name, &namelen) == -1)){
                goto error;
            }

            //value
            bytes2 = wsgi_to_bytes(obj2);
            if(unlikely(PyBytes_AsStringAndSize(bytes2, &value, &valuelen) == -1)){
                goto error;
            }

            if (unlikely(strchr(name, ':') != 0)) {
                PyErr_Format(PyExc_ValueError, "header name may not contains ':'"
                             "response header with name '%s' and value '%s'",
                             name, value);
                goto error;
            }

            if (unlikely(strchr(name, '\n') != 0 || strchr(value, '\n') != 0)) {
                PyErr_Format(PyExc_ValueError, "embedded newline in "
                             "response header with name '%s' and value '%s'",
                             name, value);
                goto error;
            }

            if (!strcasecmp(name, "Server") || !strcasecmp(name, "Date")) {
                continue;
            }

            if (client->content_length_set != 1 && !strcasecmp(name, "Content-Length")) {
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
            DEBUG("response header %s:%d : %s:%d",name, (int)namelen, value, (int)valuelen);
            add_header(bucket, name, namelen, value, valuelen);
#ifdef PY3
            //keep bytes string 
            o = PyList_Append(bucket->temp1, bytes1);
            if(o != -1){
                o = PyList_Append(bucket->temp1, bytes2);
                if(o == -1){
                    goto error;
                }
            }else{
                goto error;
            }
#endif
            Py_XDECREF(bytes1);
            Py_XDECREF(bytes2);
        }

    }else{
        RDEBUG("WARN header is lost...");
    }

    return 1;
error:
    if (PyErr_Occurred()){
        /* write_error_log(__FILE__, __LINE__); */
        call_error_logger();
    }
    Py_XDECREF(bytes1);
    Py_XDECREF(bytes2);
    return -1;
}

static int
add_status_line(write_bucket *bucket, client_t *client)
{
    PyObject *object;
    char *value = NULL;
    Py_ssize_t valuelen;

    object = client->http_status;
    //TODO ERROR CHECK
    if(object){
        DEBUG("add_status_line client %p", client);
        PyBytes_AsStringAndSize(object, &value, &valuelen);

        //write status code
        set2bucket(bucket, value, valuelen);

        add_header(bucket, "Server", 6,  SERVER, sizeof(SERVER) -1);
        //cache_time_update();
        add_header(bucket, "Date", 4, (char *)http_time, 29);
    }else{
        DEBUG("missing status_line %p", client);
    }
    return 1;
}

static PyObject*
get_chunk_data(size_t datalen)
{
    char lendata[32];
    int i = 0;
    i = snprintf(lendata, 32, "%zx", datalen);
    DEBUG("Transfer-Encoding chunk_size %s", lendata);
    return PyBytes_FromStringAndSize(lendata, (Py_ssize_t)i);
}


static void
set_first_body_data(client_t *client, char *data, size_t datalen)
{
    write_bucket *bucket = client->bucket;
    if(data){
        if(client->chunked_response){
            char *lendata  = NULL;
            Py_ssize_t len = 0;

            PyObject *chunk_data = get_chunk_data(datalen);
            //TODO CHECK ERROR
            PyBytes_AsStringAndSize(chunk_data, &lendata, &len);
            set_chunked_data(bucket, lendata, len, data, datalen);
            bucket->chunk_data = chunk_data;
        }else{
            set2bucket(bucket, data, datalen);
        }
    }
}

static response_status
write_headers(client_t *client, char *data, size_t datalen, char is_file)
{
    write_bucket *bucket = 0; 
    uint32_t hlen = 0;
    PyObject *headers = NULL, *templist = NULL;
    response_status ret;
    
    DEBUG("header write? %d", client->header_done);
    if(client->header_done){
        return STATUS_OK;
    }

    //TODO ERROR CHECK
    if (!client->headers){
        goto error;
    }
    headers = PySequence_Fast(client->headers, "header must be list");
    if (!headers){
        goto error;
    }
    hlen = PySequence_Fast_GET_SIZE(headers);

    bucket = new_write_bucket(client->fd, (hlen * 4) + 42 );

    if(bucket == NULL){
        goto error;
    }
    templist = PyList_New(hlen * 4);
    bucket->temp1 = templist;

    if(add_status_line(bucket, client) == -1){
        goto error;
    }
    //write header
    if(add_all_headers(bucket, headers, hlen, client) == -1){
        //Error
        goto error;
    }
    
    // check content_length_set
    if(data && !client->content_length_set && client->http_parser->http_minor == 1){
        //Transfer-Encoding chunked
        add_header(bucket, "Transfer-Encoding", 17, "chunked", 7);
        client->chunked_response = 1;
    }

    if (is_file && !client->content_length_set){
        if (set_file_content_length(client, bucket) == -1) {
            goto error;
        }
    }

    if(client->status_code == 101){
        add_header(bucket, "Connection", 10, "upgrade", 7);
    }else if(client->keep_alive == 1){
        //Keep-Alive
        add_header(bucket, "Connection", 10, "Keep-Alive", 10);
    }else{
        add_header(bucket, "Connection", 10, "close", 5);
    }
    
    set2bucket(bucket, CRLF, 2);

    //write body
    client->bucket = bucket;
    set_first_body_data(client, data, datalen);

    ret = writev_bucket(bucket);
    if(ret != STATUS_SUSPEND){
        client->header_done = 1;
        if(ret == STATUS_OK && data){
            client->write_bytes += datalen;
        }
        // clear
        free_write_bucket(bucket);
        client->bucket = NULL;
    }

    Py_DECREF(headers);
    return ret;
error:
    if (PyErr_Occurred()){
        /* write_error_log(__FILE__, __LINE__); */
        call_error_logger();
    }
    Py_XDECREF(headers);
    if(bucket){
        free_write_bucket(bucket);
        client->bucket = NULL;
    }
    return STATUS_ERROR;
}

static int
write_sendfile(int out_fd, int in_fd, int offset, size_t count)
{
    int size = (int)count;
    int res;
#ifdef linux
    /*
    if (size == 0) {
        struct stat info;
        DEBUG("call fstat");
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

response_status
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

            args = PyTuple_New(0);
            data = PyEval_CallObject(close, args);
            DEBUG("call response object close");
            Py_DECREF(args);
            Py_XDECREF(data);
            Py_DECREF(close);
            client->response_closed = 1;
            if (PyErr_Occurred()){
                return STATUS_ERROR;
            }
        }

    }
    return STATUS_OK;

}


static response_status
process_sendfile(client_t *client)
{
    PyObject *filelike = NULL;
    FileWrapperObject *filewrap = NULL;
    int in_fd, ret;

    filewrap = (FileWrapperObject *)client->response;
    filelike = filewrap->filelike;

    in_fd = PyObject_AsFileDescriptor(filelike);
    if (in_fd == -1) {
        PyErr_Clear();
        return STATUS_OK;
    }

    while(client->content_length > client->write_bytes){
        ret = write_sendfile(client->fd, in_fd, client->write_bytes, client->content_length);
        DEBUG("process_sendfile send %d", ret);
        switch (ret) {
            case 0:
                break;
            case -1: /* error */
                if (errno == EAGAIN || errno == EWOULDBLOCK) { /* try again later */
                    //next
                    DEBUG("process_sendfile EAGAIN %d", ret);
                    return STATUS_SUSPEND;
                } else { /* fatal error */
                    client->keep_alive = 0;
                    /* client->bad_request_code = 500; */
                    client->status_code = 500;
                    //close
                    return STATUS_ERROR;
                }
            default:
                client->write_bytes += ret;

        }
    }
    //all send
    //disable_cork(client);
    return close_response(client);
}

static response_status
process_write(client_t *client)
{
    PyObject *iterator = NULL;
    PyObject *item, *chunk_data = NULL;
    char *buf = NULL, *lendata = NULL;
    Py_ssize_t buflen, len;
    write_bucket *bucket = NULL;
    response_status ret;
    
    DEBUG("process_write start");
    iterator = client->response_iter;
    if(iterator != NULL){
        while((item =  PyIter_Next(iterator))){
            if(PyBytes_Check(item)){
                //TODO CHECK
                PyBytes_AsStringAndSize(item, &buf, &buflen);
                //write
                if(client->chunked_response){
                    bucket = new_write_bucket(client->fd, 4);
                    if(bucket == NULL){
                        /* write_error_log(__FILE__, __LINE__); */
                        call_error_logger();
                        Py_DECREF(item);
                        return STATUS_ERROR;
                    }
                    lendata  = NULL;
                    len = 0;

                    chunk_data = get_chunk_data(buflen);
                    //TODO CHECK ERROR
                    PyBytes_AsStringAndSize(chunk_data, &lendata, &len);
                    set_chunked_data(bucket, lendata, len, buf, buflen);
                    bucket->chunk_data = chunk_data;
                }else{
                    bucket = new_write_bucket(client->fd, 1);
                    if(bucket == NULL){
                        /* write_error_log(__FILE__, __LINE__); */
                        call_error_logger();
                        Py_DECREF(item);
                        return STATUS_ERROR;
                    }
                    set2bucket(bucket, buf, buflen);
                }
                bucket->temp1 = item;
                ret = writev_bucket(bucket);
                if(ret != STATUS_OK){
                    client->bucket = bucket;
                    /* Py_DECREF(item); */
                    return ret;
                }

                free_write_bucket(bucket);
                //mark
                client->write_bytes += buflen;
                //check write_bytes/content_length
                if(client->content_length_set){
                    if(client->content_length <= client->write_bytes){
                        // all done
                        /* Py_DECREF(item); */
                        break;
                    }
                }
                /* Py_DECREF(item); */
            }else{
                PyErr_SetString(PyExc_TypeError, "response item must be a byte string");
                Py_DECREF(item);
                if (PyErr_Occurred()){
                    /* client->bad_request_code = 500; */
                    client->status_code = 500;
                    /* write_error_log(__FILE__, __LINE__); */
                    call_error_logger();
                    return STATUS_ERROR;
                }
            }
        }
        if(PyErr_Occurred()){
            return STATUS_ERROR;
        }
        if(client->chunked_response){
            DEBUG("write last chunk");
            //last packet
            bucket = new_write_bucket(client->fd, 3);
            if(bucket == NULL){
                /* write_error_log(__FILE__, __LINE__); */
                call_error_logger();
                return STATUS_ERROR;
            }
            set_last_chunked_data(bucket);
            writev_bucket(bucket);
            free_write_bucket(bucket);
        }
        return close_response(client);
    }
    return STATUS_OK;
}


response_status
process_body(client_t *client)
{
    response_status ret;
    write_bucket *bucket;
    if(client->bucket){
        bucket = (write_bucket *)client->bucket;
        //retry send
        ret = writev_bucket(bucket);

        if(ret == STATUS_OK){
            client->write_bytes += bucket->total_size;
            free_write_bucket(bucket);
            client->bucket = NULL;
        }else if(ret == STATUS_ERROR){
            free_write_bucket(bucket);
            client->bucket = NULL;
            return ret;
        }else{
            //
            return ret;
        }
    }

    if (CheckFileWrapper(client->response)) {
        ret = process_sendfile(client);
    }else{
        ret = process_write(client);
    }

    return ret;
}

static response_status
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
        DEBUG("can't get fd");
        return STATUS_ERROR;
    }
    ret = write_headers(client, NULL, 0, 1);
    if(!client->content_length_set){
        if (fstat(in_fd, &info) == -1){
            PyErr_SetFromErrno(PyExc_IOError);
            /* write_error_log(__FILE__, __LINE__);  */
            call_error_logger();
            return STATUS_ERROR;
        }

        size = info.st_size;
        client->content_length_set = 1;
        client->content_length = size;
    }
    return ret;

}

static response_status
start_response_write(client_t *client)
{
    PyObject *iterator;
    PyObject *item;
    char *buf;
    Py_ssize_t buflen;
    response_status ret;

    iterator = PyObject_GetIter(client->response);
    if (PyErr_Occurred()){
        /* write_error_log(__FILE__, __LINE__); */
        call_error_logger();
        return STATUS_ERROR;
    }
    client->response_iter = iterator;

    item =  PyIter_Next(iterator);
    DEBUG("client %p", client);
    if(item != NULL && PyBytes_Check(item)){

        //write string only
        buf = PyBytes_AS_STRING(item);
        buflen = PyBytes_GET_SIZE(item);

        /* DEBUG("status_code %d body:%.*s", client->status_code, (int)buflen, buf); */
        ret = write_headers(client, buf, buflen, 0);
        //TODO when ret == STATUS_SUSPEND keep item
        Py_DECREF(item);
        return ret;
    }else{
        if (item == NULL && !PyErr_Occurred()){
            //Stop Iteration
            RDEBUG("WARN iter item == NULL");
            return write_headers(client, NULL, 0, 0);
        }else{
            PyErr_SetString(PyExc_TypeError, "response item must be a string");
            Py_XDECREF(item);
            if (PyErr_Occurred()){
                /* write_error_log(__FILE__, __LINE__); */
                call_error_logger();
                return STATUS_ERROR;
            }
        }

    }
    return STATUS_ERROR;
}

response_status
response_start(client_t *client)
{
    response_status ret ;
    if(client->status_code == 304){
        return write_headers(client, NULL, 0, 0);
    }

    if (CheckFileWrapper(client->response)) {
        DEBUG("use sendfile");
        //enable_cork(client);
        ret = start_response_file(client);
        if(ret == STATUS_OK){
            // sended header
            ret = process_sendfile(client);
        }
    }else{
        ret = start_response_write(client);
        DEBUG("start_response_write status_code %d ret = %d", client->status_code, ret);
        if(ret == STATUS_OK){
            // sended header
            ret = process_write(client);
        }
    }
    return ret;
}

void
setup_start_response(void)
{
    start_response = PyObject_NEW(ResponseObject, &ResponseObjectType);
}

void
clear_start_response(void)
{
    Py_CLEAR(start_response);
}


PyObject*
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


static PyObject*
create_status(PyObject *bytes, int bytelen, int http_minor)
{
    buffer_result r;
    buffer_t *b = new_buffer(256, 0);
    if(b == NULL){
        return NULL;
    }
    
    if(http_minor == 1){
        r = write2buf(b, "HTTP/1.1 ", 9); 
    }else{
        r = write2buf(b, "HTTP/1.0 ", 9); 
    }
    if(r != WRITE_OK){
        goto error;
    }
    r = write2buf(b, PyBytes_AS_STRING(bytes), bytelen);
    if(r != WRITE_OK){
        goto error;
    }
    r = write2buf(b, "\r\n", 2);
    if(r != WRITE_OK){
        goto error;
    }
    return getPyString(b);
error:
    free_buffer(b);
    return NULL;
}

static PyObject *
ResponseObject_call(PyObject *obj, PyObject *args, PyObject *kw)
{
    PyObject *status = NULL, *headers = NULL, *exc_info = NULL, *bytes = NULL;
    char *status_code = NULL;
    char *status_line = NULL;
    int bytelen = 0, int_code;
    ResponseObject *self = NULL;
    char *buf = NULL;

    self = (ResponseObject *)obj;
#ifdef PY3
    if (!PyArg_ParseTuple(args, "UO|O:start_response", &status, &headers, &exc_info)){
        return NULL;
    }
#else
    if (!PyArg_ParseTuple(args, "SO|O:start_response", &status, &headers, &exc_info)){
        return NULL;
    }
#endif

    if (self->cli->headers != NULL && exc_info && exc_info != Py_None) {
        // Re-raise original exception if headers sent

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

    if(self->cli->headers != NULL){
        PyErr_SetString(PyExc_TypeError, "headers already set");
        return NULL;
    }

    if (!PyList_Check(headers)) {
        PyErr_SetString(PyExc_TypeError, "response headers must be a list");
        return NULL;
    }

    
    bytes = wsgi_to_bytes(status);
    bytelen = PyBytes_GET_SIZE(bytes);
    buf = PyMem_Malloc(sizeof(char*) * bytelen);
    if (!buf) { 
        return NULL;
    }
    status_line = buf;
    strcpy(status_line, PyBytes_AS_STRING(bytes));
    
    /* DEBUG("%s :%d", (char*)status_line, bytelen); */

    if (!*status_line) {
        PyErr_SetString(PyExc_ValueError, "status message was not supplied");
        Py_XDECREF(bytes);
        if (buf) {
            PyMem_Free(buf);
        }
        return NULL;
    }

    status_code = strsep((char **)&status_line, " ");

    errno = 0;
    int_code = strtol(status_code, &status_code, 10);

    if (*status_code || errno == ERANGE) {
        PyErr_SetString(PyExc_TypeError, "status value is not an integer");
        Py_XDECREF(bytes);
        if (buf) {
            PyMem_Free(buf);
        }
        return NULL;
    }


    if (int_code < 100 || int_code > 999) {
        PyErr_SetString(PyExc_ValueError, "status code is invalid");
        Py_XDECREF(bytes);
        if (buf) {
            PyMem_Free(buf);
        }
        return NULL;
    }

    self->cli->status_code = int_code;

    Py_XDECREF(self->cli->headers);
    self->cli->headers = headers;
    Py_INCREF(self->cli->headers);

    Py_XDECREF(self->cli->http_status);

    self->cli->http_status = create_status(bytes, bytelen, self->cli->http_parser->http_minor);
    /* if(self->cli->http_parser->http_minor == 1){ */
        /* self->cli->http_status =  PyBytes_FromFormat("HTTP/1.1 %s\r\n", PyBytes_AS_STRING(bytes)); */
    /* }else{ */
        /* self->cli->http_status =  PyBytes_FromFormat("HTTP/1.0 %s\r\n", PyBytes_AS_STRING(bytes)); */
    /* } */

    /* DEBUG("set http_status %p", self->cli); */
    Py_XDECREF(bytes);
    if (buf) {
        PyMem_Free(buf);
    }
    Py_RETURN_NONE;
}

static PyObject *
FileWrapperObject_new(PyObject *self, PyObject *filelike, size_t blksize)
{
    FileWrapperObject *f;
    f = PyObject_NEW(FileWrapperObject, &FileWrapperType);
    if(f == NULL){
        return NULL;
    }

    f->filelike = filelike;
    Py_INCREF(f->filelike);
    GDEBUG("alloc FileWrapperObject %p", f);
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
    DEBUG("use FileWrapperObject_iter");
    return (PyObject *)iterator;
}

static void
FileWrapperObject_dealloc(FileWrapperObject* self)
{
    GDEBUG("dealloc FileWrapperObject %p", self);
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

PyObject *
file_wrapper(PyObject *self, PyObject *args)
{
    PyObject *filelike = NULL;
    size_t blksize = 0;
    //PyObject *result = NULL;

    if (!PyArg_ParseTuple(args, "O|l:file_wrapper", &filelike, &blksize))
        return NULL;

    return FileWrapperObject_new(self, filelike, blksize);
}

int 
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
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
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
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
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
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
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
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    FileWrapperObject_iter,                       /* tp_iter */
    0,                       /* tp_iternext */
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

