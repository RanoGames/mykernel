/* syscall.h — системные вызовы через int 0x80 (как в старом Linux i386).
 *
 * Соглашение:
 *   eax = номер syscall
 *   ebx = arg1
 *   ecx = arg2
 *   edx = arg3
 *   возврат в eax
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"

#define SYS_EXIT  1
#define SYS_WRITE 4
#define SYS_READ  3

void syscall_init(void);
void syscall_handler(struct registers* regs);

/* Вызывается из sys_exit: вернуться в ядро после exec */
void process_exit_to_kernel(int code);

#endif
