#ifndef SERVER_H
#define SERVER_H

#include "meinheld.h"
#include "picoev.h"
#include "request.h"
#include "http_parser.h"


extern uint64_t max_content_length;      //max_content_length
extern int client_body_buffer_size; //client_body_buffer_size
extern picoev_loop* main_loop; //main loop
extern PyObject* hub_switch_value;
extern PyObject* current_client;
extern PyObject* timeout_error;

void switch_wsgi_app(picoev_loop* loop, int fd, PyObject *obj);

#endif
