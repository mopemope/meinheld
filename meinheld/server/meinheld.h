#ifndef MEINHELD_H
#define MEINHELD_H

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
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/uio.h>
#endif

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>

#include "greenlet.h"
#include "picoev.h"
#include "http_parser.h"

#define SERVER "meinheld/0.5dev"

#ifdef DEVELOP
#define DEBUG(...) \
    do { \
        /*printf("DEBUG: ");*/ \
        printf("%-40s %-22s%4u: ", __FILE__, __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } while(0)
#define RDEBUG(...) \
    do { \
        /*printf("%-22s%4u: ", __FILE__, __LINE__);*/ \
        printf("\x1B[31m%-40s %-22s%4u: ", __FILE__, __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\x1B[0m\n"); \
    } while(0)
#else
#define DEBUG(...) do{}while(0)
#define RDEBUG(...) do{}while(0)
#endif

#if __GNUC__ >= 3
# define likely(x)	__builtin_expect(!!(x), 1)
# define unlikely(x)	__builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

#endif

