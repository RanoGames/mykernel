/* fat32.c — FAT32 read + write (root, 8.3) + fat32_read_file */

#include "fat32.h"
#include "ata.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define SECTOR_SIZE 512
#define MAX_NAME 13
#define CAT_MAX_BYTES 4096
#define WRITE_MAX_BYTES 4096

struct fat32_bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed));

struct fat_dirent {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed));

#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LFN       0x0F

static int mounted;
static uint32_t partition_lba;
static uint32_t fat_start;
static uint32_t data_start;
static uint32_t root_cluster;
static uint8_t  sectors_per_cluster;
static uint32_t fat_size_sectors;
static uint8_t  num_fats;
static uint32_t total_clusters;

static uint8_t sector_buf[SECTOR_SIZE];
static uint8_t cluster_buf[SECTOR_SIZE * 8];

static void mem_set(void* d, uint8_t v, size_t n) {
    uint8_t* p = d;
    for (size_t i = 0; i < n; i++) p[i] = v;
}

static void mem_copy(void* d, const void* s, size_t n) {
    uint8_t* dd = d;
    const uint8_t* ss = s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int read_sector(uint32_t lba) {
    return ata_read_sectors(partition_lba + lba, 1, sector_buf);
}

static int write_sector(uint32_t lba) {
    return ata_write_sectors(partition_lba + lba, 1, sector_buf);
}

static int find_fat_partition(void) {
    if (ata_read_sectors(0, 1, sector_buf) != 0)
        return -1;

    struct fat32_bpb* bpb = (struct fat32_bpb*)sector_buf;
    if (bpb->bytes_per_sector == 512 &&
        bpb->sectors_per_cluster >= 1 &&
        bpb->num_fats >= 1 &&
        bpb->fat_size_32 != 0 &&
        (bpb->fs_type[0] == 'F' || bpb->boot_signature == 0x29)) {
        partition_lba = 0;
        return 0;
    }

    if (sector_buf[510] != 0x55 || sector_buf[511] != 0xAA)
        return -1;

    for (int i = 0; i < 4; i++) {
        uint8_t* e = &sector_buf[446 + i * 16];
        uint8_t type = e[4];
        uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                       ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        if (type == 0x0B || type == 0x0C || type == 0x0E) {
            partition_lba = lba;
            return 0;
        }
    }
    return -1;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start + (cluster - 2) * sectors_per_cluster;
}

static uint32_t fat_get(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start + (fat_offset / SECTOR_SIZE);
    uint32_t ent_offset = fat_offset % SECTOR_SIZE;
    if (ata_read_sectors(partition_lba + fat_sector, 1, sector_buf) != 0)
        return 0x0FFFFFF7;
    uint32_t val = (uint32_t)sector_buf[ent_offset] |
                   ((uint32_t)sector_buf[ent_offset + 1] << 8) |
                   ((uint32_t)sector_buf[ent_offset + 2] << 16) |
                   ((uint32_t)sector_buf[ent_offset + 3] << 24);
    return val & 0x0FFFFFFF;
}

static int fat_set(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start + (fat_offset / SECTOR_SIZE);
    uint32_t ent_offset = fat_offset % SECTOR_SIZE;

    for (uint8_t f = 0; f < num_fats; f++) {
        uint32_t sec = fat_sector + (uint32_t)f * fat_size_sectors;
        if (ata_read_sectors(partition_lba + sec, 1, sector_buf) != 0)
            return -1;
        uint32_t old = (uint32_t)sector_buf[ent_offset] |
                       ((uint32_t)sector_buf[ent_offset + 1] << 8) |
                       ((uint32_t)sector_buf[ent_offset + 2] << 16) |
                       ((uint32_t)sector_buf[ent_offset + 3] << 24);
        uint32_t neu = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        sector_buf[ent_offset]     = (uint8_t)(neu & 0xFF);
        sector_buf[ent_offset + 1] = (uint8_t)((neu >> 8) & 0xFF);
        sector_buf[ent_offset + 2] = (uint8_t)((neu >> 16) & 0xFF);
        sector_buf[ent_offset + 3] = (uint8_t)((neu >> 24) & 0xFF);
        if (ata_write_sectors(partition_lba + sec, 1, sector_buf) != 0)
            return -1;
    }
    return 0;
}

static uint32_t fat_alloc_cluster(void) {
    for (uint32_t c = 2; c < total_clusters + 2; c++) {
        if (fat_get(c) == 0) {
            if (fat_set(c, 0x0FFFFFFF) != 0)
                return 0;
            return c;
        }
    }
    return 0;
}

static int fat_free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t next = fat_get(cluster);
        if (fat_set(cluster, 0) != 0)
            return -1;
        cluster = next;
    }
    return 0;
}

static int read_cluster(uint32_t cluster) {
    return ata_read_sectors(partition_lba + cluster_to_lba(cluster),
                            sectors_per_cluster, cluster_buf);
}

static int write_cluster(uint32_t cluster) {
    return ata_write_sectors(partition_lba + cluster_to_lba(cluster),
                             sectors_per_cluster, cluster_buf);
}

static void name_to_83(const char* name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[j++] = c;
    }
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out[j++] = c;
        }
    }
}

static void name_from_83(const char in[11], char out[MAX_NAME]) {
    int o = 0;
    for (int i = 0; i < 8 && in[i] != ' '; i++) out[o++] = in[i];
    if (in[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && in[i] != ' '; i++) out[o++] = in[i];
    }
    out[o] = '\0';
}

static int dirent_match(const struct fat_dirent* de, const char* name) {
    char want[11];
    name_to_83(name, want);
    for (int i = 0; i < 11; i++)
        if (de->name[i] != want[i]) return 0;
    return 1;
}

typedef int (*dir_iter_fn)(const struct fat_dirent* de, void* ctx);

static int iterate_dir(uint32_t start_cluster, dir_iter_fn fn, void* ctx) {
    uint32_t cluster = start_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (read_cluster(cluster) != 0)
            return -1;
        size_t bytes = (size_t)sectors_per_cluster * SECTOR_SIZE;
        for (size_t off = 0; off + sizeof(struct fat_dirent) <= bytes; off += sizeof(struct fat_dirent)) {
            struct fat_dirent* de = (struct fat_dirent*)(cluster_buf + off);
            if ((uint8_t)de->name[0] == 0x00) return 0;
            if ((uint8_t)de->name[0] == 0xE5) continue;
            if (de->attr == ATTR_LFN) continue;
            if (de->attr & ATTR_VOLUME_ID) continue;
            int r = fn(de, ctx);
            if (r != 0) return r;
        }
        cluster = fat_get(cluster);
    }
    return 0;
}

struct find_ctx {
    const char* name;
    struct fat_dirent found;
    int ok;
};

static int find_cb(const struct fat_dirent* de, void* ctx) {
    struct find_ctx* fc = ctx;
    if (dirent_match(de, fc->name)) {
        fc->found = *de;
        fc->ok = 1;
        return 1;
    }
    return 0;
}

struct slot_ctx {
    char want[11];
    uint32_t cluster;
    size_t offset;
    int found;
    int found_existing;
    struct fat_dirent existing;
};

static int find_slot_in_root(struct slot_ctx* sc) {
    uint32_t cluster = root_cluster;
    sc->found = 0;
    sc->found_existing = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (read_cluster(cluster) != 0)
            return -1;
        size_t bytes = (size_t)sectors_per_cluster * SECTOR_SIZE;
        for (size_t off = 0; off + sizeof(struct fat_dirent) <= bytes; off += 32) {
            struct fat_dirent* de = (struct fat_dirent*)(cluster_buf + off);
            uint8_t first = (uint8_t)de->name[0];

            if (first != 0x00 && first != 0xE5 && de->attr != ATTR_LFN && !(de->attr & ATTR_VOLUME_ID)) {
                int match = 1;
                for (int i = 0; i < 11; i++)
                    if (de->name[i] != sc->want[i]) { match = 0; break; }
                if (match) {
                    sc->cluster = cluster;
                    sc->offset = off;
                    sc->found = 1;
                    sc->found_existing = 1;
                    sc->existing = *de;
                    return 0;
                }
            }

            if (!sc->found && (first == 0x00 || first == 0xE5)) {
                sc->cluster = cluster;
                sc->offset = off;
                sc->found = 1;
                if (first == 0x00)
                    return 0;
            }
        }
        uint32_t next = fat_get(cluster);
        if (next >= 0x0FFFFFF8) {
            if (!sc->found) {
                uint32_t nc = fat_alloc_cluster();
                if (!nc) return -2;
                if (fat_set(cluster, nc) != 0) return -1;
                mem_set(cluster_buf, 0, (size_t)sectors_per_cluster * SECTOR_SIZE);
                if (write_cluster(nc) != 0) return -1;
                sc->cluster = nc;
                sc->offset = 0;
                sc->found = 1;
            }
            return 0;
        }
        cluster = next;
    }
    return sc->found ? 0 : -2;
}

static int ls_cb(const struct fat_dirent* de, void* ctx) {
    (void)ctx;
    char name[MAX_NAME];
    name_from_83(de->name, name);
    if (de->attr & ATTR_DIRECTORY) {
        terminal_writestring("  [DIR]  ");
        terminal_writestring(name);
        terminal_putchar('\n');
    } else {
        terminal_writestring("  [FILE] ");
        terminal_writestring(name);
        terminal_writestring("  ");
        terminal_write_uint(de->file_size);
        terminal_writestring(" bytes\n");
    }
    return 0;
}

int fat32_mount(void) {
    mounted = 0;
    if (!ata_present()) {
        if (ata_init() != 0)
            return FAT_ERR_NO_DISK;
    }
    if (find_fat_partition() != 0)
        return FAT_ERR_NOT_FAT32;
    if (read_sector(0) != 0)
        return FAT_ERR_IO;

    struct fat32_bpb* bpb = (struct fat32_bpb*)sector_buf;
    if (bpb->bytes_per_sector != 512 || bpb->fat_size_32 == 0)
        return FAT_ERR_NOT_FAT32;
    if (bpb->sectors_per_cluster == 0 || bpb->sectors_per_cluster > 8)
        return FAT_ERR_NOT_FAT32;

    sectors_per_cluster = bpb->sectors_per_cluster;
    fat_size_sectors = bpb->fat_size_32;
    num_fats = bpb->num_fats;
    fat_start = bpb->reserved_sectors;
    data_start = bpb->reserved_sectors + (uint32_t)num_fats * fat_size_sectors;
    root_cluster = bpb->root_cluster;

    uint32_t total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    total_clusters = (total_sectors - data_start) / sectors_per_cluster;
    mounted = 1;
    return FAT_OK;
}

int fat32_is_mounted(void) { return mounted; }

void fat32_info(void) {
    if (!mounted) {
        terminal_writestring("FAT32 not mounted. Use: fatmount\n");
        return;
    }
    terminal_writestring("FAT32 mounted (R/W)\n");
    terminal_writestring("  partition LBA: "); terminal_write_uint(partition_lba); terminal_putchar('\n');
    terminal_writestring("  sectors/cluster: "); terminal_write_uint(sectors_per_cluster); terminal_putchar('\n');
    terminal_writestring("  root cluster: "); terminal_write_uint(root_cluster); terminal_putchar('\n');
}

enum fat_result fat32_ls(const char* path) {
    if (!mounted) return FAT_ERR_NOT_MOUNTED;
    (void)path;
    if (iterate_dir(root_cluster, ls_cb, 0) < 0)
        return FAT_ERR_IO;
    return FAT_OK;
}

enum fat_result fat32_cat(const char* path) {
    if (!mounted) return FAT_ERR_NOT_MOUNTED;
    if (!path || !path[0]) return FAT_ERR_NOT_FOUND;

    struct find_ctx fc;
    fc.name = path;
    fc.ok = 0;
    if (iterate_dir(root_cluster, find_cb, &fc) < 0) return FAT_ERR_IO;
    if (!fc.ok) return FAT_ERR_NOT_FOUND;
    if (fc.found.attr & ATTR_DIRECTORY) return FAT_ERR_IS_DIR;

    uint32_t cluster = ((uint32_t)fc.found.first_cluster_hi << 16) | fc.found.first_cluster_lo;
    uint32_t remaining = fc.found.file_size;
    uint32_t shown = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8 && remaining > 0) {
        if (read_cluster(cluster) != 0) return FAT_ERR_IO;
        size_t chunk = (size_t)sectors_per_cluster * SECTOR_SIZE;
        if (chunk > remaining) chunk = remaining;
        for (size_t i = 0; i < chunk; i++) {
            if (shown >= CAT_MAX_BYTES) {
                terminal_writestring("\n... (truncated)\n");
                return FAT_OK;
            }
            terminal_putchar((char)cluster_buf[i]);
            shown++;
        }
        remaining -= (uint32_t)chunk;
        cluster = fat_get(cluster);
    }
    terminal_putchar('\n');
    return FAT_OK;
}

enum fat_result fat32_write(const char* name, const char* text) {
    if (!mounted) return FAT_ERR_NOT_MOUNTED;
    if (!name || !name[0] || !text) return FAT_ERR_NOT_FOUND;

    size_t len = str_len(text);
    if (len > WRITE_MAX_BYTES) return FAT_ERR_TOO_BIG;

    struct slot_ctx sc;
    name_to_83(name, sc.want);
    int sr = find_slot_in_root(&sc);
    if (sr == -2) return FAT_ERR_NO_SPACE;
    if (sr < 0) return FAT_ERR_IO;

    if (sc.found_existing) {
        if (sc.existing.attr & ATTR_DIRECTORY)
            return FAT_ERR_IS_DIR;
        uint32_t old = ((uint32_t)sc.existing.first_cluster_hi << 16) | sc.existing.first_cluster_lo;
        if (old >= 2)
            fat_free_chain(old);
    }

    uint32_t first_cluster = 0;
    uint32_t prev = 0;
    size_t pos = 0;
    size_t cluster_bytes = (size_t)sectors_per_cluster * SECTOR_SIZE;

    if (len == 0) {
        first_cluster = 0;
    } else {
        while (pos < len) {
            uint32_t c = fat_alloc_cluster();
            if (!c) return FAT_ERR_NO_SPACE;
            if (!first_cluster) first_cluster = c;
            if (prev) {
                if (fat_set(prev, c) != 0) return FAT_ERR_IO;
            }

            mem_set(cluster_buf, 0, cluster_bytes);
            size_t chunk = len - pos;
            if (chunk > cluster_bytes) chunk = cluster_bytes;
            mem_copy(cluster_buf, text + pos, chunk);
            if (write_cluster(c) != 0) return FAT_ERR_IO;

            pos += chunk;
            prev = c;
        }
        if (prev && fat_set(prev, 0x0FFFFFFF) != 0)
            return FAT_ERR_IO;
    }

    if (read_cluster(sc.cluster) != 0) return FAT_ERR_IO;
    struct fat_dirent* de = (struct fat_dirent*)(cluster_buf + sc.offset);
    mem_set(de, 0, sizeof(*de));
    for (int i = 0; i < 11; i++) de->name[i] = sc.want[i];
    de->attr = ATTR_ARCHIVE;
    de->first_cluster_hi = (uint16_t)(first_cluster >> 16);
    de->first_cluster_lo = (uint16_t)(first_cluster & 0xFFFF);
    de->file_size = (uint32_t)len;
    if (write_cluster(sc.cluster) != 0) return FAT_ERR_IO;

    return FAT_OK;
}

enum fat_result fat32_read_file(const char* name, char* buf, size_t buf_size, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!mounted) return FAT_ERR_NOT_MOUNTED;
    if (!name || !name[0] || !buf || buf_size == 0) return FAT_ERR_NOT_FOUND;

    struct find_ctx fc;
    fc.name = name;
    fc.ok = 0;
    if (iterate_dir(root_cluster, find_cb, &fc) < 0) return FAT_ERR_IO;
    if (!fc.ok) return FAT_ERR_NOT_FOUND;
    if (fc.found.attr & ATTR_DIRECTORY) return FAT_ERR_IS_DIR;

    uint32_t cluster = ((uint32_t)fc.found.first_cluster_hi << 16) | fc.found.first_cluster_lo;
    uint32_t remaining = fc.found.file_size;
    size_t written = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8 && remaining > 0 && written < buf_size) {
        if (read_cluster(cluster) != 0) return FAT_ERR_IO;
        size_t chunk = (size_t)sectors_per_cluster * SECTOR_SIZE;
        if (chunk > remaining) chunk = remaining;
        if (chunk > buf_size - written) chunk = buf_size - written;
        for (size_t i = 0; i < chunk; i++)
            buf[written++] = (char)cluster_buf[i];
        remaining -= (uint32_t)chunk;
        cluster = fat_get(cluster);
    }
    if (out_len) *out_len = written;
    return FAT_OK;
}

const char* fat_strerror(enum fat_result r) {
    switch (r) {
        case FAT_OK: return "OK";
        case FAT_ERR_NO_DISK: return "no ATA disk";
        case FAT_ERR_NOT_FAT32: return "no FAT32 partition";
        case FAT_ERR_IO: return "disk I/O error";
        case FAT_ERR_NOT_FOUND: return "not found";
        case FAT_ERR_IS_DIR: return "is a directory";
        case FAT_ERR_NOT_DIR: return "not a directory";
        case FAT_ERR_TOO_BIG: return "file too big (max 4K)";
        case FAT_ERR_NOT_MOUNTED: return "not mounted (fatmount)";
        case FAT_ERR_NO_SPACE: return "no space";
        case FAT_ERR_EXISTS: return "already exists";
        default: return "unknown";
    }
}
