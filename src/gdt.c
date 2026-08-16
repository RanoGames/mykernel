/* gdt.c — заполнение собственной таблицы дескрипторов сегментов (GDT).
 *
 * Делаем классическую "плоскую" модель памяти (flat model): всего
 * 3 сегмента (плюс обязательный нулевой), каждый покрывает ВСЮ
 * адресуемую память (база 0, лимит 4 ГБ) — то есть сегменты нужны
 * чисто формально, для совместимости с архитектурой x86, а реальная
 * защита памяти (кто куда может писать) будет позже через paging.
 *
 *   0x00 — null-дескриптор (обязателен по архитектуре, не используется)
 *   0x08 — сегмент кода ядра (kernel code)
 *   0x10 — сегмент данных ядра (kernel data)
 *
 * Именно селекторы 0x08 (код) и 0x10 (данные) — это то, что мы уже
 * "жёстко" предполагаем в других местах кода (idt.c/irq.c при вызове
 * idt_set_gate(..., 0x08, ...), и в isr.s при "mov $0x10, %ax"). Раньше
 * это предположение опиралось на GDT, унаследованную от загрузчика —
 * теперь это наша СОБСТВЕННАЯ GDT с теми же номерами селекторов, но
 * под полным контролем ядра. */

#include "gdt.h"
#include <stdint.h>

/* Один дескриptor сегмента — 8 байт в специфичном для x86 формате.
 * Поля разбиты так исторически (для совместимости с 286-м процессором),
 * поэтому база и лимит "разрезаны" на несколько кусков. */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity; /* старшие 4 бита лимита + флаги гранулярности */
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define GDT_ENTRIES 3

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdtp;

/* Объявлена в gdt_flush.s — загружает GDT инструкцией lgdt и
 * перезагружает сегментные регистры, чтобы они начали использовать
 * НАШУ новую таблицу вместо старой (унаследованной от загрузчика) */
extern void gdt_flush(uint32_t gdt_ptr_addr);

/* Заполняет один дескриптор сегмента.
 * base/limit — начало и размер сегмента (у нас всегда 0 и 4ГБ -> "плоская" модель).
 * access     — байт прав доступа (тип сегмента, кольцо привилегий, present-бит).
 * gran       — байт гранулярности (единицы измерения лимита: байты или страницы по 4КБ). */
static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle  = (base >> 16) & 0xFF;
    gdt[num].base_high     = (base >> 24) & 0xFF;

    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].granularity  = (limit >> 16) & 0x0F;

    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access        = access;
}

void gdt_init(void) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t) &gdt;

    /* Нулевой дескриптор — обязателен по архитектуре x86, никогда не используется */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* Сегмент кода ядра: база 0, лимит 0xFFFFF (в сочетании с флагом
     * гранулярности "по страницам" 4КБ это даёт полные 4ГБ),
     * access = 0x9A -> present=1, кольцо привилегий=0 (ядро),
     *          тип=code segment, readable
     * gran   = 0xCF -> гранулярность по страницам (4КБ) + 32-битный режим */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Сегмент данных ядра: то же самое, но access = 0x92 -> data segment, writable */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    gdt_flush((uint32_t) &gdtp);
}
