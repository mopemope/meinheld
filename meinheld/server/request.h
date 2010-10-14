#ifndef REQUEST_H
#define REQUEST_H

#include <Python.h>

#include <inttypes.h>
#include "buffer.h"

#define LIMIT_PATH 1024 * 8
#define LIMIT_FRAGMENT 1024
#define LIMIT_URI 1024 * 8
#define LIMIT_QUERY_STRING 1024 * 8

#define LIMIT_REQUEST_FIELDS 128 
#define LIMIT_REQUEST_FIELD_SIZE 1024 * 8 


typedef enum {
    FIELD,
    VALUE,
} field_type;

typedef struct {
    buffer *field;
    buffer *value;
} header;

typedef struct {
    buffer *path;
    buffer *uri;
    buffer *query_string;
    buffer *fragment;
    header *headers[LIMIT_REQUEST_FIELDS];
    uint32_t num_headers;
    field_type last_header_element;   
} request;

typedef struct _request_env {
    PyObject *env;
    void *next;
} request_env;

typedef struct _request_queue {
    int size;
    request_env *head;
    request_env *last;
} request_queue;

inline request *
new_request(void);

inline header *
new_header(size_t fsize, size_t flimit, size_t vsize, size_t vlimit);

inline void
free_header(header *h);

inline void
free_request(request *req);

inline void
dealloc_request(request *req);

inline void
request_list_fill(void);

inline void
request_list_clear(void);

inline void
header_list_fill(void);

inline void
header_list_clear(void);

#endif
