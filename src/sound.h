/* sound.h — PC Speaker через PIT channel 2 */

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Частота в Гц (примерно 20..20000; для спикера лучше 100..3000) */
void sound_play(uint32_t freq_hz);
void sound_stop(void);

/* Пищалка на duration_ms миллисекунд (ждёт по timer_ticks) */
void sound_beep(uint32_t freq_hz, uint32_t duration_ms);

/* Короткие пресеты */
void sound_beep_ok(void);
void sound_beep_err(void);
void sound_beep_coin(void);

#endif
