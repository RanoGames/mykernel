#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char* s) {
    write(STDOUT_FILENO, s, strlen(s));
    putchar('\n');
    return 0;
}

static void print_uint(unsigned long v, int base) {
    char buf[32];
    const char* digits = "0123456789abcdef";
    int i = 0;
    if (v == 0) {
        putchar('0');
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = digits[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (i--) putchar(buf[i]);
}

static void print_int(long v) {
    if (v < 0) {
        putchar('-');
        print_uint((unsigned long)(-(v + 1)) + 1, 10);
    } else {
        print_uint((unsigned long)v, 10);
    }
}

int vprintf(const char* fmt, va_list ap) {
    int written = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            putchar(*fmt);
            written++;
            continue;
        }
        fmt++;
        if (!*fmt) break;
        switch (*fmt) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                write(STDOUT_FILENO, s, strlen(s));
                written += (int)strlen(s);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                putchar(c);
                written++;
                break;
            }
            case 'd':
            case 'i': {
                print_int(va_arg(ap, int));
                written++; /* приближённо */
                break;
            }
            case 'u': {
                print_uint(va_arg(ap, unsigned int), 10);
                written++;
                break;
            }
            case 'x': {
                print_uint(va_arg(ap, unsigned int), 16);
                written++;
                break;
            }
            case 'p': {
                putchar('0');
                putchar('x');
                print_uint((unsigned long)va_arg(ap, void*), 16);
                written++;
                break;
            }
            case '%': {
                putchar('%');
                written++;
                break;
            }
            default:
                putchar('%');
                putchar(*fmt);
                written += 2;
                break;
        }
    }
    return written;
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}
