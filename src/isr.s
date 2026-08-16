/* isr.s — низкоуровневые "заглушки" обработчиков прерываний.
 *
 * Почему нельзя просто указать в IDT адрес C-функции напрямую:
 * когда прерывание срабатывает, процессор сам кладёт в стек часть
 * регистров, но не все, и не знает, что мы хотим сохранить остальные
 * регистры перед вызовом C-кода и восстановить после (иначе мы
 * сломаем то, что выполнялось до прерывания). Поэтому у каждого
 * прерывания — маленькая ассемблерная заглушка, которая:
 *   1. сохраняет регистры,
 *   2. вызывает общий обработчик на C,
 *   3. восстанавливает регистры,
 *   4. возвращает управление инструкцией iret.
 *
 * Обработчики исключений процессора: номера 0-31 (деление на 0,
 * page fault и т.д.) — они нужны, чтобы ядро не падало в "чёрную
 * дыру" при ошибке, а хотя бы могло вывести сообщение.
 *
 * Обработчики аппаратных прерываний (IRQ 0-15) переносим на номера
 * 32-47 таблицы IDT (это делает irq.c, перепрограммируя PIC) —
 * иначе они конфликтуют с исключениями процессора 0-31. */

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    push $0        /* этому исключению процессор не кладёт код ошибки — кладём 0 сами, чтобы стек был одинаковой формы для всех обработчиков */
    push $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    push $\num     /* код ошибки этому исключению процессор уже положил сам */
    jmp isr_common_stub
.endm

.macro IRQ num, remapped_num
.global irq\num
irq\num:
    cli
    push $0
    push $\remapped_num
    jmp irq_common_stub
.endm

/* Исключения процессора 0-31. Часть из них кладёт код ошибки в стек
 * сама (ERRCODE), часть — нет (NOERRCODE), это фиксировано в архитектуре x86. */
ISR_NOERRCODE 0    /* Division by zero */
ISR_NOERRCODE 1    /* Debug */
ISR_NOERRCODE 2    /* Non maskable interrupt */
ISR_NOERRCODE 3    /* Breakpoint */
ISR_NOERRCODE 4    /* Overflow */
ISR_NOERRCODE 5    /* Bound range exceeded */
ISR_NOERRCODE 6    /* Invalid opcode */
ISR_NOERRCODE 7    /* Device not available */
ISR_ERRCODE   8    /* Double fault */
ISR_NOERRCODE 9    /* Coprocessor segment overrun */
ISR_ERRCODE   10   /* Invalid TSS */
ISR_ERRCODE   11   /* Segment not present */
ISR_ERRCODE   12   /* Stack-segment fault */
ISR_ERRCODE   13   /* General protection fault */
ISR_ERRCODE   14   /* Page fault */
ISR_NOERRCODE 15   /* зарезервировано */
ISR_NOERRCODE 16   /* x87 floating point exception */
ISR_ERRCODE   17   /* Alignment check */
ISR_NOERRCODE 18   /* Machine check */
ISR_NOERRCODE 19   /* SIMD floating point exception */

/* Аппаратные IRQ 0-15 (таймер, клавиатура и т.д.), после перемаппинга
 * PIC поставлены в IDT под номера 32-47 */
IRQ 0, 32   /* таймер (PIT) */
IRQ 1, 33   /* клавиатура */
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/* Общая часть для исключений: сохраняем регистры, зовём C-функцию
 * isr_handler(struct registers*), восстанавливаем регистры, выходим */
.extern isr_handler
isr_common_stub:
    pusha              /* сохранить eax,ecx,edx,ebx,esp,ebp,esi,edi одной инструкцией */

    mov %ds, %ax
    push %eax          /* сохранить сегмент данных */

    mov $0x10, %ax     /* переключиться на сегмент данных ядра (0x10 — из GDT, которую нам дал GRUB) */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp          /* передаём C-функции указатель на структуру со всеми сохранёнными регистрами */
    call isr_handler
    add $4, %esp

    pop %eax           /* восстановить исходный сегмент данных */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp       /* убрать со стека код ошибки и номер прерывания, которые сами туда клали */
    sti
    iret               /* специальная инструкция возврата из прерывания */

/* То же самое, но для аппаратных IRQ — вызывает irq_handler вместо isr_handler */
.extern irq_handler
irq_common_stub:
    pusha

    mov %ds, %ax
    push %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp
    call irq_handler
    add $4, %esp

    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp
    sti
    iret
