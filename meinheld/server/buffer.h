#ifndef BUFFER_H
#define BUFFER_H

#include "meinheld.h"

typedef enum{
    WRITE_OK,
    MEMORY_ERROR,
    LIMIT_OVER,
} buffer_result;

typedef struct _buffer {
    char *buf;
    size_t buf_size;
    size_t len;
    size_t limit;
} buffer;

buffer* new_buffer(size_t buf_size, size_t limit);

buffer_result write2buf(buffer *buf, const char *c, size_t  l);

void free_buffer(buffer *buf);

PyObject* getPyString(buffer *buf);

PyObject* getPyStringAndDecode(buffer *buf);

char* getString(buffer *buf);

void buffer_list_fill(void);

void buffer_list_clear(void);
#endif
