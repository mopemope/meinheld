#include "server.h"

#include <arpa/inet.h>
#include <signal.h>

#ifdef linux
#include <sys/prctl.h>
#endif

#include <sys/un.h>
#include <sys/stat.h>

#include "http_request_parser.h"
#include "response.h"
#include "log.h"
#include "client.h"
#include "util.h"
#include "input.h"
#include "timer.h"
#include "heapq.h"

#ifdef WITH_GREENLET
#include "greensupport.h"
#endif

#define ACCEPT_TIMEOUT_SECS 1
#define READ_TIMEOUT_SECS 30

#define READ_BUF_SIZE 1024 * 64


static char *server_name = "127.0.0.1";
static short server_port = 8000;
static int listen_sock;  // listen socket

static volatile sig_atomic_t loop_done;
static volatile sig_atomic_t call_shutdown = 0;
static volatile sig_atomic_t catch_signal = 0;

static picoev_loop* main_loop = NULL; //main loop
static heapq_t *g_timers;

// active event cnt
static int activecnt = 0;

static PyObject *wsgi_app = NULL; //wsgi app

static uint8_t watch_loop = 0;
static PyObject *watchdog = NULL; //watchdog

static char *log_path = NULL; //access log path
static int log_fd = -1; //access log
static char *error_log_path = NULL; //error log path
static int err_log_fd = -1; //error log

static int is_keep_alive = 0; //keep alive support
static int keep_alive_timeout = 5;

uint64_t max_content_length = 1024 * 1024 * 16; //max_content_length
int client_body_buffer_size = 1024 * 500;  //client_body_buffer_size

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

static PyObject *app_handler_func = NULL; //""

/* gunicorn */
static int spinner = 0;
static int tempfile_fd = 0;
static int gtimeout = 0;
static int ppid = 0;

#define CLIENT_MAXFREELIST 1024

static client_t *client_free_list[CLIENT_MAXFREELIST];
static int client_numfree = 0;

static void
read_callback(picoev_loop* loop, int fd, int events, void* cb_arg);

#ifndef WITH_GREENLET
static void
write_callback(picoev_loop* loop, int fd, int events, void* cb_arg);
#endif

static void
kill_callback(picoev_loop* loop, int fd, int events, void* cb_arg);

static void
trampoline_callback(picoev_loop* loop, int fd, int events, void* cb_arg);

static void
prepare_call_wsgi(client_t *client);

static void
call_wsgi_handler(client_t *client);

static int
check_status_code(client_t *client);

static void
client_t_list_fill(void)
{
    client_t *client;
    while (client_numfree < CLIENT_MAXFREELIST) {
        client = (client_t *)PyMem_Malloc(sizeof(client_t));
        client_free_list[client_numfree++] = client;
    }
}

static void
client_t_list_clear(void)
{
    client_t *op;

    while (client_numfree) {
        op = client_free_list[--client_numfree];
        PyMem_Free(op);
    }
}

static client_t*
alloc_client_t(void)
{
    client_t *client;
    if (client_numfree) {
        client = client_free_list[--client_numfree];
        //DEBUG("use pooled client %p", client);
    }else{
        client = (client_t *)PyMem_Malloc(sizeof(client_t));
        //DEBUG("alloc client %p", client);
    }
    memset(client, 0, sizeof(client_t));
    return client;
}

static void
dealloc_client(client_t *client)
{
    GDEBUG("client dealloc %p", client);
    if (client_numfree < CLIENT_MAXFREELIST){
        client_free_list[client_numfree++] = client;
    }else{
        PyMem_Free(client);
    }
}


static client_t *
new_client_t(int client_fd, char *remote_addr, uint32_t remote_port)
{
    client_t *client;

    client = alloc_client_t();
    //client = PyMem_Malloc(sizeof(client_t));
    //memset(client, 0, sizeof(client_t));

    client->fd = client_fd;
    client->request_queue = new_request_queue();
    client->remote_addr = remote_addr;
    client->remote_port = remote_port;
    client->body_type = BODY_TYPE_NONE;
    GDEBUG("client alloc %p", client);
    return client;
}

static void
clean_client(client_t *client)
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

    DEBUG("status_code:%d env:%p", client->status_code, client->environ);
    if(client->environ){ 
        PyDict_Clear(client->environ);
        DEBUG("CLEAR environ");
        Py_CLEAR(client->environ);
    }
    if(client->body){
        if(client->body_type == BODY_TYPE_TMPFILE){
            fclose(client->body);
        }else{
            free_buffer(client->body);
        }
        client->body = NULL;
    }
    client->header_done = 0;
    client->response_closed = 0;
    client->chunked_response = 0;
    client->content_length_set = 0;
    client->content_length = 0;
    client->write_bytes = 0;
}

static void
close_client(client_t *client)
{
    client_t *new_client = NULL;
    int ret;

    if(!client->response_closed){
        close_response(client);
    }
    DEBUG("start close client:%p fd:%d status_code %d", client, client->fd, client->status_code);

    if(picoev_is_active(main_loop, client->fd)){
        if(!picoev_del(main_loop, client->fd)){
            activecnt--;
        }
        DEBUG("picoev_del client:%p fd:%d", client, client->fd);
    }

    clean_client(client);

    DEBUG("remain http pipeline size :%d", client->request_queue->size);
    if(client->request_queue->size > 0){
        if(check_status_code(client) > 0){
            //process pipeline
            prepare_call_wsgi(client);
            call_wsgi_handler(client);
        }
        return ;
    }

    if(client->http_parser != NULL){
        PyMem_Free(client->http_parser);
    }

    free_request_queue(client->request_queue);
    if(!client->keep_alive){
        close(client->fd);
        BDEBUG("close client:%p fd:%d", client, client->fd);
    }else{
        BDEBUG("keep alive client:%p fd:%d", client, client->fd);
        disable_cork(client);
        new_client = new_client_t(client->fd, client->remote_addr, client->remote_port);
        new_client->keep_alive = 1;
        init_parser(new_client, server_name, server_port);
        ret = picoev_add(main_loop, new_client->fd, PICOEV_READ, keep_alive_timeout, read_callback, (void *)new_client);
        if(ret == 0){
            activecnt++;
        }
    }
    //clear old client
    dealloc_client(client);
}

static void init_main_loop(void){
    if(main_loop == NULL){
        /* init picoev */
        picoev_init(max_fd);
        /* create loop */
        main_loop = picoev_create_loop(60);
    }
}

static void
kill_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    if ((events & PICOEV_TIMEOUT) != 0) {
        DEBUG("force shutdown...");
        loop_done = 0;
    }
}

static inline void
kill_server(int timeout)
{
    //stop accepting
    if(!picoev_del(main_loop, listen_sock)){
        activecnt--;
    }

    //shutdown timeout
    if(timeout > 0){
        //set timeout
        (void)picoev_add(main_loop, listen_sock, PICOEV_TIMEOUT, timeout, kill_callback, NULL);
    }else{
        (void)picoev_add(main_loop, listen_sock, PICOEV_TIMEOUT, 1, kill_callback, NULL);
    }
}

static inline void
set_current_request(client_t *client)
{
    request *req;
    req = shift_request(client->request_queue);
    client->bad_request_code = req->bad_request_code;
    client->body = req->body;
    client->body_type = req->body_type;
    client->environ = req->env;
    free_request(req);
    client->req = NULL;
}

static void
set_bad_request_code(client_t *client, int status_code)
{
    request *req;
    req = client->request_queue->tail;
    req->bad_request_code = status_code;
}

static int
check_status_code(client_t *client)
{
    request *req;
    req = client->request_queue->head;
    if(req && req->bad_request_code > 200){
        //error
        //shift
        set_current_request(client);
        send_error_page(client);
        close_client(client);
        return 0;
    }
    return 1;
}

/*
static int
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
        res = greenlet_throw(pyclient->greenlet, err_type, err_val, err_tb);
    }else{
        res = greenlet_switch(pyclient->greenlet, pyclient->args, pyclient->kwargs);
    }
    start_response->cli = old_client;

    Py_CLEAR(pyclient->args);
    Py_CLEAR(pyclient->kwargs);

    //check response & PyErr_Occurred
    if(res && res == Py_None){
        PyErr_SetString(PyExc_Exception, "response must be a iter or sequence object");
    }

    if(PyErr_Occurred()){
        write_error_log(__FILE__, __LINE__);
        return -1;
    }
#ifdef PY3
    if(PyLong_Check(res)){
        if(PyLong_AS_LONG(res) == -1){
            // suspend process
            return 0;
        }
    }
#else
    if(PyInt_Check(res)){
        if(PyInt_AS_LONG(res) == -1){
            // suspend process
            return 0;
        }
    }
#endif

    client->response = res;
    //next send response
    return 1;

}
*/

static PyObject *
app_handler(PyObject *self, PyObject *args)
{
    int ret, active;
    PyObject *wsgi_args = NULL, *start = NULL, *res = NULL;
    PyObject *env = NULL;
    ClientObject *pyclient;
    client_t *client;
    response_status status;

    if (!PyArg_ParseTuple(args, "O:app_handler", &env)){
        return NULL;
    }
    pyclient = (ClientObject*)PyDict_GetItem(env, client_key);
    client = pyclient->client;

    start = create_start_response(client);

    if(!start){
        return NULL;
    }

    DEBUG("call wsgi app");
    wsgi_args = Py_BuildValue("(OO)", env, start);
    res = PyObject_CallObject(wsgi_app, wsgi_args);
    Py_DECREF(wsgi_args);
    DEBUG("called wsgi app");

    //check response & PyErr_Occurred
    if (res && res == Py_None){
        PyErr_SetString(PyExc_Exception, "response must be a iter or sequence object");
        write_error_log(__FILE__, __LINE__);
        send_error_page(client);
        close_client(client);
        Py_RETURN_NONE;
    }
    //Check wsgi_app error
    if (PyErr_Occurred()){
        write_error_log(__FILE__, __LINE__);
        send_error_page(client);
        close_client(client);
        Py_RETURN_NONE;
    }

    client->response = res;

    if(client->response_closed){
        //closed
        close_client(client);
        Py_RETURN_NONE;
    }
    status = response_start(client);

#ifdef WITH_GREENLET
    while(status != STATUS_OK){
        if(status == STATUS_ERROR){
            // Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            break;
        }else{
            active = picoev_is_active(main_loop, client->fd);
            ret = picoev_add(main_loop, client->fd, PICOEV_WRITE, 300, trampoline_callback, (void *)pyclient);
            if((ret == 0 && !active)){
                activecnt++;
            }
            status = process_body(client);
        }
    }
    // send OK
    close_client(client);
#else
    switch(status){
        case STATUS_ERROR:
            // Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_client(client);
            Py_RETURN_NONE;
        case STATUS_SUSPEND:
            // continue
            // set callback
            //clear event
            active = picoev_is_active(main_loop, client->fd);
            ret = picoev_add(main_loop, client->fd, PICOEV_WRITE, 300, write_callback, (void *)pyclient);
            if((ret == 0 && !active)){
                activecnt++;
            }
        default:
            // send OK
            close_client(client);
    }
#endif
    Py_RETURN_NONE;
}

static PyMethodDef app_handler_def = {"handle",   (PyCFunction)app_handler, METH_VARARGS, 0};

static PyObject*
get_app_handler(void)
{
    if(app_handler_func == NULL){
        app_handler_func = PyCFunction_NewEx(&app_handler_def, (PyObject *)NULL, NULL);
    }
    //Py_INCREF(app_handler_func);
    return app_handler_func;
}

#ifdef WITH_GREENLET
static void
resume_wsgi_handler(ClientObject *pyclient)
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
        res = greenlet_throw(pyclient->greenlet, err_type, err_val, err_tb);
    }else{
        res = greenlet_switch(pyclient->greenlet, pyclient->args, pyclient->kwargs);
    }
    start_response->cli = old_client;

    Py_CLEAR(pyclient->args);
    Py_CLEAR(pyclient->kwargs);
    Py_XDECREF(res);
}
#endif

static void
call_wsgi_handler(client_t *client)
{
    PyObject *handler, *greenlet, *args, *res;
    ClientObject *pyclient;

    handler = get_app_handler();
    current_client = PyDict_GetItem(client->environ, client_key);
    pyclient = (ClientObject *)current_client;

    args = Py_BuildValue("(O)", client->environ);
#ifdef WITH_GREENLET
    //new greenlet
    greenlet = greenlet_new(handler, NULL);
    // set_greenlet
    pyclient->greenlet = greenlet;
    Py_INCREF(pyclient->greenlet);

    res = greenlet_switch(greenlet, args, NULL);
    //res = PyObject_CallObject(wsgi_app, args);
    Py_DECREF(args);
    Py_DECREF(greenlet);
#else
    pyclient->greenlet = NULL;
    res = PyObject_CallObject(handler, args);
    Py_DECREF(args);
#endif
    Py_XDECREF(res);
}

/*
static int
process_wsgi_app(client_t *cli)
{
    PyObject *args = NULL, *start = NULL, *res = NULL;
    PyObject *greenlet;
    ClientObject *pyclient;
    start = create_start_response(cli);

    if(!start){
        return -1;
    }
    args = Py_BuildValue("(OO)", cli->environ, start);

    current_client = PyDict_GetItem(cli->environ, client_key);
    pyclient = (ClientObject *)current_client;

    DEBUG("start client %p", cli);
    DEBUG("start environ %p", cli->environ);
    
#ifdef WITH_GREENLET
    //new greenlet
    greenlet = greenlet_new(wsgi_app, NULL);
    // set_greenlet
    pyclient->greenlet = greenlet;
    Py_INCREF(pyclient->greenlet);

    res = greenlet_switch(greenlet, args, NULL);
    //res = PyObject_CallObject(wsgi_app, args);
    Py_DECREF(args);
    Py_DECREF(greenlet);
#else
    res = PyObject_CallObject(wsgi_app, args);
    Py_DECREF(args);
#endif

    //check response & PyErr_Occurred
    if(res && res == Py_None){
        PyErr_SetString(PyExc_Exception, "response must be a iter or sequence object");
    }

    if (PyErr_Occurred()){
        write_error_log(__FILE__, __LINE__);
        return -1;
    }

#ifdef PY3
    if(PyLong_Check(res)){
        if(PyLong_AS_LONG(res) == -1){
            // suspend process
            return 0;
        }
    }
#else
    if(PyInt_Check(res)){
        if(PyInt_AS_LONG(res) == -1){
            // suspend process
            return 0;
        }
    }
#endif

    //next send response 
    cli->response = res;

    return 1;

}
*/

/*
void
switch_wsgi_app(picoev_loop* loop, int fd, PyObject *obj)
{
    ClientObject *pyclient = (ClientObject *)obj;
    
    //clear event
    picoev_del(loop, fd);
    // resume
    resume_wsgi_app(pyclient, loop);
    pyclient->resumed = 0;
}*/

/*
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
    //switch_wsgi_app(loop, client->fd, (PyObject *)pyclient);
}
*/

#ifdef WITH_GREENLET
static void
timeout_error_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject *)(cb_arg);
    client_t *client = pyclient->client;

    if ((events & PICOEV_TIMEOUT) != 0) {
        DEBUG("timeout_error_callback pyclient:%p client:%p fd:%d", pyclient, pyclient->client, pyclient->client->fd);
        if(!picoev_del(loop, fd)){
            activecnt--;
        }
        pyclient->suspended = 0;
        /* pyclient->resumed = 1; */
        PyErr_SetString(timeout_error, "timeout");
        set_so_keepalive(client->fd, 0);
        resume_wsgi_handler(pyclient);
    }
}

static void
timeout_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject *)(cb_arg);
    client_t *client = pyclient->client;

    if ((events & PICOEV_TIMEOUT) != 0) {
        DEBUG("timeout_callback pyclient:%p client:%p fd:%d", pyclient, pyclient->client, pyclient->client->fd);
        //next intval 30sec
        picoev_set_timeout(loop, client->fd, 30);
        // is_active ??
        if(write(client->fd, "", 0) < 0){
            if(!picoev_del(loop, fd)){
                activecnt--;
            }
            //resume
            pyclient->suspended = 0;
            /* pyclient->resumed = 1; */
            PyErr_SetFromErrno(PyExc_IOError);
            DEBUG("closed");
            set_so_keepalive(client->fd, 0);
            resume_wsgi_handler(pyclient);
        }
    }
}


static void
trampoline_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject*)cb_arg;
    client_t *client = pyclient->client;
    
    if(!picoev_del(loop, fd)){
        activecnt--;
    }
    DEBUG("call trampoline_callback pyclient %p", pyclient);
    if ((events & PICOEV_TIMEOUT) != 0) {

        RDEBUG("** trampoline_callback timeout **");
        //timeout
        client->keep_alive = 0;
        PyErr_SetString(timeout_error, "timeout");
    }
    resume_wsgi_handler(pyclient);
}
#endif

#ifndef WITH_GREENLET
static void
write_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject*)cb_arg;
    client_t *client = pyclient->client;
    int ret;
    DEBUG("call write_callback");
    current_client = (PyObject*)pyclient;
    if ((events & PICOEV_TIMEOUT) != 0) {

        DEBUG("** write_callback timeout **");

        //timeout
        client->keep_alive = 0;
        close_client(client);

    } else if ((events & PICOEV_WRITE) != 0) {
        ret = process_body(client);
        DEBUG("process_body ret %d", ret);
        if(ret != 0){
            //ok or die
            close_client(client);
        }
    }
}
#endif

/*
static void
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
            close_client(client, loop);
            return;
        case 0:
            // suspend
            return;
        default:
            break;
    }


    if(client->response_closed){
        //closed
        close_client(client, loop);
        return;
    }

    ret = response_start(client);
    switch(ret){
        case -1:
            // Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_client(client, loop);
            return;
        case 0:
            // continue
            // set callback
            DEBUG("set write callback %d", ret);
            //clear event
            picoev_del(loop, client->fd);
            picoev_add(loop, client->fd, PICOEV_WRITE, 0, write_callback, (void *)client);
            return;
        default:
            // send OK
            close_client(client, loop);
    }
}
*/

/*
static void
call_wsgi_app(client_t *client, picoev_loop* loop)
{
    int ret;
    ret = process_wsgi_app(client);

    DEBUG("call_wsgi_app result %d", ret);
    switch(ret){
        case -1:
            //Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_client(client, loop);
            return;
        case 0:
            // suspend
            return;
        default:
            break;
    }
    if(client->response_closed){
        //closed
        close_client(client, loop);
        return;
    }
    ret = response_start(client);
    DEBUG("response_start result %d", ret);
    switch(ret){
        case -1:
            // Internal Server Error
            client->bad_request_code = 500;
            send_error_page(client);
            close_client(client, loop);
            return;
        case 0:
            // continue
            // set callback
            DEBUG("set write callback %d", ret);
            //clear event
            picoev_del(loop, client->fd);
            picoev_add(loop, client->fd, PICOEV_WRITE, 0, write_callback, (void *)client);
            return;
        default:
            // send OK
            close_client(client, loop);
    }
}
*/

static int
check_http_expect(client_t *client)
{
    PyObject *c = NULL;
    char *val = NULL;
    int ret;

    if(client->http_parser->http_minor == 1){
        ///TODO CHECK
        c = PyDict_GetItemString(client->environ, "HTTP_EXPECT");
        if(c){
            val = PyBytes_AS_STRING(c);
            if(!strncasecmp(val, "100-continue", 12)){
                ret = write(client->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
                if(ret < 0){
                    //fail
                    PyErr_SetFromErrno(PyExc_IOError);
                    write_error_log(__FILE__, __LINE__); 
                    client->keep_alive = 0;
                    client->status_code = 500;
                    send_error_page(client);
                    close_client(client);
                    return -1;
                }
            }else{
                //417
                client->keep_alive = 0;
                client->status_code = 417;
                send_error_page(client);
                close_client(client);
                return -1;
            }
        }
        return 1;
    }
    return 0;
}

#ifdef PY3
static int
set_input_file(client_t *client)
{
    PyObject *input;
    int fd;

    FILE *tmp = (FILE *)client->body;
    fflush(tmp);
    rewind(tmp);
    fd = fileno(tmp);
    input = PyFile_FromFd(fd, "<tmpfile>", "r", -1, NULL, NULL, NULL, 1);
    if(input == NULL){
        fclose(tmp);
        client->body = NULL;
        return -1;
    }
    PyDict_SetItem((PyObject *)client->environ, wsgi_input_key, input);
    Py_DECREF(input);
    client->body = NULL;
    return 1;
}

#else
static int
set_input_file(client_t *client)
{
    PyObject *input;
    FILE *tmp = (FILE *)client->body;
    fflush(tmp);
    rewind(tmp);
    input = PyFile_FromFile(tmp, "<tmpfile>", "r", fclose);
    if(input == NULL){
        fclose(tmp);
        client->body = NULL;
        return -1;
    }
    PyDict_SetItem((PyObject *)client->environ, wsgi_input_key, input);
    Py_DECREF(input);
    client->body = NULL;
    return 0;
}
#endif

static int
set_input_object(client_t *client)
{
    PyObject *input = NULL;
    if(client->body_type == BODY_TYPE_BUFFER){
        input = InputObject_New((buffer *)client->body);
    }else{
        if(client->body){
            input = InputObject_New((buffer *)client->body);
        }else{
            input = InputObject_New(new_buffer(0, 0));
        }
    }
    if(input == NULL){
        return -1;
    }
    PyDict_SetItem((PyObject *)client->environ, wsgi_input_key, input);
    client->body = NULL;;
    Py_DECREF(input);
    return 1;
}

static void 
setting_keepalive(client_t *client)
{
    PyObject *c;
    char *val;
    if(is_keep_alive){
        //support keep-alive
        c = PyDict_GetItemString(client->environ, "HTTP_CONNECTION");
        if(client->http_parser->http_minor == 1){
            //HTTP 1.1
            if(c){
                val = PyBytes_AS_STRING(c);
                if(!strcasecmp(val, "close")){
                    client->keep_alive = 0;
                }else{
                    client->keep_alive = 1;
                }
            }else{
                client->keep_alive = 1;
            }
        }else{
            //HTTP 1.0
            if(c){
                val = PyBytes_AS_STRING(c);
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
}

static void
prepare_call_wsgi(client_t *client)
{
    set_current_request(client);

    //check Expect
    if (check_http_expect(client) < 0) {
        return;
    }

    if(client->body_type == BODY_TYPE_TMPFILE){
        if(set_input_file(client) == -1){
            return;
        }
    }else{
        if(set_input_object(client) == -1){
            return;
        }
    }

    setting_keepalive(client);
}

static void
read_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    client_t *client = ( client_t *)(cb_arg);
    //PyObject *body = NULL;
    char *key = NULL;
    int finish = 0, nread;

    if ((events & PICOEV_TIMEOUT) != 0) {

        DEBUG("** read_callback timeout %d **", fd);
        //timeout
        client->keep_alive = 0;
        if(client->request_queue->size > 0){
            //piplining
            set_bad_request_code(client, 408);
            finish = 1;
        }else{
            close_client(client);
        }

    } else if ((events & PICOEV_READ) != 0) {
        char buf[READ_BUF_SIZE];
        ssize_t r;
        if(!client->keep_alive){
            picoev_set_timeout(loop, client->fd, READ_TIMEOUT_SECS);
        }
        Py_BEGIN_ALLOW_THREADS
        r = read(client->fd, buf, sizeof(buf));
        Py_END_ALLOW_THREADS
        switch (r) {
            case 0: 
                client->keep_alive = 0;
                //503??
                if(client->request_queue->size > 0){
                    //piplining
                    set_bad_request_code(client, 503);
                    finish = 1;
                }else{
                    client->status_code = 503;
                    send_error_page(client);
                    close_client(client);
                    return;
                }
            case -1: /* error */
                if (errno == EAGAIN || errno == EWOULDBLOCK) { /* try again later */
                    break;
                } else { /* fatal error */
                    if(client->request_queue->size > 0){
                        //piplining
                        set_bad_request_code(client, 500);
                        if(errno != ECONNRESET){
                            PyErr_SetFromErrno(PyExc_IOError);
                            write_error_log(__FILE__, __LINE__); 
                        }
                        finish = 1;
                    }else{
                        if(client->keep_alive && errno == ECONNRESET){
                            client->keep_alive = 0;
                            client->status_code = 500;
                            client->header_done = 1;
                            client->response_closed = 1;
                        }else{
                            PyErr_SetFromErrno(PyExc_IOError);
                            write_error_log(__FILE__, __LINE__); 
                            client->keep_alive = 0;
                            client->status_code = 500;
                            if(errno != ECONNRESET){
                                send_error_page(client);
                            }else{
                                client->header_done = 1;
                                client->response_closed = 1;
                            }
                        }
                        close_client(client);
                        return;
                    }
                }
                break;
            default:
                BDEBUG("fd:%d \n%.*s", client->fd, (int)r, buf);
                nread = execute_parse(client, buf, r);
                BDEBUG("read request fd %d readed %d nread %d", client->fd, (int)r, nread);

                if(client->bad_request_code > 0){
                    DEBUG("fd %d bad_request code %d",client->fd,  client->bad_request_code);
                    set_bad_request_code(client, client->bad_request_code);
                    ///force end
                    finish = 1;
                    break;
                }

                if(!client->upgrade && nread != r){
                    // parse error
                    DEBUG("fd:%d parse error bad_status_code=%d", client->fd, client->bad_request_code);
                    set_bad_request_code(client, 400);
                    ///force end
                    finish = 1;
                    break;
                }
                //DEBUG("parse ok, fd %d %d nread", cli->fd, nread);

                if(parser_finish(client) > 0){
                    if(client->upgrade){
                        //WebSocket Key
                        DEBUG("upgrade websocket %d", client->fd);
                        key = buf + nread + 1;
                        buffer *b = new_buffer(r - nread -1, r - nread -1);
                        if(write2buf(b, key, r - nread -1) == WRITE_OK){
                            client->request_queue->tail->body = b;
                        }else{
                            free_buffer(b);
                        }
                    }
                    finish = 1;
                }
                break;
        }
    }
    if(finish == 1){
        if(!picoev_del(main_loop, client->fd)){
            activecnt--;
        }
        if(check_status_code(client) > 0){
            //current request ok
            prepare_call_wsgi(client);
            call_wsgi_handler(client);
        }
        return;
    }
}


static void
accept_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    int client_fd, ret;
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
        Py_BEGIN_ALLOW_THREADS
        client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
        Py_END_ALLOW_THREADS

        if (client_fd != -1) {
            DEBUG("accept fd %d", client_fd);
            //printf("connected: %d\n", client_fd);
            if(setup_sock(client_fd) == -1){
                PyErr_SetFromErrno(PyExc_IOError);
                write_error_log(__FILE__, __LINE__);
                // die
                loop_done = 0;
                return;
            }
            remote_addr = inet_ntoa (client_addr.sin_addr);
            remote_port = ntohs(client_addr.sin_port);
            client = new_client_t(client_fd, remote_addr, remote_port);
            init_parser(client, server_name, server_port);
            ret = picoev_add(loop, client_fd, PICOEV_READ, keep_alive_timeout, read_callback, (void *)client);
            if(ret == 0){
                activecnt++;
            }
        }else{
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                PyErr_SetFromErrno(PyExc_IOError);
                write_error_log(__FILE__, __LINE__);
                // die
                kill_server(0);
            }
        }

    }
}

static void
setup_server_env(void)
{
    setup_listen_sock(listen_sock);
    cache_time_init();
    setup_static_env(server_name, server_port);
    setup_start_response();
    
    ClientObject_list_fill();
    client_t_list_fill();
    request_list_fill();
    buffer_list_fill();
    InputObject_list_fill();
    
    hub_switch_value = Py_BuildValue("(i)", -1);
    client_key = NATIVE_FROMSTRING("meinheld.client");
    wsgi_input_key = NATIVE_FROMSTRING("wsgi.input");
    empty_string = NATIVE_FROMSTRING("");
}

static void
clear_server_env(void)
{
    //clean
    clear_start_response();
    clear_static_env();
    client_t_list_clear();
    
    ClientObject_list_clear();
    request_list_clear();
    buffer_list_clear();
    InputObject_list_clear();

    Py_DECREF(hub_switch_value);
    Py_DECREF(client_key);
    Py_DECREF(wsgi_input_key);
    Py_DECREF(empty_string);
}


static int 
inet_listen(void)
{
    struct addrinfo hints, *servinfo, *p;
    int flag = 1;
    int res;
    char strport[7];

    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; 
    
    snprintf(strport, sizeof (strport), "%d", server_port);
    
    if ((res = getaddrinfo(server_name, strport, &hints, &servinfo)) == -1) {
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

        Py_BEGIN_ALLOW_THREADS
        res = bind(listen_sock, p->ai_addr, p->ai_addrlen);
        Py_END_ALLOW_THREADS
        if (res == -1) {
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
    Py_BEGIN_ALLOW_THREADS
    res = listen(listen_sock, backlog);
    Py_END_ALLOW_THREADS
    if (res == -1) {
        close(listen_sock);
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    return 1;
}

static int
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

static int
unix_listen(char *sock_name, int len)
{
    int flag = 1;
    int res;
    struct sockaddr_un saddr;
    mode_t old_umask;

    DEBUG("unix domain socket %s", sock_name);

    if(len >= sizeof(saddr.sun_path)) {
        PyErr_SetString(PyExc_OSError, "AF_UNIX path too long");
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));
    if(check_unix_sockpath(sock_name) == -1){
        return -1;
    }

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
    strncpy(saddr.sun_path, sock_name, len);

    old_umask = umask(0);

    Py_BEGIN_ALLOW_THREADS
    res = bind(listen_sock, (struct sockaddr *)&saddr, sizeof(saddr));
    Py_END_ALLOW_THREADS
    if (res == -1) {
        close(listen_sock);
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    umask(old_umask);

    // BACKLOG 1024
    Py_BEGIN_ALLOW_THREADS
    res = listen(listen_sock, backlog);
    Py_END_ALLOW_THREADS
    if (res == -1) {
        close(listen_sock);
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    unix_sock_name = sock_name;
    return 1;
}

static void
fast_notify(void)
{
    spinner = (spinner + 1) % 2;
    fchmod(tempfile_fd, spinner);
    if(ppid != getppid()){
        kill_server(gtimeout);
        tempfile_fd = 0;
    }
}

static PyObject *
meinheld_listen(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *o = NULL;
    char *path;
    int ret, len;
    int sock_fd = -1;

    static char *kwlist[] = {"address", "socket_fd", 0};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Oi:listen",
                                     kwlist, &o, &sock_fd)){
        return NULL;
    }

    if(listen_sock > 0){
        PyErr_SetString(PyExc_Exception, "already set listen socket");
        return NULL;
    }

    if(o == NULL && sock_fd > 0){
        listen_sock = sock_fd;
        DEBUG("use already listened sock fd:%d", sock_fd);
        ret = 1;
    }else if(PyTuple_Check(o)){
        //inet
        if(!PyArg_ParseTuple(o, "si:listen", &server_name, &server_port)){
            return NULL;
        }
        ret = inet_listen();
    }else if(PyBytes_Check(o)){
        // unix domain
        if(!PyArg_Parse(o, "s#", &path, &len)){
            return NULL;
        }
        ret = unix_listen(path, len);
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
    DEBUG("call SIGINT");
    kill_server(0);
    if(!catch_signal){
        catch_signal = 1;
    }
}

static void
sigpipe_cb(int signum)
{
    DEBUG("call SIGPIPE");
}

static PyObject *
meinheld_access_log(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, "s:access_log", &log_path)){
        return NULL;
    }


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
    PyObject *f = NULL;

    if (!PyArg_ParseTuple(args, "s:error_log", &error_log_path)){
        return NULL;
    }
    if(err_log_fd > 0){
        close(err_log_fd);
    }
#ifdef PY3
    int fd = open(error_log_path, O_CREAT|O_APPEND|O_WRONLY, 0744);
    if(fd < 0){
        PyErr_Format(PyExc_TypeError, "not open file. %s", error_log_path);
        return NULL;
    }
    f = PyFile_FromFd(fd, NULL, NULL, -1, NULL, NULL, NULL, 1);
#else
    f = PyFile_FromString(error_log_path, "a");
#endif
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
meinheld_stop(PyObject *self, PyObject *args, PyObject *kwds)
{
    int timeout = 0;

    static char *kwlist[] = {"timeout", 0};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:timeout",
                                     kwlist, &timeout)){
        return NULL;
    }
    kill_server(timeout);
    Py_RETURN_NONE;
}

static PyObject *
meinheld_shutdown(PyObject *self, PyObject *args)
{
    call_shutdown = 1;
    kill_server(0);
    Py_RETURN_NONE;
}

static inline int
fire_timer(void)
{
    TimerObject *timer;
    int ret = 1;
    heapq_t *q = g_timers;
    time_t now = time(NULL);
    PyObject *res = NULL;

    while(q->size > 0 && loop_done){

        timer = q->heap[0];
        if(timer->seconds <= now){
            //call
            if(!timer->called){
                res = PyObject_Call(timer->callback, timer->args, timer->kwargs);
                Py_XDECREF(res);
            }

            timer = heappop(q);
            Py_DECREF(timer);
            activecnt--;

            if(PyErr_Occurred()){
                RDEBUG("scheduled call raise exception");
                //TODO PyErr_Print or write log
                PyErr_Print();
                ret = -1;
                break;
            }
        }else{
            break;
        }
    }
    return ret;

}

static PyObject *
meinheld_run_loop(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *watchdog_result;
    int ret;
    int silent = 0;

    static char *kwlist[] = {"app", "silent", 0};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i:run",
                                     kwlist, &wsgi_app, &silent)){
        return NULL;
    }

    if(listen_sock <= 0){
        PyErr_Format(PyExc_TypeError, "not found listen socket");
        return NULL;

    }

    Py_INCREF(wsgi_app);
    setup_server_env();

    init_main_loop();

    loop_done = 1;

    PyOS_setsig(SIGPIPE, sigpipe_cb);
    PyOS_setsig(SIGINT, sigint_cb);
    PyOS_setsig(SIGTERM, sigint_cb);

    ret = picoev_add(main_loop, listen_sock, PICOEV_READ, ACCEPT_TIMEOUT_SECS, accept_callback, NULL);
    if(ret == 0){
        activecnt++;
    }

    /* loop */
    while (likely(loop_done == 1 && activecnt > 0)) {
        fire_timer();
        picoev_loop_once(main_loop, 10);
        if(watch_loop){
            if(tempfile_fd){
                fast_notify();
            }else if(watchdog){
                watchdog_result = PyObject_CallFunction(watchdog, NULL);
                if(PyErr_Occurred()){
                    PyErr_Print();
                    PyErr_Clear();
                }
                Py_XDECREF(watchdog_result);
            }
        }
    }

    Py_DECREF(wsgi_app);
    Py_XDECREF(watchdog);

    picoev_destroy_loop(main_loop);
    picoev_deinit();
    main_loop = NULL;

    clear_server_env();

    close(listen_sock);
    listen_sock = 0;
    if(unix_sock_name){
       unlink(unix_sock_name);
       unix_sock_name = NULL;
    }

    if(!silent &&  catch_signal){
        //override
        PyErr_Clear();
        PyErr_SetNone(PyExc_KeyboardInterrupt);
        catch_signal = 0;
        return NULL;
    }
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
meinheld_set_client_body_buffer_size(PyObject *self, PyObject *args)
{
    int temp;
    if (!PyArg_ParseTuple(args, "i", &temp))
        return NULL;
    if(temp <= 0){
        PyErr_SetString(PyExc_ValueError, "client_body_buffer_size value out of range ");
        return NULL;
    }
    client_body_buffer_size = temp;
    Py_RETURN_NONE;
}

PyObject *
meinheld_get_client_body_buffer_size(PyObject *self, PyObject *args)
{
    return Py_BuildValue("i", client_body_buffer_size);
}

PyObject *
meinheld_set_listen_socket(PyObject *self, PyObject *args)
{
    int temp_sock;
    if (!PyArg_ParseTuple(args, "i:listen_socket", &temp_sock)){
        return NULL;
    }
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
    int timeout = 0;
    if (!PyArg_ParseTuple(args, "iii", &_fd, &_ppid, &timeout))
        return NULL;

    tempfile_fd = _fd;
    ppid = _ppid;
    gtimeout = timeout;
    watch_loop = 1;
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
    watch_loop = 1;
    Py_RETURN_NONE;
}

/*
PyObject *
meinheld_set_process_name(PyObject *self, PyObject *args)
{
#ifdef linux

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
#endif

    Py_RETURN_NONE;
}*/

PyObject *
meinheld_suspend_client(PyObject *self, PyObject *args)
{

#ifdef WITH_GREENLET
    PyObject *temp;
    ClientObject *pyclient;
    client_t *client;
    PyObject *parent;
    int timeout = 0, ret, active;

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
    
    /*
    if(pyclient->resumed == 1){
        //call later
        PyErr_SetString(PyExc_IOError, "not called resume");
        return NULL;
    }
    */

    if(client && !(pyclient->suspended)){
        pyclient->suspended = 1;
        parent = greenlet_getparent(pyclient->greenlet);

        set_so_keepalive(client->fd, 1);
        BDEBUG("meinheld_suspend_client pyclient:%p client:%p fd:%d", pyclient, client, client->fd);
        BDEBUG("meinheld_suspend_client active ? %d", picoev_is_active(main_loop, client->fd));
        active = picoev_is_active(main_loop, client->fd);
        if(timeout > 0){
            ret = picoev_add(main_loop, client->fd, PICOEV_TIMEOUT, timeout, timeout_error_callback, (void *)pyclient);
        }else{
            ret = picoev_add(main_loop, client->fd, PICOEV_TIMEOUT, 3, timeout_callback, (void *)pyclient);
        }
        if((ret == 0 && !active)){
            activecnt++;
        }
        return greenlet_switch(parent, hub_switch_value, NULL);
    }else{
        PyErr_SetString(PyExc_IOError, "already suspended");
        return NULL;
    }
    Py_RETURN_NONE;
#else
    NO_GREENLET_ERROR;
#endif
}

PyObject *
meinheld_resume_client(PyObject *self, PyObject *args)
{
#ifdef WITH_GREENLET
    PyObject *temp, *switch_args, *switch_kwargs;
    ClientObject *pyclient;
    client_t *client;
    int ret, active;

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

    if(pyclient->client && pyclient->suspended){
        set_so_keepalive(pyclient->client->fd, 0);
        pyclient->args = switch_args;
        Py_XINCREF(pyclient->args);

        pyclient->kwargs = switch_kwargs;
        Py_XINCREF(pyclient->kwargs);

        pyclient->suspended = 0;
        /* pyclient->resumed = 1; */
        DEBUG("meinheld_resume_client pyclient:%p client:%p fd:%d", pyclient, pyclient->client, pyclient->client->fd);
        DEBUG("meinheld_resume_client active ? %d", picoev_is_active(main_loop, pyclient->client->fd));
        //clear event
        active = picoev_is_active(main_loop, client->fd);
        ret = picoev_add(main_loop, client->fd, PICOEV_WRITE, 0, trampoline_callback, (void *)pyclient);
        if((ret == 0 && !active)){
            activecnt++;
        }
    }else{
        PyErr_SetString(PyExc_IOError, "already resumed");
        return NULL;
    }
    Py_RETURN_NONE;
#else
    NO_GREENLET_ERROR;
#endif
}

PyObject *
meinheld_cancel_wait(PyObject *self, PyObject *args)
{
#ifdef WITH_GREENLET
    int fd;
    if (!PyArg_ParseTuple(args, "i:cancel_event", &fd)){
        return NULL;
    }

    if(fd < 0){
        PyErr_SetString(PyExc_ValueError, "fileno value out of range ");
        return NULL;
    }
    if(!picoev_del(main_loop, fd)){
        activecnt--;
    }
    Py_RETURN_NONE;
#else
    NO_GREENLET_ERROR;
#endif
}


static PyObject*
meinheld_trampoline(PyObject *self, PyObject *args, PyObject *kwargs)
{
#ifdef WITH_GREENLET
    PyObject *current, *parent;
    ClientObject *pyclient;
    int fd, event, timeout = 0, ret, active;
    PyObject *read = Py_None, *write = Py_None;

    static char *keywords[] = {"fileno", "read", "write", "timeout", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|OOi:trampoline", keywords, &fd, &read, &write, &timeout)){
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
    if(current_client == NULL){
        PyErr_SetString(PyExc_ValueError, "server not running ");
        return NULL;
    }
    
    pyclient =(ClientObject *) current_client;

    active = picoev_is_active(main_loop, fd);
    ret = picoev_add(main_loop, fd, event, timeout, trampoline_callback, (void *)pyclient);
    if((ret == 0 && !active)){
        activecnt++;
    }
    DEBUG("set trampoline fd:%d event:%d", fd, event);

    // switch to hub
    current = pyclient->greenlet;
    parent = greenlet_getparent(current);
    DEBUG("trampoline fd:%d current:%p parent:%p", fd, current, parent);

    return greenlet_switch(parent, hub_switch_value, NULL);
#else
    NO_GREENLET_ERROR;
#endif

}

PyObject *
meinheld_get_ident(PyObject *self, PyObject *args)
{
#ifdef WITH_GREENLET
    return greenlet_getcurrent();
#else
    NO_GREENLET_ERROR;
#endif
}

static PyObject*
internal_schedule_call(int seconds, PyObject *cb, PyObject *args, PyObject *kwargs)
{
    TimerObject* timer;
    heapq_t *timers = g_timers;

    timer = TimerObject_new(seconds, cb, args, kwargs);
    if(timer == NULL){
        return NULL;
    }
        
    if(heappush(timers, timer) == -1){
        Py_DECREF(timer);
        return NULL;
    }
    activecnt++;
    return (PyObject*)timer;
}

static PyObject*
meinheld_schedule_call(PyObject *self, PyObject *args, PyObject *kwargs)
{
    long seconds = 0, ret;
    Py_ssize_t size;
    PyObject *sec = NULL, *cb = NULL, *cbargs = NULL, *timer;

    size = PyTuple_GET_SIZE(args);
    DEBUG("args size %d", (int)size);

    if(size < 2){
        PyErr_SetString(PyExc_TypeError, "schedule_call takes exactly 2 argument");
        return NULL;
    }
    sec = PyTuple_GET_ITEM(args, 0);
    cb = PyTuple_GET_ITEM(args, 1);

#ifdef PY3
    if(!PyLong_Check(sec)){
#else
    if(!PyInt_Check(sec)){
#endif
        PyErr_SetString(PyExc_TypeError, "must be integer");
        return NULL;
    }
    if(!PyCallable_Check(cb)){
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    ret = PyLong_AsLong(sec);
    if(PyErr_Occurred()){
        return NULL;
    }
    if(ret < 0){
        PyErr_SetString(PyExc_TypeError, "seconds value out of range");
        return NULL;
    }
    seconds = ret;

    if(size > 2){
        cbargs = PyTuple_GetSlice(args, 2, size);
    }

    timer = internal_schedule_call(seconds, cb, cbargs, kwargs);
    Py_XDECREF(cbargs);
    return timer;
}

static PyMethodDef ServerMethods[] = {
    {"listen", (PyCFunction)meinheld_listen, METH_VARARGS|METH_KEYWORDS, "set host and port num"},
    /* {"access_log", meinheld_access_log, METH_VARARGS, "set access log file path."}, */
    /* {"error_log", meinheld_error_log, METH_VARARGS, "set error log file path."}, */

    {"set_keepalive", meinheld_set_keepalive, METH_VARARGS, "set keep-alive support. value set timeout sec. default 0. (disable keep-alive)"},
    {"get_keepalive", meinheld_get_keepalive, METH_VARARGS, "return keep-alive support."},

    {"set_max_content_length", meinheld_set_max_content_length, METH_VARARGS, "set max_content_length"},
    {"get_max_content_length", meinheld_get_max_content_length, METH_VARARGS, "return max_content_length"},

    {"set_client_body_buffer_size", meinheld_set_client_body_buffer_size, METH_VARARGS, "set client_body_buffer_size"},
    {"get_client_body_buffer_size", meinheld_get_client_body_buffer_size, METH_VARARGS, "return client_body_buffer_size"},

    {"set_backlog", meinheld_set_backlog, METH_VARARGS, "set backlog size"},
    {"get_backlog", meinheld_get_backlog, METH_VARARGS, "return backlog size"},

    {"set_picoev_max_fd", meinheld_set_picoev_max_fd, METH_VARARGS, "set picoev max fd size"},
    {"get_picoev_max_fd", meinheld_get_picoev_max_fd, METH_VARARGS, "return picoev max fd size"},

    /* {"set_process_name", meinheld_set_process_name, METH_VARARGS, "set process name"}, */
    {"stop", (PyCFunction)meinheld_stop, METH_VARARGS|METH_KEYWORDS, "stop main loop"},
    {"shutdown", meinheld_shutdown, METH_NOARGS, "stop and close listen socket "},
    
    {"schedule_call", (PyCFunction)meinheld_schedule_call, METH_VARARGS|METH_KEYWORDS, ""},

    // support gunicorn
    {"set_listen_socket", meinheld_set_listen_socket, METH_VARARGS, "set listen_sock"},
    {"set_watchdog", meinheld_set_watchdog, METH_VARARGS, "set watchdog"},
    {"set_fastwatchdog", meinheld_set_fastwatchdog, METH_VARARGS, "set watchdog"},
    {"run", (PyCFunction)meinheld_run_loop, METH_VARARGS|METH_KEYWORDS, "set wsgi app, run the main loop"},
    // greenlet and continuation
    {"_suspend_client", meinheld_suspend_client, METH_VARARGS, "resume client"},
    {"_resume_client", meinheld_resume_client, METH_VARARGS, "resume client"},
    // io
    {"cancel_wait", meinheld_cancel_wait, METH_VARARGS, "cancel wait"},
    {"trampoline", (PyCFunction)meinheld_trampoline, METH_VARARGS | METH_KEYWORDS, "trampoline"},
    {"get_ident", meinheld_get_ident, METH_VARARGS, "return thread ident id"},

    {NULL, NULL, 0, NULL}        /* Sentinel */
};

#ifdef PY3
#define INITERROR return NULL

static struct PyModuleDef server_module_def = {
    PyModuleDef_HEAD_INIT,
    MODULE_NAME,
    NULL,
    -1,
    ServerMethods,
};

PyObject *
PyInit_server(void)
#else
#define INITERROR return

PyMODINIT_FUNC
initserver(void)
#endif
{
    PyObject *m;
#ifdef PY3
    m = PyModule_Create(&server_module_def);
#else
    m = Py_InitModule3(MODULE_NAME, ServerMethods, "");
#endif
    if(m == NULL){
        INITERROR;
    }

    if(PyType_Ready(&ResponseObjectType) < 0){
        INITERROR;
    }

    if(PyType_Ready(&FileWrapperType) < 0){
        INITERROR;
    }

    if(PyType_Ready(&ClientObjectType) < 0){
        INITERROR;
    }

    if(PyType_Ready(&InputObjectType) < 0){
        INITERROR;
    }

    if(PyType_Ready(&TimerObjectType) < 0){
        INITERROR;
    }

    timeout_error = PyErr_NewException("meinheld.server.timeout",
                      PyExc_IOError, NULL);
    if (timeout_error == NULL){
        INITERROR;
    }
    Py_INCREF(timeout_error);
    PyModule_AddObject(m, "timeout", timeout_error);

    //DEBUG("client size %u", sizeof(client_t));
    //DEBUG("request size %u", sizeof(request));
    //DEBUG("header bucket %u", sizeof(write_bucket));
    g_timers = init_queue();
    if(g_timers == NULL){
        INITERROR;
    }
#ifdef PY3
    return m;
#endif
}


