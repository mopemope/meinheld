#include "client.h"

inline PyObject* 
ClientObject_New(client_t* client)
{
    ClientObject *o = PyObject_NEW(ClientObject, &ClientObjectType);
    if(o == NULL){
        return NULL;
    }
    o->client = client;
    return (PyObject *)o;
}

static void
ClientObject_dealloc(ClientObject* self)
{
    self->client = NULL;
    PyObject_DEL(self);
}

PyTypeObject ClientObjectType = {
	PyVarObject_HEAD_INIT(&PyType_Type, 0)
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
    0,		               /* tp_iternext */
    0,             /* tp_methods */
    0,             /* tp_members */
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
