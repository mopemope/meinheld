#include "util.h"


int
setup_listen_sock(int fd)
{
    int on = 1, r = -1;
#ifdef linux
    r = setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &on, sizeof(on));
#elif defined(__FreeBSD__) || defined(__NetBSD__)
    struct accept_filter_arg afa;
    bzero(&afa, sizeof(afa));
    strcpy(afa.af_name, "httpready");
    r = setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(afa));
#endif
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
    return r;
}

int
set_so_keepalive(int fd, int flag)
{
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
}

int
setup_sock(int fd)
{
    int r;
    int on = 1;
    r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    //assert(r == 0);

    // 60 + 30 * 4
    /*
    on = 300;
    r = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &on, sizeof(on));
    assert(r == 0);
    on = 30;
    r = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &on, sizeof(on));
    assert(r == 0);
    on = 4;
    r = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &on, sizeof(on));
    assert(r == 0);
    */
    /* if(r == -1){ */
        /* return r; */
    /* } */
#if linux
    r = 0; // Use accept4() on Linux
#else
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
#endif
    /* assert(r == 0); */
    return r;
}

int
enable_cork(client_t *client)
{
    int on = 1;
#if defined(linux) || defined(__sun)
    setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on));
#elif defined(__APPLE__) || defined(__FreeBSD__)
    setsockopt(client->fd, IPPROTO_TCP, TCP_NOPUSH, &on, sizeof(on));
#elif defined(__NetBSD__)
    /* not supported */
    return 1;
#else
#error
#endif
    DEBUG("enable TCP CORK");
    client->use_cork = 1;
    return 1;
}

int
disable_cork(client_t *client)
{
    int off = 0;
    int on = 1;
    if(client->use_cork == 1){
#if defined(linux) || defined(__sun)
        setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &off, sizeof(off));
#elif defined(__APPLE__) || defined(__FreeBSD__)
        setsockopt(client->fd, IPPROTO_TCP, TCP_NOPUSH, &off, sizeof(off));
#elif defined(__NetBSD__)
    /* not supported */
    return 1;
#else
#error
#endif
        DEBUG("disable TCP CORK");
        setsockopt(client->fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        client->use_cork = 0;
    }
    return 1;
}

uintptr_t
get_current_msec()
{
    time_t sec = 0;
    uintptr_t msec = 0;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    sec = tv.tv_sec;
    msec = tv.tv_usec / 1000;

    return (uintptr_t) sec * 1000 + msec;
}

