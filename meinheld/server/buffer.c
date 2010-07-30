#include "buffer.h"

#define LIMIT_MAX 1024 * 1024 * 1024

inline buffer *
new_buffer(size_t buf_size, size_t limit)
{
    buffer *buf;
    buf = PyMem_Malloc(sizeof(buffer));
    memset(buf, 0, sizeof(buffer));
    buf->buf = PyMem_Malloc(sizeof(char) * buf_size);
    buf->buf_size = buf_size;
    if(limit){
        buf->limit = limit;
    }else{
        buf->limit = LIMIT_MAX;
    }
//    buf->fill = 1;
    return buf;
}

inline buffer_result
write2buf(buffer *buf, const char *c, size_t  l) {
    size_t newl;
    char *newbuf;
    buffer_result ret = WRITE_OK;
    newl = buf->len + l;
    
    
    if (newl >= buf->buf_size) {
        buf->buf_size *= 2;
        if(buf->buf_size <= newl) {
            buf->buf_size = (int)(newl + 1);
        }
        if(buf->buf_size > buf->limit){
            buf->buf_size = buf->limit + 1;
        }
        newbuf = (char*)PyMem_Realloc(buf->buf, buf->buf_size);
        if (!newbuf) {
            PyErr_SetString(PyExc_MemoryError,"out of memory");
            free(buf->buf);
            buf->buf = 0;
            buf->buf_size = buf->len = 0;
            return MEMORY_ERROR;
        }
        buf->buf = newbuf;
    }
    if(newl >= buf->buf_size){
        l = buf->buf_size - buf->len -1;
        ret = LIMIT_OVER;
    }
    memcpy(buf->buf + buf->len, c , l);
    buf->len += (int)l;
    return ret;
}

inline void
free_buffer(buffer *buf)
{
    PyMem_Free(buf->buf);
    PyMem_Free(buf);
}

inline PyObject *
getPyString(buffer *buf)
{
    PyObject *o;
    o = PyString_FromStringAndSize(buf->buf, buf->len);
    free_buffer(buf);
    return o;
}

inline char *
getString(buffer *buf)
{
    buf->buf[buf->len] = '\0';
    return buf->buf;
}


