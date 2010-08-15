#include "socket.h"


inline int 
CheckSocketObject(PyObject *obj)
{
    if (obj->ob_type != &SocketObjectType){
        return 0;
    }
    return 1;
}

inline PyObject* 
SocketObject_New(int fd)
{
    SocketObject *o = PyObject_NEW(SocketObject, &SocketObjectType);
    if(o == NULL){
        return NULL;
    }
    o->fd = fd;
    return (PyObject *)o;
}

static inline void
SocketObject_dealloc(SocketObject* self)
{
    PyObject_DEL(self);
}



static PyMethodDef SocketObject_method[] = {
    //{ "set_greenlet",      (PyCFunction)ClientObject_set_greenlet, METH_VARARGS, 0 },
    { NULL, NULL}
};



PyTypeObject SocketObjectType = {
	PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "meinheld.client",             /*tp_name*/
    sizeof(SocketObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)SocketObject_dealloc, /*tp_dealloc*/
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
    SocketObject_method,        /* tp_methods */
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


