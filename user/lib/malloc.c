/* malloc.c — userspace куча через syscall brk (45) */

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

#define ALIGN 8

struct mhdr {
    size_t size;
    int free;
    struct mhdr* next;
};

static struct mhdr* heap_head;
static uintptr_t heap_end;

static size_t align_up(size_t n) {
    return (n + (ALIGN - 1)) & ~(size_t)(ALIGN - 1);
}

static uintptr_t sys_brk(uintptr_t addr) {
    return (uintptr_t)__syscall3(__NR_brk, (int)addr, 0, 0);
}

static int heap_init(void) {
    if (heap_head)
        return 0;
    uintptr_t cur = sys_brk(0);
    if (!cur)
        return -1;
    heap_end = cur;
    heap_head = 0;
    return 0;
}

static struct mhdr* more_core(size_t need) {
    need = align_up(need + sizeof(struct mhdr));
    uintptr_t old = sys_brk(0);
    uintptr_t neu = sys_brk(old + need);
    if (neu < old + need)
        return 0;
    struct mhdr* b = (struct mhdr*)old;
    b->size = need - sizeof(struct mhdr);
    b->free = 1;
    b->next = 0;
    heap_end = neu;
    return b;
}

void* malloc(size_t size) {
    if (size == 0)
        return 0;
    size = align_up(size);
    if (heap_init() != 0)
        return 0;

    struct mhdr* prev = 0;
    struct mhdr* b = heap_head;
    while (b) {
        if (b->free && b->size >= size) {
            b->free = 0;
            return (void*)(b + 1);
        }
        prev = b;
        b = b->next;
    }

    b = more_core(size);
    if (!b)
        return 0;
    b->free = 0;
    if (prev)
        prev->next = b;
    else
        heap_head = b;
    return (void*)(b + 1);
}

void free(void* ptr) {
    if (!ptr)
        return;
    struct mhdr* b = ((struct mhdr*)ptr) - 1;
    b->free = 1;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }
    struct mhdr* b = ((struct mhdr*)ptr) - 1;
    if (b->size >= size)
        return ptr;
    void* n = malloc(size);
    if (!n)
        return 0;
    uint8_t* d = n;
    uint8_t* s = ptr;
    for (size_t i = 0; i < b->size; i++)
        d[i] = s[i];
    free(ptr);
    return n;
}

void* calloc(size_t n, size_t size) {
    size_t total = n * size;
    void* p = malloc(total);
    if (!p)
        return 0;
    uint8_t* b = p;
    for (size_t i = 0; i < total; i++)
        b[i] = 0;
    return p;
}
