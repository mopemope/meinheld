#include "request.h"
#include "client.h"

/* use free_list */
#define REQUEST_MAXFREELIST 1024
#define HEADER_MAXFREELIST 1024 * 16

static request *request_free_list[REQUEST_MAXFREELIST];
static int request_numfree = 0;

void
request_list_fill(void)
{
    request *req;
    while (request_numfree < REQUEST_MAXFREELIST) {
        req = (request *)PyMem_Malloc(sizeof(request));
        request_free_list[request_numfree++] = req;
    }
}

void
request_list_clear(void)
{
    request *op;

    while (request_numfree) {
        op = request_free_list[--request_numfree];
        PyMem_Free(op);
    }
}

static request*
alloc_request(void)
{
    request *req;
    if (request_numfree) {
        req = request_free_list[--request_numfree];
        GDEBUG("use pooled req %p", req);
    }else{
        req = (request *)PyMem_Malloc(sizeof(request));
        GDEBUG("alloc req %p", req);
    }
    memset(req, 0, sizeof(request));
    return req;
}

void
dealloc_request(request *req)
{
    if (request_numfree < REQUEST_MAXFREELIST){
        GDEBUG("back to request pool %p", req);
        request_free_list[request_numfree++] = req;
    }else{
        PyMem_Free(req);
    }
}


request_queue*
new_request_queue(void)
{
    request_queue *q = NULL;
    q = (request_queue *)PyMem_Malloc(sizeof(request_queue));
    if(q == NULL){
        return q;
    }
    memset(q, 0, sizeof(request_queue));
    GDEBUG("alloc req queue %p", q);
    return q;
}

void
free_request_queue(request_queue *q)
{
    request *req, *temp_req;
    req = q->head;
    while(req){
        temp_req = req;
        req = (request *)temp_req->next;
        free_request(temp_req);
    }

    GDEBUG("dealloc req queue %p", q);
    PyMem_Free(q);
}

void
push_request(request_queue *q, request *req)
{

    if(q->tail){
        q->tail->next = req;
    }else{
        q->head = req;
    }
    q->tail = req;
    q->size++;
}


request*
shift_request(request_queue *q)
{
    request *req, *temp_req;
    req = q->head;
    if(req == NULL){
        return NULL;
    }
    temp_req = req;
    req = req->next;
    q->head = req;
    q->size--;
    return temp_req;
}


request *
new_request(void)
{
    request *req = alloc_request();
    //request *req = (request *)PyMem_Malloc(sizeof(request));
    memset(req, 0, sizeof(request));
    return req;
}


void
free_request(request *req)
{
    Py_XDECREF(req->path);
    Py_XDECREF(req->field);
    Py_XDECREF(req->value);
    dealloc_request(req);
    //PyMem_Free(req);
}

