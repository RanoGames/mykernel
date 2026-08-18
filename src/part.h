#ifndef PART_H
#define PART_H
#include <stdint.h>

int part_create_mbr_fat32(uint32_t start_lba, uint32_t size_sectors);
/* GPT + protective MBR; one Microsoft basic data partition for FAT32 */
int part_create_gpt_fat32(uint32_t start_lba, uint32_t size_sectors);
void part_print_mbr(void);
void part_print_gpt(void);

#endif
