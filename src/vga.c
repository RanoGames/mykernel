/* vga.c — текстовый терминал VGA + дублирование вывода в COM1. */

#include "vga.h"
#include "io.h"
#include "serial.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

#define VGA_CTRL_PORT 0x3D4
#define VGA_DATA_PORT 0x3D5

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | ((uint16_t) color << 8);
}

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

static void update_cursor(size_t x, size_t y) {
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);

    outb(VGA_CTRL_PORT, 0x0F);
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));

    outb(VGA_CTRL_PORT, 0x0E);
    outb(VGA_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_buffer = (uint16_t*) VGA_ADDRESS;

    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            terminal_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);

    terminal_show_cursor();
    update_cursor(terminal_column, terminal_row);
}

static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    terminal_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
}

static void terminal_scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            terminal_buffer[(y - 1) * VGA_WIDTH + x] = terminal_buffer[y * VGA_WIDTH + x];

    for (size_t x = 0; x < VGA_WIDTH; x++)
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);

    terminal_row = VGA_HEIGHT - 1;
}

void terminal_putchar(char c) {
    /* Дублируем вывод в COM1 (для -nographic) */
    serial_putchar(c);

    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else {
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        if (++terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }

    if (terminal_row == VGA_HEIGHT)
        terminal_scroll();

    update_cursor(terminal_column, terminal_row);
}

void terminal_backspace(void) {
    if (terminal_column == 0)
        return;

    terminal_column--;
    terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
    update_cursor(terminal_column, terminal_row);

    /* В serial/терминале символ сам не исчезает — нужно явно:
     * назад, пробел (стереть), снова назад. Без этого в -nographic
     * Backspace «не работает», хотя буфер shell уже правильный. */
    serial_putchar('\b');
    serial_putchar(' ');
    serial_putchar('\b');
}

void terminal_hide_cursor(void) {
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 0x20);
}

void terminal_show_cursor(void) {
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 13);
    outb(VGA_CTRL_PORT, 0x0B);
    outb(VGA_DATA_PORT, 14);
    update_cursor(terminal_column, terminal_row);
}

void terminal_get_cursor(size_t* x, size_t* y) {
    if (x) *x = terminal_column;
    if (y) *y = terminal_row;
}

void terminal_set_cursor(size_t x, size_t y) {
    if (x >= VGA_WIDTH) x = VGA_WIDTH - 1;
    if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;
    terminal_column = x;
    terminal_row = y;
    update_cursor(terminal_column, terminal_row);
}

void terminal_move_left(size_t n) {
    while (n > 0 && terminal_column > 0) {
        terminal_column--;
        n--;
    }
    update_cursor(terminal_column, terminal_row);
}

void terminal_move_right(size_t n) {
    while (n > 0 && terminal_column < VGA_WIDTH - 1) {
        terminal_column++;
        n--;
    }
    update_cursor(terminal_column, terminal_row);
}

void terminal_putchar_at_cursor(char c) {
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
}

void terminal_writestring(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++)
        terminal_putchar(data[i]);
}

void terminal_write_uint(uint32_t value) {
    char buf[11];
    int i = 10;
    buf[10] = '\0';

    if (value == 0) {
        terminal_writestring("0");
        return;
    }

    while (value > 0 && i > 0) {
        buf[--i] = '0' + (value % 10);
        value /= 10;
    }
    terminal_writestring(&buf[i]);
}

void terminal_write_int(int32_t value) {
    if (value < 0) {
        terminal_writestring("-");
        terminal_write_uint((uint32_t)(-(int64_t)value));
    } else {
        terminal_write_uint((uint32_t)value);
    }
}

void terminal_write_hex(uint32_t value) {
    const char* hex_digits = "0123456789ABCDEF";
    char buf[9];
    buf[8] = '\0';
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex_digits[value & 0xF];
        value >>= 4;
    }
    terminal_writestring("0x");
    terminal_writestring(buf);
}
