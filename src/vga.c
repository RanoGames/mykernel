/* vga.c — реализация текстового терминала поверх видеопамяти VGA (0xB8000).
 * Подробные комментарии про формат видеопамяти были в первой версии
 * kernel.c — здесь тот же принцип, просто вынесено в отдельный модуль. */

#include "vga.h"
#include "io.h"
#include "serial.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* Порты видеоконтроллера (CRT Controller) для управления АППАРАТНЫМ
 * мигающим курсором. Это отдельная вещь от того, что мы сами рисуем
 * в видеопамяти — курсор реализован на уровне самой видеокарты, и
 * у него своя позиция, за которой нужно следить отдельно. Если её
 * не обновлять, курсор так и останется там, где был изначально
 * (обычно 0,0), сколько бы текста мы ни печатали. */
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

/* Переместить аппаратный курсор в позицию (x, y).
 * Позиция передаётся видеокарте как ОДНО число — индекс ячейки от
 * начала экрана (y * ширина + x), а не отдельно x и y. Индекс 16-битный,
 * поэтому пишем его двумя байтами через два разных регистра CRTC:
 * 0x0E — старший байт, 0x0F — младший байт. */
static void update_cursor(size_t x, size_t y) {
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);

    outb(VGA_CTRL_PORT, 0x0F);                  /* выбираем регистр "младший байт позиции курсора" */
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));

    outb(VGA_CTRL_PORT, 0x0E);                  /* выбираем регистр "старший байт позиции курсора" */
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

    /* Настоящий GRUB (в отличие от упрощённого загрузчика QEMU при
     * "-kernel") может оставить аппаратный курсор ВЫКЛЮЧЕННЫМ — он
     * прячет его на время своей работы (меню, загрузка) и не обязан
     * включать обратно. Раньше мы только двигали позицию курсора
     * (update_cursor), но никогда явно не снимали флаг "курсор скрыт" —
     * из-за этого под настоящим GRUB курсор не был виден, хотя под
     * `make run` всё работало. terminal_show_cursor() явно включает
     * курсор и задаёт его форму (сканлинии 13-14 — обычное подчёркивание),
     * так что теперь это не зависит от того, что оставил загрузчик. */
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
    /* Дублируем вывод в последовательный порт (COM1). Это НЕ мешает
     * обычному VGA-выводу — просто параллельная копия того же текста.
     * Благодаря этому один и тот же код (shell, kernel.c и т.д.)
     * одинаково виден что в графическом окне QEMU, что в обычном
     * терминале при запуске "qemu-system-i386 -kernel ... -nographic",
     * если у вас не получается увидеть графическое окно. */
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

/* Стереть последний символ: подвинуть курсор назад и записать пробел.
 * Используется в shell при обработке клавиши Backspace.
 * Не переходит на предыдущую строку — простая реализация, этого
 * достаточно для однострочного ввода команд. */
void terminal_backspace(void) {
    if (terminal_column == 0)
        return; /* в начале строки стирать нечего (упрощённо) */
    terminal_column--;
    terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
    update_cursor(terminal_column, terminal_row);
}

/* Скрыть аппаратный курсор: устанавливаем бит 5 в регистре 0x0A CRTC
 * (Cursor Start Register) — это стандартный способ его "выключить" */
void terminal_hide_cursor(void) {
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 0x20); /* бит 5 = 1 -> курсор невидим */
}

/* Показать курсор обратно: обычные значения "начало/конец сканлинии"
 * для курсора-подчёркивания (13 и 14 из 16 строк символа) */
void terminal_show_cursor(void) {
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, 13);
    outb(VGA_CTRL_PORT, 0x0B);
    outb(VGA_DATA_PORT, 14);
    update_cursor(terminal_column, terminal_row);
}

void terminal_writestring(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++)
        terminal_putchar(data[i]);
}

/* Вывод числа в десятичном виде */
void terminal_write_uint(uint32_t value) {
    char buf[11]; /* максимум 10 цифр для 32-бит + '\0' */
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

/* Вывод ЗНАКОВОГО числа: печатаем минус (если нужно), а дальше
 * переиспользуем уже написанную terminal_write_uint для модуля числа */
void terminal_write_int(int32_t value) {
    if (value < 0) {
        terminal_writestring("-");
        /* Осторожно: -value для INT32_MIN переполнил бы int32_t, поэтому
         * считаем беззнаково через приведение типа, а не через "-value" напрямую */
        terminal_write_uint((uint32_t)(-(int64_t)value));
    } else {
        terminal_write_uint((uint32_t)value);
    }
}

/* Вывод числа в шестнадцатеричном виде (например, для адресов) */
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
