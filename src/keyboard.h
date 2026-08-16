/* keyboard.h — интерфейс драйвера PS/2-клавиатуры. */

#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);

char keyboard_getchar(void);

/* Неблокирующее чтение: 1 и *out=символ, 0 если пусто */
int keyboard_trygetchar(char* out);

#define KEY_BACKSPACE '\b'
#define KEY_ENTER     '\n'
#define KEY_UP        ((char)0x11)
#define KEY_DOWN      ((char)0x12)
#define KEY_LEFT      ((char)0x13)
#define KEY_RIGHT     ((char)0x14)

#endif
