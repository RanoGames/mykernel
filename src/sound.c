/* sound.c — звук PC Speaker через PIT channel 2
 *
 * Канал 0 PIT уже занят системным таймером (timer.c).
 * Канал 2 подключен к динамику; gate/data — порт 0x61.
 */

#include "sound.h"
#include "io.h"
#include "timer.h"
#include <stdint.h>

#define PIT_CH2      0x42
#define PIT_CMD      0x43
#define PIT_BASE_HZ  1193182u
#define SPEAKER_PORT 0x61

void sound_play(uint32_t freq_hz) {
    if (freq_hz < 20u)
        freq_hz = 20u;
    if (freq_hz > 20000u)
        freq_hz = 20000u;

    uint32_t div = PIT_BASE_HZ / freq_hz;
    if (div == 0)
        div = 1;
    if (div > 65535u)
        div = 65535u;

    /* ch2, lobyte/hibyte, mode 3 (square wave), binary */
    outb(PIT_CMD, 0xB6);
    outb(PIT_CH2, (uint8_t)(div & 0xFF));
    outb(PIT_CH2, (uint8_t)((div >> 8) & 0xFF));

    /* bit0 = timer2 gate, bit1 = speaker data */
    uint8_t t = inb(SPEAKER_PORT);
    if ((t & 3) != 3)
        outb(SPEAKER_PORT, t | 3);
}

void sound_stop(void) {
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & 0xFC);
}

void sound_beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (duration_ms == 0)
        return;

    sound_play(freq_hz);

    /* timer 100 Hz → 1 tick ≈ 10 ms */
    uint32_t ticks = duration_ms / 10u;
    if (ticks == 0)
        ticks = 1;

    uint32_t start = timer_ticks();
    while (timer_ticks() - start < ticks) {
        __asm__ volatile ("hlt");
    }

    sound_stop();
}

void sound_beep_ok(void) {
    sound_beep(880, 80);
}

void sound_beep_err(void) {
    sound_beep(220, 150);
}

void sound_beep_coin(void) {
    sound_beep(988, 40);
    sound_beep(1319, 80);
}
