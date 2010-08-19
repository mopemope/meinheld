#ifndef SOCKET_H
#define SOCKET_H

#include "server.h"
#include "client.h"

typedef struct {
    PyObject_HEAD
    int fd;
    ClientObject *client;
    buffer *recv_buf;
    buffer *send_buf;

} NSocketObject;

extern PyTypeObject NSocketObjectType;

inline PyObject* 
NSocketObject_New(int fd, ClientObject *client);

inline int 
CheckNSocketObject(PyObject *obj);

inline void 
setup_nsocket(void);

inline void 
setup_listen_sock(int fd);

inline void 
setup_sock(int fd);

inline void 
enable_cork(client_t *client);

inline void 
disable_cork(client_t *client);

#endif
