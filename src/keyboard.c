/* keyboard.c — драйвер клавиатуры для стандартного PS/2-контроллера.
 *
 * Как это работает: при нажатии/отпускании клавиши контроллер
 * клавиатуры кладёт байт "скан-кода" в порт 0x60 и поднимает IRQ1.
 * Мы читаем этот байт в обработчике прерывания, переводим его в
 * обычный ASCII-символ по таблице (US-раскладка, "scan code set 1")
 * и складываем в собственный кольцевой буфер, откуда его потом
 * забирает shell функцией keyboard_getchar(). */

#include "keyboard.h"
#include "isr.h"
#include "io.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#define KBD_DATA_PORT 0x60

/* Если скан-код >= 0x80 — это код "клавиша отпущена" (break code),
 * а сам код клавиши = (скан-код - 0x80). Нам для простого ввода
 * текста интересны в основном нажатия, но отпускание Shift важно
 * отследить, чтобы вернуться к строчным буквам. */
#define SC_RELEASE_MASK 0x80

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36

/* Таблица перевода скан-кода в ASCII для обычного (без Shift) состояния.
 * Индекс — скан-код, значение — символ. 0 означает "не печатаемая клавиша"
 * (в этой простой версии драйвера мы её игнорируем). */
static const char scancode_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0 /*ctrl*/, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0 /*lshift*/, '\\','z','x','c','v','b','n','m',',','.','/',
    0 /*rshift*/, '*',
    0 /*alt*/, ' ' /*space*/,
    0 /*capslock*/,
    /* дальше идут F1-F10, NumLock, ScrollLock и т.д. — не обрабатываем */
};

/* То же самое, но при зажатом Shift */
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

/* Простой кольцевой буфер для символов, которые ещё не забрал shell.
 * volatile — буфер меняется из обработчика прерывания, поэтому
 * компилятор не должен кэшировать его значения в регистрах. */
#define KBD_BUFFER_SIZE 256
static volatile char kbd_buffer[KBD_BUFFER_SIZE];
static volatile size_t kbd_head = 0; /* куда пишем новый символ */
static volatile size_t kbd_tail = 0; /* откуда читаем следующий символ */

static void kbd_buffer_push(char c) {
    size_t next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next == kbd_tail)
        return; /* буфер переполнен — символ теряем (для учебного ядра ок) */
    kbd_buffer[kbd_head] = c;
    kbd_head = next;
}

/* Обработчик IRQ1, вызывается автоматически из irq.c при каждом
 * нажатии/отпускании клавиши */
static void keyboard_irq_handler(struct registers* regs) {
    (void) regs; /* параметр не нужен, но нужен для совпадения типа irq_handler_t */

    uint8_t scancode = inb(KBD_DATA_PORT);

    if (scancode & SC_RELEASE_MASK) {
        /* клавиша отпущена */
        uint8_t released = scancode & ~SC_RELEASE_MASK;
        if (released == SC_LSHIFT || released == SC_RSHIFT)
            shift_pressed = 0;
        return;
    }

    if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
        shift_pressed = 1;
        return;
    }

    if (scancode >= 128)
        return; /* за пределами нашей таблицы — игнорируем */

    char c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    if (c != 0)
        kbd_buffer_push(c);
}

void keyboard_init(void) {
    irq_register_handler(1, keyboard_irq_handler); /* IRQ1 = клавиатура */
}

char keyboard_getchar(void) {
    for (;;) {
        /* Источник 1: PS/2-клавиатура (обычное графическое окно QEMU).
         * Символы туда кладёт обработчик прерывания keyboard_irq_handler. */
        if (kbd_head != kbd_tail) {
            char c = kbd_buffer[kbd_tail];
            kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
            return c;
        }

        /* Источник 2: последовательный порт (режим "-nographic" — когда
         * графическое окно QEMU не используется вообще, а вы печатаете
         * прямо в терминал WSL). Мы не завели под serial отдельное
         * прерывание, поэтому проверяем его опросом (polling) —
         * достаточно, т.к. человек печатает всё равно не быстрее, чем
         * успевает выполниться этот цикл. */
        if (serial_has_data()) {
            char c = serial_read_char();

            /* Терминалы обычно шлют по Enter байт '\r' (0x0D), а не
             * '\n' — приводим к единому формату, который понимает shell */
            if (c == '\r')
                c = KEY_ENTER;
            /* Некоторые терминалы шлют Backspace как DEL (0x7F) вместо
             * привычного ASCII Backspace (0x08) — тоже нормализуем */
            else if (c == 0x7F)
                c = KEY_BACKSPACE;

            return c;
        }

        /* Ничего не пришло ни оттуда, ни оттуда — ждём следующего
         * прерывания (hlt экономнее пустого цикла while). Проснёмся
         * либо от клавиатуры, либо просто от таймера — и в следующей
         * итерации for(;;) снова проверим оба источника. */
        __asm__ volatile ("hlt");
    }
}
