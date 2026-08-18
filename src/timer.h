#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(uint32_t hz);
void timer_sleep_ms(uint32_t ms);
void timer_busy_ms(uint32_t ms);
uint32_t timer_ticks(void);
uint32_t timer_hz(void);
int timer_is_alive(void);

#endif
