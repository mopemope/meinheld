#ifndef INPUT_H
#define INPUT_H

#include "meinheld.h"
#include "buffer.h"
#include "http_parser.h"
#include "client.h"

typedef struct {
    PyObject_HEAD
    buffer_t *buffer;
    Py_ssize_t pos;
} InputObject;

extern PyTypeObject InputObjectType;

void InputObject_list_fill(void);

void InputObject_list_clear(void);

PyObject* InputObject_New(buffer_t *buf);

#endif
