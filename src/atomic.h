/* atomic.h — minimal UP atomics for i386 */
#ifndef ATOMIC_H
#define ATOMIC_H

#include <stdint.h>

/* Atomic exchange: returns old value of *ptr */
static inline uint32_t atomic_xchg(volatile uint32_t* ptr, uint32_t val) {
    uint32_t ret;
    __asm__ volatile ("xchgl %0, %1"
                      : "=r"(ret), "+m"(*ptr)
                      : "0"(val)
                      : "memory");
    return ret;
}

/* Atomic compare-and-swap: if *ptr == expected, set *ptr = desired.
 * Returns 1 on success, 0 on failure. */
static inline int atomic_cas(volatile uint32_t* ptr, uint32_t expected, uint32_t desired) {
    uint32_t out;
    __asm__ volatile ("lock cmpxchgl %2, %1"
                      : "=a"(out), "+m"(*ptr)
                      : "r"(desired), "a"(expected)
                      : "memory");
    return out == expected;
}

static inline void atomic_store(volatile uint32_t* ptr, uint32_t val) {
    __asm__ volatile ("movl %1, %0" : "=m"(*ptr) : "r"(val) : "memory");
}

static inline uint32_t atomic_load(volatile uint32_t* ptr) {
    uint32_t v;
    __asm__ volatile ("movl %1, %0" : "=r"(v) : "m"(*ptr) : "memory");
    return v;
}

/* Spinlock built on xchg */
typedef struct {
    volatile uint32_t locked;
} spinlock_t;

static inline void spin_init(spinlock_t* l) {
    l->locked = 0;
}

static inline void spin_lock(spinlock_t* l) {
    while (atomic_xchg(&l->locked, 1) != 0) {
        __asm__ volatile ("pause");
    }
}

static inline void spin_unlock(spinlock_t* l) {
    atomic_store(&l->locked, 0);
}

#endif
