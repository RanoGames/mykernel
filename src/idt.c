/* idt.c — заполнение и загрузка таблицы прерываний (IDT). */

#include "idt.h"

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

/* Объявлена в idt_load.s — выполняет саму инструкцию lidt,
 * которую нельзя нормально сделать из чистого C */
extern void idt_load(uint32_t idt_ptr_addr);

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = handler & 0xFFFF;
    idt[num].base_high = (handler >> 16) & 0xFFFF;
    idt[num].sel        = sel;
    idt[num].always0    = 0;
    idt[num].flags      = flags;
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t) &idt;

    /* обнуляем таблицу — незарегистрированные прерывания будут "пустыми" */
    for (int i = 0; i < IDT_ENTRIES; i++)
        idt_set_gate(i, 0, 0, 0);

    idt_load((uint32_t) &idtp);
}
