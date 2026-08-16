/* kmalloc.h — простой аллокатор кучи ядра */

#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>
#include <stdint.h>

void  kmalloc_init(void);
void* kmalloc(size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t size);

/* статистика для shell (free) */
void kmalloc_stats(size_t* used, size_t* total, size_t* free_bytes);

#endif
