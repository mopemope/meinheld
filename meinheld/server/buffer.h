#ifndef BUFFER_H
#define BUFFER_H

#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef enum{
    WRITE_OK,
    MEMORY_ERROR,
    LIMIT_OVER,
} buffer_result;

typedef struct {
    char *buf;
    size_t buf_size;
    size_t len;
    size_t limit;
} buffer;

inline buffer *
new_buffer(size_t buf_size, size_t limit);

inline buffer_result
write2buf(buffer *buf, const char *c, size_t  l);

inline void
free_buffer(buffer *buf);

inline PyObject *
getPyString(buffer *buf);

inline char *
getString(buffer *buf);

#endif
