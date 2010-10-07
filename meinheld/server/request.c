#include "request.h"

#define MAXFREELIST 100

static request *free_list[MAXFREELIST];
static int numfree = 0;

inline void
request_list_fill(void)
{
    request *req;
	while (numfree < MAXFREELIST) {
        req = (request *)PyMem_Malloc(sizeof(request));
		free_list[numfree++] = req;
	}
}

inline void
request_list_clear(void)
{
	request *op;

	while (numfree) {
		op = free_list[--numfree];
		PyMem_Free(op);
	}
}

static inline request*
alloc_request(void)
{
    request *req;
	if (numfree) {
		req = free_list[--numfree];
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
	if (numfree < MAXFREELIST){
		free_list[numfree++] = req;
    }else{
	    PyMem_Free(req);
    }
}

inline request *
new_request(void)
{
    request *req = alloc_request();
    //request *req = (request *)PyMem_Malloc(sizeof(request));

    memset(req, 0, sizeof(request));
    return req;
}

inline header *
new_header(size_t fsize, size_t flimit, size_t vsize, size_t vlimit)
{
    header *h;
    h = PyMem_Malloc(sizeof(header));
    h->field = new_buffer(fsize, flimit);
    h->value = new_buffer(vsize, vlimit);
    return h;
}

inline void
free_header(header *h)
{
    PyMem_Free(h);
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

