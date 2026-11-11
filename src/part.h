#ifndef PART_H
#define PART_H
#include <stdint.h>

struct disk_info {
    int present;
    uint32_t total_sectors;
    uint32_t size_mb;
    int has_mbr;
    int has_gpt;
    int is_hybrid;
};

/* Detect and probe ATA disk presence, capacity, and existing partition scheme */
int part_detect_disk(struct disk_info* info);

/* Standard MBR partition table with active FAT32 partition */
int part_create_mbr_fat32(uint32_t start_lba, uint32_t size_sectors);

/* Standard GPT + protective MBR + backup GPT */
int part_create_gpt_fat32(uint32_t start_lba, uint32_t size_sectors);

/* Hybrid MBR + GPT layout (BIOS bootable MBR + full GPT primary and backup tables) */
int part_create_hybrid_fat32(uint32_t start_lba, uint32_t size_sectors);

/* Human-readable partition inspection */
void part_print_mbr(void);
void part_print_gpt(void);

#endif
