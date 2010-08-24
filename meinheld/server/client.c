#include "client.h"
#include "greenlet.h"

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
    ClientObject *o = PyObject_NEW(ClientObject, &ClientObjectType);
    if(o == NULL){
        return NULL;
    }
    o->client = client;
    o->greenlet = NULL;
    o->args = NULL;
    o->kwargs = NULL;
    o->suspended = 0;    
    o->resumed = 0;    
    return (PyObject *)o;
}

static inline void
ClientObject_dealloc(ClientObject* self)
{
    self->client = NULL;
    Py_XDECREF(self->greenlet);
    PyObject_DEL(self);
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

static PyMethodDef ClientObject_method[] = {
    { "set_greenlet",      (PyCFunction)ClientObject_set_greenlet, METH_VARARGS, 0 },
    { "get_greenlet",      (PyCFunction)ClientObject_get_greenlet, METH_NOARGS, 0 },
    {"get_fd", (PyCFunction)ClientObject_get_fd, METH_VARARGS, "get fd"},
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
