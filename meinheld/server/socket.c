#include "socket.h"


inline void 
setup_listen_sock(int fd)
{
    int on = 1, r;
    r = setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &on, sizeof(on));
    assert(r == 0);
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
    assert(r == 0);
}

inline void 
setup_sock(int fd)
{
    int on = 1, r;
    r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    assert(r == 0);
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
    assert(r == 0);
}

inline void 
enable_cork(client_t *client)
{
    int on = 1, r;
    r = setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on));
    assert(r == 0);
}

inline void 
disable_cork(client_t *client)
{
    int off = 0;
    int on = 1, r;
    r = setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &off, sizeof(off));
    assert(r == 0);

    r = setsockopt(client->fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    assert(r == 0);
}


inline int 
CheckNSocketObject(PyObject *obj)
{
    if (obj->ob_type != &NSocketObjectType){
        return 0;
    }
    return 1;
}

inline PyObject* 
NSocketObject_New(int fd, ClientObject *client)
{
    NSocketObject *o = PyObject_NEW(NSocketObject, &NSocketObjectType);
    if(o == NULL){
        return NULL;
    }
    o->fd = fd;
    o->client = client;
    Py_INCREF(o->client);    
    setup_sock(fd);
    return (PyObject *)o;
}

static inline void
NSocketObject_dealloc(NSocketObject* self)
{
    Py_DECREF(self->client);
    PyObject_DEL(self);
}


static inline void
switch_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    NSocketObject *socket = (NSocketObject *)cb_arg;
    if ((events & PICOEV_TIMEOUT) != 0) {
        if(socket->send_buf){
            free_buffer(socket->send_buf);
            socket->send_buf = NULL;
        }
        if(socket->recv_buf){
            free_buffer(socket->recv_buf);
            socket->recv_buf = NULL;
        }

        PyErr_SetString(PyExc_IOError, "timeout");
        switch_wsgi_app(loop, fd, (PyObject *)socket->client);
    } else if ((events & PICOEV_WRITE) != 0 ||  (events & PICOEV_READ) != 0) {
        switch_wsgi_app(loop, fd, (PyObject *)socket->client);
    }
}

static inline void
trampolin_switch(NSocketObject *socket, int events)
{
    PyGreenlet *current, *parent;

    picoev_add(main_loop, socket->fd, events, 0, switch_callback, (void *)socket);
    // switch to hub
    current = socket->client->greenlet;
    parent = PyGreenlet_GET_PARENT(current);
    PyGreenlet_Switch(parent, hub_switch_value, NULL);

}

static inline PyObject * 
inner_write(NSocketObject *socket)
{
    buffer *send_buf = socket->send_buf;
    ssize_t r;
    r = write(socket->fd, send_buf->buf, send_buf->len);
#ifdef DEBUG
    printf("nsocket write fd:%d bytes:%d \n", socket->fd, r);
#endif
    switch (r) {
       case -1:
#ifdef DEBUG
            printf("inner_write err:%d \n", errno);
#endif
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return NULL;
            }else{
                free_buffer(socket->send_buf);
                PyErr_SetFromErrno(PyExc_IOError);
                return NULL;
            }
        default:
            //all done
            //switch 
            free_buffer(socket->send_buf);
            socket->send_buf = NULL;
            return Py_BuildValue("i", r);
    }
}

static inline PyObject * 
inner_write_all(NSocketObject *socket)
{
    buffer *send_buf = socket->send_buf;
    ssize_t r;
    ssize_t delta = send_buf->buf_size - send_buf->len;
    r = write(socket->fd, send_buf->buf + delta, send_buf->len);
#ifdef DEBUG
    printf("nsocket write fd:%d bytes:%d \n", socket->fd, r);
#endif
    switch (r) {
       case -1:
#ifdef DEBUG
            printf("inner_write_all err:%d \n", errno);
#endif
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return NULL;
            }else{
                free_buffer(socket->send_buf);
                PyErr_SetFromErrno(PyExc_IOError);
                return NULL;
            }
        default:
            send_buf->len -=r;
            if(!send_buf->len){
                //all done
                //switch 
                free_buffer(socket->send_buf);
                socket->send_buf = NULL;
                return Py_BuildValue("i", send_buf->buf_size);
            }
            return NULL;
    }
}

static inline PyObject * 
inner_read(NSocketObject *socket)
{
    buffer *recv_buf = socket->recv_buf;
    
    char buf[recv_buf->buf_size];
    ssize_t r;
    r = read(socket->fd, buf, sizeof(buf));
    // update timeout
    //picoev_set_timeout(loop, socket->fd, 5);
    switch (r) {
        case -1:
#ifdef DEBUG
            printf("inner_read err:%d \n", errno);
#endif
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return NULL;
            } else {
                free_buffer(socket->recv_buf);
                PyErr_SetFromErrno(PyExc_IOError);
                return NULL;
            }
        default:
            write2buf(recv_buf, buf, r);
            //all done
            //switch 
#ifdef DEBUG
            printf("recv_inner fd:%d \n", socket->fd);
#endif
            return getPyString(socket->recv_buf);
    }
}


static inline PyObject * 
inner_recv(NSocketObject *socket, ssize_t len)
{
    PyObject *res;

    socket->recv_buf = new_buffer(len, len);
    
    while(1){
        res = inner_read(socket);
        if(res){
            return res;
        }

        if(PyErr_Occurred()){
            // check socket Error
#ifdef DEBUG
            printf("inner_recv error occured fd:%d \n", socket->fd);
#endif
            return NULL;
        }
    
        trampolin_switch(socket, PICOEV_READ);

        if(PyErr_Occurred()){
            // check time out
#ifdef DEBUG
            printf("inner_recv timeout error occured fd:%d \n", socket->fd);
#endif
            return NULL;
        }
    }
    

}

static inline PyObject * 
inner_send(NSocketObject *socket, char *buf, ssize_t len)
{
    PyObject *res;
    socket->send_buf = new_buffer(len , len);
    write2buf(socket->send_buf, buf, len);

    while(1){
        res = inner_write(socket);
        if(res){
            if(socket->fd == socket->client->client->fd){
                // response fd ?
                socket->client->client->response_closed = 1;
            }
            return res;
        }

        if(PyErr_Occurred()){
            // check socket Error
#ifdef DEBUG
            printf("inner_send error occured fd:%d \n", socket->fd);
#endif
            return NULL;
        }
    
        trampolin_switch(socket, PICOEV_WRITE);

        if(PyErr_Occurred()){
            // check time out
#ifdef DEBUG
            printf("inner_send error occured fd:%d \n", socket->fd);
#endif
            return NULL;
        }
    }
}

static inline PyObject * 
inner_sendall(NSocketObject *socket, char *buf, ssize_t len)
{
    PyObject *res;
    socket->send_buf = new_buffer(len , len);
    write2buf(socket->send_buf, buf, len);

    while(1){
        res = inner_write(socket);
        if(res){
            if(socket->fd == socket->client->client->fd){
                // response fd ?
                socket->client->client->response_closed = 1;
            }
            return res;
        }

        if(PyErr_Occurred()){
            // check socket Error
#ifdef DEBUG
            printf("inner_sendall error occured fd:%d \n", socket->fd);
#endif
            return NULL;
        }
    
        trampolin_switch(socket, PICOEV_WRITE);

        if(PyErr_Occurred()){
            // check time out
#ifdef DEBUG
            printf("inner_sendall error occured fd:%d \n", socket->fd);
#endif
            return NULL;
        }
    }
}

static inline PyObject * 
NSocketObject_recv(NSocketObject *socket, PyObject *args)
{
    ssize_t len; 
    if (!PyArg_ParseTuple(args, "i:recv", &len)){
        return NULL;
    }
    return inner_recv(socket, len);
}

static inline PyObject * 
NSocketObject_send(NSocketObject *socket, PyObject *args)
{
    PyObject *s;
    char *buf;
    ssize_t len;
    if (!PyArg_ParseTuple(args, "S:send", &s)){
        return NULL;
    }
    
    PyString_AsStringAndSize(s, &buf, &len);

    return inner_send(socket, buf, len);
}

static inline PyObject * 
NSocketObject_sendall(NSocketObject *socket, PyObject *args)
{
    PyObject *s;
    char *buf;
    ssize_t len;
    if (!PyArg_ParseTuple(args, "S:sendall", &s)){
        return NULL;
    }
    
    PyString_AsStringAndSize(s, &buf, &len);

    return inner_sendall(socket, buf, len);
}

static inline PyObject * 
NSocketObject_close(NSocketObject *socket, PyObject *args)
{
    if(socket->fd != socket->client->client->fd){
        close(socket->fd);
    }
    Py_RETURN_NONE;
}

static PyMethodDef NSocketObject_method[] = {
    { "recv",      (PyCFunction)NSocketObject_recv, METH_VARARGS, 0 },
    { "send",      (PyCFunction)NSocketObject_send, METH_VARARGS, 0 },
    { "sendall",      (PyCFunction)NSocketObject_sendall, METH_VARARGS, 0 },
    { "close",      (PyCFunction)NSocketObject_close, METH_VARARGS, 0 },
    { NULL, NULL}
};



PyTypeObject NSocketObjectType = {
	PyObject_HEAD_INIT(&PyType_Type)
    0,
    "meinheld.nsocket",             /*tp_name*/
    sizeof(NSocketObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)NSocketObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "io raw socket ",                 /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		                   /* tp_iternext */
    NSocketObject_method,        /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
};

inline void 
setup_nsocket(void)
{
    PyGreenlet_Import();
}

