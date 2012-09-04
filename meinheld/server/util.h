#ifndef UTIL_H
#define UTIL_H

#include "meinheld.h"
#include "client.h"

int setup_listen_sock(int fd);

int setup_sock(int fd);

int enable_cork(client_t *client);

int disable_cork(client_t *client);

int set_so_keepalive(int fd, int flag);

uintptr_t get_current_msec(void);

#endif
