/* syscall.c — Linux i386-like int 0x80
 *
 * Реальные: read/write/open/close/lseek/dup/dup2, brk, getpid/uid/gid,
 *           chdir/mkdir/unlink/rename/access/getcwd, time, yield, exit
 * Заглушки (-ENOSYS): fork, execve, pipe, mmap, signals, …
 */

#include "syscall.h"
#include "errno.h"
#include "vga.h"
#include "fs.h"
#include "sched.h"
#include "timer.h"
#include <stdint.h>
#include <stddef.h>

#define USER_HEAP_BASE 0x500000u
#define USER_HEAP_MAX  0x600000u
#define FD_MAX 32

struct kernel_stat {
    uint32_t st_dev;
    uint16_t __pad1;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    uint16_t __pad2;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
};

#define S_IFREG 0100000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IRGRP 040
#define S_IROTH 04

enum fd_type { FD_NONE = 0, FD_CONSOLE, FD_RAMFILE };

struct fd_entry {
    enum fd_type type;
    int used;
    const char* data;
    size_t len;
    size_t pos;
    int writable;
    char name[48];
};

static struct fd_entry fds[FD_MAX];
static uint32_t user_brk = USER_HEAP_BASE;
static uint32_t g_kernel_esp;
static void* g_kernel_cont;
static int g_exit_code;
static uint32_t g_boot_epoch = 1700000000u;

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
        fds[i].name[0] = '\0';
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
    return -EMFILE;
}

static void name_copy(char* d, const char* s, size_t n) {
    size_t i = 0;
    while (s && s[i] && i + 1 < n) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int sys_write(int fd, const char* buf, int count) {
    if (count < 0) return -EINVAL;
    if (!buf) return -EFAULT;
    if (fd < 0 || fd >= FD_MAX || !fds[fd].used) return -EBADF;
    if (fds[fd].type == FD_CONSOLE) {
        for (int i = 0; i < count; i++) terminal_putchar(buf[i]);
        return count;
    }
    return -EBADF;
}

static int sys_read(int fd, char* buf, int count) {
    if (count < 0) return -EINVAL;
    if (!buf) return -EFAULT;
    if (fd < 0 || fd >= FD_MAX || !fds[fd].used) return -EBADF;
    if (fds[fd].type == FD_CONSOLE) return 0;
    if (fds[fd].type == FD_RAMFILE) {
        size_t left = fds[fd].len - fds[fd].pos;
        if ((size_t)count > left) count = (int)left;
        for (int i = 0; i < count; i++)
            buf[i] = fds[fd].data[fds[fd].pos + (size_t)i];
        fds[fd].pos += (size_t)count;
        return count;
    }
    return -EBADF;
}

static int sys_open(const char* path, int flags) {
    if (!path || !path[0]) return -ENOENT;
    const char* content = 0;
    size_t len = 0;
    enum fs_result r = fs_read(path, &content, &len);
    if (r != FS_OK) {
        if (flags & O_CREAT) {
            if (fs_touch(path) != FS_OK && fs_write(path, "") != FS_OK)
                return -ENOENT;
            r = fs_read(path, &content, &len);
            if (r != FS_OK) return -ENOENT;
        } else return -ENOENT;
    }
    int fd = fd_alloc();
    if (fd < 0) return fd;
    fds[fd].used = 1;
    fds[fd].type = FD_RAMFILE;
    fds[fd].data = content;
    fds[fd].len = len;
    fds[fd].pos = (flags & O_APPEND) ? len : 0;
    fds[fd].writable = (flags & (O_WRONLY | O_RDWR)) ? 1 : 0;
    name_copy(fds[fd].name, path, sizeof(fds[fd].name));
    return fd;
}

static int sys_close(int fd) {
    if (fd < 3 || fd >= FD_MAX || !fds[fd].used) return -EBADF;
    fds[fd].used = 0;
    fds[fd].type = FD_NONE;
    return 0;
}

static int sys_lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= FD_MAX || !fds[fd].used) return -EBADF;
    if (fds[fd].type != FD_RAMFILE) return -ESPIPE;
    int base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int)fds[fd].pos;
    else if (whence == SEEK_END) base = (int)fds[fd].len;
    else return -EINVAL;
    int neu = base + offset;
    if (neu < 0) return -EINVAL;
    if ((size_t)neu > fds[fd].len) neu = (int)fds[fd].len;
    fds[fd].pos = (size_t)neu;
    return neu;
}

static int sys_dup(int fd) {
    if (fd < 0 || fd >= FD_MAX || !fds[fd].used) return -EBADF;
    int n = fd_alloc();
    if (n < 0) return n;
    fds[n] = fds[fd];
    fds[n].used = 1;
    return n;
}

static int sys_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= FD_MAX || !fds[oldfd].used) return -EBADF;
    if (newfd < 0 || newfd >= FD_MAX) return -EBADF;
    if (oldfd == newfd) return newfd;
    if (fds[newfd].used && newfd >= 3) fds[newfd].used = 0;
    fds[newfd] = fds[oldfd];
    fds[newfd].used = 1;
    return newfd;
}

static uint32_t sys_brk(uint32_t new_brk) {
    if (new_brk == 0) return user_brk;
    if (new_brk < USER_HEAP_BASE || new_brk > USER_HEAP_MAX) return user_brk;
    user_brk = new_brk;
    return user_brk;
}

static int sys_chdir(const char* path) {
    if (!path) return -EFAULT;
    enum fs_result r = fs_cd(path);
    if (r == FS_OK) return 0;
    if (r == FS_ERR_NOT_FOUND) return -ENOENT;
    if (r == FS_ERR_NOT_A_DIRECTORY) return -ENOTDIR;
    return -EINVAL;
}

static int sys_mkdir(const char* path, int mode) {
    (void)mode;
    if (!path) return -EFAULT;
    enum fs_result r = fs_mkdir(path);
    if (r == FS_OK) return 0;
    if (r == FS_ERR_ALREADY_EXISTS) return -EEXIST;
    if (r == FS_ERR_NO_SPACE) return -ENOSPC;
    return -EINVAL;
}

static int sys_unlink(const char* path) {
    if (!path) return -EFAULT;
    enum fs_result r = fs_rm(path);
    if (r == FS_OK) return 0;
    if (r == FS_ERR_NOT_FOUND) return -ENOENT;
    if (r == FS_ERR_DIR_NOT_EMPTY) return -ENOTEMPTY;
    return -EINVAL;
}

static int sys_rename(const char* oldp, const char* newp) {
    if (!oldp || !newp) return -EFAULT;
    enum fs_result r = fs_mv(oldp, newp);
    if (r == FS_OK) return 0;
    if (r == FS_ERR_NOT_FOUND) return -ENOENT;
    return -EINVAL;
}

static int sys_access(const char* path, int mode) {
    (void)mode;
    if (!path) return -EFAULT;
    const char* c; size_t n;
    if (fs_read(path, &c, &n) == FS_OK) return 0;
    return -ENOENT;
}

static int sys_getcwd(char* buf, size_t size) {
    if (!buf || size == 0) return -EINVAL;
    char tmp[128];
    fs_pwd(tmp, sizeof(tmp));
    size_t i = 0;
    while (tmp[i] && i + 1 < size) { buf[i] = tmp[i]; i++; }
    buf[i] = '\0';
    if (tmp[i]) return -ERANGE;
    return (int)(i + 1);
}

static void fill_stat_file(struct kernel_stat* st, size_t size) {
    for (size_t i = 0; i < sizeof(*st); i++) ((uint8_t*)st)[i] = 0;
    st->st_mode = (uint16_t)(S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    st->st_nlink = 1;
    st->st_size = (uint32_t)size;
    st->st_blksize = 512;
    st->st_blocks = (uint32_t)((size + 511) / 512);
}

static int sys_stat(const char* path, struct kernel_stat* st) {
    if (!path || !st) return -EFAULT;
    const char* c; size_t n;
    if (fs_read(path, &c, &n) != FS_OK) return -ENOENT;
    fill_stat_file(st, n);
    return 0;
}

static int sys_fstat(int fd, struct kernel_stat* st) {
    if (!st) return -EFAULT;
    if (fd < 0 || fd >= FD_MAX || !fds[fd].used) return -EBADF;
    if (fds[fd].type == FD_CONSOLE) {
        fill_stat_file(st, 0);
        return 0;
    }
    if (fds[fd].type == FD_RAMFILE) {
        fill_stat_file(st, fds[fd].len);
        return 0;
    }
    return -EBADF;
}

static int sys_time(uint32_t* tloc) {
    uint32_t t = g_boot_epoch + timer_ticks() / 100;
    if (tloc) *tloc = t;
    return (int)t;
}

void syscall_handler(struct registers* regs) {
    uint32_t num = regs->eax;
    int ret = -ENOSYS;

    switch (num) {
        case SYS_EXIT:
        case SYS_EXIT_GROUP:
            process_exit_to_kernel((int)regs->ebx);
            break;
        case SYS_READ:
            ret = sys_read((int)regs->ebx, (char*)regs->ecx, (int)regs->edx);
            break;
        case SYS_WRITE:
            ret = sys_write((int)regs->ebx, (const char*)regs->ecx, (int)regs->edx);
            break;
        case SYS_OPEN:
            ret = sys_open((const char*)regs->ebx, (int)regs->ecx);
            break;
        case SYS_CLOSE:
            ret = sys_close((int)regs->ebx);
            break;
        case SYS_CREAT:
            ret = sys_open((const char*)regs->ebx, O_CREAT | O_WRONLY | O_TRUNC);
            break;
        case SYS_LSEEK:
            ret = sys_lseek((int)regs->ebx, (int)regs->ecx, (int)regs->edx);
            break;
        case SYS_DUP:
            ret = sys_dup((int)regs->ebx);
            break;
        case SYS_DUP2:
            ret = sys_dup2((int)regs->ebx, (int)regs->ecx);
            break;
        case SYS_BRK:
            ret = (int)sys_brk(regs->ebx);
            break;
        case SYS_GETPID:
        case SYS_GETTID:
            ret = sched_current() ? sched_current()->id : 1;
            break;
        case SYS_GETPPID:
            ret = 0;
            break;
        case SYS_GETUID:
        case SYS_GETEUID:
        case SYS_GETGID:
        case SYS_GETEGID:
            ret = 0;
            break;
        case SYS_CHDIR:
            ret = sys_chdir((const char*)regs->ebx);
            break;
        case SYS_MKDIR:
            ret = sys_mkdir((const char*)regs->ebx, (int)regs->ecx);
            break;
        case SYS_RMDIR:
        case SYS_UNLINK:
            ret = sys_unlink((const char*)regs->ebx);
            break;
        case SYS_RENAME:
            ret = sys_rename((const char*)regs->ebx, (const char*)regs->ecx);
            break;
        case SYS_ACCESS:
            ret = sys_access((const char*)regs->ebx, (int)regs->ecx);
            break;
        case SYS_GETCWD:
            ret = sys_getcwd((char*)regs->ebx, (size_t)regs->ecx);
            break;
        case SYS_STAT:
        case SYS_LSTAT:
            ret = sys_stat((const char*)regs->ebx, (struct kernel_stat*)regs->ecx);
            break;
        case SYS_FSTAT:
            ret = sys_fstat((int)regs->ebx, (struct kernel_stat*)regs->ecx);
            break;
        case SYS_TIME:
            ret = sys_time((uint32_t*)regs->ebx);
            break;
        case SYS_CLOCK_GETTIME:
            if (regs->ecx) {
                uint32_t* ts = (uint32_t*)regs->ecx;
                ts[0] = g_boot_epoch + timer_ticks() / 100;
                ts[1] = (timer_ticks() % 100) * 10000000u;
                ret = 0;
            } else ret = -EFAULT;
            break;
        case SYS_YIELD:
            sched_yield();
            ret = 0;
            break;
        case SYS_NANOSLEEP:
            sched_yield();
            ret = 0;
            break;
        case SYS_IOCTL:
        case SYS_FCNTL:
        case SYS_CHMOD:
            ret = 0;
            break;
        case SYS_SETSID:
            ret = sched_current() ? sched_current()->id : 1;
            break;
        case SYS_FORK:
        case SYS_WAITPID:
        case SYS_EXECVE:
        case SYS_PIPE:
        case SYS_LINK:
        case SYS_SYMLINK:
        case SYS_READLINK:
        case SYS_MMAP:
        case SYS_MUNMAP:
        case SYS_KILL:
        case SYS_SIGACTION:
        case SYS_SELECT:
        case SYS_GETDENTS:
        case SYS_REBOOT:
            ret = -ENOSYS;
            break;
        default:
            ret = -ENOSYS;
            break;
    }
    regs->eax = (uint32_t)ret;
}
