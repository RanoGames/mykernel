/* vga.h — интерфейс простого "терминала" на текстовом VGA-экране. */

#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

void terminal_initialize(void);
void terminal_setcolor(uint8_t color);
void terminal_putchar(char c);
void terminal_writestring(const char* data);
void terminal_backspace(void);

void terminal_hide_cursor(void);
void terminal_show_cursor(void);

/* Управление позицией курсора (для редактирования строки в shell) */
void terminal_get_cursor(size_t* x, size_t* y);
void terminal_set_cursor(size_t x, size_t y);
void terminal_move_left(size_t n);
void terminal_move_right(size_t n);
void terminal_putchar_at_cursor(char c);

void terminal_write_uint(uint32_t value);
void terminal_write_int(int32_t value);
void terminal_write_hex(uint32_t value);

#endif
