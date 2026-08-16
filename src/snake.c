/* snake.c - Mode13 or VBE by settings */

#include "snake.h"
#include "gfx.h"
#include "vbe.h"
#include "settings.h"
#include "keyboard.h"
#include "timer.h"
#include "sound.h"
#include "vga.h"
#include <stdint.h>

#define MAX_LEN 200

static int use_vbe;
static int cell;
static int grid_w, grid_h;
static int screen_w, screen_h;

static int sx[MAX_LEN], sy[MAX_LEN];
static int slen;
static int dir_x, dir_y;
static int food_x, food_y;
static int score;
static int dead;
static int death_sounded;
static uint32_t rng;
static int tail_x, tail_y;
static int grew;

static uint32_t rand_u(void) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7FFFu;
}

static void place_food(void) {
    for (;;) {
        food_x = (int)(rand_u() % (uint32_t)grid_w);
        food_y = (int)(rand_u() % (uint32_t)grid_h);
        int ok = 1;
        for (int i = 0; i < slen; i++)
            if (sx[i] == food_x && sy[i] == food_y) { ok = 0; break; }
        if (ok) return;
    }
}

static void put_cell(int gx, int gy, uint32_t color32, uint8_t color8) {
    int x = gx * cell;
    int y = gy * cell;
    if (use_vbe)
        vbe_fill_rect(x, y, cell, cell, color32);
    else
        gfx_fill_rect(x, y, cell, cell, color8);
}

static void clear_screen(void) {
    if (use_vbe) vbe_clear(0x000000);
    else gfx_clear(GFX_BLACK);
}

static void draw_border(void) {
    if (use_vbe) {
        uint32_t g = vbe_rgb(80, 80, 80);
        vbe_fill_rect(0, 0, screen_w, 2, g);
        vbe_fill_rect(0, screen_h - 2, screen_w, 2, g);
        vbe_fill_rect(0, 0, 2, screen_h, g);
        vbe_fill_rect(screen_w - 2, 0, 2, screen_h, g);
    } else {
        gfx_hline(0, 0, GFX_WIDTH, GFX_DGRAY);
        gfx_hline(0, GFX_HEIGHT - 1, GFX_WIDTH, GFX_DGRAY);
        gfx_vline(0, 0, GFX_HEIGHT, GFX_DGRAY);
        gfx_vline(GFX_WIDTH - 1, 0, GFX_HEIGHT, GFX_DGRAY);
    }
}

static void draw_cell(int gx, int gy, int kind) {
    if (use_vbe) {
        uint32_t c = 0;
        if (kind == 1) c = vbe_rgb(80, 255, 80);
        else if (kind == 2) c = vbe_rgb(0, 180, 0);
        else if (kind == 3) c = vbe_rgb(255, 60, 60);
        put_cell(gx, gy, c, 0);
    } else {
        uint8_t c = GFX_BLACK;
        if (kind == 1) c = GFX_LGREEN;
        else if (kind == 2) c = GFX_GREEN;
        else if (kind == 3) c = GFX_LRED;
        put_cell(gx, gy, 0, c);
    }
}

static void redraw_full(void) {
    if (!use_vbe) gfx_wait_vsync();
    clear_screen();
    draw_border();
    draw_cell(food_x, food_y, 3);
    for (int i = 0; i < slen; i++)
        draw_cell(sx[i], sy[i], i == 0 ? 1 : 2);
}

static void redraw_step(void) {
    if (!use_vbe) gfx_wait_vsync();
    if (!grew) draw_cell(tail_x, tail_y, 0);
    draw_cell(sx[0], sy[0], 1);
    if (slen > 1) draw_cell(sx[1], sy[1], 2);
    draw_cell(food_x, food_y, 3);
    draw_border();
}

static void step(void) {
    if (dead) return;
    int nx = sx[0] + dir_x;
    int ny = sy[0] + dir_y;
    if (nx < 0 || ny < 0 || nx >= grid_w || ny >= grid_h) { dead = 1; return; }
    for (int i = 0; i < slen; i++)
        if (sx[i] == nx && sy[i] == ny) { dead = 1; return; }

    tail_x = sx[slen - 1];
    tail_y = sy[slen - 1];
    grew = 0;
    for (int i = slen - 1; i > 0; i--) {
        sx[i] = sx[i - 1];
        sy[i] = sy[i - 1];
    }
    sx[0] = nx; sy[0] = ny;

    if (nx == food_x && ny == food_y) {
        if (slen < MAX_LEN) {
            sx[slen] = tail_x;
            sy[slen] = tail_y;
            slen++;
            grew = 1;
        }
        score++;
        place_food();
        if (settings_get()->sound_enabled) sound_beep_coin();
    }
}

static void handle_key(char c) {
    if (c == 'w' || c == 'W' || c == KEY_UP) {
        if (dir_y != 1) { dir_x = 0; dir_y = -1; }
    } else if (c == 's' || c == 'S' || c == KEY_DOWN) {
        if (dir_y != -1) { dir_x = 0; dir_y = 1; }
    } else if (c == 'a' || c == 'A' || c == KEY_LEFT) {
        if (dir_x != 1) { dir_x = -1; dir_y = 0; }
    } else if (c == 'd' || c == 'D' || c == KEY_RIGHT) {
        if (dir_x != -1) { dir_x = 1; dir_y = 0; }
    }
}

void snake_run(void) {
    struct system_settings* cfg = settings_get();
    use_vbe = cfg->gfx_use_vbe;

    if (use_vbe) {
        vbe_probe();
        if (vbe_set_mode(cfg->gfx_mode) != 0) {
            terminal_writestring("snake: VBE failed, fallback mode13\n");
            use_vbe = 0;
        } else {
            const struct vbe_info* vi = vbe_get_info();
            screen_w = (int)vi->width;
            screen_h = (int)vi->height;
            cell = (screen_w >= 1000) ? 16 : (screen_w >= 700 ? 12 : 10);
            grid_w = screen_w / cell;
            grid_h = screen_h / cell;
        }
    }
    if (!use_vbe) {
        gfx_init_mode13();
        screen_w = GFX_WIDTH;
        screen_h = GFX_HEIGHT;
        cell = 8;
        grid_w = GFX_WIDTH / cell;
        grid_h = GFX_HEIGHT / cell;
    }

    rng = timer_ticks() ^ 0xA5A5u;
    slen = 3;
    sx[0] = grid_w / 2; sy[0] = grid_h / 2;
    sx[1] = sx[0] - 1; sy[1] = sy[0];
    sx[2] = sx[0] - 2; sy[2] = sy[0];
    dir_x = 1; dir_y = 0;
    score = 0; dead = 0; death_sounded = 0;
    place_food();
    redraw_full();

    uint32_t last = timer_ticks();
    uint32_t period = use_vbe ? 6 : 5;

    for (;;) {
        char c;
        while (keyboard_trygetchar(&c)) {
            if (c == 27 || c == 'q' || c == 'Q') goto done;
            if (dead && (c == 'r' || c == 'R')) {
                slen = 3;
                sx[0] = grid_w / 2; sy[0] = grid_h / 2;
                sx[1] = sx[0] - 1; sy[1] = sy[0];
                sx[2] = sx[0] - 2; sy[2] = sy[0];
                dir_x = 1; dir_y = 0;
                score = 0; dead = 0; death_sounded = 0;
                place_food();
                redraw_full();
                continue;
            }
            handle_key(c);
        }
        uint32_t now = timer_ticks();
        if (now - last >= period) {
            last = now;
            if (!dead) {
                step();
                if (dead) {
                    if (!death_sounded && cfg->sound_enabled) {
                        sound_beep_err();
                        death_sounded = 1;
                    }
                    redraw_full();
                } else {
                    redraw_step();
                }
            }
        }
        __asm__ volatile ("hlt");
    }
done:
    if (use_vbe) {
        vbe_disable();
        gfx_restore_text();
    } else {
        gfx_restore_text();
    }
}
