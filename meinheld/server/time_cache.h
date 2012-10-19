#ifndef TIMEOUT_CACHE_H
#define TIMEOUT_CACHE_H

#include "meinheld.h"

typedef struct {
    time_t sec;
    uintptr_t msec;
    int   gmtoff;
} cache_time_t;

void cache_time_init(void);

void cache_time_update(void);

extern volatile uintptr_t current_msec;
extern volatile char *err_log_time;
extern volatile char *http_time;
extern volatile char *http_log_time;

#endif
