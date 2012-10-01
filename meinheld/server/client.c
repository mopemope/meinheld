#include "client.h"

#ifdef WITH_GREENLET
#include "greensupport.h"
#endif

#define CLIENT_MAXFREELIST 1024

static ClientObject *client_free_list[CLIENT_MAXFREELIST];
static int client_numfree = 0;

void
ClientObject_list_fill(void)
{
    ClientObject *client;
    while (client_numfree < CLIENT_MAXFREELIST) {
        client = PyObject_NEW(ClientObject, &ClientObjectType);
        client_free_list[client_numfree++] = client;
    }
}

void
ClientObject_list_clear(void)
{
    ClientObject *op;

    while (client_numfree) {
        op = client_free_list[--client_numfree];
        PyObject_DEL(op);
    }
}

static ClientObject*
alloc_ClientObject(void)
{
    ClientObject *client;
    if (client_numfree) {
        client = client_free_list[--client_numfree];
        _Py_NewReference((PyObject *)client);
        GDEBUG("use pooled %p", client);
    }else{
        client = PyObject_NEW(ClientObject, &ClientObjectType);
        GDEBUG("alloc %p", client);
    }
    return client;
}

static  void
dealloc_ClientObject(ClientObject *client)
{
    Py_CLEAR(client->greenlet);
    if (client_numfree < CLIENT_MAXFREELIST){
        client_free_list[client_numfree++] = client;
        GDEBUG("back to pool %p", client);
    }else{
        PyObject_DEL(client);
    }
}

int
CheckClientObject(PyObject *obj)
{
    if (obj->ob_type != &ClientObjectType){
        return 0;
    }
    return 1;
}

 PyObject* 
ClientObject_New(client_t* client)
{
    ClientObject *o = alloc_ClientObject();
    //ClientObject *o = PyObject_NEW(ClientObject, &ClientObjectType);
    if(o == NULL){
        return NULL;
    }

    o->client = client;
    o->greenlet = NULL;
    o->args = NULL;
    o->kwargs = NULL;
    o->suspended = 0;
    /* o->resumed = 0; */

    GDEBUG("ClientObject_New pyclient:%p client:%p fd:%d", o, o->client, o->client->fd);
    return (PyObject *)o;
}

static  void
ClientObject_dealloc(ClientObject* self)
{

    GDEBUG("ClientObject_dealloc pyclient:%p client:%p fd:%d", self, self->client, self->client->fd);
    //self->client = NULL;
    DEBUG("XDECREF greenlet:%p", self->greenlet);
    dealloc_ClientObject(self);
    //Py_XDECREF(self->greenlet);
    //PyObject_DEL(self);
}

static  PyObject *
ClientObject_set_greenlet(ClientObject *self, PyObject *args)
{
#ifdef WITH_GREENLET
    PyObject *temp;

    if (!PyArg_ParseTuple(args, "O:set_greenlet", &temp)){
        return NULL;
    }
    if(!greenlet_check(temp)){
        PyErr_SetString(PyExc_TypeError, "must be greenlet object");
        return NULL;
    }

    if(self->greenlet){
        // not set
        Py_RETURN_NONE;
    }

    Py_INCREF(temp);
    self->greenlet = temp;
    Py_RETURN_NONE;
#else
    NO_GREENLET_ERROR;
#endif
}

static  PyObject*
ClientObject_get_greenlet(ClientObject *self, PyObject *args)
{
#ifdef WITH_GREENLET
    if(self->greenlet){
        return (PyObject *)self->greenlet;
    }
    Py_RETURN_NONE;
#else
    NO_GREENLET_ERROR;
#endif
}

static  PyObject *
ClientObject_get_fd(ClientObject *self, PyObject *args)
{
    return Py_BuildValue("i", self->client->fd);
}

static  PyObject *
ClientObject_set_closed(ClientObject *self, PyObject *args)
{
    int closed;

    if (!PyArg_ParseTuple(args, "i:set_closed", &closed)){
        return NULL;
    }
    self->client->response_closed = closed;
    Py_RETURN_NONE;
}

static PyMethodDef ClientObject_method[] = {
    { "set_greenlet",      (PyCFunction)ClientObject_set_greenlet, METH_VARARGS, 0 },
    { "get_greenlet",      (PyCFunction)ClientObject_get_greenlet, METH_NOARGS, 0 },
    {"get_fd", (PyCFunction)ClientObject_get_fd, METH_VARARGS, "get fd"},
    {"set_closed", (PyCFunction)ClientObject_set_closed, METH_VARARGS, "set response closed"},
    { NULL, NULL}
};

/*
static PyMemberDef ClientObject_members[] = {
    {"_greenlet", T_OBJECT_EX, offsetof(ClientObject, greenlet), 0, "greenlet"},
    {NULL}
};*/



PyTypeObject ClientObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    "meinheld.client",             /*tp_name*/
    sizeof(ClientObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)ClientObject_dealloc, /*tp_dealloc*/
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
    "client ",                 /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                           /* tp_iternext */
    ClientObject_method,        /* tp_methods */
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


