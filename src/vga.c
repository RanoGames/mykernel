/* vga.c — text terminal: classic VGA 80x25 OR VBE framebuffer console */

#include "vga.h"
#include "io.h"
#include "serial.h"
#include "vbe.h"
#include "gfx.h"
#include <stdint.h>

#define VGA_ADDRESS 0xB8000
#define VGA_TEXT_W  80
#define VGA_TEXT_H  25
#define VGA_CTRL_PORT 0x3D4
#define VGA_DATA_PORT 0x3D5

#define FONT_W 8
#define FONT_H 16

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return (uint8_t)(fg | (bg << 4));
}
static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | ((uint16_t)color << 8);
}

static size_t terminal_row, terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

static int fb_mode;
static size_t term_cols = VGA_TEXT_W;
static size_t term_rows = VGA_TEXT_H;
static uint32_t fb_fg = 0x00FF66;
static uint32_t fb_bg = 0x000000;

static void hw_cursor(size_t x, size_t y) {
    if (fb_mode) return;
    uint16_t pos = (uint16_t)(y * VGA_TEXT_W + x);
    outb(VGA_CTRL_PORT, 0x0F);
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
    outb(VGA_CTRL_PORT, 0x0E);
    outb(VGA_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
    static const uint32_t map[16] = {
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF
    };
    fb_fg = map[color & 0xF];
    fb_bg = map[(color >> 4) & 0xF];
}

static void fb_draw_glyph(size_t cx, size_t cy, unsigned char ch, uint32_t fg, uint32_t bg) {
    const struct vbe_info* vi = vbe_get_info();
    if (!vi || !vi->active || !vi->lfb) return;
    if (cx >= term_cols || cy >= term_rows) return;
    const uint8_t* glyph = gfx_font_glyph(ch);
    int px = (int)(cx * FONT_W);
    int py = (int)(cy * FONT_H);
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            vbe_putpixel(px + col, py + row, color);
        }
    }
}

static void fb_fill_cell(size_t cx, size_t cy, uint32_t color) {
    const struct vbe_info* vi = vbe_get_info();
    if (!vi || !vi->active) return;
    vbe_fill_rect((int)(cx * FONT_W), (int)(cy * FONT_H), FONT_W, FONT_H, color);
}

static void fb_draw_cursor(void) {
    if (!fb_mode) return;
    const struct vbe_info* vi = vbe_get_info();
    if (!vi || !vi->active) return;
    int px = (int)(terminal_column * FONT_W);
    int py = (int)(terminal_row * FONT_H + FONT_H - 2);
    vbe_fill_rect(px, py, FONT_W, 2, fb_fg);
}

static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    if (fb_mode) {
        (void)color;
        fb_draw_glyph(x, y, (unsigned char)c, fb_fg, fb_bg);
        return;
    }
    terminal_buffer[y * VGA_TEXT_W + x] = vga_entry((unsigned char)c, color);
}

static void terminal_scroll(void) {
    if (fb_mode) {
        const struct vbe_info* vi = vbe_get_info();
        if (!vi || !vi->active || !vi->lfb) return;
        volatile uint32_t* fb = (volatile uint32_t*)(uintptr_t)vi->lfb;
        uint32_t pitch = vi->width;
        uint32_t line_px = (uint32_t)FONT_H * pitch;
        uint32_t rows_px = (uint32_t)(term_rows - 1) * FONT_H * pitch;
        for (uint32_t i = 0; i < rows_px; i++)
            fb[i] = fb[i + line_px];
        uint32_t start = (uint32_t)(term_rows - 1) * FONT_H * pitch;
        uint32_t end = (uint32_t)term_rows * FONT_H * pitch;
        for (uint32_t i = start; i < end && i < vi->width * vi->height; i++)
            fb[i] = fb_bg;
        terminal_row = term_rows - 1;
        return;
    }
    for (size_t y = 1; y < VGA_TEXT_H; y++)
        for (size_t x = 0; x < VGA_TEXT_W; x++)
            terminal_buffer[(y - 1) * VGA_TEXT_W + x] = terminal_buffer[y * VGA_TEXT_W + x];
    for (size_t x = 0; x < VGA_TEXT_W; x++)
        terminal_buffer[(VGA_TEXT_H - 1) * VGA_TEXT_W + x] = vga_entry(' ', terminal_color);
    terminal_row = VGA_TEXT_H - 1;
}

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    fb_fg = 0x55FF55;
    fb_bg = 0x000000;
    if (fb_mode && vbe_is_active()) {
        vbe_clear(fb_bg);
        term_cols = vbe_get_info()->width / FONT_W;
        term_rows = vbe_get_info()->height / FONT_H;
        if (term_cols < 40) term_cols = 40;
        if (term_rows < 15) term_rows = 15;
    } else {
        fb_mode = 0;
        term_cols = VGA_TEXT_W;
        term_rows = VGA_TEXT_H;
        terminal_buffer = (uint16_t*)VGA_ADDRESS;
        for (size_t y = 0; y < VGA_TEXT_H; y++)
            for (size_t x = 0; x < VGA_TEXT_W; x++)
                terminal_buffer[y * VGA_TEXT_W + x] = vga_entry(' ', terminal_color);
        terminal_show_cursor();
        hw_cursor(terminal_column, terminal_row);
    }
}

int terminal_enable_fb(int vbe_mode_id) {
    vbe_probe();
    if (vbe_set_mode(vbe_mode_id) != 0)
        return -1;
    fb_mode = 1;
    term_cols = vbe_get_info()->width / FONT_W;
    term_rows = vbe_get_info()->height / FONT_H;
    terminal_row = 0;
    terminal_column = 0;
    vbe_clear(fb_bg);
    return 0;
}

void terminal_disable_fb(void) {
    if (!fb_mode) return;
    fb_mode = 0;
    vbe_disable();
    term_cols = VGA_TEXT_W;
    term_rows = VGA_TEXT_H;
    terminal_buffer = (uint16_t*)VGA_ADDRESS;
    terminal_row = 0;
    terminal_column = 0;
}

int terminal_fb_active(void) { return fb_mode; }
size_t terminal_cols(void) { return term_cols; }
size_t terminal_rows(void) { return term_rows; }

void terminal_putchar(char c) {
    serial_putchar(c);
    if (c == '\n') {
        if (fb_mode) fb_fill_cell(terminal_column, terminal_row, fb_bg);
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\r') {
        terminal_column = 0;
    } else {
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        if (++terminal_column == term_cols) {
            terminal_column = 0;
            terminal_row++;
        }
    }
    if (terminal_row >= term_rows)
        terminal_scroll();
    if (fb_mode) fb_draw_cursor();
    else hw_cursor(terminal_column, terminal_row);
}

void terminal_backspace(void) {
    if (terminal_column > 0) {
        terminal_column--;
        terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        if (fb_mode) fb_draw_cursor();
        else hw_cursor(terminal_column, terminal_row);
    }
    serial_putchar('\b');
    serial_putchar(' ');
    serial_putchar('\b');
}

void terminal_hide_cursor(void) {
    if (fb_mode) return;
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 0x20);
}

void terminal_show_cursor(void) {
    if (fb_mode) { fb_draw_cursor(); return; }
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 13);
    outb(VGA_CTRL_PORT, 0x0B);
    outb(VGA_DATA_PORT, 14);
    hw_cursor(terminal_column, terminal_row);
}

void terminal_get_cursor(size_t* x, size_t* y) {
    if (x) *x = terminal_column;
    if (y) *y = terminal_row;
}

void terminal_set_cursor(size_t x, size_t y) {
    if (x >= term_cols) x = term_cols - 1;
    if (y >= term_rows) y = term_rows - 1;
    if (fb_mode) fb_fill_cell(terminal_column, terminal_row, fb_bg);
    terminal_column = x;
    terminal_row = y;
    if (fb_mode) fb_draw_cursor();
    else hw_cursor(terminal_column, terminal_row);
}

void terminal_move_left(size_t n) {
    while (n > 0 && terminal_column > 0) {
        if (fb_mode) fb_fill_cell(terminal_column, terminal_row, fb_bg);
        terminal_column--;
        n--;
    }
    if (fb_mode) fb_draw_cursor();
    else hw_cursor(terminal_column, terminal_row);
}

void terminal_move_right(size_t n) {
    while (n > 0 && terminal_column < term_cols - 1) {
        if (fb_mode) fb_fill_cell(terminal_column, terminal_row, fb_bg);
        terminal_column++;
        n--;
    }
    if (fb_mode) fb_draw_cursor();
    else hw_cursor(terminal_column, terminal_row);
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
    if (value == 0) { terminal_writestring("0"); return; }
    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
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
