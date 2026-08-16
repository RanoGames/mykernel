/* mlang.h — крошечный язык MyLang внутри ядра */

#ifndef MLANG_H
#define MLANG_H

/* Выполнить одну строку. 0 = ок, -1 = ошибка */
int mlang_exec_line(const char* line);

/* Выполнить весь текст (несколько строк). */
int mlang_exec_script(const char* text);

/* Сброс переменных */
void mlang_reset(void);

#endif
