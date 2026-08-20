#include "part.h"
#include "ata.h"
#include "vga.h"

static uint8_t sec[512];

static void set32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v); p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void set64(uint8_t* p, uint32_t lo, uint32_t hi) {
    set32(p, lo); set32(p + 4, hi);
}
static void mem0(void* d, int n) {
    uint8_t* p = d; while (n--) *p++ = 0;
}

/* CRC32 for GPT (poly 0xEDB88320) */
static uint32_t crc32(const uint8_t* data, uint32_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (-(int)(c & 1)));
    }
    return ~c;
}

int part_create_mbr_fat32(uint32_t start_lba, uint32_t size_sectors) {
    if (!ata_present()) return -1;
    if (start_lba < 1) start_lba = 2048;
    if (size_sectors == 0) size_sectors = 262144;

    /* Preserve existing MBR bootstrap (bytes 0..445) if present */
    if (ata_read_sectors(0, 1, sec) != 0)
        mem0(sec, 512);
    int has_code = 0;
    for (int i = 0; i < 3; i++)
        if (sec[i] != 0) has_code = 1;
    if (!has_code) {
        /* No bootloader: minimal halt stub (install may overlay real MBR later) */
        mem0(sec, 512);
        sec[0] = 0xFA; /* cli */
        sec[1] = 0xF4; /* hlt */
        sec[2] = 0xEB; sec[3] = 0xFD; /* jmp $ */
    }
    /* Only rewrite partition table + signature */
    for (int i = 446; i < 510; i++) sec[i] = 0;
    uint8_t* e = &sec[446];
    e[0] = 0x80; /* bootable */
    e[4] = 0x0C;
    set32(&e[8], start_lba);
    set32(&e[12], size_sectors);
    sec[510] = 0x55; sec[511] = 0xAA;
    return ata_write_sectors(0, 1, sec);
}

int part_create_gpt_fat32(uint32_t start_lba, uint32_t size_sectors) {
    if (!ata_present()) return -1;
    if (start_lba < 34) start_lba = 2048;
    if (size_sectors == 0) size_sectors = 262144;
    uint32_t last = start_lba + size_sectors - 1;

    /* Protective MBR */
    mem0(sec, 512);
    uint8_t* e = &sec[446];
    e[0] = 0x00;
    e[4] = 0xEE; /* GPT protective */
    set32(&e[8], 1);
    set32(&e[12], 0xFFFFFFFF);
    sec[510] = 0x55; sec[511] = 0xAA;
    if (ata_write_sectors(0, 1, sec) != 0) return -1;

    /* Partition entry array at LBA 2 — one entry, 128 bytes, type Microsoft Basic Data */
    mem0(sec, 512);
    /* type GUID: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 little-endian */
    uint8_t type_guid[16] = {
        0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44,
        0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7
    };
    for (int i = 0; i < 16; i++) sec[i] = type_guid[i];
    /* unique GUID — fixed for hobby */
    uint8_t uniq[16] = {
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x01
    };
    for (int i = 0; i < 16; i++) sec[16 + i] = uniq[i];
    set64(&sec[32], start_lba, 0);
    set64(&sec[40], last, 0);
    /* name UTF-16LE "MYKERNEL" */
    const char* nm = "MYKERNEL";
    for (int i = 0; nm[i]; i++) {
        sec[56 + i * 2] = (uint8_t)nm[i];
        sec[56 + i * 2 + 1] = 0;
    }
    uint32_t part_crc = crc32(sec, 128); /* one entry — GPT often uses 128*128 array */
    /* write full 128 entries zero except first — for CRC use 16384 bytes; we only write 1 sector of entries for simplicity */
    if (ata_write_sectors(2, 1, sec) != 0) return -1;
    /* zero remaining entry sectors LBA 3-33 optional skip */

    /* Primary GPT header LBA 1 */
    mem0(sec, 512);
    sec[0]='E'; sec[1]='F'; sec[2]='I'; sec[3]=' ';
    sec[4]='P'; sec[5]='A'; sec[6]='R'; sec[7]='T';
    set32(&sec[8], 0x00010000); /* revision */
    set32(&sec[12], 92);        /* header size */
    set32(&sec[16], 0);         /* CRC placeholder */
    set64(&sec[24], 1, 0);      /* current LBA */
    set64(&sec[32], size_sectors + start_lba + 100, 0); /* backup LBA approx */
    set64(&sec[40], 34, 0);     /* first usable */
    set64(&sec[48], last, 0);   /* last usable — simplified */
    /* disk GUID */
    for (int i = 0; i < 16; i++) sec[56 + i] = (uint8_t)(0xA0 + i);
    set64(&sec[72], 2, 0);      /* partition entries LBA */
    set32(&sec[80], 128);       /* num entries */
    set32(&sec[84], 128);       /* size of entry */
    set32(&sec[88], part_crc);

    uint32_t hdr_crc = crc32(sec, 92);
    set32(&sec[16], hdr_crc);
    if (ata_write_sectors(1, 1, sec) != 0) return -1;

    return 0;
}

void part_print_mbr(void) {
    if (!ata_present()) { terminal_writestring("No ATA disk\n"); return; }
    if (ata_read_sectors(0, 1, sec) != 0) { terminal_writestring("MBR read fail\n"); return; }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        terminal_writestring("No MBR signature\n"); return;
    }
    terminal_writestring("MBR partitions:\n");
    for (int i = 0; i < 4; i++) {
        uint8_t* e = &sec[446 + i * 16];
        uint8_t type = e[4];
        if (!type) continue;
        uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9]<<8) | ((uint32_t)e[10]<<16) | ((uint32_t)e[11]<<24);
        uint32_t sz = (uint32_t)e[12] | ((uint32_t)e[13]<<8) | ((uint32_t)e[14]<<16) | ((uint32_t)e[15]<<24);
        terminal_writestring("  #"); terminal_write_uint((uint32_t)i);
        terminal_writestring(" type="); terminal_write_uint(type);
        terminal_writestring(" LBA="); terminal_write_uint(lba);
        terminal_writestring(" size="); terminal_write_uint(sz);
        terminal_putchar('\n');
    }
}

void part_print_gpt(void) {
    if (!ata_present()) { terminal_writestring("No ATA disk\n"); return; }
    if (ata_read_sectors(1, 1, sec) != 0) { terminal_writestring("GPT read fail\n"); return; }
    if (sec[0]!='E' || sec[1]!='F' || sec[2]!='I') {
        terminal_writestring("No GPT header at LBA1\n");
        return;
    }
    terminal_writestring("GPT header OK (EFI PART)\n");
    uint32_t pe_lba = (uint32_t)sec[72] | ((uint32_t)sec[73]<<8) |
                      ((uint32_t)sec[74]<<16) | ((uint32_t)sec[75]<<24);
    if (ata_read_sectors(pe_lba, 1, sec) != 0) return;
    uint32_t start = (uint32_t)sec[32] | ((uint32_t)sec[33]<<8) |
                     ((uint32_t)sec[34]<<16) | ((uint32_t)sec[35]<<24);
    uint32_t end = (uint32_t)sec[40] | ((uint32_t)sec[41]<<8) |
                   ((uint32_t)sec[42]<<16) | ((uint32_t)sec[43]<<24);
    terminal_writestring("  Part0 start="); terminal_write_uint(start);
    terminal_writestring(" end="); terminal_write_uint(end);
    terminal_putchar('\n');
}
