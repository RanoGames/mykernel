/* kmalloc.c — first-fit аллокатор в фиксированном окне памяти.
 *
 * Куча ядра: 0x200000 .. 0x300000 (1 МБ).
 * Ядро линкуется с 1 МБ, userspace ELF — с 4 МБ, так что окно свободно.
 */

#include "kmalloc.h"
#include <stdint.h>

#define KHEAP_START 0x200000u
#define KHEAP_SIZE  0x100000u /* 1 МБ */
#define ALIGN       8u

struct block {
    size_t size;       /* размер полезных данных */
    int    free;
    struct block* next;
};

static struct block* head;
static size_t heap_used;

static size_t align_up(size_t n) {
    return (n + (ALIGN - 1)) & ~(ALIGN - 1);
}

void kmalloc_init(void) {
    head = (struct block*)KHEAP_START;
    head->size = KHEAP_SIZE - sizeof(struct block);
    head->free = 1;
    head->next = 0;
    heap_used = 0;
}

static void split_block(struct block* b, size_t size) {
    size_t total = sizeof(struct block) + b->size;
    size_t need = sizeof(struct block) + size;
    if (total < need + sizeof(struct block) + ALIGN)
        return; /* слишком маленький остаток */

    struct block* n = (struct block*)((uint8_t*)b + need);
    n->size = b->size - size - sizeof(struct block);
    n->free = 1;
    n->next = b->next;
    b->size = size;
    b->next = n;
}

static void coalesce(void) {
    struct block* b = head;
    while (b && b->next) {
        if (b->free && b->next->free) {
            b->size += sizeof(struct block) + b->next->size;
            b->next = b->next->next;
        } else {
            b = b->next;
        }
    }
}

void* kmalloc(size_t size) {
    if (size == 0)
        return 0;
    size = align_up(size);

    struct block* b = head;
    while (b) {
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = 0;
            heap_used += b->size;
            return (void*)(b + 1);
        }
        b = b->next;
    }
    return 0; /* out of memory */
}

void kfree(void* ptr) {
    if (!ptr)
        return;
    struct block* b = ((struct block*)ptr) - 1;
    if (b->free)
        return;
    if (heap_used >= b->size)
        heap_used -= b->size;
    else
        heap_used = 0;
    b->free = 1;
    coalesce();
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return 0;
    }
    struct block* b = ((struct block*)ptr) - 1;
    if (b->size >= size)
        return ptr;
    void* n = kmalloc(size);
    if (!n)
        return 0;
    uint8_t* dst = n;
    uint8_t* src = ptr;
    for (size_t i = 0; i < b->size; i++)
        dst[i] = src[i];
    kfree(ptr);
    return n;
}

void kmalloc_stats(size_t* used, size_t* total, size_t* free_bytes) {
    if (used) *used = heap_used;
    if (total) *total = KHEAP_SIZE;
    if (free_bytes) {
        size_t f = 0;
        for (struct block* b = head; b; b = b->next)
            if (b->free) f += b->size;
        *free_bytes = f;
    }
}
