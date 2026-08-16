/* fat32.c — минимальный read-only FAT32 */

#include "fat32.h"
#include "ata.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define SECTOR_SIZE 512
#define MAX_PATH_COMPONENTS 8
#define MAX_NAME 13
#define CAT_MAX_BYTES 4096

struct fat32_bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count; /* 0 for FAT32 */
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

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LFN       0x0F

static int mounted;
static uint32_t partition_lba; /* LBA начала раздела FAT */
static uint32_t fat_start;     /* LBA первой FAT */
static uint32_t data_start;    /* LBA начала data area */
static uint32_t root_cluster;
static uint8_t  sectors_per_cluster;
static uint32_t fat_size_sectors;
static uint32_t total_clusters;

static uint8_t sector_buf[SECTOR_SIZE];
static uint8_t cluster_buf[SECTOR_SIZE * 8]; /* до 8 секторов на кластер */

static int str_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void mem_copy(void* d, const void* s, size_t n) {
    uint8_t* dd = d;
    const uint8_t* ss = s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}

static int read_sector(uint32_t lba) {
    return ata_read_sectors(partition_lba + lba, 1, sector_buf);
}

/* Найти FAT32 раздел: MBR или суперфлоппи (весь диск) */
static int find_fat_partition(void) {
    if (ata_read_sectors(0, 1, sector_buf) != 0)
        return -1;

    /* Проверка: может быть безраздельный FAT (BPB прямо в LBA 0) */
    {
        struct fat32_bpb* bpb = (struct fat32_bpb*)sector_buf;
        if (bpb->bytes_per_sector == 512 &&
            bpb->sectors_per_cluster >= 1 &&
            bpb->num_fats >= 1 &&
            bpb->fat_size_32 != 0 &&
            (bpb->fs_type[0] == 'F' || bpb->boot_signature == 0x29)) {
            partition_lba = 0;
            return 0;
        }
    }

    /* MBR: ищем раздел type 0x0B или 0x0C */
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

static uint32_t fat_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start + (fat_offset / SECTOR_SIZE);
    uint32_t ent_offset = fat_offset % SECTOR_SIZE;

    if (ata_read_sectors(partition_lba + fat_sector, 1, sector_buf) != 0)
        return 0x0FFFFFF7; /* bad */

    uint32_t val = (uint32_t)sector_buf[ent_offset] |
                   ((uint32_t)sector_buf[ent_offset + 1] << 8) |
                   ((uint32_t)sector_buf[ent_offset + 2] << 16) |
                   ((uint32_t)sector_buf[ent_offset + 3] << 24);
    return val & 0x0FFFFFFF;
}

static int read_cluster(uint32_t cluster) {
    uint32_t lba = cluster_to_lba(cluster);
    return ata_read_sectors(partition_lba + lba, sectors_per_cluster, cluster_buf);
}

/* "FILE.TXT" / "DIR" → 11-байтное имя FAT */
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
    for (int i = 0; i < 8 && in[i] != ' '; i++)
        out[o++] = in[i];
    if (in[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && in[i] != ' '; i++)
            out[o++] = in[i];
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
            if ((uint8_t)de->name[0] == 0x00)
                return 0; /* конец каталога */
            if ((uint8_t)de->name[0] == 0xE5)
                continue; /* удалён */
            if (de->attr == ATTR_LFN)
                continue;
            if (de->attr & ATTR_VOLUME_ID)
                continue;

            int r = fn(de, ctx);
            if (r != 0)
                return r;
        }
        cluster = fat_next_cluster(cluster);
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
        return 1; /* stop */
    }
    return 0;
}

static int resolve_path(const char* path, struct fat_dirent* out, int* is_root) {
    *is_root = 0;
    while (*path == '/' || *path == ' ')
        path++;

    if (*path == '\0') {
        *is_root = 1;
        return 0;
    }

    uint32_t cluster = root_cluster;
    char component[MAX_NAME];

    for (;;) {
        int i = 0;
        while (*path && *path != '/' && i < MAX_NAME - 1)
            component[i++] = *path++;
        component[i] = '\0';
        while (*path == '/') path++;

        struct find_ctx fc;
        fc.name = component;
        fc.ok = 0;
        if (iterate_dir(cluster, find_cb, &fc) < 0)
            return -1;
        if (!fc.ok)
            return -2;

        int last = (*path == '\0');
        if (last) {
            *out = fc.found;
            return 0;
        }
        if (!(fc.found.attr & ATTR_DIRECTORY))
            return -3;

        cluster = ((uint32_t)fc.found.first_cluster_hi << 16) | fc.found.first_cluster_lo;
        if (cluster == 0)
            cluster = root_cluster;
    }
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

    if (bpb->bytes_per_sector != 512)
        return FAT_ERR_NOT_FAT32;
    if (bpb->fat_size_32 == 0)
        return FAT_ERR_NOT_FAT32;
    if (bpb->sectors_per_cluster == 0 || bpb->sectors_per_cluster > 8)
        return FAT_ERR_NOT_FAT32;

    sectors_per_cluster = bpb->sectors_per_cluster;
    fat_size_sectors = bpb->fat_size_32;
    fat_start = bpb->reserved_sectors;
    data_start = bpb->reserved_sectors + (uint32_t)bpb->num_fats * fat_size_sectors;
    root_cluster = bpb->root_cluster;

    uint32_t total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    uint32_t data_sectors = total_sectors - data_start;
    total_clusters = data_sectors / sectors_per_cluster;

    mounted = 1;
    return FAT_OK;
}

int fat32_is_mounted(void) {
    return mounted;
}

void fat32_info(void) {
    if (!mounted) {
        terminal_writestring("FAT32 not mounted. Use: fatmount\n");
        return;
    }
    terminal_writestring("FAT32 mounted\n");
    terminal_writestring("  partition LBA: ");
    terminal_write_uint(partition_lba);
    terminal_putchar('\n');
    terminal_writestring("  sectors/cluster: ");
    terminal_write_uint(sectors_per_cluster);
    terminal_putchar('\n');
    terminal_writestring("  root cluster: ");
    terminal_write_uint(root_cluster);
    terminal_putchar('\n');
    terminal_writestring("  data start LBA: ");
    terminal_write_uint(data_start);
    terminal_putchar('\n');
}

enum fat_result fat32_ls(const char* path) {
    if (!mounted)
        return FAT_ERR_NOT_MOUNTED;

    if (!path) path = "/";

    struct fat_dirent de;
    int is_root = 0;
    int r = resolve_path(path, &de, &is_root);
    if (r == -2) return FAT_ERR_NOT_FOUND;
    if (r == -3) return FAT_ERR_NOT_DIR;
    if (r < 0) return FAT_ERR_IO;

    uint32_t cluster;
    if (is_root) {
        cluster = root_cluster;
    } else {
        if (!(de.attr & ATTR_DIRECTORY))
            return FAT_ERR_NOT_DIR;
        cluster = ((uint32_t)de.first_cluster_hi << 16) | de.first_cluster_lo;
        if (cluster == 0) cluster = root_cluster;
    }

    if (iterate_dir(cluster, ls_cb, 0) < 0)
        return FAT_ERR_IO;
    return FAT_OK;
}

enum fat_result fat32_cat(const char* path) {
    if (!mounted)
        return FAT_ERR_NOT_MOUNTED;
    if (!path || !path[0])
        return FAT_ERR_NOT_FOUND;

    struct fat_dirent de;
    int is_root = 0;
    int r = resolve_path(path, &de, &is_root);
    if (is_root || r == -2) return FAT_ERR_NOT_FOUND;
    if (r < 0) return FAT_ERR_IO;
    if (de.attr & ATTR_DIRECTORY)
        return FAT_ERR_IS_DIR;

    uint32_t cluster = ((uint32_t)de.first_cluster_hi << 16) | de.first_cluster_lo;
    uint32_t remaining = de.file_size;
    uint32_t shown = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8 && remaining > 0) {
        if (read_cluster(cluster) != 0)
            return FAT_ERR_IO;

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
        cluster = fat_next_cluster(cluster);
    }
    if (shown && cluster_buf[0]) { /* ensure newline if file had content */ }
    terminal_putchar('\n');
    return FAT_OK;
}

const char* fat_strerror(enum fat_result r) {
    switch (r) {
        case FAT_OK: return "OK";
        case FAT_ERR_NO_DISK: return "no ATA disk (attach fat.img in QEMU)";
        case FAT_ERR_NOT_FAT32: return "no FAT32 partition found";
        case FAT_ERR_IO: return "disk I/O error";
        case FAT_ERR_NOT_FOUND: return "not found";
        case FAT_ERR_IS_DIR: return "is a directory";
        case FAT_ERR_NOT_DIR: return "not a directory";
        case FAT_ERR_TOO_BIG: return "file too big";
        case FAT_ERR_NOT_MOUNTED: return "FAT32 not mounted (fatmount)";
        default: return "unknown";
    }
}
