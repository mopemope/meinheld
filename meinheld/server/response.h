#ifndef RESPONSE_H
#define RESPONSE_H

#include "meinheld.h"

#include "client.h"
#include "time_cache.h"

typedef struct iovec iovec_t;

typedef struct {
    int fd;
    iovec_t *iov;
    uint32_t iov_cnt;
    uint32_t iov_size;
    uint32_t total;
    uint32_t total_size;
    uint8_t sended;
    PyObject *temp1; //keep origin pointer
    PyObject *chunk_data; //keep chunk_data origin pointer
} write_bucket;


typedef struct {
    PyObject_HEAD
    client_t *cli;
} ResponseObject;

typedef struct {
    PyObject_HEAD
    PyObject *filelike;

} FileWrapperObject;

typedef enum {
    STATUS_OK = 0,
    STATUS_SUSPEND,
    STATUS_ERROR 
} response_status;

extern PyTypeObject ResponseObjectType;
extern PyTypeObject FileWrapperType;
extern ResponseObject *start_response;

PyObject* create_start_response(client_t *cli);

PyObject* file_wrapper(PyObject *self, PyObject *args);

int CheckFileWrapper(PyObject *obj);

response_status response_start(client_t *client);

response_status process_body(client_t *client);

response_status close_response(client_t *client);

void setup_start_response(void);

void clear_start_response(void);

void send_error_page(client_t *client);


#endif

