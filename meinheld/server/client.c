#include "client.h"
#include "greenlet.h"

#define CLIENT_MAXFREELIST 1024

static ClientObject *client_free_list[CLIENT_MAXFREELIST];
static int client_numfree = 0;

inline void
ClientObject_list_fill(void)
{
    ClientObject *client;
	while (client_numfree < CLIENT_MAXFREELIST) {
        client = PyObject_NEW(ClientObject, &ClientObjectType);
		client_free_list[client_numfree++] = client;
	}
}

inline void
ClientObject_list_clear(void)
{
	ClientObject *op;

	while (client_numfree) {
		op = client_free_list[--client_numfree];
        PyObject_DEL(op);
	}
}

static inline ClientObject*
alloc_ClientObject(void)
{
    ClientObject *client;
	if (client_numfree) {
		client = client_free_list[--client_numfree];
		_Py_NewReference((PyObject *)client);
#ifdef DEBUG
        printf("use pooled client %p\n", client);
#endif
    }else{
        client = PyObject_NEW(ClientObject, &ClientObjectType);
#ifdef DEBUG
        printf("alloc client %p\n", client);
#endif
    }
    return client;
}

static inline void
dealloc_ClientObject(ClientObject *client)
{
    Py_CLEAR(client->greenlet);
	if (client_numfree < CLIENT_MAXFREELIST){
		client_free_list[client_numfree++] = client;
    }else{
	    PyObject_DEL(client);
    }
}

inline int 
CheckClientObject(PyObject *obj)
{
    if (obj->ob_type != &ClientObjectType){
        return 0;
    }
    return 1;
}

inline PyObject* 
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
    o->resumed = 0;    

#ifdef DEBUG
    if(o->client){
        printf("ClientObject_New pyclient:%p client:%p fd:%d \n", o, o->client, o->client->fd);
    }else{
        printf("ClientObject_New pyclient:%p client is null \n", o);
    }
#endif
    
    return (PyObject *)o;
}

static inline void
ClientObject_dealloc(ClientObject* self)
{

#ifdef DEBUG
    if(self->client){
        printf("ClientObject_dealloc pyclient:%p client:%p fd:%d \n", self, self->client, self->client->fd);
    }else{
        printf("ClientObject_dealloc pyclient:%p client is null \n", self);
    }
#endif
    //self->client = NULL;
#ifdef DEBUG
    printf("XDECREF greenlet:%p \n", self->greenlet);
#endif
    
    dealloc_ClientObject(self);
    //Py_XDECREF(self->greenlet);
    //PyObject_DEL(self);
}

static inline PyObject *
ClientObject_set_greenlet(ClientObject *self, PyObject *args)
{
    PyObject *temp;

    if (!PyArg_ParseTuple(args, "O:set_greenlet", &temp)){
        return NULL;
    }
    if(!PyGreenlet_Check(temp)){
        PyErr_SetString(PyExc_TypeError, "must be greenlet object");
        return NULL;
    }
    
    if(self->greenlet){
        // not set
        Py_RETURN_NONE;
    }

    Py_INCREF(temp);
    self->greenlet = (PyGreenlet *)temp;

    Py_RETURN_NONE;
}

static inline PyObject *
ClientObject_get_greenlet(ClientObject *self, PyObject *args)
{
    if(self->greenlet){
        return (PyObject *)self->greenlet;
    }
    Py_RETURN_NONE;
}

static inline PyObject *
ClientObject_get_fd(ClientObject *self, PyObject *args)
{
    return Py_BuildValue("i", self->client->fd);
}

static inline PyObject *
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
	PyObject_HEAD_INIT(&PyType_Type)
    0,
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
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		                   /* tp_iternext */
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

inline void 
setup_client(void)
{
    PyGreenlet_Import();
}
