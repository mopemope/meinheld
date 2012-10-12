#ifndef CLIENT_H
#define CLIENT_H

#include "meinheld.h"
#include "request.h"

typedef struct _client {
    int fd;
    char *remote_addr;
    int remote_port;

    char keep_alive;
    char upgrade;
    request *current_req;
    request_queue *request_queue;
    
    char complete;

    http_parser *http_parser;          // http req parser
    //PyObject *environ;          // wsgi environ
    uint16_t status_code;            // response status code

    PyObject *http_status;      // response status line(PyBytes)
    PyObject *headers;          // http response headers
    uint8_t header_done;            // header write status
    PyObject *response;         // wsgi response object
    PyObject *response_iter;    // wsgi response object (iter)
    uint8_t chunked_response;     // use Transfer-Encoding: chunked
    uint8_t content_length_set;     // content_length_set flag
    uint64_t content_length;         // content_length
    uint64_t write_bytes;            // send body length
    void *bucket;               //write_data
    uint8_t response_closed;    //response closed flag
    uint8_t use_cork;     // use TCP_CORK
} client_t;

typedef struct {
    PyObject_HEAD
    client_t *client;
    PyObject *greenlet;     //greenlet
    PyObject *args;         //greenlet.switch value
    PyObject *kwargs;       //greenlet.switch value
    uint8_t suspended;
    //uint8_t resumed;
} ClientObject;

extern PyTypeObject ClientObjectType;

PyObject* ClientObject_New(client_t* client);

int CheckClientObject(PyObject *obj);

void ClientObject_list_fill(void);

void ClientObject_list_clear(void);


#endif
