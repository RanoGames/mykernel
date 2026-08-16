/* user/hello.c — пример с минимальной libc (printf) */

#include <stdio.h>
#include <unistd.h>

void _start(void) {
    printf("Hello, World!\n");
    printf("2 + 2 = %d\n", 2 + 2);
    printf("hex: 0x%x\n", 0xCAFE);
    _exit(0);
}
