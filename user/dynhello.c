/* user/dynhello.c — программа, линкуемая с libhello.so */

int lib_add(int a, int b);
void lib_greet(void);

static int sys_write(int fd, const char* buf, int n) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(4), "b"(fd), "c"(buf), "d"(n) : "memory");
    return ret;
}

static void putstr(const char* s) {
    int n = 0;
    while (s[n]) n++;
    sys_write(1, s, n);
}

static void putint(int v) {
    char buf[16];
    int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) buf[i++] = '0';
    while (v > 0 && i < 15) { buf[i++] = '0' + (v % 10); v /= 10; }
    if (neg) buf[i++] = '-';
    while (i--) {
        char c = buf[i];
        sys_write(1, &c, 1);
    }
}

void _start(void) {
    putstr("dynhello: start\n");
    lib_greet();
    int s = lib_add(40, 2);
    putstr("dynhello: 40+2=");
    putint(s);
    putstr("\n");

    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(1), "b"(0));
    for (;;);
}
