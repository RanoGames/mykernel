/* shell.h — простая командная оболочка ядра. */

#ifndef SHELL_H
#define SHELL_H

/* Запускает бесконечный цикл: печатать приглашение, читать строку,
 * выполнять команду. Не возвращает управление. */
void shell_run(void);
void shell_execute_command(const char* line);

#endif
