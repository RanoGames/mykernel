/* ata.c — ATA Primary Master, PIO read */

#include "ata.h"
#include "io.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_CMD        0x1F7

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_RDY  0x40
#define ATA_STATUS_BSY  0x80

#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_IDENTIFY 0xEC

static int g_ata_ok = 0;

static void ata_delay(void) {
    for (int i = 0; i < 4; i++)
        (void)inb(0x3F6); /* alternate status — ~400ns delay */
}

static int ata_wait_not_busy(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (!(s & ATA_STATUS_BSY))
            return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_STATUS_ERR)
            return -1;
        if (s & ATA_STATUS_DF)
            return -1;
        if (s & ATA_STATUS_DRQ)
            return 0;
    }
    return -1;
}

int ata_init(void) {
    g_ata_ok = 0;

    /* Выбрать master */
    outb(ATA_DRIVE, 0xA0);
    ata_delay();

    /* IDENTIFY */
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, ATA_CMD_IDENTIFY);
    ata_delay();

    uint8_t status = inb(ATA_STATUS);
    if (status == 0)
        return -1; /* нет устройства */

    if (ata_wait_not_busy() != 0)
        return -1;

    /* AT: mid/hi ненулевые → AT */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0)
        return -1;

    if (ata_wait_drq() != 0)
        return -1;

    /* Читаем и отбрасываем identify-данные */
    for (int i = 0; i < 256; i++)
        (void)inw(ATA_DATA);

    g_ata_ok = 1;
    return 0;
}

int ata_present(void) {
    return g_ata_ok;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void* buf) {
    if (!g_ata_ok || count == 0 || !buf)
        return -1;

    uint16_t* out = (uint16_t*)buf;

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, ATA_CMD_READ_PIO);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_not_busy() != 0)
            return -1;
        if (ata_wait_drq() != 0)
            return -1;

        for (int i = 0; i < 256; i++)
            out[s * 256 + i] = inw(ATA_DATA);
    }

    return 0;
}
