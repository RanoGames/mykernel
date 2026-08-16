/* process.c — exec ELF: загрузка + вызов entry + возврат через sys_exit */

#include "process.h"
#include "elf.h"
#include "vga.h"
#include <stdint.h>

/* объявлено в syscall.c */
void process_save_kernel_context(uint32_t esp, void* cont);
int process_last_exit_code(void);
void process_exit_to_kernel(int code);

int process_exec(const uint8_t* image, size_t size) {
    uint32_t entry = 0;
    enum elf_result er = elf_load(image, size, &entry);
    if (er != ELF_OK) {
        terminal_writestring("ELF: ");
        terminal_writestring(elf_strerror(er));
        terminal_putchar('\n');
        return -1;
    }

    terminal_writestring("exec: entry=");
    terminal_write_hex(entry);
    terminal_putchar('\n');

    /* Сохраняем ESP и адрес продолжения; sys_exit прыгнет сюда */
    void* cont = &&user_done;
    uint32_t esp;
    __asm__ volatile ("mov %%esp, %0" : "=r"(esp));
    process_save_kernel_context(esp, cont);

    /* Стек для программы — в конце окна загрузки */
    uint32_t user_stack = ELF_LOAD_BASE + ELF_LOAD_MAX - 16;

    __asm__ volatile(
        "mov %0, %%esp\n"
        "jmp *%1\n"
        :
        : "r"(user_stack), "r"(entry)
        : "memory"
    );

user_done:
    return process_last_exit_code();
}
