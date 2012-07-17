#include "buffer.h"

#define LIMIT_MAX 1024 * 1024 * 1024

#define MAXFREELIST 1024 * 16 * 2

static buffer_t *buffer_free_list[MAXFREELIST];
static int numfree = 0;

void
buffer_list_fill(void)
{
    buffer_t *buf;

    while (numfree < MAXFREELIST) {
        buf = (buffer_t*)PyMem_Malloc(sizeof(buffer_t));
        buffer_free_list[numfree++] = buf;
    }
}

void
buffer_list_clear(void)
{
    buffer_t *op;

    while (numfree) {
        op = buffer_free_list[--numfree];
        PyMem_Free(op);
    }
}

static buffer_t*
alloc_buffer(void)
{
    buffer_t *buf;
    if (numfree) {
        buf = buffer_free_list[--numfree];
        //DEBUG("use pooled buf %p", buf);
    }else{
        buf = (buffer_t*)PyMem_Malloc(sizeof(buffer_t));
        //DEBUG("alloc buf %p", buf);
    }
    memset(buf, 0, sizeof(buffer_t));
    return buf;
}

static void
dealloc_buffer(buffer_t *buf)
{
    if (numfree < MAXFREELIST){
        //DEBUG("back to buffer pool %p", buf);
        buffer_free_list[numfree++] = buf;
    }else{
        PyMem_Free(buf);
    }
}

/*
static int
hex2int(int i)
{
    i = toupper(i);
    i = i - '0';
    if(i > 9){
        i = i - 'A' + '9' + 1;
    }
    return i;
}

static int
urldecode(char *buf, int len)
{
    int c, c1;
    char *s0, *t;
    t = s0 = buf;
    while(len >0){
        c = *buf++;
        if(c == '%' && len > 2){
            c = *buf++;
            c1 = c;
            c = *buf++;
            c = hex2int(c1) * 16 + hex2int(c);
            len -= 2;
        }
        *t++ = c;
        len--;
    }
    *t = 0;
    return t - s0;
}*/

buffer_t*
new_buffer(size_t buf_size, size_t limit)
{
    buffer_t *buf;

    //buf = PyMem_Malloc(sizeof(buffer));
    //memset(buf, 0, sizeof(buffer));
    buf = alloc_buffer();

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

buffer_result
write2buf(buffer_t *buf, const char *c, size_t  l) {

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

void
free_buffer(buffer_t *buf)
{
    PyMem_Free(buf->buf);
    //PyMem_Free(buf);
    dealloc_buffer(buf);
}

PyObject *
getPyString(buffer_t *buf)
{
    PyObject *o;
    o = PyBytes_FromStringAndSize(buf->buf, buf->len);
    free_buffer(buf);
    return o;
}

char *
getString(buffer_t *buf)
{
    buf->buf[buf->len] = '\0';
    return buf->buf;
}





