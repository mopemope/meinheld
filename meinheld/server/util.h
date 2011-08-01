#ifndef UTIL_H
#define UTIL_H

#include "meinheld.h"
#include "client.h"

void setup_listen_sock(int fd);

void setup_sock(int fd);

void enable_cork(client_t *client);

void disable_cork(client_t *client);

void set_so_keepalive(int fd, int flag);

#endif
