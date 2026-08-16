/* timer.h — PIT (IRQ0) */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* PIT init. hz usually 100 (tick = 10 ms). */
void timer_init(uint32_t hz);

uint32_t timer_ticks(void);
uint32_t timer_hz(void);
void timer_sleep_ms(uint32_t ms);

#endif
