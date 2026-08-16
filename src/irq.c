/* irq.c — "склеивает" вместе idt.c и isr.s:
 *   1. регистрирует адреса всех заглушек из isr.s в таблице IDT;
 *   2. перепрограммирует PIC (контроллер прерываний), чтобы IRQ
 *      не конфликтовали с исключениями процессора;
 *   3. при срабатывании IRQ вызывает нужный драйвер (например,
 *      клавиатуру) и сообщает PIC, что прерывание обработано (EOI). */

#include "isr.h"
#include "idt.h"
#include "io.h"
#include "vga.h"

/* Флаги дескриптора IDT: present=1, dpl=00, тип=32-битный interrupt gate (0xE) */
#define IDT_FLAGS 0x8E

/* Объявляем все 32 заглушки исключений и 16 заглушек IRQ из isr.s */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

/* Таблица драйверов, подписавшихся на конкретный IRQ (0..15). NULL — никто не подписан */
static irq_handler_t irq_handlers[16] = { 0 };

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}

void isr_install(void) {
    idt_set_gate(0,  (uint32_t) isr0,  0x08, IDT_FLAGS);
    idt_set_gate(1,  (uint32_t) isr1,  0x08, IDT_FLAGS);
    idt_set_gate(2,  (uint32_t) isr2,  0x08, IDT_FLAGS);
    idt_set_gate(3,  (uint32_t) isr3,  0x08, IDT_FLAGS);
    idt_set_gate(4,  (uint32_t) isr4,  0x08, IDT_FLAGS);
    idt_set_gate(5,  (uint32_t) isr5,  0x08, IDT_FLAGS);
    idt_set_gate(6,  (uint32_t) isr6,  0x08, IDT_FLAGS);
    idt_set_gate(7,  (uint32_t) isr7,  0x08, IDT_FLAGS);
    idt_set_gate(8,  (uint32_t) isr8,  0x08, IDT_FLAGS);
    idt_set_gate(9,  (uint32_t) isr9,  0x08, IDT_FLAGS);
    idt_set_gate(10, (uint32_t) isr10, 0x08, IDT_FLAGS);
    idt_set_gate(11, (uint32_t) isr11, 0x08, IDT_FLAGS);
    idt_set_gate(12, (uint32_t) isr12, 0x08, IDT_FLAGS);
    idt_set_gate(13, (uint32_t) isr13, 0x08, IDT_FLAGS);
    idt_set_gate(14, (uint32_t) isr14, 0x08, IDT_FLAGS);
    idt_set_gate(15, (uint32_t) isr15, 0x08, IDT_FLAGS);
    idt_set_gate(16, (uint32_t) isr16, 0x08, IDT_FLAGS);
    idt_set_gate(17, (uint32_t) isr17, 0x08, IDT_FLAGS);
    idt_set_gate(18, (uint32_t) isr18, 0x08, IDT_FLAGS);
    idt_set_gate(19, (uint32_t) isr19, 0x08, IDT_FLAGS);
}

/* Названия исключений — для вывода на экран при их срабатывании */
static const char* exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception"
};

/* Общий C-обработчик всех исключений процессора (вызывается из isr.s).
 * Пока делаем просто: выводим сообщение и останавливаем систему —
 * дальше можно заменить на что-то умнее (например, вывод регистров,
 * попытку восстановления и т.п.) */
void isr_handler(struct registers* regs) {
    terminal_writestring("\n[EXCEPTION] ");
    if (regs->int_no < 20)
        terminal_writestring(exception_messages[regs->int_no]);
    else
        terminal_writestring("Unknown");
    terminal_writestring(" (int ");
    terminal_write_uint(regs->int_no);
    terminal_writestring(", err ");
    terminal_write_uint(regs->err_code);
    terminal_writestring(") -- system halted\n");

    /* Необработанное исключение — безопаснее остановить систему,
     * чем продолжать выполнение в непонятном состоянии */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* --- Программируемый контроллер прерываний (PIC 8259) --- */

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20 /* команда "конец прерывания" */

#define ICW1_INIT    0x10
#define ICW1_ICW4    0x01
#define ICW4_8086    0x01

/* По умолчанию PIC шлёт IRQ0-7 как прерывания 0x08-0x0F и IRQ8-15
 * как 0x70-0x77 — это конфликтует с исключениями процессора 0-31.
 * Поэтому "перемаппиваем" PIC, чтобы IRQ0-15 приходили как
 * прерывания 32-47 (offset1=0x20, offset2=0x28). */
static void pic_remap(void) {
    /* ВАЖНО: раньше здесь читались текущие маски (inb) и потом
     * восстанавливались как есть. Это была ошибка: настоящий GRUB
     * (в отличие от упрощённого загрузчика QEMU при "-kernel") перед
     * передачей управления ядру по конвенции МАСКИРУЕТ ВСЕ прерывания
     * (записывает 0xFF в оба PIC), чтобы не засыпать ядро прерываниями
     * до того, как оно готово их принимать. Восстановление такой маски
     * "как есть" означало, что под настоящим GRUB клавиатура и все
     * остальные IRQ оставались заблокированы навсегда — именно поэтому
     * `make run` (упрощённый загрузчик QEMU, маски там обычно уже
     * разрешающие) работал, а `make run-iso` (настоящий GRUB) — нет.
     * Решение: явно разрешаем нужные нам IRQ сами, не полагаясь на то,
     * что оставил загрузчик. */

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, 0x20); /* смещение для ведущего PIC: прерывания начнутся с 32 */
    io_wait();
    outb(PIC2_DATA, 0x28); /* смещение для ведомого PIC: с 40 */
    io_wait();

    outb(PIC1_DATA, 0x04); /* сказать ведущему PIC, что к нему подключён ведомый через IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02); /* сказать ведомому PIC его "номер каскада" */
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Явно разрешаем прерывания сами (0 = разрешено, 1 = замаскировано).
     * Разрешаем всё — обработчик неизвестного IRQ (irq_handler в irq.c)
     * и так безопасно ничего не делает, если для него не зарегистрирован
     * драйвер, поэтому маскировать "на всякий случай" не нужно. */
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

void irq_install(void) {
    pic_remap();

    idt_set_gate(32, (uint32_t) irq0,  0x08, IDT_FLAGS);
    idt_set_gate(33, (uint32_t) irq1,  0x08, IDT_FLAGS);
    idt_set_gate(34, (uint32_t) irq2,  0x08, IDT_FLAGS);
    idt_set_gate(35, (uint32_t) irq3,  0x08, IDT_FLAGS);
    idt_set_gate(36, (uint32_t) irq4,  0x08, IDT_FLAGS);
    idt_set_gate(37, (uint32_t) irq5,  0x08, IDT_FLAGS);
    idt_set_gate(38, (uint32_t) irq6,  0x08, IDT_FLAGS);
    idt_set_gate(39, (uint32_t) irq7,  0x08, IDT_FLAGS);
    idt_set_gate(40, (uint32_t) irq8,  0x08, IDT_FLAGS);
    idt_set_gate(41, (uint32_t) irq9,  0x08, IDT_FLAGS);
    idt_set_gate(42, (uint32_t) irq10, 0x08, IDT_FLAGS);
    idt_set_gate(43, (uint32_t) irq11, 0x08, IDT_FLAGS);
    idt_set_gate(44, (uint32_t) irq12, 0x08, IDT_FLAGS);
    idt_set_gate(45, (uint32_t) irq13, 0x08, IDT_FLAGS);
    idt_set_gate(46, (uint32_t) irq14, 0x08, IDT_FLAGS);
    idt_set_gate(47, (uint32_t) irq15, 0x08, IDT_FLAGS);
}

/* Общий C-обработчик всех аппаратных прерываний (вызывается из isr.s).
 * regs->int_no здесь уже "перемаппленный" номер 32-47, поэтому
 * реальный номер IRQ = int_no - 32. */
void irq_handler(struct registers* regs) {
    int irq_num = regs->int_no - 32;

    if (irq_handlers[irq_num] != 0) {
        irq_handlers[irq_num](regs);
    }

    /* Обязательно сообщаем PIC, что прерывание обработано (EOI),
     * иначе он больше не пришлёт новых прерываний этого и более
     * низкого приоритета. Если прерывание пришло от ведомого PIC
     * (IRQ 8-15) — нужно послать EOI обоим контроллерам. */
    if (irq_num >= 8)
        outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}
