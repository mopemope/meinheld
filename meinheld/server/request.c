#include "request.h"

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
		request_free_list[request_numfree++] = req;
    }else{
	    PyMem_Free(req);
    }
}


inline request_env*
new_request_env(void)
{
    request_env *e = (request_env *)PyMem_Malloc(sizeof(request_env));
    memset(e, 0, sizeof(request_env));
    return e;
}

inline void
free_request_env(request_env *e)
{
    PyMem_Free(e);
}

inline request_queue*
new_request_queue(void)
{
    request_queue *q = NULL;
    q= (request_env *)PyMem_Malloc(sizeof(request_queue));
    memset(q, 0, sizeof(request_queue));
    return q;
}

inline void
free_request_queue(request_queue *q)
{
    PyObject *env;
    request_env *re, *temp_re;
    re = q->head;
    while(re){
        temp_re = re;
        env = temp_re->env;
        // force clear
        PyDict_Clear(env);
        Py_DECREF(env);
        re = (request_env *)temp_re->next;
        free_request_env(temp_re);
    }

    PyMem_Free(q);
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

