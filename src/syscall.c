/* syscall.c — int 0x80: exit, read, write, brk */

#include "syscall.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

/* Userspace heap window (отдельно от ELF @ 0x400000 и kernel heap @ 0x200000) */
#define USER_HEAP_BASE 0x500000u
#define USER_HEAP_MAX  0x600000u

static uint32_t user_brk = USER_HEAP_BASE;

static uint32_t g_kernel_esp;
static void* g_kernel_cont;
static int g_exit_code;
static int g_in_process;

void process_save_kernel_context(uint32_t esp, void* cont) {
    g_kernel_esp = esp;
    g_kernel_cont = cont;
    g_in_process = 1;
}

int process_last_exit_code(void) {
    return g_exit_code;
}

void process_exit_to_kernel(int code) {
    g_exit_code = code;
    g_in_process = 0;
    uint32_t esp = g_kernel_esp;
    void* cont = g_kernel_cont;
    __asm__ volatile(
        "mov %0, %%esp\n"
        "jmp *%1\n"
        :
        : "r"(esp), "r"(cont)
        : "memory"
    );
    for (;;) __asm__ volatile ("hlt");
}

void user_heap_reset(void) {
    user_brk = USER_HEAP_BASE;
}

static int sys_write(int fd, const char* buf, int count) {
    (void)fd;
    if (count < 0 || !buf)
        return -1;
    for (int i = 0; i < count; i++)
        terminal_putchar(buf[i]);
    return count;
}

static int sys_read(int fd, char* buf, int count) {
    (void)fd;
    (void)buf;
    (void)count;
    return 0;
}

/* Linux-style brk: arg = desired new break; return actual break */
static uint32_t sys_brk(uint32_t new_brk) {
    if (new_brk == 0)
        return user_brk;
    if (new_brk < USER_HEAP_BASE)
        return user_brk;
    if (new_brk > USER_HEAP_MAX)
        return user_brk;
    user_brk = new_brk;
    return user_brk;
}

void syscall_handler(struct registers* regs) {
    uint32_t num = regs->eax;
    int ret = -1;

    switch (num) {
        case SYS_EXIT:
            process_exit_to_kernel((int)regs->ebx);
            break;
        case SYS_WRITE:
            ret = sys_write((int)regs->ebx, (const char*)regs->ecx, (int)regs->edx);
            break;
        case SYS_READ:
            ret = sys_read((int)regs->ebx, (char*)regs->ecx, (int)regs->edx);
            break;
        case SYS_BRK:
            ret = (int)sys_brk(regs->ebx);
            break;
        default:
            ret = -1;
            break;
    }

    regs->eax = (uint32_t)ret;
}
