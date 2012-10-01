#include "heapq.h"

#define QUEUE_MAX 1024 * 4

heapq_t *
init_queue(void)
{
    heapq_t *q;
    q = (heapq_t *)PyMem_Malloc(sizeof(heapq_t));
    if(q == NULL){
        return NULL;
    }
    q->max = QUEUE_MAX;
    q->size = 0;
    q->heap = (TimerObject**)malloc(sizeof(TimerObject*) * q->max);
    if(q->heap == NULL){
        PyMem_Free(q);
        return NULL;
    }
    GDEBUG("alloc heapq_t : %p ", q);
    return q;
}

void
destroy_queue(heapq_t *q)
{
    TimerObject *obj;
    while(q->size > 0){
        obj = heappop(q);
        Py_XDECREF(obj);
    }
    free(q->heap);
    GDEBUG("dealloc heapq_t : %p ", q);
    PyMem_Free(q);
}

static void
_siftdown(heapq_t *q, uint32_t startpos, uint32_t pos)
{
    TimerObject *newitem = NULL, *parent = NULL;
    uint32_t parentpos;
    TimerObject **p = q->heap;
    newitem =  p[pos];

    while(likely(pos > startpos)){
        parentpos = (pos - 1) >> 1;
        parent = p[parentpos];
        if(newitem->seconds < parent->seconds){
            p[pos] = parent;
            pos = parentpos;
        }else{
            break;
        }
    }
    p[pos] = newitem;
}

static void
_siftup(heapq_t *q, uint32_t pos)
{
    uint32_t startpos, childpos, rightpos;
    TimerObject *newitem, *childpositem;
    uint32_t size = q->size;
    TimerObject **p = q->heap;

    startpos = pos;
    newitem = p[pos];
    childpos = (pos << 1) + 1;

    while(likely(childpos < size)){
        rightpos = childpos + 1;
        childpositem = p[childpos];
        if(rightpos < size && childpositem->seconds > p[rightpos]->seconds){
            childpos = rightpos;
            childpositem = p[childpos];
        }
        p[pos] = childpositem;
        pos = childpos;
        childpos = (pos << 1) + 1;
    }
    p[pos] = newitem;
    _siftdown(q, startpos, pos);
}

TimerObject *
heappop(heapq_t *q)
{
    uint32_t size;
    TimerObject *last = NULL, *retitem = NULL;
    TimerObject **p = q->heap;

    if(unlikely(q->size == 0)){
        return NULL;
    }

    q->size--;
    size = q->size;
    last = p[size];

    p[size] = NULL;

    if(likely(size > 0)){
        retitem = *p;
        *p = last;
        _siftup(q, 0);
    }else{
        retitem = last;
    }
    return retitem;
}

int
heappush(heapq_t *q, TimerObject *val)
{
    TimerObject **new_heap;
    uint32_t max;

    DEBUG("heappush size %d", q->size);
    if(q->size >= q->max){
        //realloc
        max = q->max * 2;
        new_heap = (TimerObject**)realloc(q->heap, sizeof(TimerObject*) * max);
        if(new_heap == NULL){
            PyErr_SetString(PyExc_Exception, "size over timer queue");
            return -1;
        }
        q->max = max;
        q->heap = new_heap;
        DEBUG("realloc max:%d", q->max);
    }
    Py_INCREF(val);
    q->heap[q->size] = val;
    q->size++;
    _siftdown(q, 0, q->size -1);
    return 1;
}

