#ifndef SERVER_H
#define SERVER_H

#include <Python.h>
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
#elif defined __APPLE__
#include <sys/uio.h>
#endif

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include "greenlet.h"
#include "picoev.h"
#include "request.h"
#include "http_parser.h"


#define SERVER "meinheld/0.4.10"


extern int max_content_length;      //max_content_length
extern int client_body_buffer_size; //client_body_buffer_size

extern picoev_loop* main_loop; //main loop

extern PyObject* hub_switch_value;

extern PyObject* current_client;
extern PyObject* timeout_error;

inline void
switch_wsgi_app(picoev_loop* loop, int fd, PyObject *obj);

#endif
