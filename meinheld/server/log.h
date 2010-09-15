#ifndef LOG_H
#define LOG_H

#include "client.h"
#include "time_cache.h"

inline int
open_log_file(const char *path);

inline void
write_error_log(char *file_name, int line);

inline int 
write_access_log(client_t *cli, int log_fd, const char *log_path);

#endif
