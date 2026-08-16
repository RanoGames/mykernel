/* unistd.h — Linux-like syscalls for MyKernel userspace */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define __NR_exit         1
#define __NR_fork         2
#define __NR_read         3
#define __NR_write        4
#define __NR_open         5
#define __NR_close        6
#define __NR_waitpid      7
#define __NR_unlink      10
#define __NR_execve      11
#define __NR_chdir       12
#define __NR_time        13
#define __NR_lseek       19
#define __NR_getpid      20
#define __NR_access      33
#define __NR_rename      38
#define __NR_mkdir       39
#define __NR_rmdir       40
#define __NR_dup         41
#define __NR_pipe        42
#define __NR_brk         45
#define __NR_ioctl       54
#define __NR_dup2        63
#define __NR_getppid     64
#define __NR_mmap        90
#define __NR_munmap      91
#define __NR_stat       106
#define __NR_fstat      108
#define __NR_yield      158
#define __NR_nanosleep  162
#define __NR_getcwd     183
#define __NR_getuid     199
#define __NR_getgid     200
#define __NR_geteuid    201
#define __NR_getegid    202
#define __NR_exit_group 252

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  64
#define O_TRUNC  512
#define O_APPEND 1024

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static inline int __syscall3(int n, int a, int b, int c) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
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
static inline int lseek(int fd, int off, int whence) {
    return __syscall3(__NR_lseek, fd, off, whence);
}
static inline int dup(int fd) { return __syscall3(__NR_dup, fd, 0, 0); }
static inline int dup2(int a, int b) { return __syscall3(__NR_dup2, a, b, 0); }
static inline int getpid(void) { return __syscall3(__NR_getpid, 0, 0, 0); }
static inline int getuid(void) { return __syscall3(__NR_getuid, 0, 0, 0); }
static inline int getgid(void) { return __syscall3(__NR_getgid, 0, 0, 0); }
static inline int chdir(const char* p) {
    return __syscall3(__NR_chdir, (int)(uintptr_t)p, 0, 0);
}
static inline int mkdir(const char* p, int mode) {
    return __syscall3(__NR_mkdir, (int)(uintptr_t)p, mode, 0);
}
static inline int unlink(const char* p) {
    return __syscall3(__NR_unlink, (int)(uintptr_t)p, 0, 0);
}
static inline int yield(void) { return __syscall3(__NR_yield, 0, 0, 0); }
static inline void _exit(int status) {
    __syscall3(__NR_exit, status, 0, 0);
    for (;;);
}

#endif
