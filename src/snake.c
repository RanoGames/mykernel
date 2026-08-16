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
static int next_dx, next_dy;   /* pending turn — applied once per step */
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

/* 8x8-ish overlay text via gfx_font_glyph (16 rows, we use top 8 or full) */
static void draw_text_xy(int px, int py, const char* s, uint32_t fg32, uint8_t fg8) {
    for (; *s; s++) {
        const uint8_t* g = gfx_font_glyph((unsigned char)*s);
        for (int row = 0; row < 16; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    if (use_vbe)
                        vbe_putpixel(px + col, py + row, fg32);
                    else
                        gfx_putpixel(px + col, py + row, fg8);
                }
            }
        }
        px += 8;
    }
}

static void draw_game_over(void) {
    /* red panel in the center */
    int box_w = use_vbe ? (screen_w / 2) : 200;
    int box_h = use_vbe ? 80 : 60;
    int bx = (screen_w - box_w) / 2;
    int by = (screen_h - box_h) / 2;

    if (use_vbe) {
        vbe_fill_rect(bx, by, box_w, box_h, vbe_rgb(160, 0, 0));
        vbe_fill_rect(bx + 2, by + 2, box_w - 4, box_h - 4, vbe_rgb(40, 0, 0));
        draw_text_xy(bx + 16, by + 16, "GAME OVER", vbe_rgb(255, 220, 220), 0);
        /* score */
        char sc[24];
        int n = 0;
        sc[n++] = 'S'; sc[n++] = 'c'; sc[n++] = 'o'; sc[n++] = 'r'; sc[n++] = 'e'; sc[n++] = ':'; sc[n++] = ' ';
        int v = score;
        char tmp[12]; int ti = 0;
        if (v == 0) tmp[ti++] = '0';
        while (v > 0 && ti < 11) { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; }
        while (ti--) sc[n++] = tmp[ti];
        sc[n] = 0;
        draw_text_xy(bx + 16, by + 40, sc, vbe_rgb(255, 255, 100), 0);
        draw_text_xy(bx + 16, by + 58, "R=retry  Esc=quit", vbe_rgb(200, 200, 200), 0);
    } else {
        gfx_fill_rect(bx, by, box_w, box_h, GFX_RED);
        gfx_fill_rect(bx + 2, by + 2, box_w - 4, box_h - 4, GFX_DGRAY);
        draw_text_xy(bx + 12, by + 12, "GAME OVER", 0, GFX_WHITE);
        char sc[24];
        int n = 0;
        sc[n++] = 'S'; sc[n++] = 'c'; sc[n++] = 'o'; sc[n++] = 'r'; sc[n++] = 'e'; sc[n++] = ':'; sc[n++] = ' ';
        int v = score;
        char tmp[12]; int ti = 0;
        if (v == 0) tmp[ti++] = '0';
        while (v > 0 && ti < 11) { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; }
        while (ti--) sc[n++] = tmp[ti];
        sc[n] = 0;
        draw_text_xy(bx + 12, by + 32, sc, 0, GFX_YELLOW);
        draw_text_xy(bx + 12, by + 44, "R=retry Esc=quit", 0, GFX_LGRAY);
    }
}

static void redraw_full(void) {
    if (!use_vbe) gfx_wait_vsync();
    clear_screen();
    draw_border();
    draw_cell(food_x, food_y, 3);
    for (int i = 0; i < slen; i++)
        draw_cell(sx[i], sy[i], i == 0 ? 1 : 2);
    if (dead)
        draw_game_over();
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

    /* apply pending direction once per tick (fixes double-key ignore) */
    if (!(next_dx == -dir_x && next_dy == -dir_y)) {
        /* only reject exact reverse; allow any other pending */
        if (next_dx != dir_x || next_dy != dir_y) {
            dir_x = next_dx;
            dir_y = next_dy;
        }
    }

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
    int ndx = next_dx, ndy = next_dy;
    if (c == 'w' || c == 'W' || c == KEY_UP) {
        ndx = 0; ndy = -1;
    } else if (c == 's' || c == 'S' || c == KEY_DOWN) {
        ndx = 0; ndy = 1;
    } else if (c == 'a' || c == 'A' || c == KEY_LEFT) {
        ndx = -1; ndy = 0;
    } else if (c == 'd' || c == 'D' || c == KEY_RIGHT) {
        ndx = 1; ndy = 0;
    } else {
        return;
    }
    /* queue if not exact reverse of *current* movement */
    if (!(ndx == -dir_x && ndy == -dir_y)) {
        next_dx = ndx;
        next_dy = ndy;
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
    next_dx = 1; next_dy = 0;
    score = 0; dead = 0; death_sounded = 0;
    place_food();
    redraw_full();

    uint32_t last = timer_ticks();
    /* ~8 moves/sec: period from timer_hz (ms → ticks) */
    uint32_t period = (timer_hz() * 120) / 1000; /* 120 ms between steps */
    if (period < 1) period = 1;

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
                next_dx = 1; next_dy = 0;
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
