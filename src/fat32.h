/* fat32.h — FAT32 R/W, mkdir, unlink, mkfs */
#ifndef FAT32_H
#define FAT32_H

#include <stddef.h>
#include <stdint.h>

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
    FAT_ERR_NOT_EMPTY,
};

int fat32_mount(void);
int fat32_is_mounted(void);
void fat32_info(void);

enum fat_result fat32_ls(const char* path);
enum fat_result fat32_cat(const char* path);
enum fat_result fat32_write(const char* name, const char* text);
enum fat_result fat32_read_file(const char* name, char* buf, size_t buf_size, size_t* out_len);
enum fat_result fat32_mkdir(const char* name);
enum fat_result fat32_unlink(const char* name);

/* Format partition at LBA with FAT32 (size_sectors = 0 → auto from geometry guess) */
enum fat_result fat32_mkfs(uint32_t part_lba, uint32_t size_sectors);

/* After host/part setup: set LBA and mount */
void fat32_set_partition_lba(uint32_t lba);

const char* fat_strerror(enum fat_result r);

#endif
