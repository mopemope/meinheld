#ifndef HTTP_REQUEST_PARSER_H
#define HTTP_REQUEST_PARSER_H

#include "server.h"
#include "buffer.h"
#include "request.h"

inline int 
init_parser(client_t *cli, const char *name, const short port);

inline size_t 
execute_parse(client_t *cli, const char *data, size_t len);

inline int 
parser_finish(client_t *cli);

inline void 
setup_static_env(char *name, int port);

inline void
clear_static_env(void);

#endif
