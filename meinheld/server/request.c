#include "request.h"

inline request *
new_request(void)
{
    request *req = (request *)PyMem_Malloc(sizeof(request));
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
    PyMem_Free(req);
}

