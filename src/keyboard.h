/* keyboard.h — интерфейс драйвера PS/2-клавиатуры. */

#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);

/* Блокирующее чтение одного символа: ждёт, пока пользователь не
 * нажмёт клавишу (PS/2 или serial при -nographic). Возвращает ASCII
 * или специальные коды ниже. */
char keyboard_getchar(void);

/* Специальные "символы", которые может вернуть keyboard_getchar() */
#define KEY_BACKSPACE '\b'
#define KEY_ENTER     '\n'

/* Коды стрелок (значения вне обычного ASCII) */
#define KEY_UP        ((char)0x11)
#define KEY_DOWN      ((char)0x12)
#define KEY_LEFT      ((char)0x13)
#define KEY_RIGHT     ((char)0x14)

#endif
