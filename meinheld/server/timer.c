#include "timer.h"
#include "greensupport.h"
#include "time_cache.h"

int
is_active_timer(TimerObject *timer)
{
    return timer && !timer->called;
}

TimerObject*
TimerObject_new(long seconds, PyObject *callback, PyObject *args, PyObject *kwargs, PyObject *greenlet)
{
    TimerObject *self;
    PyObject *temp = NULL;

    //self = PyObject_NEW(TimerObject, &TimerObjectType);
    self = PyObject_GC_New(TimerObject, &TimerObjectType);
    if(self == NULL){
        return NULL;
    }

    //DEBUG("args seconds:%ld callback:%p args:%p kwargs:%p", seconds, callback, args, kwargs);

    if(seconds > 0){
        self->seconds = current_msec/1000 + seconds;
    }else{
        self->seconds = 0;
    }

    Py_XINCREF(callback);
    Py_XINCREF(args);
    Py_XINCREF(kwargs);
    Py_XINCREF(greenlet);

    self->callback = callback;
    if(args != NULL){
        self->args = args;
    }else{
        temp = PyTuple_New(0); 
        self->args = temp;
    }
    self->kwargs = kwargs;
    self->called = 0;
    self->greenlet = greenlet;
    PyObject_GC_Track(self);
    GDEBUG("self:%p", self);
    return self;
}

void 
fire_timer(TimerObject *timer)
{
    PyObject *res = NULL;

    if(!timer->called){
        timer->called = 1;
        if (timer->greenlet) {
            DEBUG("call have greenlet timer:%p", timer);
            res = greenlet_switch(timer->greenlet, timer->args, timer->kwargs);
            if (greenlet_dead(timer->greenlet)) {
                Py_DECREF(timer->greenlet);
            }
        } else {
            DEBUG("call timer:%p", timer);
            res = PyEval_CallObjectWithKeywords(timer->callback, timer->args, timer->kwargs);
        }
        Py_XDECREF(res);
        DEBUG("called timer %p", timer);
    }
}

static int
TimerObject_clear(TimerObject *self)
{
    GDEBUG("self:%p", self);
    Py_CLEAR(self->args);
    Py_CLEAR(self->kwargs);
    Py_CLEAR(self->callback);
    Py_CLEAR(self->greenlet);
    return 0;
}

static int
TimerObject_traverse(TimerObject *self, visitproc visit, void *arg)
{
    GDEBUG("self:%p", self);
    Py_VISIT(self->args);
    Py_VISIT(self->kwargs);
    Py_VISIT(self->callback);
    Py_VISIT(self->greenlet);
    return 0;
}

static void
TimerObject_dealloc(TimerObject *self)
{
    GDEBUG("self %p", self);
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    TimerObject_clear(self);
    PyObject_GC_Del(self);
    Py_TRASHCAN_SAFE_END(self);
}

static PyObject *
TimerObject_cancel(TimerObject *self, PyObject *args)
{
    DEBUG("self %p", self);
    self->called = 1;

    Py_RETURN_NONE;
}

static PyMethodDef TimerObject_methods[] = {
    {"cancel", (PyCFunction)TimerObject_cancel, METH_NOARGS, 0},
    {NULL, NULL}
};

static PyMemberDef TimerObject_members[] = {
    {"called", T_BOOL, offsetof(TimerObject, called), READONLY, "Timer called"},
    {NULL}  /* Sentinel */
};

PyTypeObject TimerObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME ".Timer",             /*tp_name*/
    sizeof(TimerObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)TimerObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC,        /*tp_flags*/
    "Timer",           /* tp_doc */
    (traverseproc)TimerObject_traverse,                       /* tp_traverse */
    (inquiry)TimerObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    TimerObject_methods,          /* tp_methods */
    TimerObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    PyObject_GC_Del,                           /* tp_new */
};

