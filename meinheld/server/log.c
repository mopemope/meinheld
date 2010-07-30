#include "log.h"
#include <sys/file.h>

int
open_log_file(const char *path)
{
    return open(path, O_CREAT|O_APPEND|O_WRONLY, 0744);
}

void
write_error_log(char *file_name, int line)
{
    char buf[64];

    PyObject *f = PySys_GetObject("stderr");
    
    FILE *fp = PyFile_AsFile(f);
    int fd = fileno(fp);
    flock(fd, LOCK_EX);

    cache_time_update();
    fputs((char *)err_log_time, fp);
    fputs(" [error] ", fp);
    
    sprintf(buf, "pid %d, File \"%s\", line %d :", getpid(), file_name, line);
    fputs(buf, fp);
    
    PyErr_Print();
    PyErr_Clear();
    fflush(fp);
    
    flock(fd, LOCK_UN);

}

static inline int
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
    char buf[1024];
    if(log_fd > 0){
        
        PyObject *obj;
        char *method, *path, *version, *ua;
        
        obj = PyDict_GetItemString(cli->environ, "REQUEST_METHOD");
        if(obj){
            method = PyString_AS_STRING(obj);
        }else{
            method = "-";
        }
                
        obj = PyDict_GetItemString(cli->environ, "PATH_INFO");
        if(obj){
            path = PyString_AS_STRING(obj);
        }else{
            path = "-";
        }
        
        obj = PyDict_GetItemString(cli->environ, "SERVER_PROTOCOL");
        if(obj){
            version = PyString_AS_STRING(obj);
        }else{
            version = "-";
        }
        
        obj = PyDict_GetItemString(cli->environ, "HTTP_USER_AGENT");
        if(obj){
            ua = PyString_AS_STRING(obj);
        }else{
            ua = "-";
        }

        //update
        cache_time_update();
        
        sprintf(buf, "%s - - [%s] \"%s %s %s\" %d %d \"-\" \"%s\"\n", 
               cli->remote_addr,
               http_log_time,
               method,
               path,
               version,
               cli->status_code,
               cli->write_bytes,
               ua);
        return write_log(log_path, log_fd, buf, strlen(buf));
    }
    return 0;
}
