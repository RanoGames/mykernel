/* serial.c — драйвер последовательного порта COM1 через чип UART 16550.
 *
 * Стандартный адрес порта COM1 на x86 — 0x3F8. UART работает через
 * несколько регистров, смещённых от этого базового адреса (0x3F8+0,
 * 0x3F8+1 и т.д.) — это классическая, десятилетиями стабильная схема,
 * работает одинаково что в QEMU, что на настоящем железе с COM-портом. */

#include "serial.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

#define COM1_PORT 0x3F8

static void serial_configure(void) {
    outb(COM1_PORT + 1, 0x00); /* отключить прерывания от UART — мы его не через IRQ используем, а опросом */
    outb(COM1_PORT + 3, 0x80); /* включить режим настройки скорости (DLAB = 1) */
    outb(COM1_PORT + 0, 0x03); /* делитель скорости, младший байт -> 38400 бод */
    outb(COM1_PORT + 1, 0x00); /* делитель скорости, старший байт */
    outb(COM1_PORT + 3, 0x03); /* 8 бит данных, без чётности, 1 стоп-бит; DLAB обратно в 0 */
    outb(COM1_PORT + 2, 0xC7); /* включить и очистить FIFO-буферы приёма/передачи */
    outb(COM1_PORT + 4, 0x0B); /* включить линии DTR, RTS, вспомогательный выход 2 */
}

void serial_init(void) {
    serial_configure();
}

/* Проверить, свободен ли передающий регистр UART (можно ли класть
 * туда следующий байт для отправки). Бит 5 (0x20) регистра "Line
 * Status" (смещение +5) означает "буфер передачи пуст". */
static int serial_transmit_ready(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_putchar(char c) {
    /* Дожидаемся, пока UART освободит буфер — иначе можно перезаписать
     * ещё не отправленный байт и потерять данные */
    while (!serial_transmit_ready()) { }

    /* Для корректного отображения переносов строк в большинстве
     * терминалов перед '\n' дополнительно шлём '\r' (как это принято
     * в serial/telnet-протоколах — иначе курсор терминала может не
     * возвращаться в начало строки) */
    if (c == '\n')
        outb(COM1_PORT, '\r');

    outb(COM1_PORT, (uint8_t) c);
}

void serial_write(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++)
        serial_putchar(data[i]);
}

/* Бит 0 (0x01) регистра "Line Status" (смещение +5) означает
 * "в приёмном буфере есть непрочитанный байт" */
int serial_has_data(void) {
    return inb(COM1_PORT + 5) & 0x01;
}

char serial_read_char(void) {
    return (char) inb(COM1_PORT);
}
