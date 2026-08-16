/* string.h — минимальная libc для userspace */
#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

size_t strlen(const char* s);
void*  memcpy(void* dest, const void* src, size_t n);
void*  memset(void* s, int c, size_t n);
int    strcmp(const char* a, const char* b);
char*  strcpy(char* dest, const char* src);

#endif
