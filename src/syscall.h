/* syscall.h — системные вызовы через int 0x80 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include <stdint.h>

/* Номера близко к Linux i386 */
#define SYS_EXIT   1
#define SYS_READ   3
#define SYS_WRITE  4
#define SYS_OPEN   5
#define SYS_CLOSE  6
#define SYS_GETPID 20
#define SYS_BRK    45
#define SYS_YIELD  158

/* флаги open (упрощённо) */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

void syscall_handler(struct registers* regs);

void process_save_kernel_context(uint32_t esp, void* cont);
void process_exit_to_kernel(int code);
int process_last_exit_code(void);
void user_heap_reset(void);
void fd_table_reset(void);

#endif
