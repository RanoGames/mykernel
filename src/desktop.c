/* desktop.c — Start menu, desktop icons, multiple apps */

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
    m %= 60;
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

/* ---- app window ids ---- */
static int id_welcome = -1;
static int id_clock = -1;
static int id_sysinfo = -1;
static int id_calc = -1;
static int id_about = -1;
static int id_tasks = -1;
static int id_notes = -1;
static int id_help = -1;

static int start_open;
static int calc_acc, calc_val, calc_op; /* op: 0 none 1+ 2- 3* */

static int surface_alive(int id) {
    struct mk_surface* s = mk_surface_get(id);
    return id >= 0 && s && s->used;
}

/* ---- painters ---- */
static void paint_welcome(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "Welcome to MyKernel Desktop", 0, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 36, "Icons on desktop open apps", 0, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 56, "Start menu: apps + power", 0, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 76, "Drag title bar; X closes", 0, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 100, "ESC / Q leaves desktop", 0x00003399, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 130, "Not Linux-compatible yet", 0x00AA0000, 0x00F0F0F0);
}

static void paint_clock(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x001A1A2E);
    mk_buf_text(s->pixels, s->w, s->h, 16, 16, "System Clock", 0x00EEEEFF, 0x001A1A2E);
    char line[48];
    format_uptime(line, timer_ticks(), timer_hz());
    mk_buf_text(s->pixels, s->w, s->h, 16, 48, line, 0x00FFFFFF, 0x001A1A2E);
    char t[32];
    u32_to_str(timer_ticks(), t);
    int i = 0;
    line[i++] = 't'; line[i++] = 'i'; line[i++] = 'c'; line[i++] = 'k'; line[i++] = 's'; line[i++] = '=';
    for (int j = 0; t[j]; j++) line[i++] = t[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 16, 80, line, 0x00AAAAAA, 0x001A1A2E);
    u32_to_str(timer_hz() ? timer_hz() : 100, t);
    i = 0;
    line[i++] = 'P'; line[i++] = 'I'; line[i++] = 'T'; line[i++] = ' '; line[i++] = 'H'; line[i++] = 'z'; line[i++] = '=';
    for (int j = 0; t[j]; j++) line[i++] = t[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 16, 104, line, 0x00AAAAAA, 0x001A1A2E);
}

static void paint_sysinfo(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "System Information", 0, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40, "OS: MyKernel", 0, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 60, "Arch: i686 (32-bit)", 0, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 80, "ABI: Linux i386 int 0x80", 0, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 100, "Display: VBE LFB", 0, 0x00F5F5F5);
    char line[40];
    int i = 0;
    const char* p = "Tasks: ";
    while (*p) line[i++] = *p++;
    char n[12];
    u32_to_str((uint32_t)sched_task_count(), n);
    for (int j = 0; n[j]; j++) line[i++] = n[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 12, 120, line, 0, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 150, "x86_64 uses different", 0x00666666, 0x00F5F5F5);
    mk_buf_text(s->pixels, s->w, s->h, 12, 170, "syscall numbers than i386", 0x00666666, 0x00F5F5F5);
}

static void paint_calc(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00222222);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "Calculator", 0x00FFFFFF, 0x00222222);
    char line[48];
    int i = 0;
    const char* l = "Value: ";
    while (*l) line[i++] = *l++;
    char n[12];
    u32_to_str((uint32_t)(calc_val < 0 ? 0 : calc_val), n);
    if (calc_val < 0) line[i++] = '-';
    uint32_t av = (uint32_t)(calc_val < 0 ? -calc_val : calc_val);
    u32_to_str(av, n);
    for (int j = 0; n[j]; j++) line[i++] = n[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 12, 44, line, 0x00FFFF00, 0x00222222);
    mk_buf_text(s->pixels, s->w, s->h, 12, 72, "Keys: 0-9  + - *  Enter==", 0x00CCCCCC, 0x00222222);
    mk_buf_text(s->pixels, s->w, s->h, 12, 92, "C clear   (focus this win)", 0x00CCCCCC, 0x00222222);
    i = 0;
    l = "Acc: ";
    while (*l) line[i++] = *l++;
    av = (uint32_t)(calc_acc < 0 ? -calc_acc : calc_acc);
    if (calc_acc < 0) line[i++] = '-';
    u32_to_str(av, n);
    for (int j = 0; n[j]; j++) line[i++] = n[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 12, 120, line, 0x00AAAAAA, 0x00222222);
}

static void paint_about(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00E8EEF8);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "About MyKernel", 0, 0x00E8EEF8);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40, "Hobby teaching OS", 0, 0x00E8EEF8);
    mk_buf_text(s->pixels, s->w, s->h, 12, 60, "VGA / VBE / FAT / ELF", 0, 0x00E8EEF8);
    mk_buf_text(s->pixels, s->w, s->h, 12, 80, "Shell + Desktop (MK)", 0, 0x00E8EEF8);
    mk_buf_text(s->pixels, s->w, s->h, 12, 110, "Inspired by Wayland model", 0x00333333, 0x00E8EEF8);
    mk_buf_text(s->pixels, s->w, s->h, 12, 130, "Not a Linux distro", 0x00AA0000, 0x00E8EEF8);
}

static void paint_tasks(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00FAFAFA);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "Task Manager", 0, 0x00FAFAFA);
    char line[48];
    int i = 0;
    const char* p = "sched tasks: ";
    while (*p) line[i++] = *p++;
    char n[12];
    u32_to_str((uint32_t)sched_task_count(), n);
    for (int j = 0; n[j]; j++) line[i++] = n[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 12, 44, line, 0, 0x00FAFAFA);
    mk_buf_text(s->pixels, s->w, s->h, 12, 72, "Desktop is kernel-mode", 0x00444444, 0x00FAFAFA);
    mk_buf_text(s->pixels, s->w, s->h, 12, 92, "User threads: future", 0x00444444, 0x00FAFAFA);
    mk_buf_text(s->pixels, s->w, s->h, 12, 120, "shell | timer | IRQ0", 0x00006699, 0x00FAFAFA);
}

static void paint_notes(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00FFF8DC);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "Sticky Notes", 0, 0x00FFF8DC);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40, "- make && make run", 0, 0x00FFF8DC);
    mk_buf_text(s->pixels, s->w, s->h, 12, 60, "- desktop for GUI", 0, 0x00FFF8DC);
    mk_buf_text(s->pixels, s->w, s->h, 12, 80, "- snake / pong games", 0, 0x00FFF8DC);
    mk_buf_text(s->pixels, s->w, s->h, 12, 100, "- fatmount for disk", 0, 0x00FFF8DC);
    mk_buf_text(s->pixels, s->w, s->h, 12, 130, "(read-only sticky)", 0x00888888, 0x00FFF8DC);
}

static void paint_help(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00FFFFFF);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "Desktop Help", 0, 0x00FFFFFF);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40, "Double-click icons (single)", 0, 0x00FFFFFF);
    mk_buf_text(s->pixels, s->w, s->h, 12, 60, "Start: apps list + power", 0, 0x00FFFFFF);
    mk_buf_text(s->pixels, s->w, s->h, 12, 80, "Calc: type digits when open", 0, 0x00FFFFFF);
    mk_buf_text(s->pixels, s->w, s->h, 12, 100, "Panel Reboot / Shutdown", 0, 0x00FFFFFF);
    mk_buf_text(s->pixels, s->w, s->h, 12, 130, "QEMU + VBE recommended", 0x00006600, 0x00FFFFFF);
}

/* ---- open helpers ---- */
#define OPEN_APP(idvar, w, h, title, paintfn) \
    do { \
        if (surface_alive(idvar)) break; \
        int id = mk_surface_create(w, h, title); \
        if (id < 0) break; \
        idvar = id; \
        struct mk_surface* s = mk_surface_get(id); \
        if (s) { paintfn(s); mk_surface_commit(id); } \
    } while (0)

static void open_welcome(void) { OPEN_APP(id_welcome, 400, 180, "Welcome", paint_welcome); }
static void open_clock(void)   { OPEN_APP(id_clock, 260, 140, "Clock", paint_clock); }
static void open_sysinfo(void) { OPEN_APP(id_sysinfo, 320, 200, "SysInfo", paint_sysinfo); }
static void open_calc(void)    { OPEN_APP(id_calc, 280, 160, "Calculator", paint_calc); }
static void open_about(void)   { OPEN_APP(id_about, 300, 170, "About", paint_about); }
static void open_tasks(void)   { OPEN_APP(id_tasks, 300, 160, "Tasks", paint_tasks); }
static void open_notes(void)   { OPEN_APP(id_notes, 280, 160, "Notes", paint_notes); }
static void open_help(void)    { OPEN_APP(id_help, 320, 170, "Help", paint_help); }

static void on_close_id(int id) {
    if (id == id_welcome) id_welcome = -1;
    if (id == id_clock) id_clock = -1;
    if (id == id_sysinfo) id_sysinfo = -1;
    if (id == id_calc) id_calc = -1;
    if (id == id_about) id_about = -1;
    if (id == id_tasks) id_tasks = -1;
    if (id == id_notes) id_notes = -1;
    if (id == id_help) id_help = -1;
}

/* ---- desktop icons ---- */
struct desk_icon {
    int x, y;
    const char* label;
    void (*open)(void);
};

static struct desk_icon icons[] = {
    { 20,  48,  "Clock",    open_clock },
    { 20,  118, "SysInfo",  open_sysinfo },
    { 20,  188, "Calc",     open_calc },
    { 20,  258, "About",    open_about },
    { 100, 48,  "Tasks",    open_tasks },
    { 100, 118, "Notes",    open_notes },
    { 100, 188, "Help",     open_help },
    { 100, 258, "Welcome",  open_welcome },
};
#define N_ICONS ((int)(sizeof(icons) / sizeof(icons[0])))

static uint32_t icon_color(int i) {
    static const uint32_t cols[] = {
        0x00E81123, 0x000078D7, 0x00107C10, 0x00FFB900,
        0x008812CD, 0x00F7630C, 0x0000B7C3, 0x00E3008C
    };
    return cols[i % 8];
}

static void draw_icon(int idx, int x, int y, const char* label) {
    uint32_t c = icon_color(idx);
    /* shadow */
    mk_bb_fill(x + 3, y + 3, 56, 48, 0x00202020);
    /* body */
    mk_bb_fill(x, y, 56, 48, 0x00FFFFFF);
    mk_bb_fill(x + 3, y + 3, 50, 42, c);
    /* letter badge */
    char badge[2] = { label[0], 0 };
    mk_bb_text(x + 22, y + 14, badge, 0x00FFFFFF, c);
    /* label under icon with dark strip for contrast */
    mk_bb_fill(x - 2, y + 52, 60, 16, 0x00000000);
    mk_bb_text(x + 2, y + 54, label, 0x00FFFFFF, 0x00000000);
}

static int icon_at(int mx, int my) {
    for (int i = 0; i < N_ICONS; i++) {
        int x = icons[i].x, y = icons[i].y;
        if (mx >= x && mx < x + 60 && my >= y && my < y + 70)
            return i;
    }
    return -1;
}

/* ---- Start menu ---- */
#define SM_X 4
#define SM_W 200
#define SM_ITEM_H 22

static int start_menu_h(void) {
    /* 8 apps + separator + reboot + shutdown + empty */
    return 28 + 8 * SM_ITEM_H + 8 + 2 * SM_ITEM_H + 8;
}

static void start_btn_rect(int* x, int* y, int* w, int* h) {
    *x = 4;
    *y = mk_screen_h() - 28 + 3;
    *w = 88;
    *h = 22;
}

static void start_menu_rect(int* x, int* y, int* w, int* h) {
    *w = SM_W;
    *h = start_menu_h();
    *x = SM_X;
    *y = mk_screen_h() - 28 - *h;
}

static void draw_start_menu(void) {
    if (!start_open) return;
    int x, y, w, h;
    start_menu_rect(&x, &y, &w, &h);
    /* shadow */
    mk_bb_fill(x + 4, y + 4, w, h, 0x00101010);
    /* body */
    mk_bb_fill(x, y, w, h, 0x001E1E1E);
    mk_bb_fill(x, y, w, 28, 0x000078D7);
    mk_bb_text(x + 12, y + 6, "MyKernel Start", 0x00FFFFFF, 0x000078D7);
    const char* items[] = {
        "Clock", "SysInfo", "Calculator", "About",
        "Tasks", "Notes", "Help", "Welcome"
    };
    int iy = y + 28;
    for (int i = 0; i < 8; i++) {
        uint32_t bg = (i % 2) ? 0x00252525 : 0x001E1E1E;
        mk_bb_fill(x, iy, w, SM_ITEM_H, bg);
        mk_bb_fill(x + 4, iy + 4, 14, 14, icon_color(i));
        mk_bb_text(x + 24, iy + 4, items[i], 0x00F0F0F0, bg);
        iy += SM_ITEM_H;
    }
    mk_bb_fill(x, iy, w, 1, 0x00444444);
    iy += 6;
    mk_bb_fill(x + 8, iy, w - 16, SM_ITEM_H - 2, 0x00C19C00);
    mk_bb_text(x + 20, iy + 2, "Reboot", 0x00FFFFFF, 0x00C19C00);
    iy += SM_ITEM_H;
    mk_bb_fill(x + 8, iy, w - 16, SM_ITEM_H - 2, 0x00C42B1C);
    mk_bb_text(x + 16, iy + 2, "Shutdown", 0x00FFFFFF, 0x00C42B1C);
}

static int start_menu_click(int mx, int my) {
    int x, y, w, h;
    start_menu_rect(&x, &y, &w, &h);
    if (mx < x || mx >= x + w || my < y || my >= y + h)
        return -1;
    int iy = y + 28;
    for (int i = 0; i < 8; i++) {
        if (my >= iy && my < iy + SM_ITEM_H)
            return i;
        iy += SM_ITEM_H;
    }
    iy += 6;
    if (my >= iy && my < iy + SM_ITEM_H) return 100;
    iy += SM_ITEM_H;
    if (my >= iy && my < iy + SM_ITEM_H) return 101;
    return -2;
}

static void desktop_layer_paint(void) {
    /* icons under windows */
    for (int i = 0; i < N_ICONS; i++) {
        if (icons[i].y + 70 < mk_screen_h() - 28)
            draw_icon(i, icons[i].x, icons[i].y, icons[i].label);
    }
}

static void desktop_overlay_paint(void) {
    draw_start_menu();
}

static void calc_apply_op(void) {
    if (calc_op == 1) calc_acc += calc_val;
    else if (calc_op == 2) calc_acc -= calc_val;
    else if (calc_op == 3) calc_acc *= calc_val;
    else calc_acc = calc_val;
    calc_val = calc_acc;
    calc_op = 0;
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

    start_open = 0;
    calc_acc = calc_val = calc_op = 0;
    id_welcome = id_clock = id_sysinfo = id_calc = -1;
    id_about = id_tasks = id_notes = id_help = -1;

    mk_set_desktop_layer(desktop_layer_paint);
    mk_set_desktop_overlay(desktop_overlay_paint);

    open_welcome();
    open_clock();
    /* park windows right side so icons stay visible */
    if (surface_alive(id_welcome)) mk_surface_move(id_welcome, 200, 40);
    if (surface_alive(id_clock)) mk_surface_move(id_clock, 520, 40);

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
            if (k == 0x1B) {
                if (start_open) { start_open = 0; need_redraw = 1; }
                else goto done;
            }
            if (k == 'q' || k == 'Q')
                goto done;

            if (surface_alive(id_calc)) {
                if (k >= '0' && k <= '9') {
                    calc_val = calc_val * 10 + (k - '0');
                    paint_calc(mk_surface_get(id_calc));
                    mk_surface_commit(id_calc);
                    need_redraw = 1;
                } else if (k == '+') { calc_apply_op(); calc_op = 1; calc_val = 0; }
                else if (k == '-') { calc_apply_op(); calc_op = 2; calc_val = 0; }
                else if (k == '*') { calc_apply_op(); calc_op = 3; calc_val = 0; }
                else if (k == '\n' || k == '=') {
                    calc_apply_op();
                    paint_calc(mk_surface_get(id_calc));
                    mk_surface_commit(id_calc);
                    need_redraw = 1;
                } else if (k == 'c' || k == 'C') {
                    calc_acc = calc_val = calc_op = 0;
                    paint_calc(mk_surface_get(id_calc));
                    mk_surface_commit(id_calc);
                    need_redraw = 1;
                }
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

            int stx, sty, stw, sth;
            start_btn_rect(&stx, &sty, &stw, &sth);
            if (m.x >= stx && m.x < stx + stw && m.y >= sty && m.y < sty + sth) {
                start_open = !start_open;
                need_redraw = 1;
            } else if (start_open) {
                int sm = start_menu_click(m.x, m.y);
                if (sm >= 0 && sm <= 7) {
                    void (*ops[8])(void) = {
                        open_clock, open_sysinfo, open_calc, open_about,
                        open_tasks, open_notes, open_help, open_welcome
                    };
                    ops[sm]();
                    start_open = 0;
                    need_redraw = 1;
                } else if (sm == 100) {
                    mk_shutdown();
                    gfx_restore_text();
                    machine_reboot();
                } else if (sm == 101) {
                    mk_shutdown();
                    gfx_restore_text();
                    machine_shutdown();
                } else if (sm == -1) {
                    start_open = 0;
                    need_redraw = 1;
                }
            } else {
                int ic = icon_at(m.x, m.y);
                if (ic >= 0) {
                    icons[ic].open();
                    need_redraw = 1;
                } else {
                    int lx, ly, on_title, on_close;
                    int id = mk_hit_test(m.x, m.y, &lx, &ly, &on_title, &on_close);
                    if (id >= 0 && on_close) {
                        on_close_id(id);
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
            }
        }
        if (left_up)
            dragging = 0;

        if (dragging && drag_id >= 0 && (buttons & 1)) {
            mk_surface_move(drag_id, m.x - drag_off_x, m.y - drag_off_y);
            need_redraw = 1;
        }

        {
            uint32_t now = timer_ticks();
            uint32_t hz = timer_hz();
            if (hz == 0) hz = 100;
            if (surface_alive(id_clock) && (now - last_clock) >= hz) {
                paint_clock(mk_surface_get(id_clock));
                mk_surface_commit(id_clock);
                need_redraw = 1;
                last_clock = now;
            }
            if (surface_alive(id_tasks) && (now - last_clock) >= hz) {
                paint_tasks(mk_surface_get(id_tasks));
                mk_surface_commit(id_tasks);
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
    mk_set_desktop_layer(0);
    mk_set_desktop_overlay(0);
    mk_shutdown();
    gfx_restore_text();
    terminal_writestring("desktop: exited\n");
}
