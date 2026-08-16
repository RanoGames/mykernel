/* pci.h — работа с шиной PCI (Peripheral Component Interconnect).
 *
 * PCI-устройства (сетевые карты, звуковые карты, видеокарты и т.д.)
 * не имеют фиксированных портов ввода-вывода, как ATA (0x1F0) или
 * клавиатура (0x60) — вместо этого у каждого устройства есть
 * "конфигурационное пространство" (256 байт стандартных регистров:
 * Vendor ID, Device ID, BAR0-BAR5 и т.д.), доступное через два
 * специальных порта на самой материнской плате:
 *
 *   0xCF8 (CONFIG_ADDRESS) — куда записать "адрес": номер шины (bus),
 *                             устройства (device), функции (function)
 *                             и смещение регистра внутри устройства
 *   0xCFC (CONFIG_DATA)    — откуда прочитать/куда записать сами данные
 *
 * Чтобы найти нужное устройство (например, звуковую карту AC97),
 * нужно перебрать (просканировать) все возможные комбинации
 * bus/device/function и посмотреть Vendor ID + Device ID каждого —
 * этим и занимается pci_scan(). */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* Один найденный PCI-девайс — то, что мы запоминаем при сканировании */
struct pci_device {
    uint8_t  bus, device, function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;     /* класс устройства, напр. 0x04 = мультимедиа */
    uint8_t  subclass;       /* подкласс, напр. 0x01 = audio device */
    uint8_t  prog_if;
    uint8_t  header_type;
    uint32_t bar[6];         /* Base Address Registers — где у устройства регистры/память */
    uint8_t  interrupt_line; /* какой IRQ использует устройство */
};

/* Прочитать/записать 32-битный регистр конфигурационного пространства
 * устройства по смещению offset (offset должен быть кратен 4) */
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);

/* Просканировать всю шину PCI (256 bus x 32 device x 8 function — на
 * практике почти всё пустое, но перебрать надо всё) и напечатать
 * список найденных устройств в терминал (команда `lspci` в shell) */
void pci_scan_and_print(void);

/* Найти устройство по Vendor ID + Device ID (например, AC97 у Intel —
 * 0x8086:0x2415). Возвращает 1 и заполняет out, если нашли, иначе 0. */
int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* out);

/* Найти устройство по классу+подклассу (например, класс 0x02 = сетевой
 * контроллер, подкласс 0x00 = Ethernet) — полезно, когда не знаешь
 * заранее конкретную модель железа (в QEMU это может быть разное). */
int pci_find_by_class(uint8_t class_code, uint8_t subclass, struct pci_device* out);

/* Включить Bus Mastering — обязательно нужно ЛЮБОМУ устройству, которое
 * само (без участия процессора) читает/пишет оперативную память через
 * DMA. Без этого бита устройство физически не может провести DMA-
 * транзакцию, даже если весь остальной код написан правильно. */
void pci_enable_bus_mastering(const struct pci_device* dev);

#endif
