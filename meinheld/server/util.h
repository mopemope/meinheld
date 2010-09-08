#ifndef UTIL_H
#define UTIL_H

#include "server.h"
#include "client.h"

inline void 
setup_listen_sock(int fd);

inline void 
setup_sock(int fd);

inline void 
enable_cork(client_t *client);

inline void 
disable_cork(client_t *client);

inline void 
set_so_keepalive(int fd, int flag);

#endif
