#include "log.h"
#include <sys/file.h>

#define LOG_BUF_SIZE 1024 * 16

static PyObject *access_logger;
static PyObject *err_logger;

int
set_access_logger(PyObject *obj)
{
    if(access_logger != NULL){
        Py_DECREF(access_logger);
    }
    access_logger = obj;
    //Py_INCREF(access_logger);
    return 1;
}

int
set_err_logger(PyObject *obj)
{
    if(err_logger != NULL){
        Py_DECREF(err_logger);
    }
    err_logger = obj;
    //Py_INCREF(err_logger);
    return 1;
}

int
call_access_logger(PyObject *environ)
{
    PyObject *args = NULL, *res = NULL;

    if(access_logger){
        if(environ == NULL){
            environ = Py_None;
            Py_INCREF(environ);
        }

        DEBUG("call access logger %p", access_logger);
        args = PyTuple_Pack(1, environ);
        res = PyObject_CallObject(access_logger, args);
        Py_DECREF(args);
        Py_XDECREF(res);
        if(PyErr_Occurred()){
            PyErr_Print();
            PyErr_Clear();
        }
    }
    return 1;
}


int
call_error_logger(void)
{
    PyObject *exception = NULL, *v = NULL, *tb = NULL;
    PyObject *args = NULL, *res = NULL;

    if(err_logger){
        PyErr_Fetch(&exception, &v, &tb);
        if(exception == NULL){
            goto err;
        }
        PyErr_NormalizeException(&exception, &v, &tb);
        if(exception == NULL){
            goto err;
        }
        DEBUG("exc:%p val:%p tb:%p",exception, v, tb);
        /* PySys_SetObject("last_type", exception); */
        /* PySys_SetObject("last_value", v); */
        /* PySys_SetObject("last_traceback", tb); */
        
        if(v == NULL){
            v = Py_None;
            Py_INCREF(v);
        }
        if(tb == NULL){
            tb = Py_None;
            Py_INCREF(tb);
        }
        PyErr_Clear();

        args = PyTuple_Pack(3, exception, v, tb);
        if(args == NULL){
            PyErr_Print();
            goto err;
        }
        DEBUG("call error logger %p", err_logger);
        res = PyObject_CallObject(err_logger, args);
        Py_DECREF(args);
        Py_XDECREF(res);
        Py_XDECREF(exception);
        Py_XDECREF(v);
        Py_XDECREF(tb);
        if(res == NULL){
            PyErr_Print();
        }
    }else{
        PyErr_Print();
    }
err:
    PyErr_Clear();
    return 1;
}


int
open_log_file(const char *path)
{
    return open(path, O_CREAT|O_APPEND|O_WRONLY, 0744);
}

/*
int
write_error_log(char *file_name, int line)
{
    char buf[256];
    int fd, ret;

    PyObject *f = PySys_GetObject("stderr");

    fd = PyObject_AsFileDescriptor(f);
    if(fd < 0){
        return -1;
    }
    ret = flock(fd, LOCK_EX);
    if(ret < 0){
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }

    cache_time_update();
    ret = write(fd, (char *)err_log_time, strlen((char*)err_log_time));
    if(ret < 0){
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    ret = write(fd, " [error] ", 9);
    if(ret < 0){
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }

    snprintf(buf, 256, "pid %d, File \"%s\", line %d \n", getpid(), file_name, line);
    ret = write(fd, buf, strlen(buf));
    if(ret < 0){
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }

    PyErr_Print();
    PyErr_Clear();

    ret = flock(fd, LOCK_UN);
    if(ret < 0){
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    return 1;
}
*/

/*
static int
write_log(const char *new_path, int fd, const char *data, size_t len)
{
    int openfd;
    flock(fd, LOCK_EX);

    if(write(fd, data, len) < 0){

        flock(fd, LOCK_UN);
        //reopen
        openfd = open_log_file(new_path);
        if(openfd < 0){
            //fail
            return -1; 
        }

        flock(openfd, LOCK_EX);
        if(write(openfd, data, len) < 0){
            flock(openfd, LOCK_UN);
            // write fail
            return -1;
        }
        flock(openfd, LOCK_UN);
        return openfd;
    }

    flock(fd, LOCK_UN);
    return fd;
}
*/
/*
int
write_access_log(client_t *cli, int log_fd, const char *log_path)
{
    char buf[LOG_BUF_SIZE];
    if(log_fd > 0){

        PyObject *obj;
        char *method, *path, *version, *referer, *ua;

        obj = PyDict_GetItemString(cli->environ, "REQUEST_METHOD");
        if(obj){
            method = PyBytes_AS_STRING(obj);
        }else{
            method = "-";
        }

        obj = PyDict_GetItemString(cli->environ, "PATH_INFO");
        if(obj){
            path = PyBytes_AS_STRING(obj);
        }else{
            path = "-";
        }

        obj = PyDict_GetItemString(cli->environ, "SERVER_PROTOCOL");
        if(obj){
            version = PyBytes_AS_STRING(obj);
        }else{
            version = "-";
        }

        obj = PyDict_GetItemString(cli->environ, "HTTP_USER_AGENT");
        if(obj){
            ua = PyBytes_AS_STRING(obj);
        }else{
            ua = "-";
        }

        obj = PyDict_GetItemString(cli->environ, "HTTP_REFERER");
        if(obj){
            referer = PyBytes_AS_STRING(obj);
        }else{
            referer = "-";
        }

        //update
        cache_time_update();

        snprintf(buf, LOG_BUF_SIZE, "%s - - [%s] \"%s %s %s\" %d %d \"%s\" \"%s\"\n",
               cli->remote_addr,
               http_log_time,
               method,
               path,
               version,
               cli->status_code,
               cli->write_bytes,
               referer,
               ua);
        return write_log(log_path, log_fd, buf, strlen(buf));
    }
    return 0;
}
*/

