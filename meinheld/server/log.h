#ifndef LOG_H
#define LOG_H

#include "client.h"
#include "time_cache.h"

// int open_log_file(const char *path);

// int write_error_log(char *file_name, int line);

// int write_access_log(client_t *cli, int log_fd, const char *log_path);

int set_access_logger(PyObject *obj);
int set_err_logger(PyObject *obj);

int call_access_logger(PyObject *environ);
int call_error_logger(void);


#endif
