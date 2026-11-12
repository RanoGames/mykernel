/* kmalloc.c — first-fit аллокатор в фиксированном окне памяти.
 *
 * Куча ядра: 0x200000 .. 0x300000 (1 МБ).
 * Ядро линкуется с 1 МБ, userspace ELF — с 4 МБ, так что окно свободно.
 */

#include "kmalloc.h"
#include <stdint.h>

#define KHEAP_START 0x03000000u /* 48 MiB (safe upper memory in 128 MB RAM) */
#define KHEAP_SIZE  0x01000000u /* 16 MiB */
#define ALIGN       8u
#define BLOCK_MAGIC 0x4B4D414Cu /* "KMAL" */

struct block {
    uint32_t      magic;
    size_t        size;       /* размер полезных данных */
    int           free;
    struct block* next;
};

static struct block* head = 0;
static size_t heap_used = 0;

static size_t align_up(size_t n) {
    if (n > KHEAP_SIZE) return KHEAP_SIZE;
    return (n + (ALIGN - 1)) & ~(ALIGN - 1);
}

static inline uint32_t kmalloc_lock(void) {
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));
    return flags;
}

static inline void kmalloc_unlock(uint32_t flags) {
    if (flags & 0x200)
        __asm__ volatile ("sti");
}

void kmalloc_init(void) {
    uint32_t flags = kmalloc_lock();
    head = (struct block*)KHEAP_START;
    head->magic = BLOCK_MAGIC;
    head->size = KHEAP_SIZE - sizeof(struct block);
    head->free = 1;
    head->next = 0;
    heap_used = 0;
    kmalloc_unlock(flags);
}

static void split_block(struct block* b, size_t size) {
    if (!b || b->magic != BLOCK_MAGIC) return;
    size_t total = sizeof(struct block) + b->size;
    size_t need = sizeof(struct block) + size;
    if (total < need + sizeof(struct block) + ALIGN)
        return; /* слишком маленький остаток */

    struct block* n = (struct block*)((uint8_t*)b + need);
    n->magic = BLOCK_MAGIC;
    n->size = b->size - size - sizeof(struct block);
    n->free = 1;
    n->next = b->next;
    b->size = size;
    b->next = n;
}

static void coalesce(void) {
    struct block* b = head;
    while (b && b->next) {
        if (b->magic != BLOCK_MAGIC) break;
        if ((uintptr_t)b < KHEAP_START || (uintptr_t)b >= KHEAP_START + KHEAP_SIZE) break;
        if (b->next->magic != BLOCK_MAGIC) break;
        if ((uintptr_t)b->next <= (uintptr_t)b || (uintptr_t)b->next >= KHEAP_START + KHEAP_SIZE) break;

        if (b->free && b->next->free) {
            b->size += sizeof(struct block) + b->next->size;
            b->next = b->next->next;
        } else {
            b = b->next;
        }
    }
}

void* kmalloc(size_t size) {
    if (size == 0 || size > KHEAP_SIZE - sizeof(struct block))
        return 0;
    size = align_up(size);

    uint32_t flags = kmalloc_lock();
    if (!head || head->magic != BLOCK_MAGIC) {
        head = (struct block*)KHEAP_START;
        head->magic = BLOCK_MAGIC;
        head->size = KHEAP_SIZE - sizeof(struct block);
        head->free = 1;
        head->next = 0;
        heap_used = 0;
    }

    struct block* b = head;
    while (b) {
        if (b->magic != BLOCK_MAGIC) {
            kmalloc_unlock(flags);
            return 0; /* повреждение кучи */
        }
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = 0;
            heap_used += b->size;
            kmalloc_unlock(flags);
            return (void*)(b + 1);
        }
        b = b->next;
    }
    kmalloc_unlock(flags);
    return 0; /* out of memory */
}

void kfree(void* ptr) {
    if (!ptr)
        return;
    /* Проверка выравнивания и границ */
    if ((uintptr_t)ptr & (ALIGN - 1))
        return;
    if ((uintptr_t)ptr < KHEAP_START + sizeof(struct block) ||
        (uintptr_t)ptr >= KHEAP_START + KHEAP_SIZE)
        return;

    uint32_t flags = kmalloc_lock();
    struct block* b = ((struct block*)ptr) - 1;
    if (b->magic != BLOCK_MAGIC || b->free) {
        kmalloc_unlock(flags);
        return;
    }

    if (heap_used >= b->size)
        heap_used -= b->size;
    else
        heap_used = 0;
    b->free = 1;
    coalesce();
    kmalloc_unlock(flags);
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return 0;
    }
    if ((uintptr_t)ptr & (ALIGN - 1))
        return 0;
    if ((uintptr_t)ptr < KHEAP_START + sizeof(struct block) ||
        (uintptr_t)ptr >= KHEAP_START + KHEAP_SIZE)
        return 0;

    struct block* b = ((struct block*)ptr) - 1;
    if (b->magic != BLOCK_MAGIC || b->free)
        return 0;

    if (b->size >= size)
        return ptr;

    void* n = kmalloc(size);
    if (!n)
        return 0;

    uint8_t* dst = (uint8_t*)n;
    const uint8_t* src = (const uint8_t*)ptr;
    size_t to_copy = (b->size < size) ? b->size : size;
    for (size_t i = 0; i < to_copy; i++)
        dst[i] = src[i];

    kfree(ptr);
    return n;
}

void kmalloc_stats(size_t* used, size_t* total, size_t* free_bytes) {
    uint32_t flags = kmalloc_lock();
    if (used) *used = heap_used;
    if (total) *total = KHEAP_SIZE;
    if (free_bytes) {
        size_t f = 0;
        for (struct block* b = head; b; b = b->next) {
            if (b->magic != BLOCK_MAGIC) break;
            if (b->free) f += b->size;
        }
        *free_bytes = f;
    }
    kmalloc_unlock(flags);
}
