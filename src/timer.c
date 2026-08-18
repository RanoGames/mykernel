/* timer.c — PIT channel 0 → IRQ0 → timer_callback */

#include "timer.h"
#include "isr.h"
#include "io.h"
#include "sched.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182u

static volatile uint32_t g_ticks;
static uint32_t g_hz = 100;

/*
 * IRQ0 handler (called from irq_handler after PIC delivered vector 32).
 * Keep short: increment tick, optional schedule.
 */
static void timer_callback(struct registers* regs) {
    (void)regs;
    g_ticks++;
    sched_on_tick();
}

/*
 * Program PIT to `hz` (typically 100) and enable IRQ0 on the PIC.
 *
 * Mode 3 (square wave), access lobyte/hibyte, channel 0.
 * divisor = 1193182 / hz  (input clock of the 8253/8254).
 */
void timer_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    if (hz > 1000) hz = 1000;

    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor < 1) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    outb(PIT_COMMAND, 0x36); /* ch0, lobyte/hibyte, mode 3, binary */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    g_ticks = 0;
    g_hz = hz;

    irq_register_handler(0, timer_callback);
    irq_unmask(0); /* allow IRQ0 through PIC master */
    /* IF (sti) is enabled later in kernel_main */
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t start = g_ticks;
    uint32_t need = (g_hz * ms) / 1000;
    if (need == 0) need = 1;

    uint32_t spins = 0;
    while ((g_ticks - start) < need) {
        if (g_ticks == start) {
            spins++;
            if (spins > 1000) {
                timer_busy_ms(ms);
                return;
            }
        }
        __asm__ volatile ("hlt");
    }
}

void timer_busy_ms(uint32_t ms) {
    for (uint32_t m = 0; m < ms; m++) {
        for (volatile uint32_t i = 0; i < 20000u; i++)
            __asm__ volatile ("pause");
    }
}

uint32_t timer_ticks(void) {
    return g_ticks;
}

uint32_t timer_hz(void) {
    return g_hz;
}

int timer_is_alive(void) {
    uint32_t a = g_ticks;
    timer_busy_ms(20);
    return g_ticks != a;
}
