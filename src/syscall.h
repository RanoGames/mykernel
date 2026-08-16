/* syscall.h — номера syscall близко к Linux i386 */

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
#define SYS_ACCESS         33
#define SYS_KILL           37
#define SYS_RENAME         38
#define SYS_MKDIR          39
#define SYS_RMDIR          40
#define SYS_DUP            41
#define SYS_PIPE           42
#define SYS_BRK            45
#define SYS_IOCTL          54
#define SYS_FCNTL          55
#define SYS_DUP2           63
#define SYS_GETPPID        64
#define SYS_SETSID         66
#define SYS_SIGACTION      67
#define SYS_SYMLINK        83
#define SYS_READLINK       85
#define SYS_REBOOT         88
#define SYS_MMAP           90
#define SYS_MUNMAP         91
#define SYS_STAT          106
#define SYS_LSTAT         107
#define SYS_FSTAT         108
#define SYS_GETDENTS      141
#define SYS_SELECT        142
#define SYS_YIELD         158
#define SYS_NANOSLEEP     162
#define SYS_GETCWD        183
#define SYS_GETUID        199
#define SYS_GETGID        200
#define SYS_GETEUID       201
#define SYS_GETEGID       202
#define SYS_GETTID        224
#define SYS_EXIT_GROUP    252
#define SYS_CLOCK_GETTIME 265

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
