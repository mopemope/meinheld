#ifndef CLIENT_H
#define CLIENT_H

#include "server.h"
#include "greenlet.h"

typedef struct _client {
    int fd;
    char *remote_addr;
    int remote_port;
    
    uint8_t http_major;
    uint8_t http_minor;

    uint8_t keep_alive;
    request *req;
    int body_length;
    int body_readed;
    void *body;
    int bad_request_code;
    request_body_type body_type;    
    char upgrade;               // new protocol
    uint8_t complete;

    http_parser *http;          // http req parser
    PyObject *environ;          // wsgi environ
    int status_code;            // response status code
    
    PyObject *http_status;      // response status line(PyString)
    PyObject *headers;          // http response headers
    uint8_t header_done;            // header write status
    PyObject *response;         // wsgi response object 
    PyObject *response_iter;    // wsgi response object (iter)
    uint8_t chunked_response;     // use Transfer-Encoding: chunked
    uint8_t content_length_set;     // content_length_set flag
    int content_length;         // content_length
    int write_bytes;            // send body length
    void *bucket;               //write_data
    uint8_t response_closed;    //response closed flag
    uint8_t use_cork;     // use TCP_CORK
} client_t;

typedef struct {
    PyObject_HEAD
    client_t *client;
    PyGreenlet *greenlet;
    PyObject *args;         //greenlet.switch value
    PyObject *kwargs;       //greenlet.switch value
    uint8_t suspended;
    uint8_t resumed;
} ClientObject;

extern PyTypeObject ClientObjectType;

inline PyObject* 
ClientObject_New(client_t* client);

inline void 
setup_client(void);

inline int 
CheckClientObject(PyObject *obj);

inline void
ClientObject_list_fill(void);

inline void
ClientObject_list_clear(void);


#endif
