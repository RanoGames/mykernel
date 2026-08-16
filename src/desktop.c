/* desktop.c — простой рабочий стол: обои, панель, окно, курсор, drag */

#include "desktop.h"
#include "mkdisp.h"
#include "mkdraw.h"
#include "vbe.h"
#include "mouse.h"
#include "keyboard.h"
#include "vga.h"
#include "timer.h"
#include "settings.h"

static void paint_welcome(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F0F0F0);
    mk_buf_rect(s->pixels, s->w, s->h, 0, 0, s->w, 4, 0x00007ACC);
    mk_buf_text(s->pixels, s->w, s->h, 16, 20,
                "Welcome to MyKernel Desktop", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 16, 44,
                "This is a Wayland-inspired compositor", 0x00333333, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 16, 64,
                "running inside the kernel.", 0x00333333, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 16, 96,
                "- Drag the blue title bar to move", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 16, 116,
                "- Click red X to close this window", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 16, 136,
                "- Press ESC to leave the desktop", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 16, 168,
                "Linux Wayland apps will NOT run here yet.", 0x00AA0000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 16, 188,
                "This is our own MK display protocol.", 0x00666666, 0x00F0F0F0);
}

void desktop_run(void) {
    /* Prefer 800x600; fall back to 640x480 */
    int mode = VBE_MODE_800x600;
    if (mk_init(mode) != 0) {
        mode = VBE_MODE_640x480;
        if (mk_init(mode) != 0) {
            terminal_writestring("desktop: VBE init failed\n");
            return;
        }
    }

    int win = mk_surface_create(420, 220, "Welcome");
    if (win >= 0) {
        paint_welcome(mk_surface_get(win));
        mk_surface_commit(win);
    }

    mk_compose();

    int dragging = 0;
    int drag_id = -1;
    int drag_off_x = 0, drag_off_y = 0;
    int prev_buttons = 0;
    int need_redraw = 1;
    uint32_t last_draw = timer_ticks();

    for (;;) {
        char k;
        while (keyboard_trygetchar(&k)) {
            if (k == 0x1B || k == 'q' || k == 'Q') { /* ESC */
                goto done;
            }
        }

        struct mouse_state m;
        mouse_get(&m);

        int buttons = m.buttons;
        int left = buttons & 1;
        int left_down = left && !(prev_buttons & 1);
        int left_up = !left && (prev_buttons & 1);

        if (left_down) {
            int lx, ly, on_title, on_close;
            int id = mk_hit_test(m.x, m.y, &lx, &ly, &on_title, &on_close);
            if (id >= 0 && on_close) {
                mk_surface_destroy(id);
                need_redraw = 1;
                dragging = 0;
            } else if (id >= 0 && on_title) {
                dragging = 1;
                drag_id = id;
                struct mk_surface* s = mk_surface_get(id);
                drag_off_x = m.x - s->x;
                drag_off_y = m.y - s->y;
            }
        }
        if (left_up)
            dragging = 0;

        if (dragging && drag_id >= 0 && (buttons & 1)) {
            mk_surface_move(drag_id, m.x - drag_off_x, m.y - drag_off_y);
            need_redraw = 1;
        }

        /* redraw on motion for cursor, throttled ~ every 1 tick */
        static int last_mx = -1, last_my = -1;
        if (m.x != last_mx || m.y != last_my) {
            need_redraw = 1;
            last_mx = m.x;
            last_my = m.y;
        }

        if (need_redraw && (timer_ticks() != last_draw || left_down || left_up)) {
            mk_compose();
            need_redraw = 0;
            last_draw = timer_ticks();
        }

        prev_buttons = buttons;
        __asm__ volatile ("hlt");
    }

done:
    mk_shutdown();
    terminal_initialize();
    terminal_writestring("desktop: exited\n");
}
