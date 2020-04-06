#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

typedef struct meta
{
    size_t size;
    struct meta * next;
} meta_t;

//Thread Safe malloc/free: locking version 
void *ts_malloc_lock(size_t size);
void ts_free_lock(void *ptr);

//Thread Safe malloc/free: non-locking version 
void *ts_malloc_nolock(size_t size);
void ts_free_nolock(void *ptr);
