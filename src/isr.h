/* isr.h — общая структура, в которую isr.s складывает состояние
 * регистров перед вызовом C-обработчика, плюс объявления обработчиков. */

#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* Порядок полей должен ТОЧНО соответствовать порядку, в котором
 * isr.s кладёт регистры в стек (см. isr_common_stub/irq_common_stub) */
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* от pusha, в обратном порядке */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss; /* это кладёт сам процессор при прерывании */
};

/* Тип функции-обработчика конкретного IRQ, который можно зарегистрировать */
typedef void (*irq_handler_t)(struct registers*);

void isr_install(void);        /* регистрирует обработчики исключений 0-31 в IDT */
void irq_install(void);        /* перепрограммирует PIC и регистрирует обработчики IRQ 32-47 */

/* Позволяет драйверам (например, клавиатуре) подписаться на свой IRQ */
void irq_register_handler(uint8_t irq, irq_handler_t handler);

void irq_mask(uint8_t irq);
void irq_unmask(uint8_t irq);
void pic_init_masks(void);
uint16_t irq_get_mask(void); /* bit i set = IRQ i masked */

#endif
