#include "install.h"
#include "part.h"
#include "fat32.h"
#include "ata.h"
#include "vga.h"
#include "vfs.h"
#include <stdint.h>

/* Optional embedded kernel image (objcopy symbols). If missing, size=0. */
extern char _binary_build_mykernel_bin_start[] __attribute__((weak));
extern char _binary_build_mykernel_bin_end[] __attribute__((weak));
extern char _binary_build_mbr_bin_start[] __attribute__((weak));
extern char _binary_build_mbr_bin_end[] __attribute__((weak));
extern char _binary_build_stage2_bin_start[] __attribute__((weak));
extern char _binary_build_stage2_bin_end[] __attribute__((weak));

#define KERNEL_LBA  8192
#define META_LBA    8191
#define STAGE2_LBA  1

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

    if (!mbr || !mbr_end || mbr_end <= mbr) {
        step("WARN: mbr.bin not linked — using part MBR only");
        return 0;
    }
    uint8_t sec[512];
    for (int i = 0; i < 512; i++) sec[i] = 0;
    uint32_t mlen = (uint32_t)(mbr_end - mbr);
    if (mlen > 446) mlen = 446; /* keep partition table area for hybrid */
    for (uint32_t i = 0; i < mlen && i < 512; i++) sec[i] = (uint8_t)mbr[i];
    /* Ensure signature */
    sec[510] = 0x55; sec[511] = 0xAA;
    /* Bootable FAT partition entry */
    sec[446] = 0x80;
    sec[446 + 4] = 0x0C;
    sec[446 + 8] = (uint8_t)(2048);
    sec[446 + 9] = (uint8_t)(2048 >> 8);
    sec[446 + 10] = (uint8_t)(2048 >> 16);
    sec[446 + 11] = (uint8_t)(2048 >> 24);
    uint32_t psz = 262144;
    sec[446 + 12] = (uint8_t)(psz);
    sec[446 + 13] = (uint8_t)(psz >> 8);
    sec[446 + 14] = (uint8_t)(psz >> 16);
    sec[446 + 15] = (uint8_t)(psz >> 24);
    if (ata_write_sectors(0, 1, sec) != 0) return -1;

    if (s2 && s2_end && s2_end > s2) {
        step("Writing stage2...");
        if (write_raw(STAGE2_LBA, s2, (uint32_t)(s2_end - s2)) != 0)
            return -1;
    }
    return 0;
}

static int install_kernel_image(void) {
    const char* k = _binary_build_mykernel_bin_start;
    const char* kend = _binary_build_mykernel_bin_end;
    if (!k || !kend || kend <= k) {
        step("WARN: kernel payload not linked (build with KERNEL_PAYLOAD=1)");
        return 0;
    }
    uint32_t ksize = (uint32_t)(kend - k);
    uint32_t sects = (ksize + 511) / 512;
    step("Writing kernel image at LBA 8192...");
    /* meta at 8191 */
    uint8_t meta[512];
    for (int i = 0; i < 512; i++) meta[i] = 0;
    meta[0] = 'L'; meta[1] = 'N'; meta[2] = 'R'; meta[3] = 'K'; /* LE 0x4B524E4C */
    meta[0] = 0x4C; meta[1] = 0x4E; meta[2] = 0x52; meta[3] = 0x4B;
    meta[4] = (uint8_t)(sects);
    meta[5] = (uint8_t)(sects >> 8);
    meta[6] = (uint8_t)(sects >> 16);
    meta[7] = (uint8_t)(sects >> 24);
    if (ata_write_sectors(META_LBA, 1, meta) != 0) return -1;
    if (write_raw(KERNEL_LBA, k, ksize) != 0) return -1;
    step("Kernel sectors written.");
    return 0;
}

int install_run(void) {
    if (!ata_present()) {
        step("FAIL: no ATA disk");
        return -1;
    }
    const uint32_t start = 2048;
    const uint32_t size = 262144;

    step("1/6 Bootloader (MBR+stage2)...");
    if (install_bootloader() != 0) { step("FAIL: bootloader"); return -1; }

    step("2/6 GPT (optional secondary layout)...");
    part_create_gpt_fat32(start, size);
    /* MBR already written by bootloader path; GPT protective may overwrite — rewrite hybrid MBR */
    install_bootloader();

    step("3/6 FAT32 mkfs @ LBA 2048...");
    if (fat32_mkfs(start, size) != FAT_OK) { step("FAIL: mkfs"); return -1; }

    step("4/6 Mount + seed files...");
    fat32_set_partition_lba(start);
    if (fat32_mount() != 0) { step("FAIL: mount"); return -1; }
    fat32_mkdir("BOOT");
    fat32_mkdir("BIN");
    fat32_write("README.TXT", "MyKernel installed volume. Boot via BIOS MBR stage2 or GRUB.\r\n");
    fat32_write("GRUB.CFG", "set timeout=3\nmenuentry \"MyKernel\" {\n multiboot /BOOT/KERNEL.BIN\n boot\n}\n");

    step("5/6 Embedded kernel to raw LBA + FAT note...");
    install_kernel_image();
    fat32_write("KERNEL.TXT", "Raw image at LBA 8192; stage2 loads it to 1MB.\r\n");

    step("6/6 VFS mount /mnt");
    vfs_mount("/mnt", VFS_FS_FAT32);

    step("DONE. Reboot with: qemu-system-i386 -hda disk.img -m 128");
    step("(no -kernel needed if stage2 + payload present)");
    return 0;
}

void install_wizard(void) {
    terminal_writestring("\n=== MyKernel Install Wizard ===\n");
    terminal_writestring("Writes disk: MBR/stage2, GPT, FAT32, kernel payload\n");
    if (install_run() == 0)
        terminal_writestring("Install OK.\n");
    else
        terminal_writestring("Install failed.\n");
}
