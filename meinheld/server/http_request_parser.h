#ifndef HTTP_REQUEST_PARSER_H
#define HTTP_REQUEST_PARSER_H

#include "client.h"
//#include "buffer.h"
//#include "request.h"

int init_parser(client_t *cli, const char *name, const short port);

size_t execute_parse(client_t *cli, const char *data, size_t len);

int parser_finish(client_t *cli);

void setup_static_env(char *name, int port);

void clear_static_env(void);

void parser_list_fill(void);

void parser_list_clear(void);

void dealloc_parser(http_parser *p);

PyObject* new_environ(client_t *client);

#endif
