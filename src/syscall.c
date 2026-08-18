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
#include "power.h"
#include "kmalloc.h"
#include <stdint.h>
#include <stddef.h>

#define USER_HEAP_BASE 0x500000u
#define USER_HEAP_MAX  0x600000u
/* Anonymous mmap arena for glibc (separate from brk) */
#define MMAP_BASE      0x01000000u
#define MMAP_END       0x02000000u /* 16 MiB */
#define PAGE_SIZE      4096u
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
static uint32_t mmap_bump = MMAP_BASE;
static uint32_t g_umask = 0022;
static uint32_t tls_base = 0;
static uint32_t tls_entry = 6;
static uint32_t g_boot_epoch = 1700000000u;

void user_heap_reset(void) {
    user_brk = USER_HEAP_BASE;
    mmap_bump = MMAP_BASE;
}

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


/* ---- extra Linux-ish syscalls ---- */

struct mk_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

struct mk_timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};

struct mk_timezone {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
};

struct mk_sysinfo {
    int32_t uptime;
    uint32_t loads[3];
    uint32_t totalram;
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
    char _f[20-2*sizeof(uint32_t)-sizeof(uint32_t)];
};

struct mk_iovec {
    void* iov_base;
    uint32_t iov_len;
};

static void cpy_str(char* d, const char* s, size_t n) {
    size_t i = 0;
    while (s[i] && i + 1 < n) { d[i] = s[i]; i++; }
    d[i] = 0;
    while (i < n) d[i++] = 0;
}

static int sys_uname(struct mk_utsname* u) {
    if (!u) return -EFAULT;
    cpy_str(u->sysname, "MyKernel", 65);
    cpy_str(u->nodename, "mykernel", 65);
    cpy_str(u->release, "0.2.0", 65);
    cpy_str(u->version, "hobby", 65);
    cpy_str(u->machine, "i686", 65);
    return 0;
}

static int sys_gettimeofday(struct mk_timeval* tv, struct mk_timezone* tz) {
    (void)tz;
    if (!tv) return -EFAULT;
    uint32_t t = g_boot_epoch + timer_ticks() / 100;
    tv->tv_sec = (int32_t)t;
    tv->tv_usec = (int32_t)((timer_ticks() % 100) * 10000u);
    return 0;
}

static int sys_sysinfo(struct mk_sysinfo* info) {
    if (!info) return -EFAULT;
    size_t used = 0, total = 0, free_b = 0;
    kmalloc_stats(&used, &total, &free_b);
    info->uptime = (int32_t)(timer_ticks() / 100);
    info->loads[0] = info->loads[1] = info->loads[2] = 0;
    info->totalram = (uint32_t)total;
    info->freeram = (uint32_t)free_b;
    info->sharedram = 0;
    info->bufferram = 0;
    info->totalswap = 0;
    info->freeswap = 0;
    info->procs = 1;
    info->pad = 0;
    info->totalhigh = 0;
    info->freehigh = 0;
    info->mem_unit = 1;
    return 0;
}

static int sys_writev(int fd, const struct mk_iovec* iov, int iovcnt) {
    if (iovcnt < 0 || iovcnt > 64) return -EINVAL;
    if (!iov && iovcnt) return -EFAULT;
    int total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base && iov[i].iov_len) return -EFAULT;
        int n = sys_write(fd, (const char*)iov[i].iov_base, (int)iov[i].iov_len);
        if (n < 0) return n;
        total += n;
        if ((uint32_t)n < iov[i].iov_len) break;
    }
    return total;
}

static int sys_readv(int fd, const struct mk_iovec* iov, int iovcnt) {
    if (iovcnt < 0 || iovcnt > 64) return -EINVAL;
    if (!iov && iovcnt) return -EFAULT;
    int total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base && iov[i].iov_len) return -EFAULT;
        int n = sys_read(fd, (char*)iov[i].iov_base, (int)iov[i].iov_len);
        if (n < 0) return n;
        total += n;
        if ((uint32_t)n < iov[i].iov_len) break;
    }
    return total;
}



static uint32_t align_up_page(uint32_t n) {
    return (n + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

/* Linux i386 mmap2: offset is in pages */
static int sys_mmap2(uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags,
                     int fd, uint32_t pgoff) {
    (void)prot;
    (void)pgoff;
    if (len == 0) return -EINVAL;
    len = align_up_page(len);
    if (len > (MMAP_END - MMAP_BASE)) return -ENOMEM;

    if (flags & MAP_ANONYMOUS) {
        uint32_t base;
        if ((flags & MAP_FIXED) && addr) {
            if (addr < MMAP_BASE || addr + len > MMAP_END) return -EINVAL;
            base = addr;
        } else {
            if (mmap_bump + len > MMAP_END) return -ENOMEM;
            base = mmap_bump;
            mmap_bump += len;
        }
        uint8_t* p = (uint8_t*)base;
        for (uint32_t i = 0; i < len; i++) p[i] = 0;
        return (int)base;
    }

    /* file-backed: not really mapped — return ENOSYS for now unless fd is dummy */
    (void)fd;
    return -ENOSYS;
}

static int sys_munmap(uint32_t addr, uint32_t len) {
    (void)addr;
    (void)len;
    /* bump allocator: cannot free mid-arena; accept success for glibc */
    return 0;
}

static int sys_mprotect(uint32_t addr, uint32_t len, uint32_t prot) {
    (void)addr; (void)len; (void)prot;
    return 0; /* no paging yet */
}

static int sys_madvise(uint32_t addr, uint32_t len, int advice) {
    (void)addr; (void)len; (void)advice;
    return 0;
}

struct user_desc {
    uint32_t entry_number;
    uint32_t base_addr;
    uint32_t limit;
    uint32_t flags; /* bitfield packed by userspace; we only use base */
};

static int sys_set_thread_area(struct user_desc* u) {
    if (!u) return -EFAULT;
    if (u->entry_number == (uint32_t)-1)
        u->entry_number = tls_entry;
    tls_base = u->base_addr;
    /* Real kernel would load GDT TLS; we record base for future */
    return 0;
}

static int sys_get_thread_area(struct user_desc* u) {
    if (!u) return -EFAULT;
    u->base_addr = tls_base;
    u->entry_number = tls_entry;
    u->limit = 0xfffff;
    return 0;
}

static int sys_pipe(int* pipefd) {
    if (!pipefd) return -EFAULT;
    /* No real pipes yet */
    return -ENOSYS;
}

static int sys_futex(uint32_t* uaddr, int op, uint32_t val, void* timeout,
                     uint32_t* uaddr2, uint32_t val3) {
    (void)uaddr; (void)val; (void)timeout; (void)uaddr2; (void)val3;
    int cmd = op & 0x7f; /* strip private/clock flags */
    /* FUTEX_WAIT=0 FUTEX_WAKE=1 — pretend success so single-threaded glibc lives */
    if (cmd == 0) return 0;      /* WAIT: no contention */
    if (cmd == 1) return 0;      /* WAKE */
    return -ENOSYS;
}

static int sys_getrlimit(uint32_t resource, uint32_t* rlim) {
    (void)resource;
    if (!rlim) return -EFAULT;
    /* rlim_cur, rlim_max as two uint32 (older i386) or uint64 — write soft=RLIM */
    rlim[0] = 0xffffffffu;
    rlim[1] = 0xffffffffu;
    return 0;
}

static int sys_prctl(int option, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (option == 15 /* PR_GET_NAME */ || option == 4 /* PR_SET_NAME */)
        return 0;
    return -ENOSYS;
}

static int sys_clock_getres(int clk, uint32_t* ts) {
    (void)clk;
    if (!ts) return -EFAULT;
    ts[0] = 0;
    ts[1] = 10000000u; /* 10 ms in ns */
    return 0;
}

static int sys_getdents64(int fd, void* dirp, uint32_t count) {
    (void)fd; (void)dirp; (void)count;
    return -ENOSYS; /* until FS dirents wired */
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
        case 24: /* getuid */
        case 47: /* getgid */
        case 49: /* geteuid */
        case 50: /* getegid */
        case 199:
        case 200:
        case 201:
        case 202:
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
            /* old mmap: treat like mmap2 with offset bytes->pages if needed */
            ret = sys_mmap2(regs->ebx, regs->ecx, regs->edx, regs->esi, (int)regs->edi, regs->ebp / 4096u);
            break;
        case SYS_MMAP2:
            ret = sys_mmap2(regs->ebx, regs->ecx, regs->edx, regs->esi, (int)regs->edi, regs->ebp);
            break;
        case SYS_MUNMAP:
            ret = sys_munmap(regs->ebx, regs->ecx);
            break;
        case SYS_MPROTECT:
            ret = sys_mprotect(regs->ebx, regs->ecx, regs->edx);
            break;
        case SYS_MADVISE:
        case SYS_MREMAP:
            ret = sys_madvise(regs->ebx, regs->ecx, (int)regs->edx);
            break;
        case SYS_MSYNC:
        case SYS_MLOCK:
        case SYS_MUNLOCK:
        case SYS_MLOCKALL:
        case SYS_MUNLOCKALL:
            ret = 0;
            break;

        case SYS_UNAME:
            ret = sys_uname((struct mk_utsname*)regs->ebx);
            break;
        case SYS_GETTIMEOFDAY:
            ret = sys_gettimeofday((struct mk_timeval*)regs->ebx, (struct mk_timezone*)regs->ecx);
            break;
        case SYS_SYSINFO:
            ret = sys_sysinfo((struct mk_sysinfo*)regs->ebx);
            break;
        case SYS_WRITEV:
            ret = sys_writev((int)regs->ebx, (const struct mk_iovec*)regs->ecx, (int)regs->edx);
            break;
        case SYS_READV:
            ret = sys_readv((int)regs->ebx, (const struct mk_iovec*)regs->ecx, (int)regs->edx);
            break;
        case SYS_SYNC:
            ret = 0;
            break;
        case SYS_CLOCK_NANOSLEEP:
            /* treat as yield + short sleep */
            timer_busy_ms(10);
            ret = 0;
            break;

        case SYS_SET_THREAD_AREA:
            ret = sys_set_thread_area((struct user_desc*)regs->ebx);
            break;
        case SYS_GET_THREAD_AREA:
            ret = sys_get_thread_area((struct user_desc*)regs->ebx);
            break;
        case SYS_FUTEX:
            ret = sys_futex((uint32_t*)regs->ebx, (int)regs->ecx, regs->edx,
                            (void*)regs->esi, (uint32_t*)regs->edi, regs->ebp);
            break;
        case SYS_PIPE2:
            ret = sys_pipe((int*)regs->ebx);
            break;
        case SYS_GETRLIMIT:
        case SYS_UGETRLIMIT:
            ret = sys_getrlimit(regs->ebx, (uint32_t*)regs->ecx);
            break;
        case SYS_SETRLIMIT:
            ret = 0;
            break;
        case SYS_PRCTL:
            ret = sys_prctl((int)regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
            break;
        case SYS_CLOCK_GETRES:
            ret = sys_clock_getres((int)regs->ebx, (uint32_t*)regs->ecx);
            break;
        case SYS_GETDENTS64:
        case SYS_GETDENTS:
            ret = sys_getdents64((int)regs->ebx, (void*)regs->ecx, regs->edx);
            break;
        case SYS_RT_SIGPROCMASK:
        case SYS_SIGPROCMASK:
        case SYS_RT_SIGACTION:
        case SYS_SIGACTION:
            ret = 0; /* single-threaded: pretend OK */
            break;
        case SYS_UMASK:
            {
                uint32_t old = g_umask;
                if (regs->ebx != (uint32_t)-1)
                    g_umask = regs->ebx & 0777;
                ret = (int)old;
            }
            break;
        case SYS_PAUSE:
            ret = -EINTR;
            break;
        case SYS_ALARM:
            ret = 0;
            break;
        case SYS_TIMES:
            ret = (int)timer_ticks();
            break;
        case SYS_GETPGID:
        case SYS_GETSID:
            ret = 1;
            break;
        case SYS_TGKILL:
        case SYS_KILL:
            ret = -ENOSYS;
            break;
        case SYS_OPENAT:
            ret = sys_open((const char*)regs->ecx, (int)regs->edx);
            break;
        case SYS_POLL:
        case SYS_SELECT:
            ret = 0;
            break;

        case SYS_REBOOT:
            /* magic in ebx/ecx optional; edx = cmd */
            if (regs->edx == 0x4321FEDCu || regs->edx == 0xCDEF0123u)
                machine_shutdown();
            else
                machine_reboot();
            ret = 0;
            break;
        default:
            ret = -ENOSYS;
            break;
    }
    regs->eax = (uint32_t)ret;
}
