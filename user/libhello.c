/* user/libhello.c — минимальная shared library для dyld-демо */

#include <stdint.h>

static int sys_write(int fd, const char* buf, int n) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(4), "b"(fd), "c"(buf), "d"(n) : "memory");
    return ret;
}

/* экспортируемые символы */
int lib_add(int a, int b) {
    return a + b;
}

void lib_greet(void) {
    const char* msg = "libhello.so: greet()\n";
    int n = 0;
    while (msg[n]) n++;
    sys_write(1, msg, n);
}
