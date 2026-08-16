/* timer.h — PIT (IRQ0) */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Инициализация PIT. hz обычно 100 (тик = 10 мс). */
void timer_init(uint32_t hz);

uint32_t timer_ticks(void);
uint32_t timer_hz(void);
void timer_sleep_ms(uint32_t ms);

#endif
