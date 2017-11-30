#include "server.h"

#include <arpa/inet.h>
#include <signal.h>

#ifdef linux
#include <sys/prctl.h>
#include <sys/socket.h>
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

typedef struct {
   TimerObject **q;
   uint32_t size;
   uint32_t max;
} pending_queue_t;

static char *server_name = "127.0.0.1";
static uint16_t server_port = 8000;
/* static int listen_sock;  // listen socket */
static PyObject *listen_socks = NULL;  // listen socket

static volatile sig_atomic_t loop_done;
static volatile sig_atomic_t call_shutdown = 0;
static volatile sig_atomic_t catch_signal = 0;

static picoev_loop* main_loop = NULL; //main loop
static heapq_t *g_timers;
static pending_queue_t *g_pendings = NULL;

// active event cnt
static int activecnt = 0;

static PyObject *wsgi_app = NULL; //wsgi app

static uint8_t watch_loop = 0;
static PyObject *watchdog = NULL; //watchdog
static char is_write_access_log = 0;

static int is_keep_alive = 0; //keep alive support
static int keep_alive_timeout = 5;

uint64_t max_content_length = 1024 * 1024 * 16; //max_content_length
int client_body_buffer_size = 1024 * 500;  //client_body_buffer_size

static char *unix_sock_name = NULL;

static int backlog = 1024 * 4; // backlog size
static int max_fd = 1024 * 4;  // picoev max_fd

// greenlet hub switch value
static PyObject *hub_switch_value;
PyObject* current_client;
PyObject* timeout_error;

/* reuse object */
static PyObject *client_key = NULL; //meinheld.client
static PyObject *wsgi_input_key = NULL; //wsgi.input key
// https://gist.github.com/mitsuhiko/5721107
static PyObject *wsgi_input_terminated_key = NULL; //wsgi.input_terminated key
static PyObject *status_code_key = NULL; //STATUS_CODE
static PyObject *bytes_sent_key = NULL; // SEND_BYTES
static PyObject *request_time_key = NULL; // REQUEST_TIME
static PyObject *local_time_key = NULL; // LOCAL_TIME
static PyObject *empty_string = NULL; //""

static PyObject *app_handler_func = NULL;

/* gunicorn */
static time_t watchdog_lasttime;
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

static PyObject*
internal_schedule_call(int seconds, PyObject *cb, PyObject *args, PyObject *kwargs, PyObject *greenlet);

static int
prepare_call_wsgi(client_t *client);

static void
call_wsgi_handler(client_t *client);

static int
check_status_code(client_t *client);

static pending_queue_t*
init_pendings(void)
{
    pending_queue_t *pendings = NULL;

    pendings = PyMem_Malloc(sizeof(pending_queue_t));
    if (pendings == NULL) {
        return NULL;
    }
    pendings->size = 0;
    pendings->max= 1024;
    pendings->q = (TimerObject**)malloc(sizeof(TimerObject*) * pendings->max);
    if (pendings->q == NULL) {
        PyMem_Free(pendings);
        return NULL;
    }
    return pendings;
}

static int
realloc_pendings(void)
{
    TimerObject **new_heap;
    uint32_t max;
    pending_queue_t *pendings = g_pendings;

    if (pendings->size >= pendings->max) {
        //realloc
        max = pendings->max * 2;
        new_heap = (TimerObject**)realloc(pendings->q, sizeof(TimerObject*) * max);
        if (new_heap == NULL) {
            PyErr_SetString(PyExc_Exception, "size over timer queue");
            return -1;
        }
        pendings->max = max;
        pendings->q = new_heap;
        RDEBUG("realloc max:%d", pendings->max);
    }
    return 1;
}

static void
destroy_pendings(void)
{
    int i = 0, len;
    TimerObject *timer = NULL;
    TimerObject **t = g_pendings->q;
    if (g_pendings == NULL) {
        return;
    }
    len = g_pendings->size;
    t += i;

    while(len--) {
        timer = *t;
        Py_DECREF(timer);
        t++;
    }

    free(g_pendings->q);
    PyMem_Free(g_pendings);
    g_pendings = NULL;
}


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
        GDEBUG("use pooled %p", client);
    } else {
        client = (client_t *)PyMem_Malloc(sizeof(client_t));
        GDEBUG("alloc %p", client);
    }
    memset(client, 0, sizeof(client_t));
    return client;
}

static void
dealloc_client(client_t *client)
{
    if (client_numfree < CLIENT_MAXFREELIST) {
        client_free_list[client_numfree++] = client;
        GDEBUG("back to pool %p", client);
    } else {
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
    client->complete = 1;
    client->request_queue = new_request_queue();
    client->remote_addr = remote_addr;
    client->remote_port = remote_port;
    /* client->body_type = BODY_TYPE_NONE; */
    GDEBUG("client alloc %p", client);
    return client;
}

static void
set_log_value(client_t *client, PyObject *environ, uintptr_t delta_msec)
{
    PyObject *status_code = NULL, *bytes = NULL, *request_time = NULL, *local_time = NULL;

    status_code = PyLong_FromLong(client->status_code);
    bytes = PyLong_FromLong(client->write_bytes);
    request_time = PyLong_FromLong(delta_msec);
    local_time = NATIVE_FROMSTRING((char*)http_log_time);

    if (status_code) {
        PyDict_SetItem(environ, status_code_key, status_code);
        Py_DECREF(status_code);
    }

    if (bytes) {
        PyDict_SetItem(environ, bytes_sent_key, bytes);
        Py_DECREF(bytes);
    }

    if (request_time) {
        PyDict_SetItem(environ, request_time_key, request_time);
        Py_DECREF(request_time);
    }

    if (local_time) {
        PyDict_SetItem(environ, local_time_key, local_time);
        Py_DECREF(local_time);
    }
}

static void
clean_client(client_t *client)
{
    PyObject *environ = NULL;
    uintptr_t end, delta_msec = 0;

    request *req = client->current_req;

    if (is_write_access_log) {
        DEBUG("write access log");
        cache_time_update();
        if (req) {
            environ = req->environ;
            end = current_msec;
            if (req->start_msec > 0){
                delta_msec = end - req->start_msec;
            }
            set_log_value(client, environ, delta_msec);
            call_access_logger(environ);
        } else {
            if (client->status_code != 408) {
                environ = new_environ(client);
                set_log_value(client, environ, delta_msec);
                call_access_logger(environ);
            }
        }
    }

    Py_CLEAR(client->http_status);
    Py_CLEAR(client->headers);
    Py_CLEAR(client->response_iter);
    Py_CLEAR(client->response);

    if (req == NULL) {
        goto init;
    }

    DEBUG("status_code:%d env:%p", client->status_code, req->environ);
    if (req->environ) {
        /* PyDict_Clear(client->environ); */
        /* DEBUG("CLEAR environ"); */
        Py_CLEAR(req->environ);
    }
    if (req->body) {
        if (req->body_type == BODY_TYPE_TMPFILE) {
            fclose(req->body);
        } else {
            free_buffer(req->body);
        }
        req->body = NULL;
    }
    free_request(req);

init:
    client->current_req = NULL;
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

    if (!client->response_closed) {
        close_response(client);
    }
    DEBUG("start close client:%p fd:%d status_code %d", client, client->fd, client->status_code);

    if (picoev_is_active(main_loop, client->fd)) {
        if (!picoev_del(main_loop, client->fd)) {
            activecnt--;
            DEBUG("activecnt:%d", activecnt);
        }
        DEBUG("picoev_del client:%p fd:%d", client, client->fd);
    }

    clean_client(client);

    DEBUG("remain http pipeline size :%d", client->request_queue->size);
    if (client->request_queue->size > 0) {
        if (check_status_code(client) > 0) {
            //process pipeline
            if (prepare_call_wsgi(client) > 0) {
                call_wsgi_handler(client);
            }
        }
        return ;
    }

    if (client->http_parser != NULL) {
        /* PyMem_Free(client->http_parser); */
        dealloc_parser(client->http_parser);
    }

    free_request_queue(client->request_queue);
    if (!client->keep_alive) {
        close(client->fd);
        BDEBUG("close client:%p fd:%d", client, client->fd);
    } else {
        BDEBUG("keep alive client:%p fd:%d", client, client->fd);
        new_client = new_client_t(client->fd, client->remote_addr, client->remote_port);
        new_client->keep_alive = 1;
        init_parser(new_client, server_name, server_port);
        ret = picoev_add(main_loop, new_client->fd, PICOEV_READ, keep_alive_timeout, read_callback, (void *)new_client);
        if (ret == 0) {
            activecnt++;
        }
    }
    //clear old client
    dealloc_client(client);
}

static void init_main_loop(void)
{
    if (main_loop == NULL) {
        /* init picoev */
        picoev_init(max_fd);
        /* create loop */
        main_loop = picoev_create_loop(60);
    }
}

static void
kill_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{

    picoev_del(loop, fd);
    if ((events & PICOEV_TIMEOUT) != 0) {
        DEBUG("force shutdown...");
        loop_done = 0;
    }
}

static inline void
kill_server(int timeout)
{

    int listen_sock = 0;
    PyObject *iter = NULL, *item;
    int set_callback = 0;

    if (main_loop == NULL) {
        return;
    }

    iter = PyObject_GetIter(listen_socks);
    if (PyErr_Occurred()){
        call_error_logger();
        return;
    }

    while((item =  PyIter_Next(iter))){
#ifdef PY3
        if (PyLong_Check(item)) {
            listen_sock = (int)PyLong_AsLong(item);
#else
        if (PyInt_Check(item)) {
            listen_sock = (int)PyInt_AsLong(item);
#endif

            //stop accepting
            if (!picoev_del(main_loop, listen_sock)) {
                activecnt--;
                DEBUG("activecnt:%d", activecnt);
            }

            if (!set_callback) {
                //shutdown timeout
                if (timeout > 0) {
                    //set timeout
                    (void)picoev_add(main_loop, listen_sock, PICOEV_TIMEOUT, timeout, kill_callback, NULL);
                } else {
                    (void)picoev_add(main_loop, listen_sock, PICOEV_TIMEOUT, 1, kill_callback, NULL);
                }
                set_callback = 1;
            }

        } else {
            //TODO WARN???
        }
        Py_DECREF(item);
    }
    Py_DECREF(iter);

}

static inline void
set_current_request(client_t *client)
{
    request *req;
    req = shift_request(client->request_queue);
    client->current_req = req;
}

static void
set_bad_request_code(client_t *client, int status_code)
{
    request *req;
    req = client->request_queue->tail;
    req->bad_request_code = status_code;
    DEBUG("set bad request code %d", status_code);
}

static int
check_status_code(client_t *client)
{
    request *req;
    req = client->request_queue->head;
    if (req && req->bad_request_code > 200) {
        //error
        //shift
        DEBUG("bad status code %d", req->bad_request_code);
        set_current_request(client);
        client->status_code = req->bad_request_code;
        send_error_page(client);
        close_client(client);
        return -1;
    }
    return 1;
}

static PyObject *
app_handler(PyObject *self, PyObject *args)
{
    int ret, active;
    PyObject *wsgi_args = NULL, *start = NULL, *current = NULL, *parent = NULL, *res = NULL;
    PyObject *env = NULL;
    ClientObject *pyclient;
    client_t *client;
    request *req;
    response_status status;

    if (!PyArg_ParseTuple(args, "O:app_handler", &env)) {
        return NULL;
    }
    pyclient = (ClientObject*)PyDict_GetItem(env, client_key);
    client = pyclient->client;

    req = client->current_req;
    start = create_start_response(client);

    if (!start) {
        return NULL;
    }

    DEBUG("call wsgi app");
    wsgi_args = PyTuple_Pack(2, env, start);
    res = PyObject_CallObject(wsgi_app, wsgi_args);
    Py_DECREF(wsgi_args);
    DEBUG("called wsgi app");

    //check response & PyErr_Occurred
    if (res && res == Py_None) {
        PyErr_SetString(PyExc_Exception, "response must be a iter or sequence object");
        goto error;
    }
    //Check wsgi_app error
    if (PyErr_Occurred()) {
        goto error;
    }

    client->response = res;

    if (client->response_closed) {
        //closed
        close_client(client);
        Py_RETURN_NONE;
    }
    status = response_start(client);

#ifdef WITH_GREENLET
    while(status != STATUS_OK) {
        if (status == STATUS_ERROR) {
            // Internal Server Error
            req->bad_request_code = 500;
            goto error;
        } else {
            active = picoev_is_active(main_loop, client->fd);
            ret = picoev_add(main_loop, client->fd, PICOEV_WRITE, 300, trampoline_callback, (void *)pyclient);
            if ((ret == 0 && !active)) {
                activecnt++;
            }

            // switch to hub
            current = pyclient->greenlet;
            parent = greenlet_getparent(current);

            /* Py_INCREF(hub_switch_value); */
            res = greenlet_switch(parent, hub_switch_value, NULL);
            Py_XDECREF(res);

            // try again after event switch
            status = process_body(client);
        }
    }
    status = close_response(client);
    if (status == STATUS_ERROR) {
        //TODO logging error
    }
    // send OK
    close_client(client);
#else
    switch(status) {
        case STATUS_ERROR:
            // Internal Server Error
            req->bad_request_code = 500;
            goto error;
        case STATUS_SUSPEND:
            // continue
            // set callback
            active = picoev_is_active(main_loop, client->fd);
            ret = picoev_add(main_loop, client->fd, PICOEV_WRITE, 300, write_callback, (void *)pyclient);
            if ((ret == 0 && !active)) {
                activecnt++;
            }
        default:
            // send OK
            close_client(client);
    }
#endif
    Py_RETURN_NONE;

error:
    client->status_code = 500;
    status = close_response(client);
    if (status == STATUS_ERROR) {
        //TODO logging error
    }
    /* write_error_log(__FILE__, __LINE__); */
    call_error_logger();
    send_error_page(client);
    close_client(client);
    Py_RETURN_NONE;
}

static PyMethodDef app_handler_def = {"_app_handler",   (PyCFunction)app_handler, METH_VARARGS, 0};

static PyObject*
get_app_handler(void)
{
    if (app_handler_func == NULL) {
        app_handler_func = PyCFunction_NewEx(&app_handler_def, (PyObject *)NULL, NULL);
    }
    //Py_INCREF(app_handler_func);
    return app_handler_func;
}

#ifdef WITH_GREENLET
static void
resume_greenlet(PyObject *greenlet)
{
    PyObject *res = NULL;
    PyObject *err_type, *err_val, *err_tb;

    if (PyErr_Occurred()) {
        PyErr_Fetch(&err_type, &err_val, &err_tb);
        PyErr_Clear();
        //set error
        res = greenlet_throw(greenlet, err_type, err_val, err_tb);
    } else {
        /* Py_INCREF(hub_switch_value); */
        res = greenlet_switch(greenlet, hub_switch_value, NULL);
        if (res == NULL) {
            call_error_logger();
            /* PyErr_Fetch(&err_type, &err_val, &err_tb); */
            /* PyErr_Clear(); */
            //set error
            /* res = greenlet_throw(greenlet, err_type, err_val, err_tb); */
        }
    }
    Py_XDECREF(res);
    if (greenlet_dead(greenlet)) {
        Py_DECREF(greenlet);
    }
}

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
    if (PyErr_Occurred()) {
        PyErr_Fetch(&err_type, &err_val, &err_tb);
        PyErr_Clear();
        //set error
        res = greenlet_throw(pyclient->greenlet, err_type, err_val, err_tb);
    } else {
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
    request *req = NULL;

    handler = get_app_handler();
    req = client->current_req;
    current_client = PyDict_GetItem(req->environ, client_key);
    pyclient = (ClientObject *)current_client;

    args = PyTuple_Pack(1, req->environ);
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


#ifdef WITH_GREENLET
static void
timeout_error_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    ClientObject *pyclient = (ClientObject *)(cb_arg);
    client_t *client = pyclient->client;

    if ((events & PICOEV_TIMEOUT) != 0) {
        DEBUG("timeout_error_callback pyclient:%p client:%p fd:%d", pyclient, pyclient->client, pyclient->client->fd);
        if (!picoev_del(loop, fd)) {
            activecnt--;
            DEBUG("activecnt:%d", activecnt);
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
        if (write(client->fd, "", 0) < 0) {
            if (!picoev_del(loop, fd)) {
                activecnt--;
                DEBUG("activecnt:%d", activecnt);
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
    PyObject *o = NULL;
    ClientObject *pyclient = NULL;
    client_t *client = NULL;


    if (!picoev_del(loop, fd)) {
        activecnt--;
        DEBUG("activecnt:%d", activecnt);
    }

    YDEBUG("call trampoline_callback fd:%d event:%d cb_arg:%p", fd, events, cb_arg);
    o = (PyObject*)cb_arg;

    if (CheckClientObject(o)) {
        pyclient = (ClientObject*)cb_arg;
        client = pyclient->client;

        if ((events & PICOEV_TIMEOUT) != 0) {

            RDEBUG("** trampoline_callback timeout **");
            //timeout
            client->keep_alive = 0;
            PyErr_SetString(timeout_error, "timeout");
        }
        YDEBUG("resume_wsgi_handler");
        resume_wsgi_handler(pyclient);
    } else if (greenlet_check(o)) {
        YDEBUG("resume_greenlet");
        resume_greenlet(o);
    }
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
        if (ret != 0) {
            //ok or die
            close_client(client);
        }
    }
}
#endif

static int
check_http_expect(client_t *client)
{
    PyObject *c = NULL;
    char *val = NULL;
    int ret;
    request *req = client->current_req;

    if (client->http_parser->http_minor == 1) {
        ///TODO CHECK
        c = PyDict_GetItemString(req->environ, "HTTP_EXPECT");
        if (c) {
            val = PyBytes_AS_STRING(c);
            if (!strncasecmp(val, "100-continue", 12)) {
                ret = write(client->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
                if (ret < 0) {
                    //fail
                    PyErr_SetFromErrno(PyExc_IOError);
                    /* write_error_log(__FILE__, __LINE__);  */
                    call_error_logger();
                    client->keep_alive = 0;
                    client->status_code = 500;
                    send_error_page(client);
                    close_client(client);
                    return -1;
                }
            } else {
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
    request *req = client->current_req;

    FILE *tmp = (FILE *)req->body;
    fflush(tmp);
    rewind(tmp);

    fd = fileno(tmp);
    input = PyFile_FromFd(fd, "<tmpfile>", "rb", -1, NULL, NULL, NULL, 1);
    if (input == NULL) {
        fclose(tmp);
        req->body = NULL;
        return -1;
    }
    //env["wsgi.input"] = tmpfile
    //
    PyDict_SetItem((PyObject *)req->environ, wsgi_input_key, input);
    Py_DECREF(input);
    req->body = NULL;
    return 1;
}

#else
static int
set_input_file(client_t *client)
{
    PyObject *input;
    request *req = client->current_req;
    FILE *tmp = (FILE *)req->body;
    fflush(tmp);
    rewind(tmp);
    input = PyFile_FromFile(tmp, "<tmpfile>", "r", fclose);
    if (input == NULL) {
        fclose(tmp);
        req->body = NULL;
        return -1;
    }
    //env["wsgi.input"] = tmpfile
    //
    PyDict_SetItem((PyObject *)req->environ, wsgi_input_key, input);
    Py_DECREF(input);
    req->body = NULL;
    return 0;
}
#endif

static int
set_input_object(client_t *client)
{
    PyObject *input = NULL;
    request *req = client->current_req;

    if (req->body_type == BODY_TYPE_BUFFER) {
        input = InputObject_New((buffer_t*)req->body);
    } else {
        if (req->body) {
            input = InputObject_New((buffer_t*)req->body);
        } else {
            input = InputObject_New(new_buffer(0, 0));
        }
    }
    if (input == NULL) {
        return -1;
    }
    PyDict_SetItem((PyObject *)req->environ, wsgi_input_key, input);
    Py_DECREF(input);
    req->body = NULL;
    return 1;
}

/*
static void
setting_keepalive(client_t *client)
{
    PyObject *c;
    char *val;
    if (is_keep_alive) {
        //support keep-alive
        c = PyDict_GetItemString(client->environ, "HTTP_CONNECTION");
        if (client->http_parser->http_minor == 1) {
            //HTTP 1.1
            if (c) {
                val = PyBytes_AS_STRING(c);
                if (!strcasecmp(val, "close")) {
                    client->keep_alive = 0;
                } else {
                    client->keep_alive = 1;
                }
            } else {
                client->keep_alive = 1;
            }
        } else {
            //HTTP 1.0
            if (c) {
                val = PyBytes_AS_STRING(c);
                if (!strcasecmp(val, "keep-alive")) {
                    client->keep_alive = 1;
                } else {
                    client->keep_alive = 0;
                }
            } else {
                client->keep_alive = 0;
            }
        }
    }
}
*/

static int
prepare_call_wsgi(client_t *client)
{
    request *req = NULL;

    set_current_request(client);

    req = client->current_req;

    //check Expect
    if (check_http_expect(client) < 0) {
        return -1;
    }

    if (req->body_type == BODY_TYPE_TMPFILE) {
        if (set_input_file(client) == -1) {
            return -1;
        }
    } else {
        if (set_input_object(client) == -1) {
            return -1;
        }
    }
    if (!PyDict_SetItem((PyObject *)req->environ, wsgi_input_terminated_key, Py_True)) {
        return -1;
    }

    if (!is_keep_alive) {
        client->keep_alive = 0;
    }
    /* setting_keepalive(client); */
    return 1;
}

static int
set_read_error(client_t *client, int status_code)
{
    client->keep_alive = 0;
    if (status_code == 0) {
        // bad request
        status_code = 400;
    }
    if (client->request_queue->size > 0) {
        //piplining
        set_bad_request_code(client, status_code);
        //finish = 1
        return 1;
    } else {
        if (!client->complete) {
            // read error while reading request.
            client->status_code = status_code;
            send_error_page(client);
        } // else keepalive timeout. should not send any data.
        close_client(client);
        return -1;
    }
}

static int
read_timeout(int fd, client_t *client)
{
    RDEBUG("** read timeout fd:%d", fd);
    //timeout
    return set_read_error(client, 408);
}

static int
compare_key(PyObject *env, char *key, char *compare)
{
    int ret = -1;
    char *val = NULL;

    PyObject *c = PyDict_GetItemString(env, key);
    if (c) {
#ifdef PY3
        c = PyUnicode_AsLatin1String(c);
        val = PyBytes_AS_STRING(c);
#else
        val = PyBytes_AS_STRING(c);
        Py_INCREF(c);
#endif
        ret = strcasecmp(val, compare);
    }
    Py_XDECREF(c);
    return ret;
}


static int
check_websocket(PyObject *env)
{
    //Support only RFC6455
    if (PyMapping_HasKeyString(env, "HTTP_SEC_WEBSOCKET_KEY") == 1) {
        if (compare_key(env, "HTTP_SEC_WEBSOCKET_VERSION", "13") == 0) {
            return 1;
        }
    }

    return -1;
}


static int
parse_new_protocol(request *req, char *buf, ssize_t readed, int nread)
{
    PyObject *env, *c;
    char *val = NULL;

    env = req->environ;
    c = PyDict_GetItemString(env, "HTTP_UPGRADE");
    if (c) {
#ifdef PY3
        c = PyUnicode_AsLatin1String(c);
        val = PyBytes_AS_STRING(c);
#else
        val = PyBytes_AS_STRING(c);
        Py_INCREF(c);
#endif
        DEBUG("Upgrade protocol %s", val);
        if (!strcasecmp(val, "websocket")) {
            Py_DECREF(c);
            //Support only RFC6455
            return check_websocket(env);
        }
    }
    // protocol not found error
    PyErr_SetString(PyExc_IOError,"unknow protocol");
    return -1;
}

static int
parse_http_request(int fd, client_t *client, char *buf, ssize_t r)
{
    int nread = 0;
    request *req = NULL;

    BDEBUG("fd:%d \n%.*s", fd, (int)r, buf);
    nread = execute_parse(client, buf, r);
    BDEBUG("read request fd %d readed %d nread %d", fd, (int)r, nread);

    req = client->current_req;

    if (client->upgrade) {
        //TODO  New protocol
        if (parse_new_protocol(req, buf, r, nread) == -1) {
            return set_read_error(client, req->bad_request_code);
        }
    } else if (req == NULL) {
        DEBUG("fd %d bad_request code 400", fd);
        return set_read_error(client, 400);
    } else if (nread != r || req->bad_request_code > 0) {
        DEBUG("fd %d bad_request code %d", fd,  req->bad_request_code);
        return set_read_error(client, req->bad_request_code);
    }

    if (parser_finish(client) > 0) {
        return 1;
    }
    return 0;
}

static int
read_request(picoev_loop *loop, int fd, client_t *client, char call_time_update)
{
    char buf[READ_BUF_SIZE];
    ssize_t r;

    if (!client->keep_alive) {
        picoev_set_timeout(loop, fd, READ_TIMEOUT_SECS);
    }

    Py_BEGIN_ALLOW_THREADS
    r = read(client->fd, buf, sizeof(buf));
    Py_END_ALLOW_THREADS
    switch (r) {
        case 0:
            return set_read_error(client, 503);
        case -1:
            // Error
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // try again later
                return 0;
            } else {
                // Fatal error
                client->keep_alive = 0;
                if (errno == ECONNRESET) {
                    client->header_done = 1;
                    client->response_closed = 1;
                } else {
                    PyErr_SetFromErrno(PyExc_IOError);
                    /* write_error_log(__FILE__, __LINE__);  */
                    call_error_logger();
                }
                return set_read_error(client, 500);
            }
        default:
            if (call_time_update) {
                cache_time_update();
            }
            return parse_http_request(fd, client, buf, r);

    }
}

static void
read_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    client_t *client = ( client_t *)(cb_arg);
    int finish = 0;

    if ((events & PICOEV_TIMEOUT) != 0) {
        finish = read_timeout(fd, client);

    } else if ((events & PICOEV_READ) != 0) {
        finish = read_request(loop, fd, client, 0);
    }
    if (finish == 1) {
        if (!picoev_del(main_loop, client->fd)) {
            activecnt--;
            DEBUG("activecnt:%d", activecnt);
        }
        if (check_status_code(client) > 0) {
            //current request ok
            if (prepare_call_wsgi(client) > 0) {
                call_wsgi_handler(client);
            }
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
    int finish = 0;
    if ((events & PICOEV_TIMEOUT) != 0) {
        // time out
        // next turn or other process
        return;
    } else if ((events & PICOEV_READ) != 0) {
        int i;
        socklen_t client_len = sizeof(client_addr);
        for (i=0; i<8; ++i) {
#if linux && defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
            client_fd = accept4(fd, (struct sockaddr *)&client_addr, &client_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
            client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
#endif

            if (client_fd != -1) {
                DEBUG("accept fd %d", client_fd);
                //printf("connected: %d\n", client_fd);
                if (setup_sock(client_fd) == -1) {
                    PyErr_SetFromErrno(PyExc_IOError);
                    /* write_error_log(__FILE__, __LINE__); */
                    call_error_logger();
                    // die
                    loop_done = 0;
                    return;
                }
                remote_addr = inet_ntoa (client_addr.sin_addr);
                remote_port = ntohs(client_addr.sin_port);
                client = new_client_t(client_fd, remote_addr, remote_port);
                init_parser(client, server_name, server_port);

                finish = read_request(loop, fd, client, 1);
                if (finish == 1) {
                    if (check_status_code(client) > 0) {
                        //current request ok
                        if (prepare_call_wsgi(client) > 0) {
                            call_wsgi_handler(client);
                        }
                    }
                } else if (finish == 0) {
                    ret = picoev_add(loop, client_fd, PICOEV_READ, keep_alive_timeout, read_callback, (void *)client);
                    if (ret == 0) {
                        activecnt++;
                    }
                }
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    PyErr_SetFromErrno(PyExc_IOError);
                    /* write_error_log(__FILE__, __LINE__); */
                    call_error_logger();
                    // die
                    kill_server(0);
                }
                break;
            }
        }
    }
}

static void
setup_server_env(void)
{
    /* setup_listen_sock(listen_sock); */
    cache_time_init();
    setup_static_env(server_name, server_port);
    setup_start_response();

    ClientObject_list_fill();
    client_t_list_fill();
    parser_list_fill();
    request_list_fill();
    buffer_list_fill();
    InputObject_list_fill();

    client_key = NATIVE_FROMSTRING("meinheld.client");
    wsgi_input_key = NATIVE_FROMSTRING("wsgi.input");
    wsgi_input_terminated_key = NATIVE_FROMSTRING("wsgi.input_terminated");
    status_code_key = NATIVE_FROMSTRING("STATUS_CODE");
    bytes_sent_key = NATIVE_FROMSTRING("SEND_BYTES");
    request_time_key = NATIVE_FROMSTRING("REQUEST_TIME");
    local_time_key = NATIVE_FROMSTRING("LOCAL_TIME");
    empty_string = NATIVE_FROMSTRING("");
}

static void
clear_server_env(void)
{
    //clean
    clear_start_response();
    clear_static_env();
    client_t_list_clear();
    parser_list_clear();

    ClientObject_list_clear();
    request_list_clear();
    buffer_list_clear();
    InputObject_list_clear();

    Py_DECREF(client_key);
    Py_DECREF(wsgi_input_key);
    Py_DECREF(wsgi_input_terminated_key);
    Py_DECREF(status_code_key);
    Py_DECREF(bytes_sent_key);
    Py_DECREF(request_time_key);
    Py_DECREF(local_time_key);
    Py_DECREF(empty_string);
}


static int
inet_listen(void)
{
    struct addrinfo hints, *servinfo, *p;
    int flag = 1;
    int res;
    char strport[7];
    int listen_sock = 0;
    PyObject *fd = NULL;

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

#ifdef PY3
    fd =  PyLong_FromLong((long) listen_sock);
#else
    fd =  PyInt_FromLong((long) listen_sock);
#endif
    listen_socks = PyList_New(0);
    if (PyList_Append(listen_socks, fd) == -1) {
        return -1;
    }
    Py_DECREF(fd);
    return 1;
}

static int
check_unix_sockpath(char *sock_name)
{

    if (!access(sock_name, F_OK)) {
        if (unlink(sock_name) < 0) {
            PyErr_SetFromErrno(PyExc_IOError);
            return -1;
        }
    }
    return 1;
}

static int
unix_listen(char *sock_name, int len)
{
    int listen_sock = 0;
    int flag = 1;
    int res;
    struct sockaddr_un saddr;
    mode_t old_umask;
    PyObject *fd;

    DEBUG("unix domain socket %s", sock_name);

    if (len >= sizeof(saddr.sun_path)) {
        PyErr_SetString(PyExc_OSError, "AF_UNIX path too long");
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));
    if (check_unix_sockpath(sock_name) == -1) {
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
#ifdef PY3
    fd =  PyLong_FromLong((long) listen_sock);
#else
    fd =  PyInt_FromLong((long) listen_sock);
#endif
    listen_socks = PyList_New(0);
    if (PyList_Append(listen_socks, fd) == -1) {
        return -1;
    }
    Py_DECREF(fd);
    return 1;
}

static void
fast_notify(void)
{
    spinner = (spinner + 1) % 2;
    fchmod(tempfile_fd, spinner);
    if (ppid != getppid()) {
        kill_server(gtimeout);
        tempfile_fd = 0;
    }
}
static PyObject*
set_listen_socket(PyObject *temp)
{
    if (listen_socks != NULL) {
        PyErr_SetString(PyExc_Exception, "already set listen socket");
        return NULL;
    }
#ifdef PY3
    if (PyLong_Check(temp)) {
#else
    if (PyInt_Check(temp)) {
#endif
       listen_socks = PyList_New(0);
       if (PyList_Append(listen_socks, temp) == -1) {
            return NULL;
       }
        Py_DECREF(temp);
    } else if (PyList_Check(temp)) {
        listen_socks = temp;
        Py_INCREF(listen_socks);
    } else {
        PyErr_SetString(PyExc_TypeError, "must be list or int");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
meinheld_listen(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *o = NULL;
    PyObject *sock_fd = NULL;
    char *path;
    int ret, len;

    static char *kwlist[] = {"address", "socket_fd", 0};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO:listen",
                                     kwlist, &o, &sock_fd)) {
        return NULL;
    }

    if (listen_socks != NULL) {
        PyErr_SetString(PyExc_Exception, "already set listen socket");
        return NULL;
    }

    if (o == NULL && sock_fd != NULL) {
        return set_listen_socket(sock_fd);
    } else if (PyTuple_Check(o)) {
        //inet
        if (!PyArg_ParseTuple(o, "si:listen", &server_name, &server_port)) {
            return NULL;
        }
        ret = inet_listen();
    } else if (PyBytes_Check(o)) {
        // unix domain
        if (!PyArg_Parse(o, "s#", &path, &len)) {
            return NULL;
        }
        ret = unix_listen(path, len);
    } else {
        PyErr_SetString(PyExc_TypeError, "args tuple or string(path)");
        return NULL;
    }
    if (ret < 0) {
        //error
        return NULL;
    }

    Py_RETURN_NONE;
}

static void
sigint_cb(int signum)
{
    DEBUG("call SIGINT");
    if (!catch_signal) {
        catch_signal = signum;
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
    PyObject *o = NULL;
    PyObject *func = NULL;

    if (!PyArg_ParseTuple(args, "O:access_logger", &o)) {
        return NULL;
    }

    if (o == Py_None) {
        is_write_access_log = 0;
        set_access_logger(NULL);
        Py_RETURN_NONE;
    }

    func = PyObject_GetAttrString(o, "access");
    if (func == NULL) {
        return NULL;
    }

    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }
    set_access_logger(func);
    is_write_access_log = 1;
    Py_RETURN_NONE;
}

static PyObject *
meinheld_error_log(PyObject *self, PyObject *args)
{
    PyObject *o = NULL;
    PyObject *func = NULL;

    if (!PyArg_ParseTuple(args, "O:error_logger", &o)) {
        return NULL;
    }

    if (o == Py_None) {
        set_err_logger(NULL);
        Py_RETURN_NONE;
    }

    func = PyObject_GetAttrString(o, "error");
    if (func == NULL) {
        return NULL;
    }

    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }
    set_err_logger(func);
    Py_RETURN_NONE;
}


static PyObject *
meinheld_stop(PyObject *self, PyObject *args, PyObject *kwds)
{
    int timeout = 0;

    static char *kwlist[] = {"timeout", 0};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:timeout",
                                     kwlist, &timeout)) {
        return NULL;
    }
    kill_server(timeout);
    Py_RETURN_NONE;
}


static inline int
fire_pendings(void)
{
    int ret = 1;
    TimerObject *timer = NULL;
    pending_queue_t *pendings = g_pendings;

    while(pendings->size && loop_done && activecnt > 0) {
        timer =  *(pendings->q + --pendings->size);
        DEBUG("start timer:%p activecnt:%d", timer, activecnt);
        fire_timer(timer);
        Py_DECREF(timer);
        activecnt--;

        DEBUG("fin timer:%p activecnt:%d", timer, activecnt);
        if (PyErr_Occurred()) {
            RDEBUG("pending call raise exception");
            call_error_logger();
            ret = -1;
            break;
        }
    }
    return ret;
}

static inline int
fire_timers(void)
{
    TimerObject *timer;
    int ret = 1;
    heapq_t *q = g_timers;
    time_t now = current_msec / 1000;

    while(q->size > 0 && loop_done && activecnt > 0) {

        timer = q->heap[0];
        DEBUG("seconds:%d", timer->seconds);
        DEBUG("now:%d", now);
        if (timer->seconds <= now) {
            //call
            timer = heappop(q);
            fire_timer(timer);

            Py_DECREF(timer);
            activecnt--;
            DEBUG("fin timer:%p activecnt:%d", timer, activecnt);

            if (PyErr_Occurred()) {
                RDEBUG("scheduled call raise exception");
                call_error_logger();
                ret = -1;
                break;
            }
            /* timer = q->heap[0]; */
        } else {
            break;
        }
    }
    return ret;

}

static int
listen_all_sockets(void)
{
    PyObject *iter = NULL, *item = NULL;
    int listen_sock = 0;
    int ret = 0;

    iter = PyObject_GetIter(listen_socks);

    if (PyErr_Occurred()){
        call_error_logger();
        return -1;
    }

    DEBUG("socks iter %p", iter);
    DEBUG("socks size %d", PyList_Size(listen_socks));

    while((item =  PyIter_Next(iter))){
#ifdef PY3
        if (PyLong_Check(item)) {
            listen_sock = (int)PyLong_AsLong(item);
#else
        if (PyInt_Check(item)) {
            listen_sock = (int)PyInt_AsLong(item);
#endif
            setup_listen_sock(listen_sock);
            ret = picoev_add(main_loop, listen_sock, PICOEV_READ, ACCEPT_TIMEOUT_SECS, accept_callback, NULL);
            if (ret == 0) {
                activecnt++;
            }
        }
        Py_DECREF(item);
    }
    Py_DECREF(iter);
    return 1;
}

static int
close_all_sockets(void)
{
    PyObject *iter = NULL, *item = NULL;
    int listen_sock = 0;

    iter = PyObject_GetIter(listen_socks);

    if (PyErr_Occurred()){
        call_error_logger();
        return -1;
    }

    while((item =  PyIter_Next(iter))){
#ifdef PY3
        if (PyLong_Check(item)) {
            listen_sock = (int)PyLong_AsLong(item);
#else
        if (PyInt_Check(item)) {
            listen_sock = (int)PyInt_AsLong(item);
#endif
            close(listen_sock);
            if (unix_sock_name) {
               unlink(unix_sock_name);
               unix_sock_name = NULL;
            }
        }
        Py_DECREF(item);
    }
    Py_DECREF(iter);
    return 1;
}

static PyObject *
meinheld_run_loop(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *watchdog_result;
    int silent = 0;
    int interrupted = 0;

    static char *kwlist[] = {"app", "silent", 0};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i:run",
                                     kwlist, &wsgi_app, &silent)) {
        return NULL;
    }

    if (listen_socks == NULL) {
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


    if (listen_all_sockets() < 0) {
        //FATAL Error
        return NULL;
    }

    /* loop */
    while (likely(loop_done == 1 && activecnt > 0)) {
        /* DEBUG("before activecnt:%d", activecnt); */
        fire_pendings();
        fire_timers();
        picoev_loop_once(main_loop, 10);
        if (unlikely(catch_signal != 0)) {
            if (catch_signal == SIGINT) {
                interrupted = 1;
            }
            catch_signal = 0;
            kill_server(0);
        }
        if (watch_loop && watchdog_lasttime != main_loop->now) {
            watchdog_lasttime = main_loop->now;
            if (tempfile_fd) {
                fast_notify();
            } else if (watchdog) {
                watchdog_result = PyObject_CallFunction(watchdog, NULL);
                if (PyErr_Occurred()) {
                    PyErr_Print();
                    PyErr_Clear();
                }
                Py_XDECREF(watchdog_result);
            }
        }
        /* DEBUG("after activecnt:%d", activecnt); */
        /* DEBUG("pendings->size:%d", g_pendings->size); */
    }

    Py_DECREF(wsgi_app);
    Py_CLEAR(watchdog);

    current_client = NULL;
    picoev_destroy_loop(main_loop);
    picoev_deinit();
    main_loop = NULL;

    clear_server_env();

    if (close_all_sockets() < 0) {
        Py_CLEAR(listen_socks);
        return NULL;
    }
    Py_CLEAR(listen_socks);


    if (!silent && interrupted) {
        //override
        PyErr_Clear();
        PyErr_SetNone(PyExc_KeyboardInterrupt);
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
    if (on < 0) {
        PyErr_SetString(PyExc_ValueError, "keep alive value out of range ");
        return NULL;
    }
    is_keep_alive = on;
    if (is_keep_alive) {
        keep_alive_timeout = on;
    } else {
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
    if (temp <= 0) {
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
    if (temp <= 0) {
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
    if (temp <= 0) {
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
    if (temp <= 0) {
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
    PyObject *temp;
    if (!PyArg_ParseTuple(args, "O:listen_socket", &temp)) {
        return NULL;
    }
    return set_listen_socket(temp);
}

PyObject *
meinheld_set_fastwatchdog(PyObject *self, PyObject *args)
{
    int _fd;
    int _ppid;
    int timeout = 0;
    if (!PyArg_ParseTuple(args, "iii", &_fd, &_ppid, &timeout))
        return NULL;

    watchdog_lasttime = 0;
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

    if (!PyCallable_Check(temp)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }
    Py_INCREF(temp);
    Py_XDECREF(watchdog);
    watchdog_lasttime = 0;
    watchdog = temp;
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

    if (!PyArg_ParseTuple(args, "s:process name", &name)) {
        return NULL;
    }
    Py_GetArgcArgv(&argc, &argv);

    for(i = 0;i < argc; i++) {
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
    PyObject *temp = NULL, *parent = NULL, *res = NULL;
    ClientObject *pyclient;
    client_t *client;
    int timeout = 0, ret = 0, active = 0;

    if (!PyArg_ParseTuple(args, "O|i:_suspend_client", &temp, &timeout)) {
        return NULL;
    }
    if (timeout < 0) {
        PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
        return NULL;
    }

    // check client object
    if (!CheckClientObject(temp)) {
        PyErr_SetString(PyExc_TypeError, "must be a client object");
        return NULL;
    }

    pyclient = (ClientObject *)temp;
    client = pyclient->client;

    if (!pyclient->greenlet) {
        PyErr_SetString(PyExc_ValueError, "greenlet is not set");
        return NULL;
    }

    /*
    if (pyclient->resumed == 1) {
        //call later
        PyErr_SetString(PyExc_IOError, "not called resume");
        return NULL;
    }
    */

    if (client && !(pyclient->suspended)) {
        pyclient->suspended = 1;
        parent = greenlet_getparent(pyclient->greenlet);

        set_so_keepalive(client->fd, 1);
        BDEBUG("meinheld_suspend_client pyclient:%p client:%p fd:%d", pyclient, client, client->fd);
        BDEBUG("meinheld_suspend_client active ? %d", picoev_is_active(main_loop, client->fd));
        active = picoev_is_active(main_loop, client->fd);
        if (timeout > 0) {
            ret = picoev_add(main_loop, client->fd, PICOEV_TIMEOUT, timeout, timeout_error_callback, (void *)pyclient);
        } else {
            ret = picoev_add(main_loop, client->fd, PICOEV_TIMEOUT, 3, timeout_callback, (void *)pyclient);
        }
        if ((ret == 0 && !active)) {
            activecnt++;
        }
        /* Py_INCREF(hub_switch_value); */
        res = greenlet_switch(parent, hub_switch_value, NULL);
        return res;
    } else {
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

    if (!PyArg_ParseTuple(args, "O|OO:_resume_client", &temp, &switch_args, &switch_kwargs)) {
        return NULL;
    }

    // check client object
    if (!CheckClientObject(temp)) {
        PyErr_SetString(PyExc_TypeError, "must be a client object");
        return NULL;
    }

    pyclient = (ClientObject *)temp;
    client = pyclient->client;

    if (!pyclient->greenlet) {
        PyErr_SetString(PyExc_ValueError, "greenlet is not set");
        return NULL;
    }

    if (!pyclient->suspended) {
        // not suspend
        PyErr_SetString(PyExc_IOError, "not suspended or dead ");
        return NULL;
    }

    if (pyclient->client && pyclient->suspended) {
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
        if ((ret == 0 && !active)) {
            activecnt++;
        }
    } else {
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
    if (!PyArg_ParseTuple(args, "i:cancel_event", &fd)) {
        return NULL;
    }

    if (fd < 0) {
        PyErr_SetString(PyExc_ValueError, "fileno value out of range ");
        return NULL;
    }
    if (picoev_is_active(main_loop, fd)) {
        if (!picoev_del(main_loop, fd)) {
            activecnt--;
            DEBUG("activecnt:%d", activecnt);
        }
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
    PyObject *current = NULL, *parent = NULL, *res = NULL;
    ClientObject *pyclient;
    int fd, event, timeout = 0, ret, active;
    PyObject *read = Py_None, *write = Py_None;

    static char *keywords[] = {"fileno", "read", "write", "timeout", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|OOi:trampoline", keywords, &fd, &read, &write, &timeout)) {
        return NULL;
    }

    if (fd < 0) {
        PyErr_SetString(PyExc_ValueError, "fileno value out of range ");
        return NULL;
    }

    if (timeout < 0) {
        PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
        return NULL;
    }

    if (PyObject_IsTrue(read) && PyObject_IsTrue(write)) {
        event = PICOEV_READWRITE;
    } else if (PyObject_IsTrue(read)) {
        event = PICOEV_READ;
    } else if (PyObject_IsTrue(write)) {
        event = PICOEV_WRITE;
    } else {
        event = PICOEV_TIMEOUT;
        if (timeout <= 0) {
            PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
            return NULL;
        }
    }

    /*
    if (current_client == NULL) {
        //TODO Cheange Exception class and Messages
        PyErr_SetString(PyExc_ValueError, "server not running ");
        //PyErr_SetString(PyExc_ValueError, " WSGI Handler");
        return NULL;
    }*/

    current = greenlet_getcurrent();
    pyclient = (ClientObject *) current_client;
    Py_DECREF(current);
    if (pyclient != NULL && pyclient->greenlet == current) {
        active = picoev_is_active(main_loop, fd);
        ret = picoev_add(main_loop, fd, event, timeout, trampoline_callback, (void *)pyclient);
        if ((ret == 0 && !active)) {
            activecnt++;
        }
        DEBUG("call from wsgi app");

        // switch to hub
        current = pyclient->greenlet;
        parent = greenlet_getparent(current);
        YDEBUG("trampoline fd:%d event:%d current:%p parent:%p cb_arg:%p", fd, event, current, parent, pyclient);

        /* Py_INCREF(hub_switch_value); */
        res = greenlet_switch(parent, hub_switch_value, NULL);
        return res;
    } else {
        DEBUG("call from greenlet");
        parent = greenlet_getparent(current);
        if (parent == NULL) {
            PyErr_SetString(PyExc_IOError, "call from same greenlet");
            return NULL;
        }

        active = picoev_is_active(main_loop, fd);
        ret = picoev_add(main_loop, fd, event, timeout, trampoline_callback, current);
        if ((ret == 0 && !active)) {
            activecnt++;
        }
        YDEBUG("trampoline fd:%d event:%d current:%p parent:%p cb_arg:%p", fd, event, current, parent, current);
        /* Py_INCREF(hub_switch_value); */
        res = greenlet_switch(parent, hub_switch_value, NULL);
        return res;
    }
#else
    NO_GREENLET_ERROR;
#endif

}


static PyObject*
meinheld_spawn(PyObject *self, PyObject *args, PyObject *kwargs)
{
#ifdef WITH_GREENLET
    PyObject *greenlet = NULL, *res = NULL;
    PyObject *func = NULL, *func_args = NULL, *func_kwargs = NULL;

    static char *keywords[] = {"func", "args", "kwargs", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OO:spawn", keywords, &func, &func_args, &func_kwargs)) {
        return NULL;
    }

    //new greenlet
    greenlet = greenlet_new(func, NULL);
    if (greenlet == NULL) {
        return NULL;
    }
    res = internal_schedule_call(0, func, func_args, func_kwargs, greenlet);
    Py_XDECREF(res);
    DEBUG("greenlet refcnt:%d", (int)Py_REFCNT(greenlet));
    Py_RETURN_NONE;

#else
    NO_GREENLET_ERROR;
#endif
}

static PyObject*
meinheld_sleep(PyObject *self, PyObject *args, PyObject *kwargs)
{
#ifdef WITH_GREENLET
    PyObject *current = NULL, *parent = NULL, *res = NULL;
    int sec = 0;
    static char *keywords[] = {"seconds", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i:sleep", keywords, &sec)) {
        return NULL;
    }

    current = greenlet_getcurrent();
    parent = greenlet_getparent(current);
    Py_DECREF(current);
    if (parent == NULL) {
        PyErr_SetString(PyExc_IOError, "call from same greenlet");
        return NULL;
    }
    DEBUG("sleep sec:%d", sec);
    res = internal_schedule_call(sec, NULL, NULL, NULL, current);
    Py_XDECREF(res);
    res = greenlet_switch(parent, hub_switch_value, NULL);
    Py_XDECREF(res);

    Py_RETURN_NONE;

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
internal_schedule_call(int seconds, PyObject *cb, PyObject *args, PyObject *kwargs, PyObject *greenlet)
{
    TimerObject* timer;
    heapq_t *timers = g_timers;
    pending_queue_t *pendings = g_pendings;

    timer = TimerObject_new(seconds, cb, args, kwargs, greenlet);
    if (timer == NULL) {
        return NULL;
    }
    DEBUG("seconds:%d", seconds);
    if (!seconds) {
        if (realloc_pendings() == -1) {
            Py_DECREF(timer);
            return NULL;
        }
        Py_INCREF(timer);

        //timer->pending = ++pendings->size;
        pendings->q[pendings->size] = timer;
        pendings->size++;
        DEBUG("add timer:%p pendings->size:%d", timer, pendings->size);
    } else {
        if (heappush(timers, timer) == -1) {
            Py_DECREF(timer);
            return NULL;
        }
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

    if (size < 2) {
        PyErr_SetString(PyExc_TypeError, "schedule_call takes exactly 2 argument");
        return NULL;
    }
    sec = PyTuple_GET_ITEM(args, 0);
    cb = PyTuple_GET_ITEM(args, 1);

#ifdef PY3
    if (!PyLong_Check(sec)) {
#else
    if (!PyInt_Check(sec)) {
#endif
        PyErr_SetString(PyExc_TypeError, "must be integer");
        return NULL;
    }
    if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    ret = PyLong_AsLong(sec);
    if (PyErr_Occurred()) {
        return NULL;
    }
    if (ret < 0) {
        PyErr_SetString(PyExc_TypeError, "seconds value out of range");
        return NULL;
    }
    seconds = ret;

    if (size > 2) {
        cbargs = PyTuple_GetSlice(args, 2, size);
    }

    timer = internal_schedule_call(seconds, cb, cbargs, kwargs, NULL);
    Py_XDECREF(cbargs);
    return timer;
}

static PyMethodDef ServerMethods[] = {
    {"listen", (PyCFunction)meinheld_listen, METH_VARARGS|METH_KEYWORDS, "set host and port num"},
    {"set_access_logger", meinheld_access_log, METH_VARARGS, "set access logger function."},
    {"set_error_logger", meinheld_error_log, METH_VARARGS, "set error logger function."},

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
    {"shutdown", (PyCFunction)meinheld_stop, METH_VARARGS|METH_KEYWORDS, "stop main loop "},

    {"schedule_call", (PyCFunction)meinheld_schedule_call, METH_VARARGS|METH_KEYWORDS, ""},
    {"spawn", (PyCFunction)meinheld_spawn, METH_VARARGS|METH_KEYWORDS, ""},
    {"sleep", (PyCFunction)meinheld_sleep, METH_VARARGS|METH_KEYWORDS, ""},

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
    if (m == NULL) {
        INITERROR;
    }

    if (PyType_Ready(&ResponseObjectType) < 0) {
        INITERROR;
    }

    if (PyType_Ready(&FileWrapperType) < 0) {
        INITERROR;
    }

    if (PyType_Ready(&ClientObjectType) < 0) {
        INITERROR;
    }

    if (PyType_Ready(&InputObjectType) < 0) {
        INITERROR;
    }

    if (PyType_Ready(&TimerObjectType) < 0) {
        INITERROR;
    }

    timeout_error = PyErr_NewException("meinheld.server.timeout",
                      PyExc_IOError, NULL);
    if (timeout_error == NULL) {
        INITERROR;
    }
    Py_INCREF(timeout_error);
    PyModule_AddObject(m, "timeout", timeout_error);

    //DEBUG("client size %u", sizeof(client_t));
    //DEBUG("request size %u", sizeof(request));
    //DEBUG("header bucket %u", sizeof(write_bucket));
    g_timers = init_queue();
    if (g_timers == NULL) {
        INITERROR;
    }
    g_pendings = init_pendings();
    if (g_pendings == NULL) {
        INITERROR;
    }

#ifdef WITH_GREENLET
    hub_switch_value = PyTuple_New(0);
#endif

#ifdef PY3
    return m;
#endif
}


