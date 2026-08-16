/* pci.c — доступ к конфигурационному пространству PCI и сканирование шины.
 *
 * Формат "адреса", который пишется в порт CONFIG_ADDRESS (0xCF8),
 * стандартизирован и выглядит так (32 бита):
 *
 *   бит 31    : Enable bit (всегда 1, иначе обращение проигнорируется)
 *   биты 30-24: зарезервированы (0)
 *   биты 23-16: номер шины (bus, 0-255)
 *   биты 15-11: номер устройства на шине (device, 0-31)
 *   биты 10-8 : номер функции устройства (function, 0-7 — у одной
 *               физической карты может быть несколько логических
 *               "функций", например у некоторых звуковых карт)
 *   биты 7-2  : смещение регистра внутри устройства (offset, кратно 4)
 *   биты 1-0  : всегда 0
 *
 * После записи такого "адреса" в 0xCF8, чтение/запись 0xCFC читает
 * или пишет соответствующие 4 байта конфигурационного пространства
 * именно этого устройства. */

#include "pci.h"
#include "io.h"
#include "vga.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* Собрать 32-битный "адрес" для CONFIG_ADDRESS по правилам выше */
static uint32_t pci_make_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (uint32_t)0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)(device & 0x1F) << 11)
         | ((uint32_t)(function & 0x07) << 8)
         | ((uint32_t)(offset & 0xFC));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    /* Читаем целиком 32-битное слово, содержащее нужные нам 2 байта,
     * и выбираем нужную половину — PCI не поддерживает произвольное
     * 16-битное смещение в порту 0xCFC, только по границе offset&0xFC */
    uint32_t word = pci_config_read32(bus, device, function, offset & 0xFC);
    if (offset & 2)
        return (uint16_t)(word >> 16);
    return (uint16_t)(word & 0xFFFF);
}

void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t word = pci_config_read32(bus, device, function, offset & 0xFC);
    if (offset & 2)
        word = (word & 0x0000FFFF) | ((uint32_t)value << 16);
    else
        word = (word & 0xFFFF0000) | value;
    pci_config_write32(bus, device, function, offset & 0xFC, word);
}

/* Заполнить структуру pci_device данными об устройстве по известным
 * bus/device/function (сам вызывающий уже убедился, что оно существует) */
static void pci_fill_device(uint8_t bus, uint8_t device, uint8_t function, struct pci_device* out) {
    out->bus = bus;
    out->device = device;
    out->function = function;

    uint32_t id_word = pci_config_read32(bus, device, function, 0x00);
    out->vendor_id = (uint16_t)(id_word & 0xFFFF);
    out->device_id = (uint16_t)(id_word >> 16);

    uint32_t class_word = pci_config_read32(bus, device, function, 0x08);
    out->prog_if     = (uint8_t)(class_word >> 8);
    out->subclass    = (uint8_t)(class_word >> 16);
    out->class_code  = (uint8_t)(class_word >> 24);

    uint32_t header_word = pci_config_read32(bus, device, function, 0x0C);
    out->header_type = (uint8_t)(header_word >> 16);

    for (int i = 0; i < 6; i++)
        out->bar[i] = pci_config_read32(bus, device, function, 0x10 + i * 4);

    uint32_t irq_word = pci_config_read32(bus, device, function, 0x3C);
    out->interrupt_line = (uint8_t)(irq_word & 0xFF);
}

/* Есть ли вообще устройство по данному адресу? Если Vendor ID читается
 * как 0xFFFF — там ничего не подключено (стандартная договорённость PCI) */
static int pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor = pci_config_read16(bus, device, function, 0x00);
    return vendor != 0xFFFF;
}

/* Общий перебор всех bus/device/function с вызовом callback на каждом
 * реально существующем устройстве. Используется и для печати списка
 * (pci_scan_and_print), и для поиска конкретного устройства — чтобы
 * не дублировать сами тройные циклы перебора в нескольких местах. */
typedef void (*pci_visit_fn)(struct pci_device* dev, void* userdata);

static void pci_for_each_device(pci_visit_fn visit, void* userdata) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t device = 0; device < 32; device++) {
            if (!pci_device_exists((uint8_t)bus, (uint8_t)device, 0))
                continue;

            struct pci_device dev;
            pci_fill_device((uint8_t)bus, (uint8_t)device, 0, &dev);
            visit(&dev, userdata);

            /* Бит 0x80 в header_type означает "многофункциональное
             * устройство" — тогда стоит проверить и функции 1..7,
             * иначе у устройства точно есть только функция 0 */
            if (dev.header_type & 0x80) {
                for (uint32_t function = 1; function < 8; function++) {
                    if (!pci_device_exists((uint8_t)bus, (uint8_t)device, (uint8_t)function))
                        continue;
                    struct pci_device dev_f;
                    pci_fill_device((uint8_t)bus, (uint8_t)device, (uint8_t)function, &dev_f);
                    visit(&dev_f, userdata);
                }
            }
        }
    }
}

/* --- pci_scan_and_print: печать списка в терминал --- */

static void print_visitor(struct pci_device* dev, void* userdata) {
    (void)userdata;
    terminal_writestring("  ");
    terminal_write_uint(dev->bus);
    terminal_writestring(":");
    terminal_write_uint(dev->device);
    terminal_writestring(".");
    terminal_write_uint(dev->function);
    terminal_writestring("  vendor=");
    terminal_write_hex(dev->vendor_id);
    terminal_writestring(" device=");
    terminal_write_hex(dev->device_id);
    terminal_writestring(" class=");
    terminal_write_hex(dev->class_code);
    terminal_writestring(" subclass=");
    terminal_write_hex(dev->subclass);
    terminal_writestring("\n");
}

void pci_scan_and_print(void) {
    terminal_writestring("PCI devices:\n");
    pci_for_each_device(print_visitor, 0);
}

/* --- pci_find_device / pci_find_by_class --- */

struct find_by_id_ctx {
    uint16_t vendor_id, device_id;
    struct pci_device* out;
    int found;
};

static void find_by_id_visitor(struct pci_device* dev, void* userdata) {
    struct find_by_id_ctx* ctx = (struct find_by_id_ctx*)userdata;
    if (ctx->found)
        return;
    if (dev->vendor_id == ctx->vendor_id && dev->device_id == ctx->device_id) {
        *ctx->out = *dev;
        ctx->found = 1;
    }
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* out) {
    struct find_by_id_ctx ctx = { vendor_id, device_id, out, 0 };
    pci_for_each_device(find_by_id_visitor, &ctx);
    return ctx.found;
}

struct find_by_class_ctx {
    uint8_t class_code, subclass;
    struct pci_device* out;
    int found;
};

static void find_by_class_visitor(struct pci_device* dev, void* userdata) {
    struct find_by_class_ctx* ctx = (struct find_by_class_ctx*)userdata;
    if (ctx->found)
        return;
    if (dev->class_code == ctx->class_code && dev->subclass == ctx->subclass) {
        *ctx->out = *dev;
        ctx->found = 1;
    }
}

int pci_find_by_class(uint8_t class_code, uint8_t subclass, struct pci_device* out) {
    struct find_by_class_ctx ctx = { class_code, subclass, out, 0 };
    pci_for_each_device(find_by_class_visitor, &ctx);
    return ctx.found;
}

/* --- Bus Mastering --- */

void pci_enable_bus_mastering(const struct pci_device* dev) {
    /* Регистр Command находится по смещению 0x04 (нижние 16 бит слова).
     * Бит 2 (0x04) этого регистра — Bus Master Enable: без него
     * устройство не может само читать/писать оперативную память (DMA),
     * даже если запрограммировать все остальные регистры правильно —
     * запросы DMA будут молча игнорироваться чипсетом. */
    uint16_t command = pci_config_read16(dev->bus, dev->device, dev->function, 0x04);
    command |= 0x0004;
    pci_config_write16(dev->bus, dev->device, dev->function, 0x04, command);
}
