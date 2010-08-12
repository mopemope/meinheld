#ifndef RESPONSE_H
#define RESPONSE_H

#include <Python.h>
#include <sys/uio.h>
#include <inttypes.h>

#include "server.h"
#include "time_cache.h"

typedef struct iovec iovec_t;


typedef struct {
    int fd;
    iovec_t *iov;
    uint32_t iov_cnt;
    uint32_t iov_size;
    uint32_t total;
    uint32_t total_size;
} write_bucket;


typedef struct {
    PyObject_HEAD
    client_t *cli;
} ResponseObject;

typedef struct {
    PyObject_HEAD
    PyObject *filelike;

} FileWrapperObject;

extern PyTypeObject ResponseObjectType;
extern PyTypeObject FileWrapperType;

extern ResponseObject *start_response;

PyObject * create_start_response(client_t *cli);
PyObject * file_wrapper(PyObject *self, PyObject *args);

int CheckFileWrapper(PyObject *obj);

inline int
response_start(client_t *client);

inline int 
process_body(client_t *client);

inline void
setup_start_response(void);

inline void
clear_start_response(void);

void
send_error_page(client_t *client);


#endif

