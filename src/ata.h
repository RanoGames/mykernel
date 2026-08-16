/* ata.h — простой драйвер ATA (IDE) в режиме PIO, LBA28.
 *
 * Читает сектора с первого диска (bus 0, master).
 * Для QEMU: -drive file=fat.img,format=raw,if=ide
 */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE 512

/* 0 = ок, -1 = ошибка / нет диска */
int ata_init(void);

/* Прочитать count секторов, начиная с lba, в buf (count*512 байт). */
int ata_read_sectors(uint32_t lba, uint8_t count, void* buf);

int ata_present(void);

#endif
