/* irq.c — IDT gates, PIC, isr_handler (в т.ч. syscall 128). */

#include "isr.h"
#include "idt.h"
#include "io.h"
#include "vga.h"
#include "syscall.h"

#define IDT_FLAGS     0x8E /* kernel interrupt gate */
#define IDT_FLAGS_USER 0xEE /* DPL=3 — int 0x80 из userspace */

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr128(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

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

    /* int 0x80 — системные вызовы */
    idt_set_gate(128, (uint32_t) isr128, 0x08, IDT_FLAGS_USER);
}

static const char* exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception"
};

void isr_handler(struct registers* regs) {
    if (regs->int_no == 128) {
        syscall_handler(regs);
        return;
    }

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

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20
#define ICW1_INIT    0x10
#define ICW1_ICW4    0x01
#define ICW4_8086    0x01

static void pic_remap(void) {
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
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

void irq_handler(struct registers* regs) {
    int irq_num = regs->int_no - 32;
    if (irq_handlers[irq_num] != 0)
        irq_handlers[irq_num](regs);
    if (irq_num >= 8)
        outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}
