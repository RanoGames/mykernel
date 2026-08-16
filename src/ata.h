/* ata.h — ATA PIO LBA28 read/write */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE 512

int ata_init(void);
int ata_present(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void* buf);
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buf);

#endif
