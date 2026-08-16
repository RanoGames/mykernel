/* syscall.c — int 0x80 */

#include "syscall.h"
#include "vga.h"
#include "fs.h"
#include "sched.h"
#include <stdint.h>
#include <stddef.h>

#define USER_HEAP_BASE 0x500000u
#define USER_HEAP_MAX  0x600000u
#define FD_MAX 16

enum fd_type { FD_NONE = 0, FD_CONSOLE, FD_RAMFILE };

struct fd_entry {
    enum fd_type type;
    int used;
    const char* data;
    size_t len;
    size_t pos;
    int writable;
};

static struct fd_entry fds[FD_MAX];
static uint32_t user_brk = USER_HEAP_BASE;
static uint32_t g_kernel_esp;
static void* g_kernel_cont;
static int g_exit_code;

void process_save_kernel_context(uint32_t esp, void* cont) {
    g_kernel_esp = esp;
    g_kernel_cont = cont;
}

int process_last_exit_code(void) { return g_exit_code; }

void process_exit_to_kernel(int code) {
    g_exit_code = code;
    uint32_t esp = g_kernel_esp;
    void* cont = g_kernel_cont;
    __asm__ volatile("mov %0, %%esp\njmp *%1\n" : : "r"(esp), "r"(cont) : "memory");
    for (;;) __asm__ volatile ("hlt");
}

void user_heap_reset(void) { user_brk = USER_HEAP_BASE; }

void fd_table_reset(void) {
    for (int i = 0; i < FD_MAX; i++) {
        fds[i].used = 0;
        fds[i].type = FD_NONE;
        fds[i].data = 0;
        fds[i].len = 0;
        fds[i].pos = 0;
        fds[i].writable = 0;
    }
    for (int i = 0; i <= 2; i++) {
        fds[i].used = 1;
        fds[i].type = FD_CONSOLE;
        fds[i].writable = (i != 0);
    }
}

static int fd_alloc(void) {
    for (int i = 3; i < FD_MAX; i++)
        if (!fds[i].used) return i;
    return -1;
}

static int sys_write(int fd, const char* buf, int count) {
    if (count < 0 || !buf || fd < 0 || fd >= FD_MAX || !fds[fd].used) return -1;
    if (fds[fd].type == FD_CONSOLE) {
        for (int i = 0; i < count; i++) terminal_putchar(buf[i]);
        return count;
    }
    return -1;
}

static int sys_read(int fd, char* buf, int count) {
    if (count < 0 || !buf || fd < 0 || fd >= FD_MAX || !fds[fd].used) return -1;
    if (fds[fd].type == FD_CONSOLE) return 0;
    if (fds[fd].type == FD_RAMFILE) {
        size_t left = fds[fd].len - fds[fd].pos;
        if ((size_t)count > left) count = (int)left;
        for (int i = 0; i < count; i++)
            buf[i] = fds[fd].data[fds[fd].pos + (size_t)i];
        fds[fd].pos += (size_t)count;
        return count;
    }
    return -1;
}

static int sys_open(const char* path, int flags) {
    (void)flags;
    if (!path || !path[0]) return -1;
    const char* content; size_t len;
    if (fs_read(path, &content, &len) != FS_OK) return -1;
    int fd = fd_alloc();
    if (fd < 0) return -1;
    fds[fd].used = 1;
    fds[fd].type = FD_RAMFILE;
    fds[fd].data = content;
    fds[fd].len = len;
    fds[fd].pos = 0;
    return fd;
}

static int sys_close(int fd) {
    if (fd < 3 || fd >= FD_MAX || !fds[fd].used) return -1;
    fds[fd].used = 0;
    fds[fd].type = FD_NONE;
    return 0;
}

static uint32_t sys_brk(uint32_t new_brk) {
    if (new_brk == 0) return user_brk;
    if (new_brk < USER_HEAP_BASE || new_brk > USER_HEAP_MAX) return user_brk;
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
        case SYS_OPEN:
            ret = sys_open((const char*)regs->ebx, (int)regs->ecx);
            break;
        case SYS_CLOSE:
            ret = sys_close((int)regs->ebx);
            break;
        case SYS_GETPID:
            ret = sched_current() ? sched_current()->id : 0;
            break;
        case SYS_BRK:
            ret = (int)sys_brk(regs->ebx);
            break;
        case SYS_YIELD:
            sched_yield();
            ret = 0;
            break;
        default:
            ret = -1;
            break;
    }
    regs->eax = (uint32_t)ret;
}
