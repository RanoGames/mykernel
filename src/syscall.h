/* syscall.h — системные вызовы через int 0x80 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include <stdint.h>

/* Номера как в Linux i386 */
#define SYS_EXIT  1
#define SYS_READ  3
#define SYS_WRITE 4
#define SYS_BRK   45

void syscall_handler(struct registers* regs);

void process_save_kernel_context(uint32_t esp, void* cont);
void process_exit_to_kernel(int code);
int process_last_exit_code(void);

/* сброс userspace brk перед exec */
void user_heap_reset(void);

#endif
