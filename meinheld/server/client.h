#ifndef CLIENT_H
#define CLIENT_H

#include "server.h"

typedef struct {
    PyObject_HEAD
    client_t *client;
} ClientObject;

extern PyTypeObject ClientObjectType;

inline PyObject* 
ClientObject_New(client_t* client);

#endif
