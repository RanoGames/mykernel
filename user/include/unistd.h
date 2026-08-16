/* unistd.h — минимальные syscalls MyKernel */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Linux i386 numbers */
#define __NR_exit  1
#define __NR_read  3
#define __NR_write 4

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

static inline ssize_t write(int fd, const void* buf, size_t count) {
    return (ssize_t)__syscall3(__NR_write, fd, (int)buf, (int)count);
}

static inline ssize_t read(int fd, void* buf, size_t count) {
    return (ssize_t)__syscall3(__NR_read, fd, (int)buf, (int)count);
}

static inline void _exit(int status) {
    __syscall3(__NR_exit, status, 0, 0);
    for (;;);
}

#endif
