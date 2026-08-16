/* keyboard.h — интерфейс драйвера PS/2-клавиатуры. */

#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);

/* Блокирующее чтение одного символа: "спит" (крутится в hlt),
 * пока пользователь не нажмёт клавишу, соответствующую печатаемому
 * символу. Возвращает ASCII-код символа, либо специальные коды
 * ниже для служебных клавиш. */
char keyboard_getchar(void);

/* Специальные "символы", которые может вернуть keyboard_getchar() */
#define KEY_BACKSPACE '\b'
#define KEY_ENTER     '\n'

#endif
