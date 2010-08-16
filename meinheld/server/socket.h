#ifndef SOCKET_H
#define SOCKET_H

#include "server.h"


typedef struct {
    PyObject_HEAD
    int fd;
} NSocketObject;

extern PyTypeObject NSocketObjectType;

inline PyObject* 
NSocketObject_New(int fd);

inline int 
CheckNSocketObject(PyObject *obj);

#endif
