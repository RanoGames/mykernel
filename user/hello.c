/* user/hello.c — printf + malloc demo */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void _start(void) {
    printf("Hello, World!\n");

    char* s = (char*)malloc(32);
    if (!s) {
        printf("malloc failed\n");
        _exit(1);
    }
    strcpy(s, "heap works!");
    printf("malloc: %s\n", s);
    printf("ptr=%p\n", (void*)s);
    free(s);

    _exit(0);
}
