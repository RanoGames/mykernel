/* fat32.h — FAT32 read/write (8.3 names) на ATA-диске */

#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>

enum fat_result {
    FAT_OK = 0,
    FAT_ERR_NO_DISK,
    FAT_ERR_NOT_FAT32,
    FAT_ERR_IO,
    FAT_ERR_NOT_FOUND,
    FAT_ERR_IS_DIR,
    FAT_ERR_NOT_DIR,
    FAT_ERR_TOO_BIG,
    FAT_ERR_NOT_MOUNTED,
    FAT_ERR_NO_SPACE,
    FAT_ERR_EXISTS,
};

int fat32_mount(void);
int fat32_is_mounted(void);
void fat32_info(void);

enum fat_result fat32_ls(const char* path);
enum fat_result fat32_cat(const char* path);

/* Создать/перезаписать файл в корне (имя 8.3, например NOTE.TXT) */
enum fat_result fat32_write(const char* name, const char* text);

/* Прочитать файл в буфер. *out_len = байт прочитано */
enum fat_result fat32_read_file(const char* name, char* buf, size_t buf_size, size_t* out_len);

const char* fat_strerror(enum fat_result r);

#endif
