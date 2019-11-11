#ifndef MEINHELD_H
#define MEINHELD_H

#include <Python.h>
#include <structmember.h>

#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#ifdef linux
#include <sys/sendfile.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/uio.h>
#endif

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>

#include "http_parser.h"

#define SERVER "meinheld/1.0.1"
#define MODULE_NAME "meinheld.server"

#ifdef DEVELOP
#define DEBUG(...) \
    do { \
        /*printf("DEBUG: ");*/ \
        printf("%-40s %-26s%4u: ", __FILE__, __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } while(0)
#define RDEBUG(...) \
    do { \
        /*printf("%-22s%4u: ", __FILE__, __LINE__);*/ \
        printf("\x1B[31m%-40s %-26s%4u: ", __FILE__, __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\x1B[0m\n"); \
    } while(0)
#define GDEBUG(...) \
    do { \
        /*printf("%-22s%4u: ", __FILE__, __LINE__);*/ \
        printf("\x1B[32m%-40s %-26s%4u: ", __FILE__, __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\x1B[0m\n"); \
    } while(0)
#define BDEBUG(...) \
    do { \
        /*printf("%-22s%4u: ", __FILE__, __LINE__);*/ \
        printf("\x1B[1;34m%-40s %-26s%4u: ", __FILE__, __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\x1B[0m\n"); \
    } while(0)
#define YDEBUG(...) \
    do { \
        /*printf("%-22s%4u: ", __FILE__, __LINE__);*/ \
        printf("\x1B[1;33m%-40s %-26s%4u: ", __FILE__, __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\x1B[0m\n"); \
    } while(0)
#else
#define DEBUG(...) do{}while(0)
#define RDEBUG(...) do{}while(0)
#define GDEBUG(...) do{}while(0)
#define BDEBUG(...) do{}while(0)
#define YDEBUG(...) do{}while(0)
#endif

#if __GNUC__ >= 3
# define likely(x)    __builtin_expect(!!(x), 1)
# define unlikely(x)    __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

#define NO_GREENLET_ERROR \
    PyErr_SetString(PyExc_NotImplementedError, "greenlet not support"); \
    return NULL;\

//#if (PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION >= 2) || PY_MAJOR_VERSION > 3
#if PY_MAJOR_VERSION >= 3
static __inline__ char*
as_str(PyObject* obj)
{
    char *c = NULL;
    PyObject *latin1;

    latin1 = PyUnicode_AsLatin1String(obj);
    if(latin1 == NULL){
        return NULL;
    }
    c = PyBytes_AsString(latin1);
    Py_DECREF(latin1);
    return c;
}
# define PY3
# define NATIVE_GET_STRING_SIZE  PyUnicode_GET_SIZE
# define NATIVE_ASSTRING  as_str
# define NATIVE_FROMSTRING  PyUnicode_FromString
# define NATIVE_FROMSTRINGANDSIZE  PyUnicode_FromStringAndSize
# define NATIVE_FROMFORMAT  PyUnicode_FromFormat
#else
# define NATIVE_GET_STRING_SIZE  PyBytes_GET_SIZE
# define NATIVE_ASSTRING  PyBytes_AsString
# define NATIVE_FROMSTRING  PyBytes_FromString
# define NATIVE_FROMSTRINGANDSIZE  PyBytes_FromStringAndSize
# define NATIVE_FROMFORMAT  PyBytes_FromFormat
#endif

#if PY_MAJOR_VERSION < 3
#ifndef Py_REFCNT
#  define Py_REFCNT(ob) (((PyObject *) (ob))->ob_refcnt)
#endif
#ifndef Py_TYPE
#  define Py_TYPE(ob)   (((PyObject *) (ob))->ob_type)
#endif
#endif


#endif
