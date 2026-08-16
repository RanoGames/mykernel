/* snake.c — без полного clear каждый кадр (нет мигания),
 * без курсора мыши, рамка серая только 1px */

#include "snake.h"
#include "gfx.h"
#include "keyboard.h"
#include "timer.h"
#include "sound.h"
#include "vga.h"
#include <stdint.h>

#define CELL 8
#define GRID_W (GFX_WIDTH / CELL)  /* 40 */
#define GRID_H (GFX_HEIGHT / CELL) /* 25 */
#define MAX_LEN 200

static int sx[MAX_LEN], sy[MAX_LEN];
static int slen;
static int dir_x, dir_y;
static int food_x, food_y;
static int score;
static int dead;
static int death_sounded;
static uint32_t rng;

/* предыдущий хвост — чтобы стереть одну клетку */
static int tail_x, tail_y;
static int grew;

static uint32_t rand_u(void) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7FFFu;
}

static void place_food(void) {
    for (;;) {
        food_x = (int)(rand_u() % GRID_W);
        food_y = (int)(rand_u() % GRID_H);
        int ok = 1;
        for (int i = 0; i < slen; i++)
            if (sx[i] == food_x && sy[i] == food_y) { ok = 0; break; }
        if (ok) return;
    }
}

static void draw_cell(int gx, int gy, uint8_t color) {
    /* клетка целиком CELL×CELL — без щелей */
    gfx_fill_rect(gx * CELL, gy * CELL, CELL, CELL, color);
}

static void draw_border(void) {
    /* тонкая серая рамка по краю экрана */
    gfx_hline(0, 0, GFX_WIDTH, GFX_DGRAY);
    gfx_hline(0, GFX_HEIGHT - 1, GFX_WIDTH, GFX_DGRAY);
    gfx_vline(0, 0, GFX_HEIGHT, GFX_DGRAY);
    gfx_vline(GFX_WIDTH - 1, 0, GFX_HEIGHT, GFX_DGRAY);
}

static void redraw_full(void) {
    gfx_wait_vsync();
    gfx_clear(GFX_BLACK);
    draw_border();
    draw_cell(food_x, food_y, GFX_LRED);
    for (int i = 0; i < slen; i++)
        draw_cell(sx[i], sy[i], i == 0 ? GFX_LGREEN : GFX_GREEN);
}

static void redraw_step(void) {
    gfx_wait_vsync();
    /* стереть старый хвост, если не выросли */
    if (!grew)
        draw_cell(tail_x, tail_y, GFX_BLACK);
    /* новая голова */
    draw_cell(sx[0], sy[0], GFX_LGREEN);
    /* бывшая голова → тело */
    if (slen > 1)
        draw_cell(sx[1], sy[1], GFX_GREEN);
    /* еда */
    draw_cell(food_x, food_y, GFX_LRED);
    draw_border();
}

static void step(void) {
    if (dead) return;

    int nx = sx[0] + dir_x;
    int ny = sy[0] + dir_y;

    if (nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) {
        dead = 1;
        return;
    }
    for (int i = 0; i < slen; i++)
        if (sx[i] == nx && sy[i] == ny) {
            dead = 1;
            return;
        }

    tail_x = sx[slen - 1];
    tail_y = sy[slen - 1];
    grew = 0;

    for (int i = slen; i > 0; i--) {
        sx[i] = sx[i - 1];
        sy[i] = sy[i - 1];
    }
    sx[0] = nx;
    sy[0] = ny;

    if (nx == food_x && ny == food_y) {
        if (slen < MAX_LEN - 1) {
            slen++;
            grew = 1;
        }
        score++;
        place_food();
        sound_beep_coin();
    }
}

void snake_run(void) {
    rng = timer_ticks() ^ 0xA5A5u;
    if (rng == 0) rng = 1;

    gfx_init_mode13();
    sound_beep_ok();

    slen = 4;
    sx[0] = 10; sy[0] = 12;
    sx[1] = 9;  sy[1] = 12;
    sx[2] = 8;  sy[2] = 12;
    sx[3] = 7;  sy[3] = 12;
    dir_x = 1;
    dir_y = 0;
    score = 0;
    dead = 0;
    death_sounded = 0;
    place_food();
    redraw_full();

    uint32_t last = timer_ticks();
    int running = 1;

    while (running) {
        char c;
        while (keyboard_trygetchar(&c)) {
            if (c == 'q' || c == 'Q' || c == 27) {
                running = 0;
                break;
            }
            if (dead && (c == KEY_ENTER || c == ' ')) {
                slen = 4;
                sx[0] = 10; sy[0] = 12;
                sx[1] = 9;  sy[1] = 12;
                sx[2] = 8;  sy[2] = 12;
                sx[3] = 7;  sy[3] = 12;
                dir_x = 1; dir_y = 0;
                score = 0; dead = 0; death_sounded = 0;
                place_food();
                redraw_full();
                sound_beep_ok();
            }
            int ndx = dir_x, ndy = dir_y;
            if (c == KEY_UP || c == 'w' || c == 'W') { ndx = 0; ndy = -1; }
            else if (c == KEY_DOWN || c == 's' || c == 'S') { ndx = 0; ndy = 1; }
            else if (c == KEY_LEFT || c == 'a' || c == 'A') { ndx = -1; ndy = 0; }
            else if (c == KEY_RIGHT || c == 'd' || c == 'D') { ndx = 1; ndy = 0; }
            if (!(ndx == -dir_x && ndy == -dir_y)) {
                dir_x = ndx;
                dir_y = ndy;
            }
        }

        uint32_t now = timer_ticks();
        if (!dead && now - last >= 12u) {
            last = now;
            step();
            if (!dead)
                redraw_step();
            else {
                gfx_wait_vsync();
                gfx_fill_rect(80, 90, 160, 20, GFX_RED);
                if (!death_sounded) {
                    sound_beep_err();
                    death_sounded = 1;
                }
            }
        }

        __asm__ volatile ("hlt");
    }

    sound_stop();
    gfx_restore_text();
    terminal_writestring("Snake score: ");
    terminal_write_uint((uint32_t)score);
    terminal_putchar('\n');
}
