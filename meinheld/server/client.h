#ifndef CLIENT_H
#define CLIENT_H

#include "server.h"
#include "greenlet.h"

typedef struct {
    PyObject_HEAD
    client_t *client;
    PyGreenlet *greenlet;
    PyObject *args;         //greenlet.switch value
    PyObject *kwargs;       //greenlet.switch value
    PyObject *err_type;      //greenlet.throw value
    PyObject *err_val;       //greenlet.throw value
    PyObject *err_tb;       //gunicorn.throw value
    uint8_t suspended;
    uint8_t resumed;
} ClientObject;

extern PyTypeObject ClientObjectType;

inline PyObject* 
ClientObject_New(client_t* client);

inline void 
setup_client(void);

inline int 
CheckClientObject(PyObject *obj);

inline void
catch_error(ClientObject *pyclient);

#endif
