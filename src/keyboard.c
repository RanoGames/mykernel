/* keyboard.c — PS/2 + serial */

#include "keyboard.h"
#include "isr.h"
#include "io.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#define KBD_DATA_PORT 0x60
#define SC_RELEASE_MASK 0x80
#define SC_EXTENDED     0xE0
#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_UP     0x48
#define SC_DOWN   0x50
#define SC_LEFT   0x4B
#define SC_RIGHT  0x4D

static const char scancode_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',
    0, '*',
    0, ' ',
    0,
};

static const char scancode_to_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?',
    0, '*',
    0, ' ',
    0,
};

static volatile int shift_pressed = 0;
static volatile int extended = 0;

#define KBD_BUFFER_SIZE 256
static volatile char kbd_buffer[KBD_BUFFER_SIZE];
static volatile size_t kbd_head = 0;
static volatile size_t kbd_tail = 0;

static void kbd_buffer_push(char c) {
    size_t next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next == kbd_tail)
        return;
    kbd_buffer[kbd_head] = c;
    kbd_head = next;
}

static void keyboard_irq_handler(struct registers* regs) {
    (void) regs;
    uint8_t scancode = inb(KBD_DATA_PORT);

    if (scancode == SC_EXTENDED) {
        extended = 1;
        return;
    }

    int is_extended = extended;
    extended = 0;

    if (scancode & SC_RELEASE_MASK) {
        uint8_t released = scancode & ~SC_RELEASE_MASK;
        if (!is_extended && (released == SC_LSHIFT || released == SC_RSHIFT))
            shift_pressed = 0;
        return;
    }

    if (is_extended) {
        switch (scancode) {
            case SC_UP:    kbd_buffer_push(KEY_UP);    break;
            case SC_DOWN:  kbd_buffer_push(KEY_DOWN);  break;
            case SC_LEFT:  kbd_buffer_push(KEY_LEFT);  break;
            case SC_RIGHT: kbd_buffer_push(KEY_RIGHT); break;
            default: break;
        }
        return;
    }

    if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
        shift_pressed = 1;
        return;
    }

    if (scancode >= 128)
        return;

    char c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    if (c != 0)
        kbd_buffer_push(c);
}

void keyboard_init(void) {
    irq_register_handler(1, keyboard_irq_handler);
}

int keyboard_trygetchar(char* out) {
    if (kbd_head != kbd_tail) {
        *out = kbd_buffer[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
        return 1;
    }
    if (serial_has_data()) {
        char c = serial_read_char();
        if (c == '\r') c = KEY_ENTER;
        else if (c == 0x7F) c = KEY_BACKSPACE;
        else if (c == 0x1B) {
            if (!serial_has_data()) return 0;
            char c2 = serial_read_char();
            if (c2 != '[' || !serial_has_data()) return 0;
            char c3 = serial_read_char();
            if (c3 == 'A') { *out = KEY_UP; return 1; }
            if (c3 == 'B') { *out = KEY_DOWN; return 1; }
            if (c3 == 'C') { *out = KEY_RIGHT; return 1; }
            if (c3 == 'D') { *out = KEY_LEFT; return 1; }
            return 0;
        }
        *out = c;
        return 1;
    }
    return 0;
}

char keyboard_getchar(void) {
    for (;;) {
        char c;
        if (keyboard_trygetchar(&c))
            return c;
        __asm__ volatile ("hlt");
    }
}
