/* syscall.c — обработка int 0x80 */

#include "syscall.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

/* Контекст возврата из userspace-программы в exec() */
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

int process_is_running(void) {
    return g_in_process;
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

static int sys_write(int fd, const char* buf, int count) {
    (void)fd; /* 1 = stdout; пока всё пишем на экран */
    if (count < 0)
        return -1;
    if (!buf)
        return -1;
    for (int i = 0; i < count; i++)
        terminal_putchar(buf[i]);
    return count;
}

static int sys_read(int fd, char* buf, int count) {
    (void)fd;
    (void)buf;
    (void)count;
    /* Пока не блокируем ввод из userspace — заглушка */
    return 0;
}

void syscall_handler(struct registers* regs) {
    uint32_t num = regs->eax;
    int ret = -1;

    switch (num) {
        case SYS_EXIT:
            process_exit_to_kernel((int)regs->ebx);
            /* не возвращаемся */
            break;
        case SYS_WRITE:
            ret = sys_write((int)regs->ebx, (const char*)regs->ecx, (int)regs->edx);
            break;
        case SYS_READ:
            ret = sys_read((int)regs->ebx, (char*)regs->ecx, (int)regs->edx);
            break;
        default:
            ret = -1;
            break;
    }

    regs->eax = (uint32_t)ret;
}
