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
#include <sys/sendfile.h>
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


#define SERVER "meinheld/0.4.3"

typedef enum {
    BODY_TYPE_NONE,
    BODY_TYPE_TMPFILE,
    BODY_TYPE_BUFFER
} request_body_type;

extern int max_content_length; //max_content_length

extern picoev_loop* main_loop; //main loop

extern PyObject* hub_switch_value;

extern PyObject* current_client;
extern PyObject* timeout_error;

inline void
switch_wsgi_app(picoev_loop* loop, int fd, PyObject *obj);

#endif
