#include "stringio.h"

#define IO_MAXFREELIST 1024

static StringIOObject *io_free_list[IO_MAXFREELIST];
static int io_numfree = 0;

inline void
StringIOObject_list_fill(void)
{
    StringIOObject *io;
	while (io_numfree < IO_MAXFREELIST) {
        io = PyObject_NEW(StringIOObject, &StringIOObjectType);
		io_free_list[io_numfree++] = io;
	}
}

inline void
StringIOObject_list_clear(void)
{
	StringIOObject *op;

	while (io_numfree) {
		op = io_free_list[--io_numfree];
        PyObject_DEL(op);
	}
}

static inline StringIOObject*
alloc_StringIOObject(void)
{
    StringIOObject *io;
	if (io_numfree) {
		io = io_free_list[--io_numfree];
		_Py_NewReference((PyObject *)io);
#ifdef DEBUG
        printf("use pooled StringIOObject %p\n", io);
#endif
    }else{
        io = PyObject_NEW(StringIOObject, &StringIOObjectType);
#ifdef DEBUG
        printf("alloc StringIOObject %p\n", io);
#endif
    }
    return io;
}

static inline void
dealloc_StringIOObject(StringIOObject *io)
{
    //Py_CLEAR(client->greenlet);
	if (io_numfree < IO_MAXFREELIST){
#ifdef DEBUG
        printf("back to StringIOObject pool %p\n", io);
#endif
		io_free_list[io_numfree++] = io;
    }else{
	    PyObject_DEL(io);
    }
}

inline int 
CheckStringIOObject(PyObject *obj)
{
    if (obj->ob_type != &StringIOObjectType){
        return 0;
    }
    return 1;
}

inline PyObject*
StringIOObject_New(buffer *buffer)
{
    StringIOObject *io;
    io = alloc_StringIOObject();
    io->buffer = buffer;
    io->pos = 0;
    return (PyObject *)io;
}

inline void
StringIOObject_dealloc(StringIOObject *self)
{
    if(self->buffer){
        free_buffer(self->buffer);
        self->buffer = NULL;
    }
    dealloc_StringIOObject(self);
}

static inline PyObject*
StringIOObject_flush(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static inline PyObject* 
StringIOObject_getval(StringIOObject *self, PyObject *args)
{
    if(self->buffer == NULL){
        Py_RETURN_NONE;
    }
    PyObject *o;
    o = getPyString(self->buffer);
    self->buffer = NULL;
    return o;
}

static inline PyObject* 
StringIOObject_isatty(PyObject *self, PyObject *args)
{
    Py_INCREF(Py_False);
    return Py_False;
}

static inline PyObject* 
StringIOObject_read(StringIOObject *self, PyObject *args)
{
    Py_ssize_t n = -1, l = 0;

    if (!PyArg_ParseTuple(args, "|n:read", &n)){
        return NULL;
    }
    l = self->buffer->len - self->pos;
    if (n < 0 || n > l) {
        n = l;
        if (n < 0) {
            n = 0;
        }
    }
    self->buffer->buf += n;
    self->pos += n;
    return PyString_FromStringAndSize(self->buffer->buf, n);
}

static inline PyObject* 
StringIOObject_readline(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static inline PyObject* 
StringIOObject_readlines(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static inline PyObject* 
StringIOObject_reset(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static inline PyObject* 
StringIOObject_tell(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static inline PyObject* 
StringIOObject_truncate(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static inline PyObject* 
StringIOObject_close(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static inline PyObject* 
StringIOObject_seek(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static struct PyMethodDef StringIOObject_methods[] = {
  
  {"flush",     (PyCFunction)StringIOObject_flush,    METH_NOARGS, ""},
  {"getvalue",  (PyCFunction)StringIOObject_getval,   METH_VARARGS, ""},
  {"isatty",    (PyCFunction)StringIOObject_isatty,   METH_NOARGS, ""},
  {"read",	(PyCFunction)StringIOObject_read,     METH_VARARGS, ""},
  {"readline",	(PyCFunction)StringIOObject_readline, METH_VARARGS, ""},
  {"readlines",	(PyCFunction)StringIOObject_readlines,METH_VARARGS, ""},
  {"reset",	(PyCFunction)StringIOObject_reset,	  METH_NOARGS, ""},
  {"tell",      (PyCFunction)StringIOObject_tell,     METH_NOARGS,  ""},
  {"truncate",  (PyCFunction)StringIOObject_truncate, METH_VARARGS, ""},
  {"close",     (PyCFunction)StringIOObject_close,    METH_NOARGS, ""},
  {"seek",      (PyCFunction)StringIOObject_seek,     METH_VARARGS, ""},  
  {NULL,	NULL}
};



PyTypeObject StringIOObjectType = {
	PyObject_HEAD_INIT(&PyType_Type)
    0,
    "meinheld.stringio",             /*tp_name*/
    sizeof(StringIOObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)StringIOObject_dealloc, /*tp_dealloc*/
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
    "stringio",                 /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		                   /* tp_iternext */
    StringIOObject_methods,        /* tp_methods */
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

