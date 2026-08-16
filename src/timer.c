/* timer.c — Programmable Interval Timer (PIT), IRQ0 */

#include "timer.h"
#include "isr.h"
#include "io.h"
#include "sched.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182u

static volatile uint32_t g_ticks;
static uint32_t g_hz = 100;

static void timer_callback(struct registers* regs) {
    (void)regs;
    g_ticks++;
    sched_on_tick();
}

/* hz: типично 100 (10 ms/тик) или 1000 (1 ms). 0 → 100. */
void timer_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    if (hz > 1000) hz = 1000; /* не гоняем PIT слишком часто */

    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor < 1) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    /* Mode 3 (square wave), channel 0, access lobyte/hibyte */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    g_ticks = 0;
    g_hz = hz;
    irq_register_handler(0, timer_callback);
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t ticks_needed = (g_hz * ms) / 1000;
    if (ticks_needed == 0)
        ticks_needed = 1;
    uint32_t target = g_ticks + ticks_needed;
    while (g_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

uint32_t timer_ticks(void) {
    return g_ticks;
}

uint32_t timer_hz(void) {
    return g_hz;
}
