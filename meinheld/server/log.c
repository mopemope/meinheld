#include "log.h"
#include <sys/file.h>

#define LOG_BUF_SIZE 1024 * 16

int
open_log_file(const char *path)
{
    return open(path, O_CREAT|O_APPEND|O_WRONLY, 0744);
}

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
