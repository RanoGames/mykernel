/* process.h — запуск ELF-программы и возврат в ядро. */

#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

/* Загрузить ELF из буфера и выполнить. Возвращает код из sys_exit. */
int process_exec(const uint8_t* image, size_t size);

void process_save_kernel_context(uint32_t esp, uint32_t ebp, void* cont);
int process_last_exit_code(void);

#endif
void process_exit_to_kernel(int code);
