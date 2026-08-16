/* timer.c — Programmable Interval Timer, IRQ0 */

#include "timer.h"
#include "isr.h"
#include "io.h"
#include "sched.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182u

static volatile uint32_t g_ticks;

static void timer_callback(struct registers* regs) {
    (void)regs;
    g_ticks++;
    sched_on_tick();
}

void timer_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor == 0) divisor = 1;

    outb(PIT_COMMAND, 0x36); /* ch0, lobyte/hibyte, mode 3 */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    g_ticks = 0;
    irq_register_handler(0, timer_callback);
}

uint32_t timer_ticks(void) {
    return g_ticks;
}
