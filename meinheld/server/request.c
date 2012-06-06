#include "request.h"
#include "client.h"

/* use free_list */
#define REQUEST_MAXFREELIST 1024

static request *request_free_list[REQUEST_MAXFREELIST];
static int request_numfree = 0;

#define HEADER_MAXFREELIST 1024 * 16

static header *header_free_list[HEADER_MAXFREELIST];
static int header_numfree = 0;

inline void
request_list_fill(void)
{
    request *req;
    while (request_numfree < REQUEST_MAXFREELIST) {
        req = (request *)PyMem_Malloc(sizeof(request));
        request_free_list[request_numfree++] = req;
    }
}

inline void
request_list_clear(void)
{
    request *op;

    while (request_numfree) {
        op = request_free_list[--request_numfree];
        PyMem_Free(op);
    }
}

static inline request*
alloc_request(void)
{
    request *req;
    if (request_numfree) {
        req = request_free_list[--request_numfree];
#ifdef DEBUG
        printf("use pooled req %p\n", req);
#endif
    }else{
        req = (request *)PyMem_Malloc(sizeof(request));
#ifdef DEBUG
        printf("alloc req %p\n", req);
#endif
    }
    memset(req, 0, sizeof(request));
    return req;
}

inline void
dealloc_request(request *req)
{
    if (request_numfree < REQUEST_MAXFREELIST){
#ifdef DEBUG
        printf("back to request pool %p\n", req);
#endif
        request_free_list[request_numfree++] = req;
    }else{
        PyMem_Free(req);
    }
}


inline request_queue * 
new_request_queue(void)
{
    request_queue *q = NULL;
    q = (request_queue *)PyMem_Malloc(sizeof(request_queue));
    memset(q, 0, sizeof(request_queue));
    return q;
}

inline void
free_request_queue(request_queue *q)
{
    request *req, *temp_req;
    req = q->head;
    while(req){
        temp_req = req;
        req = (request *)temp_req->next;
        free_request(temp_req);
    }

    PyMem_Free(q);
}

inline void 
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


inline request*
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


inline request *
new_request(void)
{
    request *req = alloc_request();
    //request *req = (request *)PyMem_Malloc(sizeof(request));

    memset(req, 0, sizeof(request));
    return req;
}

inline void
header_list_fill(void)
{
    header *h;
    while (header_numfree < HEADER_MAXFREELIST) {
        h = (header *)PyMem_Malloc(sizeof(header));
        header_free_list[header_numfree++] = h;
    }
}

inline void
header_list_clear(void)
{
    header *op;

    while (header_numfree) {
        op = header_free_list[--header_numfree];
        PyMem_Free(op);
    }
}

static inline header*
alloc_header(void)
{
    header *h;
    if (header_numfree) {
        h = header_free_list[--header_numfree];
#ifdef DEBUG
        printf("use pooled header %p\n", h);
#endif
    }else{
        h = (header *)PyMem_Malloc(sizeof(header));
#ifdef DEBUG
        printf("alloc header %p\n", h);
#endif
    }
    memset(h, 0, sizeof(header));
    return h;
}

inline void
dealloc_header(header *h)
{
    if (header_numfree < HEADER_MAXFREELIST){
#ifdef DEBUG
        printf("back to header pool %p\n", h);
#endif
        header_free_list[header_numfree++] = h;
    }else{
        PyMem_Free(h);
    }
}

inline header *
new_header(size_t fsize, size_t flimit, size_t vsize, size_t vlimit)
{
    header *h;
    //h = PyMem_Malloc(sizeof(header));
    h = alloc_header();
    h->field = new_buffer(fsize, flimit);
    h->value = new_buffer(vsize, vlimit);
    return h;
}

inline void
free_header(header *h)
{
    //PyMem_Free(h);
    dealloc_header(h);
}

inline void
free_request(request *req)
{
    uint32_t i;
    header *h;
    if(req->path){
        free_buffer(req->path);
        req->path = NULL;
    }
    if(req->uri){
        free_buffer(req->uri); 
        req->uri = NULL;
    }
    if(req->query_string){
        free_buffer(req->query_string); 
        req->query_string = NULL;
    }
    if(req->fragment){
        free_buffer(req->fragment); 
        req->fragment = NULL;
    }
    for(i = 0; i < req->num_headers+1; i++){
        h = req->headers[i];
        if(h){
            free_buffer(h->field);
            free_buffer(h->value);
            free_header(h);
            req->headers[i] = NULL;
        }
    }
    dealloc_request(req);
    //PyMem_Free(req);
}

