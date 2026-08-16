/* user/hello.c — пример программы для MyKernel.
 *
 * Собирается отдельно (см. Makefile), линкуется по адресу 0x400000,
 * загружается командой: exec hello
 *
 * Системные вызовы (как Linux i386):
 *   eax = номер, ebx/ecx/edx = аргументы, int $0x80, результат в eax
 */

#define SYS_EXIT  1
#define SYS_WRITE 4

static int syscall3(int n, int a, int b, int c) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static void sys_write(const char* s, int len) {
    syscall3(SYS_WRITE, 1, (int)s, len);
}

static void sys_exit(int code) {
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;);
}

void _start(void) {
    const char msg[] = "Hello from ELF userspace!\n";
    sys_write(msg, sizeof(msg) - 1);
    sys_exit(0);
}
