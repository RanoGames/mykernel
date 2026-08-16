/* unistd.h — syscalls MyKernel */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define __NR_exit   1
#define __NR_read   3
#define __NR_write  4
#define __NR_open   5
#define __NR_close  6
#define __NR_getpid 20
#define __NR_brk    45
#define __NR_yield  158

#define O_RDONLY 0

static inline int __syscall3(int n, int a, int b, int c) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static inline int write(int fd, const void* buf, size_t count) {
    return __syscall3(__NR_write, fd, (int)(uintptr_t)buf, (int)count);
}

static inline int read(int fd, void* buf, size_t count) {
    return __syscall3(__NR_read, fd, (int)(uintptr_t)buf, (int)count);
}

static inline int open(const char* path, int flags) {
    return __syscall3(__NR_open, (int)(uintptr_t)path, flags, 0);
}

static inline int close(int fd) {
    return __syscall3(__NR_close, fd, 0, 0);
}

static inline int getpid(void) {
    return __syscall3(__NR_getpid, 0, 0, 0);
}

static inline int yield(void) {
    return __syscall3(__NR_yield, 0, 0, 0);
}

static inline void _exit(int status) {
    __syscall3(__NR_exit, status, 0, 0);
    for (;;);
}

#endif
