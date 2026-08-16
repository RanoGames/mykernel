/* stdlib.h — malloc/free через brk */
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void* malloc(size_t size);
void  free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t n, size_t size);

#endif
