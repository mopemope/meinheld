#ifndef LOG_H
#define LOG_H

#include "server.h"
#include "time_cache.h"

int
open_log_file(const char *path);

void
write_error_log(char *file_name, int line);

int 
write_access_log(client_t *cli, int log_fd, const char *log_path);

#endif
