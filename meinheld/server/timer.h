#ifndef TIMER_H
#define TIMER_H

#include "meinheld.h"

typedef struct {
  PyObject_HEAD PyObject *args;
  PyObject *kwargs;
  PyObject *callback;
  time_t seconds;
  char called;
} TimerObject;

extern PyTypeObject TimerObjectType;

TimerObject *TimerObject_new(long seconds, PyObject *callback, PyObject *args,
                             PyObject *kwargs);

void fire_timer(TimerObject *timer);

int is_active_timer(TimerObject *timer);

#endif
