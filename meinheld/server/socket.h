#ifndef SOCKET_H
#define SOCKET_H

#include "server.h"


typedef struct {
    PyObject_HEAD
    int fd;
} SocketObject;

extern PyTypeObject SocketObjectType;

inline PyObject* 
SocketObject_New(int fd);

inline int 
CheckSocketObject(PyObject *obj);

#endif
