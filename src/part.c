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
static uint32_t crc32_continue(uint32_t c, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (-(int)(c & 1)));
    }
    return c;
}

static uint32_t crc32(const uint8_t* data, uint32_t len) {
    return ~crc32_continue(0xFFFFFFFFu, data, len);
}

int part_detect_disk(struct disk_info* info) {
    if (!info) return -1;
    mem0(info, sizeof(*info));

    if (!ata_present()) {
        if (ata_init() != 0) return -1;
    }
    info->present = 1;

    /* Check MBR on LBA 0 */
    uint32_t mbr_max_extent = 0;
    if (ata_read_sectors(0, 1, sec) == 0) {
        if (sec[510] == 0x55 && sec[511] == 0xAA) {
            info->has_mbr = 1;
            int has_pmbr_type = 0;
            int has_fat_type = 0;
            for (int i = 0; i < 4; i++) {
                uint8_t* e = &sec[446 + i * 16];
                uint8_t type = e[4];
                if (!type) continue;
                uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                               ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
                uint32_t sz  = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                               ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
                if (type == 0xEE) has_pmbr_type = 1;
                if (type == 0x0C || type == 0x0B || type == 0x0E) has_fat_type = 1;
                if (lba + sz > mbr_max_extent) mbr_max_extent = lba + sz;
            }
            if (has_pmbr_type && has_fat_type) info->is_hybrid = 1;
        }
    }

    /* Check Primary GPT on LBA 1 */
    uint32_t gpt_backup_lba = 0;
    if (ata_read_sectors(1, 1, sec) == 0) {
        if (sec[0] == 'E' && sec[1] == 'F' && sec[2] == 'I' && sec[3] == ' ' &&
            sec[4] == 'P' && sec[5] == 'A' && sec[6] == 'R' && sec[7] == 'T') {
            info->has_gpt = 1;
            gpt_backup_lba = (uint32_t)sec[32] | ((uint32_t)sec[33] << 8) |
                             ((uint32_t)sec[34] << 16) | ((uint32_t)sec[35] << 24);
        }
    }

    if (gpt_backup_lba > 0) {
        info->total_sectors = gpt_backup_lba + 1;
    } else {
        /* Probe capacity using binary search via ata_read_sectors */
        uint32_t low = 0;
        if (ata_read_sectors(0, 1, sec) == 0) low = 1;
        uint32_t high = 4096;
        while (high < 0x0FFFFFFFu && ata_read_sectors(high - 1, 1, sec) == 0) {
            low = high;
            high *= 2;
        }
        while (low + 1 < high) {
            uint32_t mid = low + (high - low) / 2;
            if (ata_read_sectors(mid, 1, sec) == 0) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        info->total_sectors = low;
    }

    if (info->total_sectors < mbr_max_extent)
        info->total_sectors = mbr_max_extent;
    if (info->total_sectors == 0)
        info->total_sectors = 131072; /* fallback 64 MB */

    info->size_mb = info->total_sectors / 2048;
    if (info->total_sectors >= 63 * 255) {
        info->heads = 255;
        info->sectors_per_track = 63;
    } else {
        info->heads = 16;
        info->sectors_per_track = 63;
    }
    info->cylinders = info->total_sectors / (info->heads * info->sectors_per_track);
    if (info->cylinders == 0 && info->total_sectors > 0)
        info->cylinders = 1;
    return 0;
}

int part_create_mbr_fat32(uint32_t start_lba, uint32_t size_sectors) {
    if (!ata_present()) {
        if (ata_init() != 0) return -1;
    }
    if (start_lba < 1) start_lba = 2048;

    struct disk_info di;
    part_detect_disk(&di);
    if (size_sectors == 0) {
        if (di.total_sectors > start_lba)
            size_sectors = di.total_sectors - start_lba;
        else
            size_sectors = 131072 - start_lba;
    }

    /* Preserve existing MBR bootstrap (bytes 0..445) if present */
    if (ata_read_sectors(0, 1, sec) != 0)
        mem0(sec, 512);
    int has_code = 0;
    for (int i = 0; i < 3; i++)
        if (sec[i] != 0) has_code = 1;
    if (!has_code) {
        mem0(sec, 512);
        sec[0] = 0xFA; /* cli */
        sec[1] = 0xF4; /* hlt */
        sec[2] = 0xEB; sec[3] = 0xFD; /* jmp $ */
    }

    /* Rewrite partition table + signature */
    for (int i = 446; i < 510; i++) sec[i] = 0;
    uint8_t* e = &sec[446];
    e[0] = 0x80; /* bootable */
    e[1] = 0x01; e[2] = 0x01; e[3] = 0x00; /* CHS start */
    e[4] = 0x0C; /* FAT32 LBA */
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF; /* CHS end */
    set32(&e[8], start_lba);
    set32(&e[12], size_sectors);
    sec[510] = 0x55; sec[511] = 0xAA;
    return ata_write_sectors(0, 1, sec);
}

int part_create_gpt_fat32(uint32_t start_lba, uint32_t size_sectors) {
    if (!ata_present()) {
        if (ata_init() != 0) return -1;
    }
    if (start_lba < 34) start_lba = 2048;

    struct disk_info di;
    part_detect_disk(&di);
    uint32_t disk_sectors = di.total_sectors ? di.total_sectors : 131072;

    if (size_sectors == 0 || start_lba + size_sectors + 34 > disk_sectors) {
        if (disk_sectors > start_lba + 34)
            size_sectors = disk_sectors - start_lba - 34;
        else
            size_sectors = 65536;
    }
    uint32_t last_lba = start_lba + size_sectors - 1;
    uint32_t backup_lba = disk_sectors - 1;
    uint32_t last_usable = backup_lba - 33;

    /* 1. Protective MBR at LBA 0 */
    mem0(sec, 512);
    /* Preserve bootstrap code in bytes 0..445 if any */
    if (ata_read_sectors(0, 1, sec) != 0) mem0(sec, 512);
    for (int i = 446; i < 510; i++) sec[i] = 0;
    uint8_t* e = &sec[446];
    e[0] = 0x00;
    e[1] = 0x00; e[2] = 0x02; e[3] = 0x00;
    e[4] = 0xEE; /* GPT protective */
    e[5] = 0xFF; e[6] = 0xFF; e[7] = 0xFF;
    set32(&e[8], 1);
    set32(&e[12], backup_lba);
    sec[510] = 0x55; sec[511] = 0xAA;
    if (ata_write_sectors(0, 1, sec) != 0) return -1;

    /* 2. Partition entry array at LBA 2 (one entry, 128 bytes) */
    mem0(sec, 512);
    /* type GUID: Microsoft Basic Data (EBD0A0A2-B9E5-4433-87C0-68B6B72699C7) */
    uint8_t type_guid[16] = {
        0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44,
        0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7
    };
    for (int i = 0; i < 16; i++) sec[i] = type_guid[i];
    uint8_t uniq[16] = {
        0x11,0x22,0x33,0x44, 0x55,0x66, 0x77,0x88,
        0x99,0xAA,0xBB,0xCC, 0xDD,0xEE,0xFF,0x01
    };
    for (int i = 0; i < 16; i++) sec[16 + i] = uniq[i];
    set64(&sec[32], start_lba, 0);
    set64(&sec[40], last_lba, 0);
    /* name "MYKERNEL" UTF-16LE */
    const char* nm = "MYKERNEL";
    for (int i = 0; nm[i]; i++) {
        sec[56 + i * 2] = (uint8_t)nm[i];
        sec[56 + i * 2 + 1] = 0;
    }

    uint8_t zero_sec[512];
    mem0(zero_sec, 512);

    /* Compute CRC32 over full 16384 bytes (128 entries * 128 bytes = 32 sectors) */
    uint32_t c = crc32_continue(0xFFFFFFFFu, sec, 512);
    for (int s = 1; s < 32; s++)
        c = crc32_continue(c, zero_sec, 512);
    uint32_t part_crc = ~c;

    /* Write primary partition entries LBA 2..33 */
    if (ata_write_sectors(2, 1, sec) != 0) return -1;
    for (int s = 1; s < 32; s++) {
        if (ata_write_sectors(2 + s, 1, zero_sec) != 0) return -1;
    }

    /* 3. Primary GPT header LBA 1 */
    mem0(sec, 512);
    sec[0]='E'; sec[1]='F'; sec[2]='I'; sec[3]=' ';
    sec[4]='P'; sec[5]='A'; sec[6]='R'; sec[7]='T';
    set32(&sec[8], 0x00010000); /* revision */
    set32(&sec[12], 92);        /* header size */
    set32(&sec[16], 0);         /* CRC placeholder */
    set64(&sec[24], 1, 0);      /* current LBA */
    set64(&sec[32], backup_lba, 0); /* backup LBA */
    set64(&sec[40], 34, 0);     /* first usable */
    set64(&sec[48], last_usable, 0); /* last usable */
    for (int i = 0; i < 16; i++) sec[56 + i] = (uint8_t)(0xA0 + i);
    set64(&sec[72], 2, 0);      /* partition entries LBA */
    set32(&sec[80], 128);       /* num entries */
    set32(&sec[84], 128);       /* size of entry */
    set32(&sec[88], part_crc);

    uint32_t hdr_crc = crc32(sec, 92);
    set32(&sec[16], hdr_crc);
    if (ata_write_sectors(1, 1, sec) != 0) return -1;

    /* 4. Backup partition entries */
    uint32_t bkp_entries_lba = backup_lba - 32;
    /* Reconstruct entry sector */
    mem0(sec, 512);
    for (int i = 0; i < 16; i++) sec[i] = type_guid[i];
    for (int i = 0; i < 16; i++) sec[16 + i] = uniq[i];
    set64(&sec[32], start_lba, 0);
    set64(&sec[40], last_lba, 0);
    for (int i = 0; nm[i]; i++) {
        sec[56 + i * 2] = (uint8_t)nm[i];
        sec[56 + i * 2 + 1] = 0;
    }
    if (ata_write_sectors(bkp_entries_lba, 1, sec) != 0) return -1;
    for (int s = 1; s < 32; s++) {
        if (ata_write_sectors(bkp_entries_lba + s, 1, zero_sec) != 0) return -1;
    }

    /* 5. Backup GPT header */
    mem0(sec, 512);
    sec[0]='E'; sec[1]='F'; sec[2]='I'; sec[3]=' ';
    sec[4]='P'; sec[5]='A'; sec[6]='R'; sec[7]='T';
    set32(&sec[8], 0x00010000);
    set32(&sec[12], 92);
    set32(&sec[16], 0);
    set64(&sec[24], backup_lba, 0); /* current LBA is backup */
    set64(&sec[32], 1, 0);          /* backup LBA is primary (1) */
    set64(&sec[40], 34, 0);
    set64(&sec[48], last_usable, 0);
    for (int i = 0; i < 16; i++) sec[56 + i] = (uint8_t)(0xA0 + i);
    set64(&sec[72], bkp_entries_lba, 0);
    set32(&sec[80], 128);
    set32(&sec[84], 128);
    set32(&sec[88], part_crc);

    uint32_t bkp_crc = crc32(sec, 92);
    set32(&sec[16], bkp_crc);
    if (ata_write_sectors(backup_lba, 1, sec) != 0) return -1;

    return 0;
}

int part_create_hybrid_fat32(uint32_t start_lba, uint32_t size_sectors) {
    if (!ata_present()) {
        if (ata_init() != 0) return -1;
    }
    if (start_lba < 34) start_lba = 2048;

    struct disk_info di;
    part_detect_disk(&di);
    uint32_t disk_sectors = di.total_sectors ? di.total_sectors : 131072;

    if (size_sectors == 0 || start_lba + size_sectors + 34 > disk_sectors) {
        if (disk_sectors > start_lba + 34)
            size_sectors = disk_sectors - start_lba - 34;
        else
            size_sectors = 65536;
    }

    /* 1. Create full GPT tables (primary + backup) */
    if (part_create_gpt_fat32(start_lba, size_sectors) != 0)
        return -1;

    /* 2. Overlay Hybrid MBR on LBA 0 */
    if (ata_read_sectors(0, 1, sec) != 0)
        mem0(sec, 512);

    /* Clear all 4 partition entries */
    for (int i = 446; i < 510; i++) sec[i] = 0;

    /* Entry 0: Bootable FAT32 partition for BIOS MBR bootstrap */
    uint8_t* e0 = &sec[446];
    e0[0] = 0x80; /* Bootable / Active */
    e0[1] = 0x01; e0[2] = 0x01; e0[3] = 0x00; /* CHS start */
    e0[4] = 0x0C; /* FAT32 LBA */
    e0[5] = 0xFE; e0[6] = 0xFF; e0[7] = 0xFF; /* CHS end */
    set32(&e0[8], start_lba);
    set32(&e0[12], size_sectors);

    /* Entry 1: GPT Protective partition covering GPT header and partition entries */
    uint8_t* e1 = &sec[446 + 16];
    e1[0] = 0x00;
    e1[1] = 0x00; e1[2] = 0x02; e1[3] = 0x00;
    e1[4] = 0xEE; /* GPT Protective */
    e1[5] = 0xFF; e1[6] = 0xFF; e1[7] = 0xFF;
    set32(&e1[8], 1);
    set32(&e1[12], start_lba - 1);

    sec[510] = 0x55; sec[511] = 0xAA;
    return ata_write_sectors(0, 1, sec);
}

void part_print_mbr(void) {
    if (!ata_present()) {
        if (ata_init() != 0) {
            terminal_writestring("No ATA disk detected\n");
            return;
        }
    }
    if (ata_read_sectors(0, 1, sec) != 0) {
        terminal_writestring("MBR read failed (I/O error)\n");
        return;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        terminal_writestring("No valid MBR signature (0x55AA) found at LBA 0\n");
        return;
    }

    struct disk_info di;
    part_detect_disk(&di);
    terminal_writestring("Disk Size: ");
    terminal_write_uint(di.size_mb);
    terminal_writestring(" MB (");
    terminal_write_uint(di.total_sectors);
    terminal_writestring(" sectors)\n");
    terminal_writestring("MBR Partition Table (LBA 0):\n");
    terminal_writestring("  # | Boot | Type | Start LBA  | Sectors    | Size (MB)\n");
    terminal_writestring("  --+------+------+------------+------------+----------\n");

    int found = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t* e = &sec[446 + i * 16];
        uint8_t type = e[4];
        if (!type) continue;
        found = 1;
        uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9]<<8) | ((uint32_t)e[10]<<16) | ((uint32_t)e[11]<<24);
        uint32_t sz  = (uint32_t)e[12] | ((uint32_t)e[13]<<8) | ((uint32_t)e[14]<<16) | ((uint32_t)e[15]<<24);
        uint32_t mb  = sz / 2048;

        terminal_writestring("  ");
        terminal_write_uint((uint32_t)i + 1);
        terminal_writestring(" |  ");
        terminal_writestring(e[0] == 0x80 ? "*   " : "    ");
        terminal_writestring("| 0x");
        terminal_write_hex(type);
        terminal_writestring(" | ");
        terminal_write_uint(lba);
        terminal_writestring(" | ");
        terminal_write_uint(sz);
        terminal_writestring(" | ");
        terminal_write_uint(mb);
        terminal_writestring(" MB");

        if (type == 0x0C) terminal_writestring(" (FAT32 LBA)");
        else if (type == 0x0B) terminal_writestring(" (FAT32)");
        else if (type == 0xEE) terminal_writestring(" (GPT Protective)");
        else if (type == 0x83) terminal_writestring(" (Linux)");
        terminal_putchar('\n');
    }
    if (!found)
        terminal_writestring("  (No partitions defined)\n");
}

void part_print_gpt(void) {
    if (!ata_present()) {
        if (ata_init() != 0) {
            terminal_writestring("No ATA disk detected\n");
            return;
        }
    }
    if (ata_read_sectors(1, 1, sec) != 0) {
        terminal_writestring("GPT read failed at LBA 1\n");
        return;
    }
    if (sec[0]!='E' || sec[1]!='F' || sec[2]!='I' || sec[3]!=' ' ||
        sec[4]!='P' || sec[5]!='A' || sec[6]!='R' || sec[7]!='T') {
        terminal_writestring("No GPT header signature ('EFI PART') at LBA 1\n");
        return;
    }

    uint32_t current_lba = (uint32_t)sec[24] | ((uint32_t)sec[25]<<8) | ((uint32_t)sec[26]<<16) | ((uint32_t)sec[27]<<24);
    uint32_t backup_lba  = (uint32_t)sec[32] | ((uint32_t)sec[33]<<8) | ((uint32_t)sec[34]<<16) | ((uint32_t)sec[35]<<24);
    uint32_t pe_lba      = (uint32_t)sec[72] | ((uint32_t)sec[73]<<8) | ((uint32_t)sec[74]<<16) | ((uint32_t)sec[75]<<24);
    uint32_t num_entries = (uint32_t)sec[80] | ((uint32_t)sec[81]<<8) | ((uint32_t)sec[82]<<16) | ((uint32_t)sec[83]<<24);

    terminal_writestring("GPT Header OK (EFI PART revision 1.0):\n");
    terminal_writestring("  Current LBA: "); terminal_write_uint(current_lba);
    terminal_writestring("  Backup LBA: "); terminal_write_uint(backup_lba);
    terminal_putchar('\n');
    terminal_writestring("  Entries LBA: "); terminal_write_uint(pe_lba);
    terminal_writestring("  Max Entries: "); terminal_write_uint(num_entries);
    terminal_putchar('\n');

    if (ata_read_sectors(pe_lba, 1, sec) != 0) {
        terminal_writestring("Failed to read partition entries array\n");
        return;
    }

    uint32_t start = (uint32_t)sec[32] | ((uint32_t)sec[33]<<8) | ((uint32_t)sec[34]<<16) | ((uint32_t)sec[35]<<24);
    uint32_t end   = (uint32_t)sec[40] | ((uint32_t)sec[41]<<8) | ((uint32_t)sec[42]<<16) | ((uint32_t)sec[43]<<24);

    if (start != 0 || end != 0) {
        uint32_t sz = end - start + 1;
        terminal_writestring("  Partition 1:\n");
        terminal_writestring("    Start LBA: "); terminal_write_uint(start);
        terminal_writestring("  End LBA: "); terminal_write_uint(end);
        terminal_writestring(" ("); terminal_write_uint(sz); terminal_writestring(" sectors, ");
        terminal_write_uint(sz / 2048); terminal_writestring(" MB)\n");
        terminal_writestring("    Name: ");
        for (int i = 0; i < 36; i++) {
            char c = (char)sec[56 + i * 2];
            if (!c) break;
            terminal_putchar(c);
        }
        terminal_putchar('\n');
    } else {
        terminal_writestring("  (No active partitions in entry sector)\n");
    }
}
