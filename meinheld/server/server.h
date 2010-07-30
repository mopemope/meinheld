#ifndef SERVER_H
#define SERVER_H

#include <Python.h>
#include <assert.h>
#include <fcntl.h>   
#include <stddef.h> 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include "picoev.h"
#include "request.h"
#include "http_parser.h"

#define SERVER "meinheld/0.1.1dev"

typedef enum {
    BODY_TYPE_NONE,
    BODY_TYPE_TMPFILE,
    BODY_TYPE_BUFFER
} request_body_type;


typedef struct _client {
    int fd;
    char *remote_addr;
    uint32_t remote_port;
    uint8_t keep_alive;
    request *req;
    uint32_t body_length;
    int body_readed;
    void *body;
    int bad_request_code;
    request_body_type body_type;    
    uint8_t complete;

    http_parser *http;          // http req parser
    PyObject *environ;          // wsgi environ
    int status_code;            // response status code
    
    PyObject *http_status;      // response status line(PyString)
    PyObject *headers;          // http response headers
    uint8_t header_done;            // header write status
    PyObject *response;         // wsgi response object 
    PyObject *response_iter;    // wsgi response object (iter)
    uint8_t content_length_set;     // content_length_set flag
    uint32_t content_length;         // content_length
    uint32_t write_bytes;            // send body length
    void *bucket;               //write_data
    uint8_t response_closed;    //response closed flag
} client_t;

extern int max_content_length; //max_content_length


#endif
