/* desktop.c — desktop with Welcome, Clock, SysInfo apps */

#include "desktop.h"
#include "mkdisp.h"
#include "mkdraw.h"
#include "vbe.h"
#include "mouse.h"
#include "keyboard.h"
#include "vga.h"
#include "gfx.h"
#include "timer.h"
#include "platform.h"
#include "power.h"
#include "sched.h"

static void u32_to_str(uint32_t v, char* out) {
    char tmp[12];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

static void format_uptime(char* buf, uint32_t ticks, uint32_t hz) {
    if (hz == 0) hz = 100;
    uint32_t sec = ticks / hz;
    uint32_t m = sec / 60;
    uint32_t s = sec % 60;
    uint32_t h = m / 60;
    m = m % 60;
    /* H:MM:SS */
    char th[12], tm[12], ts[12];
    u32_to_str(h, th);
    u32_to_str(m, tm);
    u32_to_str(s, ts);
    int i = 0, j;
    for (j = 0; th[j]; j++) buf[i++] = th[j];
    buf[i++] = ':';
    if (m < 10) buf[i++] = '0';
    for (j = 0; tm[j]; j++) buf[i++] = tm[j];
    buf[i++] = ':';
    if (s < 10) buf[i++] = '0';
    for (j = 0; ts[j]; j++) buf[i++] = ts[j];
    buf[i] = 0;
}

static void paint_welcome(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12,
                "Welcome to MyKernel Desktop", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40,
                "Drag title bar to move windows", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 60,
                "Click X to close a window", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 80,
                "Panel: Reboot / Shutdown", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 100,
                "Click MyKernel button for apps", 0x00003399, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 130,
                "ESC or Q = leave desktop", 0x00000000, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 160,
                "Linux apps need more syscalls still", 0x00AA0000, 0x00F0F0F0);
}

static void paint_clock(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x001A1A2E);
    mk_buf_text(s->pixels, s->w, s->h, 16, 16,
                "System Clock", 0x00E0E0FF, 0x001A1A2E);
    char line[64];
    char up[32];
    format_uptime(up, timer_ticks(), timer_hz());
    /* "Uptime  H:MM:SS" */
    const char* p = "Uptime  ";
    int i = 0;
    while (*p) line[i++] = *p++;
    p = up;
    while (*p) line[i++] = *p++;
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 16, 56, line, 0x00FFFFFF, 0x001A1A2E);

    char tbuf[32];
    u32_to_str(timer_ticks(), tbuf);
    i = 0; p = "Ticks   ";
    while (*p) line[i++] = *p++;
    p = tbuf;
    while (*p) line[i++] = *p++;
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 16, 80, line, 0x00AAAAAA, 0x001A1A2E);

    u32_to_str(timer_hz(), tbuf);
    i = 0; p = "PIT Hz  ";
    while (*p) line[i++] = *p++;
    p = tbuf;
    while (*p) line[i++] = *p++;
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 16, 104, line, 0x00AAAAAA, 0x001A1A2E);

    mk_buf_text(s->pixels, s->w, s->h, 16, 140,
                "Updates every second", 0x0066CCFF, 0x001A1A2E);
}

static void paint_sysinfo(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12,
                "System Information", 0x00000000, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40,
                "OS:     MyKernel", 0x00000000, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 60,
                "Arch:   i386", 0x00000000, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 80,
                "ABI:    int 0x80 (Linux-like)", 0x00000000, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 100,
                "Display: VBE LFB", 0x00000000, 0x00F5F5F5);

    char line[48];
    char n[16];
    u32_to_str((uint32_t)sched_task_count(), n);
    int i = 0;
    const char* p = "Tasks:  ";
    while (*p) line[i++] = *p++;
    p = n;
    while (*p) line[i++] = *p++;
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 12, 120, line, 0x00000000, 0x00F5F5F5);

    mk_buf_text(s->pixels, s->w, s->h, 12, 150,
                "Syscalls: uname, mmap2, futex...", 0x00333333, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 170,
                "New: getrandom, nice, hostname", 0x00006600, 0x00F5F5F5);
}

/* Panel start button geometry (left side) */
static void start_btn_rect(int* x, int* y, int* w, int* h) {
    *x = 4;
    *y = mk_screen_h() - 28 + 4;
    *w = 72;
    *h = 20;
}

static int open_clock(void) {
    int id = mk_surface_create(280, 180, "Clock");
    if (id < 0) return -1;
    struct mk_surface* s = mk_surface_get(id);
    if (s) {
        s->x = 40;
        s->y = 80;
        paint_clock(s);
        mk_surface_commit(id);
    }
    return id;
}

static int open_sysinfo(void) {
    int id = mk_surface_create(340, 210, "SysInfo");
    if (id < 0) return -1;
    struct mk_surface* s = mk_surface_get(id);
    if (s) {
        s->x = 200;
        s->y = 60;
        paint_sysinfo(s);
        mk_surface_commit(id);
    }
    return id;
}

void desktop_run(void) {
    if (platform_is_virtualbox()) {
        terminal_writestring("desktop: needs Bochs VBE (QEMU). Not supported on VirtualBox.\n");
        return;
    }

    int mode = VBE_MODE_800x600;
    if (mk_init(mode) != 0) {
        mode = VBE_MODE_640x480;
        if (mk_init(mode) != 0) {
            terminal_writestring("desktop: VBE init failed\n");
            return;
        }
    }

    int win = mk_surface_create(420, 200, "Welcome");
    if (win >= 0) {
        paint_welcome(mk_surface_get(win));
        mk_surface_commit(win);
    }

    int clock_id = open_clock();
    int sysinfo_id = open_sysinfo();

    mk_compose();

    int dragging = 0;
    int drag_id = -1;
    int drag_off_x = 0, drag_off_y = 0;
    int prev_buttons = 0;
    int need_redraw = 1;
    uint32_t last_draw = timer_ticks();
    uint32_t last_clock = timer_ticks();

    for (;;) {
        char k;
        while (keyboard_trygetchar(&k)) {
            if (k == 0x1B || k == 'q' || k == 'Q')
                goto done;
            /* hotkeys */
            if (k == 'c' || k == 'C') {
                if (clock_id < 0 || !mk_surface_get(clock_id) || !mk_surface_get(clock_id)->used)
                    clock_id = open_clock();
                need_redraw = 1;
            }
            if (k == 'i' || k == 'I') {
                if (sysinfo_id < 0 || !mk_surface_get(sysinfo_id) || !mk_surface_get(sysinfo_id)->used)
                    sysinfo_id = open_sysinfo();
                need_redraw = 1;
            }
        }

        struct mouse_state m;
        mouse_get(&m);

        int buttons = m.buttons;
        int left = buttons & 1;
        int left_down = left && !(prev_buttons & 1);
        int left_up = !left && (prev_buttons & 1);

        if (left_down) {
            int rx, sx, by, bw, bh;
            mk_panel_power_rects(&rx, &sx, &by, &bw, &bh);
            if (m.y >= by && m.y < by + bh) {
                if (m.x >= rx && m.x < rx + bw) {
                    mk_shutdown();
                    gfx_restore_text();
                    machine_reboot();
                }
                if (m.x >= sx && m.x < sx + bw) {
                    mk_shutdown();
                    gfx_restore_text();
                    machine_shutdown();
                }
            }

            /* Start button → reopen apps if closed */
            int stx, sty, stw, sth;
            start_btn_rect(&stx, &sty, &stw, &sth);
            if (m.x >= stx && m.x < stx + stw && m.y >= sty && m.y < sty + sth) {
                if (clock_id < 0 || !mk_surface_get(clock_id) || !mk_surface_get(clock_id)->used)
                    clock_id = open_clock();
                if (sysinfo_id < 0 || !mk_surface_get(sysinfo_id) || !mk_surface_get(sysinfo_id)->used)
                    sysinfo_id = open_sysinfo();
                need_redraw = 1;
            }

            int lx, ly, on_title, on_close;
            int id = mk_hit_test(m.x, m.y, &lx, &ly, &on_title, &on_close);
            if (id >= 0 && on_close) {
                if (id == clock_id) clock_id = -1;
                if (id == sysinfo_id) sysinfo_id = -1;
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

        /* Refresh clock ~1 Hz */
        {
            uint32_t now = timer_ticks();
            uint32_t hz = timer_hz();
            if (hz == 0) hz = 100;
            if (clock_id >= 0 && (now - last_clock) >= hz) {
                struct mk_surface* cs = mk_surface_get(clock_id);
                if (cs && cs->used) {
                    paint_clock(cs);
                    mk_surface_commit(clock_id);
                    need_redraw = 1;
                }
                last_clock = now;
            }
        }

        static int last_mx = -1, last_my = -1;
        int mouse_moved = (m.x != last_mx || m.y != last_my);
        if (mouse_moved) {
            last_mx = m.x;
            last_my = m.y;
        }

        if (need_redraw) {
            uint32_t now = timer_ticks();
            uint32_t min_dt = dragging ? 1 : 0;
            if ((now - last_draw) >= min_dt || left_up || left_down) {
                mk_compose();
                need_redraw = 0;
                last_draw = now;
                last_mx = m.x;
                last_my = m.y;
            }
        } else if (mouse_moved) {
            mk_cursor_move(m.x, m.y);
        }

        prev_buttons = buttons;
        __asm__ volatile ("hlt");
    }

done:
    mk_shutdown();
    gfx_restore_text();
    terminal_writestring("desktop: exited\n");
}
