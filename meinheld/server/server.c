#include "server.h"

#include <arpa/inet.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "cStringIO.h"

#include "http_request_parser.h"
#include "response.h"
#include "log.h"
#include "client.h"
#include "util.h"

#define ACCEPT_TIMEOUT_SECS 1
#define SHORT_TIMEOUT_SECS 2

#define MAX_BUFSIZE 1024 * 8
#define INPUT_BUF_SIZE 1024 * 8

static char *server_name = "127.0.0.1";
static short server_port = 8000;
static int listen_sock;  // listen socket

static int loop_done; // main loop flag

picoev_loop* main_loop; //main loop

static PyObject *wsgi_app = NULL; //wsgi app

static PyObject *watchdog = NULL; //watchdog

static char *log_path = NULL; //access log path
static int log_fd = -1; //access log
static char *error_log_path = NULL; //error log path
static int err_log_fd = -1; //error log

static int is_keep_alive = 0; //keep alive support
static int keep_alive_timeout = 5;

int max_content_length = 1024 * 1024 * 16; //max_content_length

static char *unix_sock_name = NULL;

static int backlog = 1024 * 4; // backlog size
static int max_fd = 1024 * 4;  // picoev max_fd

// greenlet hub switch value
PyObject* hub_switch_value;
PyObject* current_client;
PyObject* timeout_error;

/* reuse object */
static PyObject *client_key = NULL; //meinheld.client
static PyObject *wsgi_input_key = NULL; //wsgi.input key
static PyObject *empty_string = NULL; //""

/* gunicorn */
static int spinner = 0;
static int tempfile_fd = 0;
static int ppid = 0;

static void
r_callback(picoev_loop* loop, int fd, int events, void* cb_arg);

static void
w_callback(picoev_loop* loop, int fd, int events, void* cb_arg);

static inline void
resume_wsgi_app(ClientObject *pyclient, picoev_loop* loop);


static inline int
setsig(int sig, void* handler)
{
    struct sigaction context, ocontext;
    context.sa_handler = handler;
    sigemptyset(&context.sa_mask);
    context.sa_flags = 0;
    return sigaction(sig, &context, &ocontext);
}


static inline client_t *
new_client_t(int client_fd, char *remote_addr, uint32_t remote_port){
    client_t *client;
    
    client = PyMem_Malloc(sizeof(client_t)); 
    memset(client, 0, sizeof(client_t));
    
    //printf("size %d\n", sizeof(client_t));

    client->fd = client_fd;
    
    client->remote_addr = remote_addr;
    client->remote_port = remote_port;
    client->req = new_request();    
    client->body_type = BODY_TYPE_NONE;
    //printf("input_buf_size %d\n", client->input_buf_size);
    return client;
}

static inline void
clean_cli(client_t *client)
{
    write_access_log(client, log_fd, log_path);
    if(client->req){
        free_request(client->req);
        client->req = NULL;
    }
    Py_CLEAR(client->http_status);
    Py_CLEAR(client->headers);
    Py_CLEAR(client->response_iter);
    
    Py_CLEAR(client->response);
#ifdef DEBUG
    printf("close environ %p \n", client->environ);
#endif
    
    // force clear
    PyDict_Clear(client->environ);
    
    Py_CLEAR(client->environ);
    if(client->body_type == BODY_TYPE_TMPFILE){
        if(client->body){
            fclose(client->body);
        }
    }else{
        Py_CLEAR(client->body);
    }
    if(client->http != NULL){
        PyMem_Free(client->http);
        client->http = NULL;
    }
    
}

static inline void 
close_conn(client_t *cli, picoev_loop* loop)
{
    client_t *new_client;
    if(!cli->response_closed){
        close_response(cli);
    }

    picoev_del(loop, cli->fd);

#ifdef DEBUG
    printf("picoev_del client:%p fd:%d \n", cli, cli->fd);
#endif
    clean_cli(cli);
    if(!cli->keep_alive){
        close(cli->fd);
#ifdef DEBUG
        printf("close client:%p fd:%d \n", cli, cli->fd);
#endif
    }else{
        disable_cork(cli);
        new_client = new_client_t(cli->fd, cli->remote_addr, cli->remote_port);
        new_client->keep_alive = 1;
        init_parser(new_client, server_name, server_port);
        picoev_add(main_loop, new_client->fd, PICOEV_READ, keep_alive_timeout, r_callback, (void *)new_client);
    }
    PyMem_Free(cli);

}

static inline int
process_resume_wsgi_app(ClientObject *pyclient)
{
    PyObject *res = NULL;
    PyObject *err_type, *err_val, *err_tb;
    client_t *old_client;
    client_t *client = pyclient->client;
    
    //swap bind client_t

    old_client = start_response->cli;
    start_response->cli = client;
    
    current_client = (PyObject *)pyclient;
    if(PyErr_Occurred()){
        PyErr_Fetch(&err_type, &err_val, &err_tb);
        PyErr_Clear();
        //set error
        res = PyGreenlet_Throw(pyclient->greenlet, err_type, err_val, err_tb);
    }else{
        res = PyGreenlet_Switch(pyclient->greenlet, pyclient->args, pyclient->kwargs);
    }
    start_response->cli = old_client;
    
    Py_CLEAR(pyclient->args);
    Py_CLEAR(pyclient->kwargs);
    

    //check response & PyErr_Occurred
    if(res && res == Py_None){
        PyErr_SetString(PyExc_Exception, "response must be a iter or sequence object");
    }

    if (PyErr_Occurred()){ 
        write_error_log(__FILE__, __LINE__);
        return -1;
    }
    
    if(PyInt_Check(res)){
        if(PyInt_AS_LONG(res) == -1){
            // suspend process
            return 0;
        }
    }

    client->response = res;
    //next send response 
    return 1;
    
}

static inline int
process_wsgi_app(client_t *cli)
{
    PyObject *args = NULL, *start = NULL, *res = NULL;
    PyGreenlet *greenlet;
    ClientObject *pyclient;
    start = create_start_response(cli);

    if(!start){
        return -1;
    }
    args = Py_BuildValue("(OO)", cli->environ, start);

    current_client = PyDict_GetItem(cli->environ, client_key);
    pyclient = (ClientObject *)current_client;

#ifdef DEBUG
    printf("start environ %p \n", cli->environ);
#endif

    //new greenlet
    greenlet = PyGreenlet_New(wsgi_app, NULL);
    // set_greenlet
    pyclient->greenlet = greenlet;
    Py_INCREF(pyclient->greenlet);

    res = PyGreenlet_Switch(greenlet, args, NULL);
    //res = PyObject_CallObject(wsgi_app, args);
    Py_DECREF(args);
    Py_DECREF(greenlet);
    

    //check response & PyErr_Occurred
    if(res && res == Py_None){
        PyErr_SetString(PyExc_Exception, "response must be a iter or sequence object");
    }

    if (PyErr_Occurred()){ 
        write_error_log(__FILE__, __LINE__);
        return -1;
    }
    
    if(PyInt_Check(res)){
        if(PyInt_AS_LONG(res) == -1){
            // suspend process
            return 0;
        }
    }

    //next send response 
    cli->response = res;
    
    return 1;
    
}

inline void
switch_wsgi_app(picoev_loop* loop, int fd, PyObject *obj)
{
    ClientObject *pyclient = (ClientObject *)obj;
    
    //clear event
    picoev_del(loop, fd);
    // resume
    resume_wsgi_app(pyclient, loop);
    pyclient->resumed = 0;
}

static void
resume_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject *)(cb_arg);
    client_t *client = pyclient->client;
    if ((events & PICOEV_TIMEOUT) != 0) {
        PyErr_SetString(timeout_error, "timeout");
        set_so_keepalive(client->fd, 0);
        
    }else if ((events & PICOEV_WRITE) != 0) {
        //None
    }
    switch_wsgi_app(loop, client->fd, (PyObject *)pyclient); 
}

static void
timeout_error_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject *)(cb_arg);
    client_t *client = pyclient->client;
    
    if ((events & PICOEV_TIMEOUT) != 0) {
#ifdef DEBUG
        printf("timeout_error_callback pyclient:%p client:%p fd:%d \n", pyclient, pyclient->client, pyclient->client->fd);
#endif
        pyclient->suspended = 0;
        pyclient->resumed = 1;
        PyErr_SetString(timeout_error, "timeout");
        set_so_keepalive(client->fd, 0);
        switch_wsgi_app(loop, client->fd, (PyObject *)pyclient); 
    }
}

static void
timeout_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject *)(cb_arg);
    client_t *client = pyclient->client;
    if ((events & PICOEV_TIMEOUT) != 0) {
#ifdef DEBUG
        printf("timeout_callback pyclient:%p client:%p fd:%d \n", pyclient, pyclient->client, pyclient->client->fd);
#endif
        //next intval 30sec
        picoev_set_timeout(loop, client->fd, 30);
        
        // is_active ??
        if(write(client->fd, "", 0) < 0){
            //resume       
            pyclient->suspended = 0;
            pyclient->resumed = 1;
            PyErr_SetFromErrno(PyExc_IOError);
#ifdef DEBUG
            printf("closed \n");
#endif
            set_so_keepalive(client->fd, 0);
            switch_wsgi_app(loop, client->fd, (PyObject *)pyclient); 
        }
    }
}


static void
w_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    client_t *client = ( client_t *)(cb_arg);
    int ret;
#ifdef DEBUG
    printf("call w_callback \n");
#endif
    if ((events & PICOEV_TIMEOUT) != 0) {

#ifdef DEBUG
        printf("** w_callback timeout ** \n");
#endif

        //timeout
        client->keep_alive = 0;
        close_conn(client, loop);
    
    } else if ((events & PICOEV_WRITE) != 0) {
        ret = process_body(client);
#ifdef DEBUG
        printf("process_body ret %d \n", ret);
#endif
        if(ret != 0){
            //ok or die
            close_conn(client, loop);
        }
    }
}

static inline void
resume_wsgi_app(ClientObject *pyclient, picoev_loop* loop)
{
    int ret;
    client_t *client = pyclient->client;
    ret = process_resume_wsgi_app(pyclient);
    switch(ret){
        case -1:
            //Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_conn(client, loop);
            return;
        case 0:
            // suspend
            return;
        default:
            break;
    }


    if(client->response_closed){
        //closed
        close_conn(client, loop);
        return;
    }
    
    ret = response_start(client);
    switch(ret){
        case -1:
            // Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_conn(client, loop);
            return;
        case 0:
            // continue
            // set callback
#ifdef DEBUG
            printf("set write callback %d \n", ret);
#endif
            //clear event
            picoev_del(loop, client->fd);
            picoev_add(loop, client->fd, PICOEV_WRITE, 0, w_callback, (void *)client);
            return;
        default:
            // send OK
            close_conn(client, loop);
    }
}

static inline void
call_wsgi_app(client_t *client, picoev_loop* loop)
{
    int ret;
    ret = process_wsgi_app(client);
    switch(ret){
        case -1:
            //Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_conn(client, loop);
            return;
        case 0:
            // suspend
            return;
        default:
            break;
    }
    
    
    if(client->response_closed){
        //closed
        close_conn(client, loop);
        return;
    }
    ret = response_start(client);
    switch(ret){
        case -1:
            // Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_conn(client, loop);
            return;
        case 0:
            // continue
            // set callback
#ifdef DEBUG
            printf("set write callback %d \n", ret);
#endif
            //clear event
            picoev_del(loop, client->fd);
            picoev_add(loop, client->fd, PICOEV_WRITE, 0, w_callback, (void *)client);
            return;
        default:
            // send OK
            close_conn(client, loop);
    }
    
    
}

static inline void
prepare_call_wsgi(client_t *client)
{
    PyObject *input = NULL, *object = NULL, *c = NULL;
    char *val;

    if(client->body_type == BODY_TYPE_TMPFILE){
        FILE *tmp = (FILE *)client->body;
        fflush(tmp);
        rewind(tmp);
        input = PyFile_FromFile(tmp, "<tmpfile>", "r", fclose);
        PyDict_SetItem((PyObject *)client->environ, wsgi_input_key, input);
        Py_DECREF(input);
        client->body = NULL;
    }else{
        if(client->body_type == BODY_TYPE_BUFFER){
            object = PycStringIO->cgetvalue((PyObject *)client->body);
            Py_XDECREF((PyObject *)client->body);
            input = PycStringIO->NewInput(object);
            PyDict_SetItem((PyObject *)client->environ, wsgi_input_key, input);
        }else{
            if(client->body){
                object = client->body;
            }else{
                object = empty_string;
                Py_INCREF(empty_string);
            }
            input = PycStringIO->NewInput(object);
            PyDict_SetItem((PyObject *)client->environ, wsgi_input_key, input);
        }
        client->body = object;
        //Py_DECREF(object);
        Py_DECREF(input);
    }
    if(is_keep_alive){
        //support keep-alive
        c = PyDict_GetItemString(client->environ, "HTTP_CONNECTION");
        if(c){
            val = PyString_AS_STRING(c);
            if(!strcasecmp(val, "keep-alive")){
                client->keep_alive = 1;
            }else{
                client->keep_alive = 0;
            }
        }else{
            client->keep_alive = 0;
        }
    }
}

static void
r_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    client_t *cli = ( client_t *)(cb_arg);
    PyObject *body = NULL;
    char *key = NULL;

    if ((events & PICOEV_TIMEOUT) != 0) {

#ifdef DEBUG
        printf("** r_callback timeout %d ** \n", fd);
#endif
        //timeout
        cli->keep_alive = 0;
        close_conn(cli, loop);
    
    } else if ((events & PICOEV_READ) != 0) {

#ifdef DEBUG
        printf("ready read \n");
#endif
        /* update timeout, and read */
        int finish = 0, nread;
        char buf[INPUT_BUF_SIZE];
        ssize_t r;
        if(!cli->keep_alive){
            picoev_set_timeout(loop, cli->fd, SHORT_TIMEOUT_SECS);
        }
        r = read(cli->fd, buf, sizeof(buf));
        switch (r) {
            case 0: 
                cli->keep_alive = 0;
                //503??
                cli->status_code = 503;
                send_error_page(cli);
                close_conn(cli, loop);
                return;
            case -1: /* error */
                if (errno == EAGAIN || errno == EWOULDBLOCK) { /* try again later */
                    break;
                } else { /* fatal error */
                    if(cli->keep_alive && errno == ECONNRESET){
                        
                        cli->keep_alive = 0;
                        cli->status_code = 500;
                        cli->header_done = 1;
                        cli->response_closed = 1;
                        
                    }else{
                        PyErr_SetFromErrno(PyExc_IOError);
                        write_error_log(__FILE__, __LINE__); 
                        cli->keep_alive = 0;
                        cli->status_code = 500;
                        if(errno != ECONNRESET){
                            send_error_page(cli);
                        }else{
                            cli->header_done = 1;
                            cli->response_closed = 1;
                        }
                    }
                    close_conn(cli, loop);
                    return;
                }
                break;
            default:

                nread = execute_parse(cli, buf, r);
#ifdef DEBUG
                printf("read request fd %d readed %d nread %d \n", cli->fd, r, nread);
                printf("%s\n", buf);
#endif
                
                if(cli->bad_request_code > 0){
#ifdef DEBUG
                    printf("fd %d bad_request code %d \n",cli->fd,  cli->bad_request_code);
#endif
                    send_error_page(cli);
                    close_conn(cli, loop);
                    return;
                }

                if(!cli->upgrade && nread != r){
                    // parse error
#ifdef DEBUG
                    printf("fd %d parse error %d \n", cli->fd, cli->bad_request_code);
#endif
                    cli->bad_request_code = 400;
                    send_error_page(cli);
                    close_conn(cli, loop);
                    return;
                }
                
#ifdef DEBUG
                printf("parse ok, fd %d %d nread \n", cli->fd, nread);
#endif
               
                if(parser_finish(cli) > 0){
                    if(cli->upgrade){
                        //WebSocket Key
                        key = buf + nread + 1;
                        body = PyString_FromStringAndSize(key, r - nread);
                        cli->body = body;

                    }
                    finish = 1;
                }
                break;
        }

        if(finish == 1){
            picoev_del(loop, cli->fd);
            prepare_call_wsgi(cli);
            call_wsgi_app(cli, loop);
            return;
        }
    }
}


static void
accept_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    int client_fd;
    client_t *client;
    struct sockaddr_in client_addr;
    char *remote_addr;
    uint32_t remote_port;
    if ((events & PICOEV_TIMEOUT) != 0) {
        // time out
        // next turn or other process
        return;
    }else if ((events & PICOEV_READ) != 0) {

        socklen_t client_len = sizeof(client_addr);
        client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd != -1) {
#ifdef DEBUG
            printf("accept fd %d \n", client_fd);
#endif
            //printf("connected: %d\n", client_fd);
            setup_sock(client_fd);
            remote_addr = inet_ntoa (client_addr.sin_addr);
            remote_port = ntohs(client_addr.sin_port);
            client = new_client_t(client_fd, remote_addr, remote_port);
            init_parser(client, server_name, server_port);
            picoev_add(loop, client_fd, PICOEV_READ, keep_alive_timeout, r_callback, (void *)client);
        }else{
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                PyErr_SetFromErrno(PyExc_IOError);
                write_error_log(__FILE__, __LINE__);
                // die
                loop_done = 0;
            }
        }

    }
}

static inline void
setup_server_env(void)
{
    setup_listen_sock(listen_sock);
    cache_time_init();
    setup_static_env(server_name, server_port);
    setup_start_response();
    setup_client();
    PycString_IMPORT;
    PyGreenlet_Import();
    
    hub_switch_value = Py_BuildValue("(i)", -1);
    client_key = PyString_FromString("meinheld.client");
    wsgi_input_key = PyString_FromString("wsgi.input");
    empty_string = PyString_FromString("");
}

static inline int 
inet_listen(void)
{
    struct addrinfo hints, *servinfo, *p;
    int flag = 1;
    int rv;
    char strport[7];

    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; 
    
    snprintf(strport, sizeof (strport), "%d", server_port);
    
    if ((rv = getaddrinfo(server_name, strport, &hints, &servinfo)) == -1) {
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((listen_sock = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            //perror("server: socket");
            continue;
        }

        if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &flag,
                sizeof(int)) == -1) {
            close(listen_sock);
            PyErr_SetFromErrno(PyExc_IOError);
            return -1;
        }

        if (bind(listen_sock, p->ai_addr, p->ai_addrlen) == -1) {
            close(listen_sock);
            PyErr_SetFromErrno(PyExc_IOError);
            return -1;
        }

        break;
    }

    if (p == NULL)  {
        close(listen_sock);
        PyErr_SetString(PyExc_IOError,"server: failed to bind\n");
        return -1;
    }

    freeaddrinfo(servinfo); // all done with this structure
    
    // BACKLOG 
    if (listen(listen_sock, backlog) == -1) {
        close(listen_sock);
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    return 1;
}

static inline int
check_unix_sockpath(char *sock_name)
{
    if(!access(sock_name, F_OK)){
        if(unlink(sock_name) < 0){
            PyErr_SetFromErrno(PyExc_IOError);
            return -1;
        }
    }
    return 1;
}

static inline int
unix_listen(char *sock_name)
{
    int flag = 1;
    struct sockaddr_un saddr;
    mode_t old_umask;

#ifdef DEBUG
    printf("unix domain socket %s\n", sock_name);
#endif
    memset(&saddr, 0, sizeof(saddr));
    check_unix_sockpath(sock_name);

    if ((listen_sock = socket(AF_UNIX, SOCK_STREAM,0)) == -1) {
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }

    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &flag,
            sizeof(int)) == -1) {
        close(listen_sock);
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }

    saddr.sun_family = PF_UNIX;
    strcpy(saddr.sun_path, sock_name);
    
    old_umask = umask(0);

    if (bind(listen_sock, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
        close(listen_sock);
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    umask(old_umask);

    // BACKLOG 1024
    if (listen(listen_sock, backlog) == -1) {
        close(listen_sock);
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    unix_sock_name = sock_name;
    return 1;
}

static inline void
fast_notify()
{
    spinner = (spinner + 1) % 2;
    fchmod(tempfile_fd, spinner);
    if(ppid != getppid()){
        loop_done = 0;
        tempfile_fd = 0;
    }
}

static PyObject *
meinheld_listen(PyObject *self, PyObject *args)
{
    PyObject *o;
    int ret;

    if (!PyArg_ParseTuple(args, "O:listen", &o))
        return NULL;

    if(listen_sock > 0){
        PyErr_SetString(PyExc_Exception, "already set listen socket");
        return NULL;
    }
    
    if(PyTuple_Check(o)){
        //inet 
        if(!PyArg_ParseTuple(o, "si:listen", &server_name, &server_port))
            return NULL;
        ret = inet_listen();
    }else if(PyString_Check(o)){
        // unix domain 
        ret = unix_listen(PyString_AS_STRING(o));
    }else{
        PyErr_SetString(PyExc_TypeError, "args tuple or string(path)");
        return NULL;
    }
    if(ret < 0){
        //error 
        listen_sock = -1;
        return NULL;
    }

    Py_RETURN_NONE;
}

static void 
sigint_cb(int signum)
{
    loop_done = 0;
}

static void 
sigpipe_cb(int signum)
{
}

static PyObject * 
meinheld_access_log(PyObject *self, PyObject *args)
{   
    if (!PyArg_ParseTuple(args, "s:access_log", &log_path))
        return NULL;
    

    if(log_fd > 0){
        close(log_fd);
    }
    
    if(!strcasecmp(log_path, "stdout")){
        log_fd = 1;
        Py_RETURN_NONE;
    }
    if(!strcasecmp(log_path, "stderr")){
        log_fd = 2;
        Py_RETURN_NONE;
    }

    log_fd = open_log_file(log_path);
    if(log_fd < 0){
        PyErr_Format(PyExc_TypeError, "not open file. %s", log_path);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject * 
meinheld_error_log(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, "s:error_log", &error_log_path))
        return NULL;
    if(err_log_fd > 0){
        close(err_log_fd);
    }
    PyObject *f = PyFile_FromString(error_log_path, "a");
    if(!f){
        PyErr_Format(PyExc_TypeError, "not open file. %s", error_log_path);
        return NULL;
    }
    PySys_SetObject("stderr", f);
    Py_DECREF(f);
    err_log_fd = PyObject_AsFileDescriptor(f);

    Py_RETURN_NONE;
}


static PyObject *
meinheld_stop(PyObject *self, PyObject *args)
{
    loop_done = 0;
    Py_RETURN_NONE;
}

static PyObject *
meinheld_run_loop(PyObject *self, PyObject *args)
{
    int i = 0;
    //PyObject *app;
    PyObject *watchdog_result;
    if (!PyArg_ParseTuple(args, "O:run", &wsgi_app))
        return NULL; 
    
    if(listen_sock <= 0){
        PyErr_Format(PyExc_TypeError, "not found listen socket");
        return NULL;
        
    }
    
    Py_INCREF(wsgi_app);
    setup_server_env();

    /* init picoev */
    picoev_init(max_fd);
    /* create loop */
    main_loop = picoev_create_loop(60);
    loop_done = 1;
    
    setsig(SIGPIPE, sigpipe_cb);
    setsig(SIGINT, sigint_cb);
    setsig(SIGTERM, sigint_cb);

    picoev_add(main_loop, listen_sock, PICOEV_READ, ACCEPT_TIMEOUT_SECS, accept_callback, NULL);
    
    /* loop */
    while (loop_done) {
        picoev_loop_once(main_loop, 10);
        i++;
        // watchdog slow.... skip check
        
        //if(watchdog && i > 1){
        if(watchdog){
            watchdog_result = PyObject_CallFunction(watchdog, NULL);
            if(PyErr_Occurred()){
                PyErr_Print();
                PyErr_Clear();
            }
            Py_XDECREF(watchdog_result);
            i = 0;
        }else if(tempfile_fd){
            fast_notify();
        }
#ifdef DEBUG
        //printf("loop \n");
#endif
    }

    Py_DECREF(wsgi_app);
    Py_XDECREF(watchdog);
    
    picoev_destroy_loop(main_loop);
    picoev_deinit();
    
    //clean
    clear_start_response();
    clear_static_env();

    Py_DECREF(hub_switch_value);
    Py_DECREF(client_key);
    Py_DECREF(wsgi_input_key);
    Py_DECREF(empty_string);

    if(unix_sock_name){
        unlink(unix_sock_name);
    }
    printf("Bye.\n");
    Py_RETURN_NONE;
}


PyObject *
meinheld_set_keepalive(PyObject *self, PyObject *args)
{
    int on;
    if (!PyArg_ParseTuple(args, "i", &on))
        return NULL;
    if(on < 0){
        PyErr_SetString(PyExc_ValueError, "keep alive value out of range ");
        return NULL;
    }
    is_keep_alive = on;
    if(is_keep_alive){
        keep_alive_timeout = on;
    }else{
        keep_alive_timeout = 2;
    }
    Py_RETURN_NONE;
}

PyObject *
meinheld_get_keepalive(PyObject *self, PyObject *args)
{
    return Py_BuildValue("i", is_keep_alive);
}

PyObject *
meinheld_set_backlog(PyObject *self, PyObject *args)
{
    int temp;
    if (!PyArg_ParseTuple(args, "i", &temp))
        return NULL;
    if(temp <= 0){
        PyErr_SetString(PyExc_ValueError, "backlog value out of range ");
        return NULL;
    }
    backlog = temp;
    Py_RETURN_NONE;
}

PyObject *
meinheld_get_backlog(PyObject *self, PyObject *args)
{
    return Py_BuildValue("i", backlog);
}

PyObject *
meinheld_set_picoev_max_fd(PyObject *self, PyObject *args)
{
    int temp;
    if (!PyArg_ParseTuple(args, "i", &temp))
        return NULL;
    if(temp <= 0){
        PyErr_SetString(PyExc_ValueError, "max_fd value out of range ");
        return NULL;
    }
    max_fd = temp;
    Py_RETURN_NONE;
}

PyObject *
meinheld_get_picoev_max_fd(PyObject *self, PyObject *args)
{
    return Py_BuildValue("i", max_fd);
}

PyObject *
meinheld_set_max_content_length(PyObject *self, PyObject *args)
{
    int temp;
    if (!PyArg_ParseTuple(args, "i", &temp))
        return NULL;
    if(temp <= 0){
        PyErr_SetString(PyExc_ValueError, "max_content_length value out of range ");
        return NULL;
    }
    max_content_length = temp;
    Py_RETURN_NONE;
}

PyObject *
meinheld_get_max_content_length(PyObject *self, PyObject *args)
{
    return Py_BuildValue("i", max_content_length);
}

PyObject *
meinheld_set_listen_socket(PyObject *self, PyObject *args)
{
    int temp_sock;
    if (!PyArg_ParseTuple(args, "i:listen_socket", &temp_sock))
        return NULL;
    if(listen_sock > 0){
        PyErr_SetString(PyExc_Exception, "already set listen socket");
        return NULL;
    }
    listen_sock = temp_sock;
    Py_RETURN_NONE;
}

PyObject *
meinheld_set_fastwatchdog(PyObject *self, PyObject *args)
{
    int _fd;
    int _ppid;
    if (!PyArg_ParseTuple(args, "ii", &_fd, &_ppid))
        return NULL;
    
    tempfile_fd = _fd;
    ppid = _ppid;
    Py_RETURN_NONE;
}

PyObject *
meinheld_set_watchdog(PyObject *self, PyObject *args)
{
    PyObject *temp;
    if (!PyArg_ParseTuple(args, "O:watchdog", &temp))
        return NULL;
    
    if(!PyCallable_Check(temp)){
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }
    watchdog = temp;
    Py_INCREF(watchdog);
    Py_RETURN_NONE;
}

PyObject *
meinheld_set_process_name(PyObject *self, PyObject *args)
{
    int i = 0,argc,len;
    char **argv;
    char *name;

    if (!PyArg_ParseTuple(args, "s:process name", &name)){
        return NULL;
    }

    Py_GetArgcArgv(&argc, &argv);

    for(i = 0;i < argc; i++){
        len = strlen(argv[i]);
        memset(argv[i], 0, len);
    }

    strcpy(*argv, name);
    prctl(15, name, 0, 0, 0);
    Py_RETURN_NONE;
}

PyObject *
meinheld_suspend_client(PyObject *self, PyObject *args)
{
    PyObject *temp;
    ClientObject *pyclient;
    client_t *client;
    PyGreenlet *parent;
    int timeout = 0;

    if (!PyArg_ParseTuple(args, "O|i:_suspend_client", &temp, &timeout)){
        return NULL;
    }
    if(timeout < 0){
        PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
        return NULL;
    }
    
    // check client object
    if(!CheckClientObject(temp)){
        PyErr_SetString(PyExc_TypeError, "must be a client object");
        return NULL;
    }

    pyclient = (ClientObject *)temp;
    client = pyclient->client;

    if(!pyclient->greenlet){
        PyErr_SetString(PyExc_ValueError, "greenlet is not set");
        return NULL;
    }

    if(pyclient->resumed == 1){
        //call later
        PyErr_SetString(PyExc_IOError, "not called resume");
        return NULL;
    }
    
    if(client && !(pyclient->suspended)){
        pyclient->suspended = 1;
        parent = PyGreenlet_GET_PARENT(pyclient->greenlet);

        set_so_keepalive(client->fd, 1);
#ifdef DEBUG
        printf("meinheld_suspend_client pyclient:%p client:%p fd:%d \n", pyclient, client, client->fd);
        printf("meinheld_suspend_client active ? %d \n", picoev_is_active(main_loop, client->fd));
#endif
        //clear event
        picoev_del(main_loop, client->fd);
        if(timeout > 0){
            picoev_add(main_loop, client->fd, PICOEV_TIMEOUT, timeout, timeout_error_callback, (void *)pyclient);
        }else{
            picoev_add(main_loop, client->fd, PICOEV_TIMEOUT, 300, timeout_callback, (void *)pyclient);
        }
        return PyGreenlet_Switch(parent, hub_switch_value, NULL);
    }else{
        PyErr_SetString(PyExc_Exception, "already suspended");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyObject *
meinheld_resume_client(PyObject *self, PyObject *args)
{
    PyObject *temp, *switch_args, *switch_kwargs;
    ClientObject *pyclient;
    client_t *client;

    if (!PyArg_ParseTuple(args, "O|OO:_resume_client", &temp, &switch_args, &switch_kwargs)){
        return NULL;
    }

    // check client object
    if(!CheckClientObject(temp)){
        PyErr_SetString(PyExc_TypeError, "must be a client object");
        return NULL;
    }

    pyclient = (ClientObject *)temp;
    client = pyclient->client;

    if(!pyclient->greenlet){
        PyErr_SetString(PyExc_ValueError, "greenlet is not set");
        return NULL;
    }

    if(!pyclient->suspended){
        // not suspend
        PyErr_SetString(PyExc_IOError, "not suspended or dead ");
        return NULL;
    }

    if(pyclient->client && !pyclient->resumed){
        set_so_keepalive(pyclient->client->fd, 0);
        pyclient->args = switch_args;
        Py_XINCREF(pyclient->args);
    
        pyclient->kwargs = switch_kwargs;
        Py_XINCREF(pyclient->kwargs);

        pyclient->suspended = 0;
        pyclient->resumed = 1;
#ifdef DEBUG
        printf("meinheld_resume_client pyclient:%p client:%p fd:%d \n", pyclient, pyclient->client, pyclient->client->fd);
        printf("meinheld_resume_client active ? %d \n", picoev_is_active(main_loop, pyclient->client->fd));
#endif
        //clear event
        picoev_del(main_loop, client->fd);
        picoev_add(main_loop, client->fd, PICOEV_WRITE, 0, resume_callback, (void *)pyclient);
    }else{
        PyErr_SetString(PyExc_Exception, "already resumed");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyObject *
meinheld_cancel_wait(PyObject *self, PyObject *args)
{
    int fd;
    if (!PyArg_ParseTuple(args, "i:cancel_event", &fd)){
        return NULL;
    }

    if(fd < 0){
        PyErr_SetString(PyExc_ValueError, "fileno value out of range ");
        return NULL;
    }
    picoev_del(main_loop, fd);
    Py_RETURN_NONE;
}

static inline void
trampolin_switch_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject *)cb_arg;

    if ((events & PICOEV_TIMEOUT) != 0) {
        PyErr_SetString(PyExc_IOError, "timeout");
        switch_wsgi_app(loop, fd, (PyObject *)pyclient);
    } else if ((events & PICOEV_WRITE) != 0 ||  (events & PICOEV_READ) != 0) {
        switch_wsgi_app(loop, fd, (PyObject *)pyclient);
    }
}

static inline PyObject*
meinheld_trampolin(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyGreenlet *current, *parent;
    ClientObject *pyclient;
    int fd, event, timeout = 0;
    PyObject *read = Py_None, *write = Py_None;

	static char *keywords[] = {"fileno", "read", "write", "timeout", NULL};
	
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|OOi:trampolin", keywords, &fd, &read, &write, &timeout)){
		return NULL;
    }
    
    if(fd < 0){
        PyErr_SetString(PyExc_ValueError, "fileno value out of range ");
        return NULL;
    }
    
    if(timeout < 0){
        PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
        return NULL;
    }

    if(PyObject_IsTrue(read) && PyObject_IsTrue(write)){
        event = PICOEV_READWRITE;
    }else if(PyObject_IsTrue(read)){
        event = PICOEV_READ;
    }else if(PyObject_IsTrue(write)){
        event = PICOEV_WRITE;
    }else{
        event = PICOEV_TIMEOUT;
        if(timeout <= 0){
            PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
            return NULL;
        }
    }

    pyclient =(ClientObject *) current_client;
    
    picoev_del(main_loop, fd);
    picoev_add(main_loop, fd, event, timeout, trampolin_switch_callback, (void *)pyclient);
   
    // switch to hub
    current = pyclient->greenlet;
    parent = PyGreenlet_GET_PARENT(current);
    return PyGreenlet_Switch(parent, hub_switch_value, NULL);

}




static PyMethodDef WsMethods[] = {
    {"listen", meinheld_listen, METH_VARARGS, "set host and port num"},
    {"access_log", meinheld_access_log, METH_VARARGS, "set access log file path."},
    {"error_log", meinheld_error_log, METH_VARARGS, "set error log file path."},

    {"set_keepalive", meinheld_set_keepalive, METH_VARARGS, "set keep-alive support. value set timeout sec. default 0. (disable keep-alive)"},
    {"get_keepalive", meinheld_get_keepalive, METH_VARARGS, "return keep-alive support."},
    
    {"set_max_content_length", meinheld_set_max_content_length, METH_VARARGS, "set max_content_length"},
    {"get_max_content_length", meinheld_get_max_content_length, METH_VARARGS, "return max_content_length"},
    
    {"set_backlog", meinheld_set_backlog, METH_VARARGS, "set backlog size"},
    {"get_backlog", meinheld_get_backlog, METH_VARARGS, "return backlog size"},

    {"set_picoev_max_fd", meinheld_set_picoev_max_fd, METH_VARARGS, "set picoev max fd size"},
    {"get_picoev_max_fd", meinheld_get_picoev_max_fd, METH_VARARGS, "return picoev max fd size"},

    {"set_process_name", meinheld_set_process_name, METH_VARARGS, "set process name"},
    {"stop", meinheld_stop, METH_VARARGS, "stop main loop"},
    // support gunicorn 
    {"set_listen_socket", meinheld_set_listen_socket, METH_VARARGS, "set listen_sock"},
    {"set_watchdog", meinheld_set_watchdog, METH_VARARGS, "set watchdog"},
    {"set_fastwatchdog", meinheld_set_fastwatchdog, METH_VARARGS, "set watchdog"},
    {"run", meinheld_run_loop, METH_VARARGS, "set wsgi app, run the main loop"},
    // greenlet and continuation
    {"_suspend_client", meinheld_suspend_client, METH_VARARGS, "resume client"},
    {"_resume_client", meinheld_resume_client, METH_VARARGS, "resume client"},
    // io
    {"cancel_wait", meinheld_cancel_wait, METH_VARARGS, "cancel wait"},
    {"trampolin", (PyCFunction)meinheld_trampolin, METH_VARARGS | METH_KEYWORDS, "trampolin"},

    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
initserver(void)
{
    PyObject *m;
    m = Py_InitModule("meinheld.server", WsMethods);
    if(m == NULL){
        return;
    }

    if(PyType_Ready(&FileWrapperType) < 0){ 
        return;
    }

    if(PyType_Ready(&ClientObjectType) < 0){
        return;
    }

    timeout_error = PyErr_NewException("meinheld.server.timeout",
					  PyExc_IOError, NULL);
	if (timeout_error == NULL)
		return;
	Py_INCREF(timeout_error);
	PyModule_AddObject(m, "timeout", timeout_error);

#ifdef DEBUG
    printf("client size %d \n", sizeof(client_t));
#endif
}


