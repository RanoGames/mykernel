/* syscall.h — номера syscall близко к Linux i386 (для будущего glibc/musl) */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include <stdint.h>

#define SYS_EXIT            1
#define SYS_FORK            2
#define SYS_READ            3
#define SYS_WRITE           4
#define SYS_OPEN            5
#define SYS_CLOSE           6
#define SYS_WAITPID         7
#define SYS_CREAT           8
#define SYS_LINK            9
#define SYS_UNLINK         10
#define SYS_EXECVE         11
#define SYS_CHDIR          12
#define SYS_TIME           13
#define SYS_CHMOD          15
#define SYS_LSEEK          19
#define SYS_GETPID         20
#define SYS_MOUNT          21
#define SYS_UMOUNT         22
#define SYS_SETUID         23
#define SYS_GETUID         24
#define SYS_PTRACE         26
#define SYS_ALARM          27
#define SYS_PAUSE          29
#define SYS_ACCESS         33
#define SYS_SYNC           36
#define SYS_KILL           37
#define SYS_RENAME         38
#define SYS_MKDIR          39
#define SYS_RMDIR          40
#define SYS_DUP            41
#define SYS_PIPE           42
#define SYS_TIMES          43
#define SYS_BRK            45
#define SYS_SETGID         46
#define SYS_GETGID         47
#define SYS_IOCTL          54
#define SYS_FCNTL          55
#define SYS_UMASK          60
#define SYS_DUP2           63
#define SYS_GETPPID        64
#define SYS_SETSID         66
#define SYS_SIGACTION      67
#define SYS_SETHOSTNAME    74
#define SYS_SETRLIMIT      75
#define SYS_GETRLIMIT      76
#define SYS_GETRUSAGE      77
#define SYS_GETTIMEOFDAY   78
#define SYS_SETTIMEOFDAY   79
#define SYS_SYMLINK        83
#define SYS_READLINK       85
#define SYS_REBOOT         88
#define SYS_MMAP           90
#define SYS_MUNMAP         91
#define SYS_TRUNCATE       92
#define SYS_FTRUNCATE      93
#define SYS_FCHMOD         94
#define SYS_SOCKETCALL    102
#define SYS_STAT          106
#define SYS_LSTAT         107
#define SYS_FSTAT         108
#define SYS_SYSINFO       116
#define SYS_UNAME         122
#define SYS_MPROTECT      125
#define SYS_SIGPROCMASK   126
#define SYS_GETPGID       132
#define SYS_FCHDIR        133
#define SYS_GETDENTS      141
#define SYS_SELECT        142
#define SYS_FLOCK         143
#define SYS_MSYNC         144
#define SYS_READV         145
#define SYS_WRITEV        146
#define SYS_GETSID        147
#define SYS_FDATASYNC     148
#define SYS_MLOCK         150
#define SYS_MUNLOCK       151
#define SYS_MLOCKALL      152
#define SYS_MUNLOCKALL    153
#define SYS_SCHED_YIELD   158
#define SYS_YIELD         158
#define SYS_NANOSLEEP     162
#define SYS_MREMAP        163
#define SYS_POLL          168
#define SYS_PRCTL         172
#define SYS_RT_SIGACTION  174
#define SYS_RT_SIGPROCMASK 175
#define SYS_RT_SIGRETURN  173
#define SYS_PREAD64       180
#define SYS_PWRITE64      181
#define SYS_GETCWD        183
#define SYS_CAPGET        184
#define SYS_CAPSET        185
#define SYS_VFORK         190
#define SYS_UGETRLIMIT    191
#define SYS_MMAP2         192
#define SYS_TRUNCATE64    193
#define SYS_FTRUNCATE64   194
#define SYS_STAT64        195
#define SYS_LSTAT64       196
#define SYS_FSTAT64       197
#define SYS_LCHOWN32      198
#define SYS_GETUID32      199
#define SYS_GETGID32      200
#define SYS_GETEUID32     201
#define SYS_GETEUID       201
#define SYS_GETEGID32     202
#define SYS_GETEGID       202
#define SYS_GETTID        224
#define SYS_FKCTL         221
#define SYS_FCNTL64       221
#define SYS_GETDENTS64    220
#define SYS_MADVISE       219
#define SYS_FUTEX         240
#define SYS_SET_THREAD_AREA 243
#define SYS_GET_THREAD_AREA 244
#define SYS_EXIT_GROUP    252
#define SYS_CLOCK_GETTIME 265
#define SYS_CLOCK_SETTIME 266
#define SYS_CLOCK_GETRES  267
#define SYS_CLOCK_NANOSLEEP 230
#define SYS_TGKILL        270
#define SYS_OPENAT        295
#define SYS_MKDIRAT       296
#define SYS_UNLINKAT      301
#define SYS_RENAMEAT      302
#define SYS_FACCESSAT     307
#define SYS_PIPE2         331

/* mmap flags (Linux) */
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define PROT_NONE   0x0
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  64
#define O_TRUNC  512
#define O_APPEND 1024

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

void syscall_handler(struct registers* regs);
void process_save_kernel_context(uint32_t esp, uint32_t ebp, void* cont);
void process_exit_to_kernel(int code);
int process_last_exit_code(void);
void user_heap_reset(void);
void fd_table_reset(void);

#endif
