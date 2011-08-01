#ifndef STRINGIO_H
#define STRINGIO_H

#include "meinheld.h"
#include "buffer.h"

typedef struct {
    PyObject_HEAD
    buffer *buffer;
    Py_ssize_t pos;
} StringIOObject;

extern PyTypeObject StringIOObjectType;

void StringIOObject_list_fill(void);

void StringIOObject_list_clear(void);

PyObject* StringIOObject_New(buffer *buffer);

#endif
