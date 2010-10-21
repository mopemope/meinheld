#ifndef STRINGIO_H
#define STRINGIO_H

#include <Python.h>
#include "buffer.h"

typedef struct {
    PyObject_HEAD
    buffer *buffer;
} StringIOObject;

extern PyTypeObject StringIOObjectType;

inline void
StringIOObject_list_fill(void);

inline void
StringIOObject_list_clear(void);

inline PyObject* 
StringIOObject_New(buffer *buffer);

#endif
