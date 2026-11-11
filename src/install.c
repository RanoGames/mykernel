#include "install.h"
#include "part.h"
#include "fat32.h"
#include "ata.h"
#include "vga.h"
#include "vfs.h"
#include "keyboard.h"
#include <stdint.h>
#include <stddef.h>

/* Linker symbols for running kernel image in RAM (1MB) */
extern char _kernel_start[];
extern char _kernel_data_end[];

/* Optional embedded blobs (weak symbols) */
extern char _binary_build_mykernel_bin_start[] __attribute__((weak));
extern char _binary_build_mykernel_bin_end[]   __attribute__((weak));
extern char _binary_build_mbr_bin_start[]      __attribute__((weak));
extern char _binary_build_mbr_bin_end[]        __attribute__((weak));
extern char _binary_build_stage2_bin_start[]   __attribute__((weak));
extern char _binary_build_stage2_bin_end[]     __attribute__((weak));

extern const uint8_t _binary_build_hello_elf_start[]    __attribute__((weak));
extern const uint8_t _binary_build_hello_elf_end[]      __attribute__((weak));
extern const uint8_t _binary_build_dynhello_elf_start[]  __attribute__((weak));
extern const uint8_t _binary_build_dynhello_elf_end[]    __attribute__((weak));
extern const uint8_t _binary_build_libhello_so_start[]   __attribute__((weak));
extern const uint8_t _binary_build_libhello_so_end[]     __attribute__((weak));

#define KERNEL_LBA  8192
#define META_LBA    8191
#define STAGE2_LBA  1

/* Builtin BIOS MBR bootstrap (88 bytes code + DAP struct, loads 32 sectors from LBA 1 to 0x7E00) */
static const uint8_t s_builtin_mbr[92] = {
    0xfa, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0, 0xbc, 0x00, 0x7c,
    0xfb, 0x88, 0x16, 0x39, 0x7c, 0xb4, 0x42, 0x8a, 0x16, 0x39, 0x7c, 0xbe,
    0x4c, 0x7c, 0xcd, 0x13, 0x72, 0x09, 0x8a, 0x16, 0x39, 0x7c, 0xea, 0x00,
    0x7e, 0x00, 0x00, 0xbe, 0x3a, 0x7c, 0xac, 0x84, 0xc0, 0x74, 0x06, 0xb4,
    0x0e, 0xcd, 0x10, 0xeb, 0xf5, 0xfa, 0xf4, 0xeb, 0xfc, 0x00, 0x4d, 0x42,
    0x52, 0x20, 0x64, 0x69, 0x73, 0x6b, 0x20, 0x65, 0x72, 0x72, 0x6f, 0x72,
    0x00, 0x8d, 0x74, 0x00, 0x10, 0x00, 0x20, 0x00, 0x00, 0x7e, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Builtin Stage2 bootloader (440 bytes: reads LBA 8191 meta, loads kernel from LBA 8192 to 1MB, enters PM, jumps to kernel) */
static const uint8_t s_builtin_stage2[440] = {
    0xfa, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0, 0xbc, 0x00, 0x90, 0x88, 0x16, 0x53, 0x7f,
    0xbe, 0x70, 0x7f, 0xe8, 0x1d, 0x01, 0xc7, 0x06, 0x64, 0x7f, 0x00, 0x05, 0xc7, 0x06, 0x66, 0x7f,
    0x00, 0x00, 0x66, 0xc7, 0x06, 0x68, 0x7f, 0xff, 0x1f, 0x00, 0x00, 0xe8, 0xf9, 0x00, 0x0f, 0x82,
    0x17, 0x01, 0x66, 0x81, 0x3e, 0x00, 0x05, 0x4c, 0x4e, 0x52, 0x4b, 0x0f, 0x85, 0x02, 0x01, 0x66,
    0xa1, 0x04, 0x05, 0x66, 0x85, 0xc0, 0x0f, 0x84, 0xff, 0x00, 0x66, 0x3d, 0x00, 0x03, 0x00, 0x00,
    0x76, 0x06, 0x66, 0xb8, 0x00, 0x03, 0x00, 0x00, 0x66, 0xa3, 0x54, 0x7f, 0x66, 0xa1, 0x08, 0x05,
    0x66, 0x85, 0xc0, 0x75, 0x06, 0x66, 0xb8, 0x0c, 0x00, 0x10, 0x00, 0x66, 0xa3, 0x58, 0x7f, 0xbe,
    0x78, 0x7f, 0xe8, 0xbe, 0x00, 0x66, 0xc7, 0x06, 0x5c, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x66, 0xa1,
    0x5c, 0x7f, 0x66, 0x3b, 0x06, 0x54, 0x7f, 0x73, 0x3d, 0x66, 0x89, 0xc3, 0x66, 0xc1, 0xe3, 0x09,
    0x66, 0x81, 0xc3, 0x00, 0x00, 0x02, 0x00, 0x66, 0x89, 0xd9, 0x66, 0xc1, 0xe9, 0x04, 0x89, 0x0e,
    0x66, 0x7f, 0x66, 0x83, 0xe3, 0x0f, 0x89, 0x1e, 0x64, 0x7f, 0x66, 0xa1, 0x5c, 0x7f, 0x66, 0x05,
    0x00, 0x20, 0x00, 0x00, 0x66, 0xa3, 0x68, 0x7f, 0xe8, 0x6c, 0x00, 0x0f, 0x82, 0x8a, 0x00, 0x66,
    0xff, 0x06, 0x5c, 0x7f, 0xeb, 0xb8, 0xbe, 0x7e, 0x7f, 0xe8, 0x67, 0x00, 0xe4, 0x92, 0xa8, 0x02,
    0x75, 0x06, 0x0c, 0x02, 0x24, 0xfe, 0xe6, 0x92, 0xfa, 0x0f, 0x01, 0x16, 0xb0, 0x7f, 0x0f, 0x20,
    0xc0, 0x66, 0x83, 0xc8, 0x01, 0x0f, 0x22, 0xc0, 0xea, 0xed, 0x7e, 0x08, 0x00, 0x66, 0xb8, 0x10,
    0x00, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xe0, 0x8e, 0xe8, 0x8e, 0xd0, 0xbc, 0x00, 0x00, 0x09, 0x00,
    0xbe, 0x00, 0x00, 0x02, 0x00, 0xbf, 0x00, 0x00, 0x10, 0x00, 0x8b, 0x0d, 0x54, 0x7f, 0x00, 0x00,
    0xc1, 0xe1, 0x09, 0xc1, 0xe9, 0x02, 0xf3, 0xa5, 0xb8, 0x02, 0xb0, 0xad, 0x2b, 0x31, 0xdb, 0x8b,
    0x0d, 0x58, 0x7f, 0x00, 0x00, 0xff, 0xe1, 0xb4, 0x42, 0x8a, 0x16, 0x53, 0x7f, 0xbe, 0x60, 0x7f,
    0xcd, 0x13, 0xc3, 0xac, 0x84, 0xc0, 0x74, 0x08, 0xb4, 0x0e, 0xb7, 0x00, 0xcd, 0x10, 0xeb, 0xf3,
    0xc3, 0xbe, 0x82, 0x7f, 0xe8, 0xec, 0xff, 0xeb, 0x06, 0xbe, 0x89, 0x7f, 0xe8, 0xe4, 0xff, 0xfa,
    0xf4, 0xeb, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x73, 0x74, 0x61, 0x67, 0x65, 0x32, 0x20, 0x00, 0x6c, 0x6f, 0x61, 0x64, 0x20, 0x00, 0x70, 0x6d,
    0x20, 0x00, 0x6e, 0x6f, 0x4b, 0x52, 0x4e, 0x4c, 0x00, 0x64, 0x69, 0x73, 0x6b, 0x45, 0x52, 0x52,
    0x00, 0x8d, 0x74, 0x00, 0x8d, 0xbd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00,
    0x17, 0x00, 0x98, 0x7f, 0x00, 0x00, 0x00, 0x00
};

static void step(const char* s) {
    terminal_writestring("[install] ");
    terminal_writestring(s);
    terminal_putchar('\n');
}

static int write_raw(uint32_t lba, const void* data, uint32_t nbytes) {
    const uint8_t* p = (const uint8_t*)data;
    uint8_t sec[512];
    uint32_t off = 0;
    while (off < nbytes) {
        for (int i = 0; i < 512; i++)
            sec[i] = (off + (uint32_t)i < nbytes) ? p[off + (uint32_t)i] : 0;
        if (ata_write_sectors(lba, 1, sec) != 0) return -1;
        lba++;
        off += 512;
    }
    return 0;
}

static int install_bootloader(void) {
    const char* mbr = _binary_build_mbr_bin_start;
    const char* mbr_end = _binary_build_mbr_bin_end;
    const char* s2 = _binary_build_stage2_bin_start;
    const char* s2_end = _binary_build_stage2_bin_end;

    uint32_t mbr_len = (mbr && mbr_end > mbr) ? (uint32_t)(mbr_end - mbr) : sizeof(s_builtin_mbr);
    const uint8_t* mbr_src = (mbr && mbr_end > mbr) ? (const uint8_t*)mbr : s_builtin_mbr;

    uint32_t s2_len = (s2 && s2_end > s2) ? (uint32_t)(s2_end - s2) : sizeof(s_builtin_stage2);
    const uint8_t* s2_src = (s2 && s2_end > s2) ? (const uint8_t*)s2 : s_builtin_stage2;

    /* Read existing LBA 0 to preserve partition table entries (bytes 446..509) */
    uint8_t sec[512];
    for (int i = 0; i < 512; i++) sec[i] = 0;
    if (ata_read_sectors(0, 1, sec) != 0) {
        for (int i = 0; i < 512; i++) sec[i] = 0;
    }

    /* Copy MBR bootstrap code (up to 446 bytes) */
    uint32_t code_len = mbr_len > 446 ? 446 : mbr_len;
    for (uint32_t i = 0; i < code_len; i++)
        sec[i] = mbr_src[i];

    /* Ensure boot signature */
    sec[510] = 0x55;
    sec[511] = 0xAA;

    if (ata_write_sectors(0, 1, sec) != 0)
        return -1;

    /* Write stage2 bootloader to LBA 1 (32 sectors) */
    if (write_raw(STAGE2_LBA, s2_src, s2_len) != 0)
        return -1;

    return 0;
}

static int install_kernel_image(void) {
    const char* k = 0;
    uint32_t ksize = 0;

    if (_binary_build_mykernel_bin_start && _binary_build_mykernel_bin_end > _binary_build_mykernel_bin_start) {
        k = _binary_build_mykernel_bin_start;
        ksize = (uint32_t)(_binary_build_mykernel_bin_end - _binary_build_mykernel_bin_start);
    } else if (_kernel_data_end > _kernel_start) {
        k = _kernel_start;
        ksize = (uint32_t)(_kernel_data_end - _kernel_start);
    }

    if (!k || ksize == 0) {
        step("FAIL: kernel image not found");
        return -1;
    }

    uint32_t sects = (ksize + 511) / 512;
    terminal_writestring("      Kernel payload: ");
    terminal_write_uint(ksize);
    terminal_writestring(" bytes (");
    terminal_write_uint(sects);
    terminal_writestring(" sectors)\n");

    /* Metadata sector at LBA 8191 */
    uint8_t meta[512];
    for (int i = 0; i < 512; i++) meta[i] = 0;
    /* Magic LE 0x4B524E4C ('KRNL') */
    meta[0] = 0x4C; meta[1] = 0x4E; meta[2] = 0x52; meta[3] = 0x4B;
    /* Sector count */
    meta[4] = (uint8_t)(sects & 0xFF);
    meta[5] = (uint8_t)((sects >> 8) & 0xFF);
    meta[6] = (uint8_t)((sects >> 16) & 0xFF);
    meta[7] = (uint8_t)((sects >> 24) & 0xFF);
    /* Entry point address: default 0x10000C */
    uint32_t entry = 0x10000C;
    meta[8]  = (uint8_t)(entry & 0xFF);
    meta[9]  = (uint8_t)((entry >> 8) & 0xFF);
    meta[10] = (uint8_t)((entry >> 16) & 0xFF);
    meta[11] = (uint8_t)((entry >> 24) & 0xFF);

    if (ata_write_sectors(META_LBA, 1, meta) != 0) return -1;
    if (write_raw(KERNEL_LBA, k, ksize) != 0) return -1;
    return 0;
}

static void install_fat32_files(void) {
    const void* k_data = 0;
    size_t k_len = 0;

    if (_binary_build_mykernel_bin_start && _binary_build_mykernel_bin_end > _binary_build_mykernel_bin_start) {
        k_data = _binary_build_mykernel_bin_start;
        k_len = (size_t)(_binary_build_mykernel_bin_end - _binary_build_mykernel_bin_start);
    } else if (_kernel_data_end > _kernel_start) {
        k_data = _kernel_start;
        k_len = (size_t)(_kernel_data_end - _kernel_start);
    }

    if (k_data && k_len > 0) {
        fat32_write_file("BOOT/KERNEL.BIN", k_data, k_len);
        fat32_write_file("KERNEL.BIN", k_data, k_len);
    }

    fat32_write("BOOT/GRUB.CFG",
        "set timeout=5\n"
        "set default=0\n\n"
        "menuentry \"MyKernel OS\" {\n"
        "    multiboot /BOOT/KERNEL.BIN\n"
        "    boot\n"
        "}\n");

    fat32_write("GRUB.CFG",
        "set timeout=5\n"
        "set default=0\n\n"
        "menuentry \"MyKernel OS\" {\n"
        "    multiboot /KERNEL.BIN\n"
        "    boot\n"
        "}\n");

    fat32_write("README.TXT",
        "==============================\r\n"
        "   MyKernel Operating System  \r\n"
        "==============================\r\n\r\n"
        "Installed successfully via MyKernel Installer.\r\n"
        "- BIOS Boot: MBR -> Stage2 @ LBA 1 -> Kernel @ LBA 8192\r\n"
        "- Multiboot / GRUB: /BOOT/KERNEL.BIN\r\n"
        "- Filesystem: FAT32 (4 KB clusters)\r\n");

    fat32_write("KERNEL.TXT",
        "Raw flat kernel image deployed at LBA 8192.\r\n"
        "Stage2 reads metadata from LBA 8191 and loads kernel to 1MB.\r\n");

    if (_binary_build_hello_elf_start && _binary_build_hello_elf_end > _binary_build_hello_elf_start) {
        size_t sz = (size_t)(_binary_build_hello_elf_end - _binary_build_hello_elf_start);
        fat32_write_file("BIN/HELLO.ELF", _binary_build_hello_elf_start, sz);
    }
    if (_binary_build_dynhello_elf_start && _binary_build_dynhello_elf_end > _binary_build_dynhello_elf_start) {
        size_t sz = (size_t)(_binary_build_dynhello_elf_end - _binary_build_dynhello_elf_start);
        fat32_write_file("BIN/DYNHELLO.ELF", _binary_build_dynhello_elf_start, sz);
    }
    if (_binary_build_libhello_so_start && _binary_build_libhello_so_end > _binary_build_libhello_so_start) {
        size_t sz = (size_t)(_binary_build_libhello_so_end - _binary_build_libhello_so_start);
        fat32_write_file("LIB/LIBHELLO.SO", _binary_build_libhello_so_start, sz);
    }
}

int install_run_mode(int mode) {
    step("[1/6] Disk Detection: Probing ATA drives...");
    struct disk_info di;
    if (part_detect_disk(&di) != 0 || !di.present) {
        step("FAIL: No ATA drive detected on primary IDE controller.");
        return -1;
    }
    terminal_writestring("      Detected ATA Drive: ");
    terminal_write_uint(di.size_mb);
    terminal_writestring(" MB (");
    terminal_write_uint(di.total_sectors);
    terminal_writestring(" sectors)\n");

    const uint32_t start_lba = 2048;
    uint32_t size_sectors = 0;
    if (di.total_sectors > start_lba + 34)
        size_sectors = di.total_sectors - start_lba - 34;
    else
        size_sectors = 65536;

    if (mode == INSTALL_MODE_MBR) {
        step("[2/6] Partitioning: Writing MBR partition table (LBA 2048)...");
        if (part_create_mbr_fat32(start_lba, size_sectors) != 0) {
            step("FAIL: MBR partition creation failed.");
            return -1;
        }
    } else if (mode == INSTALL_MODE_GPT) {
        step("[2/6] Partitioning: Writing GPT layout (Primary + Backup)...");
        if (part_create_gpt_fat32(start_lba, size_sectors) != 0) {
            step("FAIL: GPT partition creation failed.");
            return -1;
        }
    } else {
        step("[2/6] Partitioning: Writing Hybrid MBR + GPT layout...");
        if (part_create_hybrid_fat32(start_lba, size_sectors) != 0) {
            step("FAIL: Hybrid partition creation failed.");
            return -1;
        }
    }

    step("[3/6] Bootloader: Writing MBR & Stage2 bootloader...");
    if (install_bootloader() != 0) {
        step("FAIL: Bootloader write failed.");
        return -1;
    }

    step("[4/6] Formatting: Initializing FAT32 filesystem (mkfs)...");
    if (fat32_mkfs(start_lba, size_sectors) != FAT_OK) {
        step("FAIL: FAT32 format failed.");
        return -1;
    }

    step("[5/6] Deployment: Writing kernel binary (LBA 8192) & seeding files...");
    if (install_kernel_image() != 0) {
        step("FAIL: Kernel image deployment failed.");
        return -1;
    }

    fat32_set_partition_lba(start_lba);
    if (fat32_mount() != FAT_OK) {
        step("FAIL: FAT32 volume mount failed.");
        return -1;
    }

    fat32_mkdir("BOOT");
    fat32_mkdir("BIN");
    fat32_mkdir("LIB");
    install_fat32_files();

    step("[6/6] Mount & Verification: Registering VFS /mnt...");
    vfs_mount("/mnt", VFS_FS_FAT32);

    step("SUCCESS: Installation pipeline complete.");
    return 0;
}

int install_run(void) {
    return install_run_mode(INSTALL_MODE_HYBRID);
}

void install_wizard(void) {
    terminal_writestring("\n==================================================\n");
    terminal_writestring("          MyKernel Setup & Install Wizard         \n");
    terminal_writestring("==================================================\n\n");

    struct disk_info di;
    if (part_detect_disk(&di) != 0 || !di.present) {
        terminal_writestring("[!] ERROR: No ATA drive detected on Primary Controller.\n");
        terminal_writestring("    Ensure a disk is connected (e.g. qemu -hda disk.img)\n\n");
        return;
    }

    terminal_writestring("[*] Primary Disk Detected:\n");
    terminal_writestring("    - Capacity: ");
    terminal_write_uint(di.size_mb);
    terminal_writestring(" MB (");
    terminal_write_uint(di.total_sectors);
    terminal_writestring(" sectors)\n");
    terminal_writestring("    - Existing MBR: ");
    terminal_writestring(di.has_mbr ? "Yes" : "No");
    terminal_writestring(" | Existing GPT: ");
    terminal_writestring(di.has_gpt ? "Yes" : "No");
    terminal_putchar('\n');

    terminal_writestring("\nSelect Partitioning & Installation Layout:\n");
    terminal_writestring("  [1] Hybrid MBR + GPT (Recommended: BIOS boot + GPT compliance)\n");
    terminal_writestring("  [2] Traditional MBR (Legacy BIOS)\n");
    terminal_writestring("  [3] Pure GPT + Protective MBR (UEFI)\n");
    terminal_writestring("Press 1, 2, or 3 (Proceeding with default [1] in 2 seconds...)\n\n");

    int mode = INSTALL_MODE_HYBRID;
    for (int t = 0; t < 2000000; t++) {
        char c = 0;
        if (keyboard_trygetchar(&c)) {
            if (c == '2') { mode = INSTALL_MODE_MBR; break; }
            if (c == '3') { mode = INSTALL_MODE_GPT; break; }
            if (c == '1' || c == '\n') { mode = INSTALL_MODE_HYBRID; break; }
        }
    }

    if (mode == INSTALL_MODE_MBR)
        terminal_writestring("Selected: [2] Traditional MBR\n\n");
    else if (mode == INSTALL_MODE_GPT)
        terminal_writestring("Selected: [3] Pure GPT\n\n");
    else
        terminal_writestring("Selected: [1] Hybrid MBR + GPT\n\n");

    int res = install_run_mode(mode);
    if (res == 0) {
        terminal_writestring("\n==================================================\n");
        terminal_writestring("  [ SUCCESS ] Installation completed successfully!\n");
        terminal_writestring("==================================================\n");
        terminal_writestring("You can now boot directly from this disk:\n");
        terminal_writestring("  qemu-system-i386 -hda disk.img -m 128\n\n");
    } else {
        terminal_writestring("\n[ FAIL ] Installation failed. See error log above.\n\n");
    }
}
