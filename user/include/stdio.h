/* stdio.h — минимальный printf для MyKernel userspace */
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

int putchar(int c);
int puts(const char* s);
int printf(const char* fmt, ...);
int vprintf(const char* fmt, va_list ap);

#endif
