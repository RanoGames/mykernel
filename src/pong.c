/* pong.c — простой Pong на VBE или Mode13 */

#include "pong.h"
#include "gfx.h"
#include "vbe.h"
#include "settings.h"
#include "keyboard.h"
#include "timer.h"
#include "platform.h"
#include "sound.h"
#include "vga.h"
#include <stdint.h>

static int use_vbe;
static int W, H;

static void clear(uint32_t c32, uint8_t c8) {
    if (use_vbe) vbe_clear(c32);
    else gfx_clear(c8);
}
static void rect(int x, int y, int w, int h, uint32_t c32, uint8_t c8) {
    if (use_vbe) vbe_fill_rect(x, y, w, h, c32);
    else gfx_fill_rect(x, y, w, h, c8);
}

void pong_run(void) {
    struct system_settings* cfg = settings_get();
    use_vbe = cfg->gfx_use_vbe && !platform_is_virtualbox();
    if (use_vbe) {
        if (!vbe_probe() || vbe_set_mode(cfg->gfx_mode) != 0) use_vbe = 0;
        else {
            const struct vbe_info* vi = vbe_get_info();
            W = (int)vi->width; H = (int)vi->height;
        }
    }
    if (!use_vbe) {
        gfx_init_mode13();
        W = GFX_WIDTH; H = GFX_HEIGHT;
    }

    int paddle_h = H / 6;
    int paddle_w = use_vbe ? 12 : 6;
    int ball = use_vbe ? 10 : 6;
    int p1y = H / 2 - paddle_h / 2;
    int p2y = H / 2 - paddle_h / 2;
    int bx = W / 2, by = H / 2;
    int bdx = use_vbe ? 5 : 3;
    int bdy = use_vbe ? 3 : 2;
    int s1 = 0, s2 = 0;
    int margin = use_vbe ? 20 : 8;

    uint32_t last = timer_ticks();
    int use_pit = 1;
    {
        uint32_t a = timer_ticks();
        timer_busy_ms(30);
        if (timer_ticks() == a) use_pit = 0;
    }
    for (;;) {
        char c;
        while (keyboard_trygetchar(&c)) {
            if (c == 27 || c == 'q' || c == 'Q') goto done;
            if (c == 'w' || c == 'W') p1y -= use_vbe ? 16 : 8;
            if (c == 's' || c == 'S') p1y += use_vbe ? 16 : 8;
            if (c == KEY_UP) p2y -= use_vbe ? 16 : 8;
            if (c == KEY_DOWN) p2y += use_vbe ? 16 : 8;
        }
        if (p1y < 0) p1y = 0;
        if (p1y + paddle_h > H) p1y = H - paddle_h;
        if (p2y < 0) p2y = 0;
        if (p2y + paddle_h > H) p2y = H - paddle_h;

        int tick = 0;
        if (use_pit) {
            if (timer_ticks() - last >= 2) { last = timer_ticks(); tick = 1; }
        } else {
            timer_busy_ms(20);
            tick = 1;
        }
        if (tick) {
            bx += bdx; by += bdy;
            if (by <= 0 || by + ball >= H) {
                bdy = -bdy;
                if (cfg->sound_enabled) sound_beep(400, 20);
            }
            if (bx <= margin + paddle_w && by + ball >= p1y && by <= p1y + paddle_h && bdx < 0) {
                bdx = -bdx;
                if (cfg->sound_enabled) sound_beep(600, 20);
            }
            if (bx + ball >= W - margin - paddle_w && by + ball >= p2y && by <= p2y + paddle_h && bdx > 0) {
                bdx = -bdx;
                if (cfg->sound_enabled) sound_beep(600, 20);
            }
            if (bx < 0) {
                s2++; bx = W / 2; by = H / 2; bdx = use_vbe ? 5 : 3;
                if (cfg->sound_enabled) sound_beep_err();
            }
            if (bx > W) {
                s1++; bx = W / 2; by = H / 2; bdx = use_vbe ? -5 : -3;
                if (cfg->sound_enabled) sound_beep_err();
            }

            clear(0x001010, GFX_BLACK);
            for (int y = 0; y < H; y += 16)
                rect(W / 2 - 1, y, 2, 8, vbe_rgb(40, 80, 40), GFX_DGRAY);
            rect(margin, p1y, paddle_w, paddle_h, vbe_rgb(80, 200, 255), GFX_LCYAN);
            rect(W - margin - paddle_w, p2y, paddle_w, paddle_h, vbe_rgb(255, 180, 80), GFX_YELLOW);
            rect(bx, by, ball, ball, vbe_rgb(255, 255, 255), GFX_WHITE);
        }
        if (use_pit)
            __asm__ volatile ("hlt");
    }
done:
    if (use_vbe) { vbe_disable(); gfx_restore_text(); }
    else gfx_restore_text();
    terminal_writestring("Pong score: ");
    terminal_write_uint((uint32_t)s1);
    terminal_writestring(" - ");
    terminal_write_uint((uint32_t)s2);
    terminal_putchar('\n');
}
