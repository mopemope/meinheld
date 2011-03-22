#include "util.h"


inline void 
setup_listen_sock(int fd)
{
    int on = 1, r;
#ifdef linux
    r = setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &on, sizeof(on));
#elif defined(__FreeBSD__)
    struct accept_filter_arg afa;
    bzero(&afa, sizeof(afa));
    strcpy(afa.af_name, "httpready");
    r = setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(afa));
#endif
    assert(r == 0);
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
    assert(r == 0);
}

inline void 
set_so_keepalive(int fd, int flag)
{
    int r;
    r = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    assert(r == 0);

}

inline void 
setup_sock(int fd)
{
    int on = 1, r;
    r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    assert(r == 0);

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
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
    assert(r == 0);
}

inline void 
enable_cork(client_t *client)
{
    int on = 1, r;
#ifdef linux
    r = setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on));
#elif defined(__APPLE__) || defined(__FreeBSD__)
    r = setsockopt(client->fd, IPPROTO_TCP, TCP_NOPUSH, &on, sizeof(on));
#else
#error
#endif
    assert(r == 0);
    client->use_cork = 1;
}

inline void 
disable_cork(client_t *client)
{
    if(client->use_cork == 1){
        int off = 0;
        int on = 1, r;
#ifdef linux
        r = setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &off, sizeof(off));
#elif defined(__APPLE__) || defined(__FreeBSD__)
        r = setsockopt(client->fd, IPPROTO_TCP, TCP_NOPUSH, &off, sizeof(on));
#else
#error
#endif
        assert(r == 0);

        r = setsockopt(client->fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        assert(r == 0);
    }
}


