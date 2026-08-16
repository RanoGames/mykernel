/* fat32.h — read-only FAT32 на ATA-диске.
 *
 * Поддержка: mount, ls корня/подкаталогов, cat файла (8.3 имена).
 * LFN (длинные имена) пропускаются — видны только 8.3.
 */

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
};

int fat32_mount(void);
int fat32_is_mounted(void);
void fat32_info(void);

/* path: "" или "/" = корень; "DIR" или "DIR/FILE.TXT" (8.3) */
enum fat_result fat32_ls(const char* path);
enum fat_result fat32_cat(const char* path);

const char* fat_strerror(enum fat_result r);

#endif
