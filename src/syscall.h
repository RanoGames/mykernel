/* syscall.h — системные вызовы через int 0x80 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include <stdint.h>

#define SYS_EXIT  1
#define SYS_WRITE 4
#define SYS_READ  3

void syscall_handler(struct registers* regs);

void process_save_kernel_context(uint32_t esp, void* cont);
void process_exit_to_kernel(int code);
int process_last_exit_code(void);

#endif
