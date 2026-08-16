/* elf.h — минимальный загрузчик ELF32 (ET_EXEC, i386). */

#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>

#define ELF_LOAD_BASE 0x400000u
#define ELF_LOAD_MAX  0x4000000u /* до 64 МБ (0x400000..0x4400000) */
#define ELF_LINUX_BASE 0x08048000u
#define ELF_LINUX_MAX  0x00800000u /* 8 МБ от linux base */

enum elf_result {
    ELF_OK = 0,
    ELF_ERR_BAD_MAGIC,
    ELF_ERR_NOT_32,
    ELF_ERR_NOT_EXEC,
    ELF_ERR_BAD_ARCH,
    ELF_ERR_NO_LOAD,
    ELF_ERR_TOO_BIG,
};

/* Загружает ELF из буфера image (size байт) в физическую память
 * по адресам p_vaddr из заголовков. entry — виртуальный адрес точки входа. */
enum elf_result elf_load(const uint8_t* image, size_t size, uint32_t* entry);

const char* elf_strerror(enum elf_result r);

#endif
