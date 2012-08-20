#ifndef GREENSUPPORT_H
#define GREENSUPPORT_H

#include "meinheld.h"

//void import_greenlet(void);

extern PyObject *greenlet_exit;
extern PyObject *greenlet_error;

PyObject* greenlet_getcurrent(void);
PyObject* greenlet_new(PyObject *o, PyObject *parent);
PyObject* greenlet_setparent(PyObject *g, PyObject *parent);
PyObject* greenlet_getparent(PyObject *g);
PyObject* greenlet_switch(PyObject *g, PyObject *args, PyObject *kwargs);
PyObject* greenlet_throw(PyObject *g, PyObject *typ, PyObject *val, PyObject *tb);
PyObject* greenlet_throw_err(PyObject *g);
int greenlet_dead(PyObject *g);
int greenlet_check(PyObject *g);
PyObject* get_greenlet_dict(PyObject *o);

#endif
