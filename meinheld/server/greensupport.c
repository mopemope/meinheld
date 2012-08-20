#include "greensupport.h"

#include "greenlet.h"

PyObject *greenlet_exit;
PyObject *greenlet_error;

static int init = 0;

static inline void
import_greenlet(void)
{
    if(!init){
        PyGreenlet_Import();
        greenlet_exit = PyExc_GreenletExit;
        greenlet_error = PyExc_GreenletError;
        init = 1;
    }
}

PyObject*
greenlet_getcurrent(void)
{
    import_greenlet();
    return (PyObject*)PyGreenlet_GetCurrent();
}

PyObject*
greenlet_new(PyObject *o, PyObject *parent)
{
    import_greenlet();
    return (PyObject*)PyGreenlet_New(o, (PyGreenlet*)parent);
}

PyObject*
greenlet_setparent(PyObject *g, PyObject *parent)
{
    import_greenlet();
    return PyGreenlet_SetParent((PyGreenlet*)g, (PyGreenlet*)parent);
}

PyObject*
greenlet_getparent(PyObject *g)
{
    import_greenlet();
    return (PyObject*)PyGreenlet_GET_PARENT((PyGreenlet*)g);
}

PyObject*
greenlet_switch(PyObject *g, PyObject *args, PyObject *kwargs)
{
    import_greenlet();
    return PyGreenlet_Switch((PyGreenlet*)g, args, kwargs);
}

PyObject*
greenlet_throw(PyObject *g, PyObject *typ, PyObject *val, PyObject *tb)
{
    import_greenlet();
    return PyGreenlet_Throw((PyGreenlet*)g, typ, val, tb);
}

PyObject*
greenlet_throw_err(PyObject *g)
{
    PyObject *type, *value, *traceback;

    import_greenlet();
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_Clear();

    if(traceback == NULL){
        traceback = Py_None;
    }
    Py_INCREF(type);
    Py_INCREF(value);
    Py_INCREF(traceback);
    return PyGreenlet_Throw((PyGreenlet*)g, type, value, traceback);
}

int
greenlet_dead(PyObject *g)
{
    if(PyGreenlet_ACTIVE((PyGreenlet*)g) || !PyGreenlet_STARTED((PyGreenlet*)g)){
        return 0;
    }else{
        return 1;
    }
}

int
greenlet_check(PyObject *g)
{
    return PyGreenlet_Check((PyGreenlet*)g);
}

PyObject*
get_greenlet_dict(PyObject *o)
{
    PyGreenlet *g = (PyGreenlet*)o;
    if (g->dict == NULL) {
        g->dict = PyDict_New();
        if (g->dict == NULL){
          return NULL;
        }
    }
    Py_INCREF(g->dict);
    return g->dict;
}

