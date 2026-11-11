/* desktop.c — Start menu, desktop icons, multiple apps, window management */

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
#include "shell.h"
#include "fs.h"
#include "kmalloc.h"

static int g_desktop_running = 0;

int desktop_is_running(void) {
    return g_desktop_running;
}

/* ---- String and Number Helpers ---- */
static void u32_to_str(uint32_t v, char* out) {
    char tmp[12];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

static void int_to_str(int v, char* buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int neg = 0;
    uint32_t uv;
    if (v < 0) { neg = 1; uv = (uint32_t)(-v); }
    else uv = (uint32_t)v;
    char tmp[16];
    int n = 0;
    while (uv && n < 15) { tmp[n++] = (char)('0' + (uv % 10)); uv /= 10; }
    int i = 0;
    if (neg) buf[i++] = '-';
    while (n) buf[i++] = tmp[--n];
    buf[i] = '\0';
}

static int desk_streq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void format_uptime(char* buf, uint32_t ticks, uint32_t hz) {
    if (hz == 0) hz = 100;
    uint32_t sec = ticks / hz;
    uint32_t m = (sec / 60) % 60;
    uint32_t s = sec % 60;
    uint32_t h = (sec / 3600);
    char th[12], tm[12], ts[12];
    u32_to_str(h, th);
    u32_to_str(m, tm);
    u32_to_str(s, ts);
    int i = 0, j;
    if (h < 10) buf[i++] = '0';
    for (j = 0; th[j]; j++) buf[i++] = th[j];
    buf[i++] = ':';
    if (m < 10) buf[i++] = '0';
    for (j = 0; tm[j]; j++) buf[i++] = tm[j];
    buf[i++] = ':';
    if (s < 10) buf[i++] = '0';
    for (j = 0; ts[j]; j++) buf[i++] = ts[j];
    buf[i] = 0;
}

/* ---- App Window IDs ---- */
static int id_welcome = -1;
static int id_clock   = -1;
static int id_sysinfo = -1;
static int id_calc    = -1;
static int id_term    = -1;
static int id_about   = -1;
static int id_tasks   = -1;
static int id_display = -1;
static int id_notes   = -1;
static int id_help    = -1;
static int id_trash   = -1;
static int id_demo    = -1;
static int active_focus_id = -1;

static int start_open = 0;

static int surface_alive(int id) {
    struct mk_surface* s = mk_surface_get(id);
    return id >= 0 && s && s->used;
}

static void on_close_id(int id);

/* ---- Open Helpers ---- */
#define OPEN_APP(idvar, w, h, title, paintfn) \
    do { \
        if (surface_alive(idvar)) { \
            mk_surface_restore(idvar); \
            active_focus_id = idvar; \
            break; \
        } \
        int id = mk_surface_create(w, h, title); \
        if (id < 0) break; \
        idvar = id; \
        active_focus_id = id; \
        struct mk_surface* s = mk_surface_get(id); \
        if (s) { paintfn(s); mk_surface_commit(id); } \
    } while (0)

/* ========================================================================= */
/* 1. Calculator Application                                                 */
/* ========================================================================= */
static int calc_acc = 0;
static int calc_val = 0;
static int calc_op = 0; /* op: 0 none, 1: +, 2: -, 3: *, 4: / */
static int calc_fresh = 1;
static int calc_error = 0;

static void calc_execute_op(void) {
    if (calc_error) return;
    if (calc_op == 1) calc_acc += calc_val;
    else if (calc_op == 2) calc_acc -= calc_val;
    else if (calc_op == 3) calc_acc *= calc_val;
    else if (calc_op == 4) {
        if (calc_val == 0) calc_error = 1;
        else calc_acc /= calc_val;
    } else {
        calc_acc = calc_val;
    }
    calc_val = calc_acc;
}

static void calc_digit(int d) {
    if (calc_error) { calc_error = 0; calc_val = 0; calc_acc = 0; calc_op = 0; }
    if (calc_fresh) {
        calc_val = d;
        calc_fresh = 0;
    } else {
        if (calc_val >= 0 && calc_val < 100000000)
            calc_val = calc_val * 10 + d;
        else if (calc_val < 0 && calc_val > -100000000)
            calc_val = calc_val * 10 - d;
    }
}

static void calc_set_op(int op) {
    if (calc_error) return;
    if (!calc_fresh && calc_op != 0) {
        calc_execute_op();
    } else {
        calc_acc = calc_val;
    }
    calc_op = op;
    calc_fresh = 1;
}

static void calc_equals(void) {
    if (calc_error) return;
    if (calc_op != 0) {
        calc_execute_op();
        calc_op = 0;
    }
    calc_fresh = 1;
}

static void calc_clear(void) {
    calc_acc = 0;
    calc_val = 0;
    calc_op = 0;
    calc_fresh = 1;
    calc_error = 0;
}

static void calc_backspace(void) {
    if (calc_error) { calc_clear(); return; }
    if (!calc_fresh) {
        calc_val /= 10;
        if (calc_val == 0) calc_fresh = 1;
    }
}

static void calc_negate(void) {
    if (calc_error) return;
    calc_val = -calc_val;
}

static void paint_calc(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00202530);

    /* LCD Screen Display */
    mk_buf_box(s->pixels, s->w, s->h, 8, 8, s->w - 16, 44, 0x00334455, 0x0010141A);

    /* Expression line */
    if (calc_op != 0) {
        char exp[32];
        char acc_str[16];
        int_to_str(calc_acc, acc_str);
        int ei = 0;
        for (int j = 0; acc_str[j]; j++) exp[ei++] = acc_str[j];
        exp[ei++] = ' ';
        if (calc_op == 1) exp[ei++] = '+';
        else if (calc_op == 2) exp[ei++] = '-';
        else if (calc_op == 3) exp[ei++] = '*';
        else if (calc_op == 4) exp[ei++] = '/';
        exp[ei] = '\0';
        mk_buf_text(s->pixels, s->w, s->h, 14, 12, exp, 0x0088CCFF, 0x0010141A);
    }

    /* Value line */
    if (calc_error) {
        mk_buf_text(s->pixels, s->w, s->h, 14, 30, "Error: Div by 0", 0x00FF4444, 0x0010141A);
    } else {
        char val_str[24];
        int_to_str(calc_val, val_str);
        int vlen = 0;
        while (val_str[vlen]) vlen++;
        int vx = s->w - 20 - vlen * 8;
        if (vx < 14) vx = 14;
        mk_buf_text(s->pixels, s->w, s->h, vx, 30, val_str, 0x0000FFCC, 0x0010141A);
    }

    /* Buttons */
    mk_buf_btn(s->pixels, s->w, s->h, 8, 58, 46, 32, "C", 0x00D13438, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 59, 58, 46, 32, "<-", 0x003A4455, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 110, 58, 46, 32, "+/-", 0x003A4455, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 161, 58, 47, 32, "/", 0x000078D7, 0x00FFFFFF, 0);

    mk_buf_btn(s->pixels, s->w, s->h, 8, 96, 46, 32, "7", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 59, 96, 46, 32, "8", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 110, 96, 46, 32, "9", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 161, 96, 47, 32, "*", 0x000078D7, 0x00FFFFFF, 0);

    mk_buf_btn(s->pixels, s->w, s->h, 8, 134, 46, 32, "4", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 59, 134, 46, 32, "5", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 110, 134, 46, 32, "6", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 161, 134, 47, 32, "-", 0x000078D7, 0x00FFFFFF, 0);

    mk_buf_btn(s->pixels, s->w, s->h, 8, 172, 46, 32, "1", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 59, 172, 46, 32, "2", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 110, 172, 46, 32, "3", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 161, 172, 47, 32, "+", 0x000078D7, 0x00FFFFFF, 0);

    mk_buf_btn(s->pixels, s->w, s->h, 8, 210, 97, 32, "0", 0x002C3240, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 110, 210, 98, 32, "=", 0x00107C10, 0x00FFFFFF, 0);
}

static void calc_on_click(int lx, int ly) {
    if (lx < 8 || lx >= 208) return;
    if (ly >= 58 && ly < 90) {
        if (lx < 56) calc_clear();
        else if (lx < 107) calc_backspace();
        else if (lx < 158) calc_negate();
        else calc_set_op(4); /* / */
    } else if (ly >= 96 && ly < 128) {
        if (lx < 56) calc_digit(7);
        else if (lx < 107) calc_digit(8);
        else if (lx < 158) calc_digit(9);
        else calc_set_op(3); /* * */
    } else if (ly >= 134 && ly < 166) {
        if (lx < 56) calc_digit(4);
        else if (lx < 107) calc_digit(5);
        else if (lx < 158) calc_digit(6);
        else calc_set_op(2); /* - */
    } else if (ly >= 172 && ly < 204) {
        if (lx < 56) calc_digit(1);
        else if (lx < 107) calc_digit(2);
        else if (lx < 158) calc_digit(3);
        else calc_set_op(1); /* + */
    } else if (ly >= 210 && ly < 242) {
        if (lx < 107) calc_digit(0);
        else calc_equals();
    }
}

static void open_calc(void) {
    if (surface_alive(id_calc)) {
        mk_surface_restore(id_calc);
        active_focus_id = id_calc;
        return;
    }
    OPEN_APP(id_calc, 216, 252, "Calculator", paint_calc);
}

/* ========================================================================= */
/* 2. Display Settings Application                                           */
/* ========================================================================= */
static void paint_display(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x001B202A);

    /* Header */
    mk_buf_box(s->pixels, s->w, s->h, 10, 10, s->w - 20, 28, 0x002D3748, 0x00141820);
    mk_buf_text(s->pixels, s->w, s->h, 18, 16, "Display & Hardware Configuration", 0x0038BDF8, 0x00141820);

    /* Resolution Section */
    mk_buf_text(s->pixels, s->w, s->h, 14, 46, "Select Screen Resolution:", 0x00E2E8F0, 0x001B202A);
    int cur_w = mk_screen_w();
    int m0_act = (cur_w == 640);
    int m1_act = (cur_w == 800);
    int m2_act = (cur_w == 1024);
    int m3_act = (cur_w == 1920);

    mk_buf_btn(s->pixels, s->w, s->h, 14, 66, 92, 26, m0_act ? "[*] 640x480" : "640x480",
               m0_act ? 0x000E4A7A : 0x0028303F, m0_act ? 0x0038BDF8 : 0x00FFFFFF, m0_act);
    mk_buf_btn(s->pixels, s->w, s->h, 114, 66, 92, 26, m1_act ? "[*] 800x600" : "800x600",
               m1_act ? 0x000E4A7A : 0x0028303F, m1_act ? 0x0038BDF8 : 0x00FFFFFF, m1_act);
    mk_buf_btn(s->pixels, s->w, s->h, 214, 66, 96, 26, m2_act ? "[*] 1024x768" : "1024x768",
               m2_act ? 0x000E4A7A : 0x0028303F, m2_act ? 0x0038BDF8 : 0x00FFFFFF, m2_act);
    mk_buf_btn(s->pixels, s->w, s->h, 318, 66, 96, 26, m3_act ? "[*] 1080p" : "1080p (FHD)",
               m3_act ? 0x000E4A7A : 0x0028303F, m3_act ? 0x0038BDF8 : 0x00FFFFFF, m3_act);

    /* Timer HZ Section */
    mk_buf_text(s->pixels, s->w, s->h, 14, 104, "Display Refresh / Timer Frequency (Cadence):", 0x00E2E8F0, 0x001B202A);
    uint32_t cur_hz = timer_hz();
    int h50  = (cur_hz == 50);
    int h100 = (cur_hz == 100);
    int h144 = (cur_hz == 144);
    int h200 = (cur_hz == 200);
    int h500 = (cur_hz == 500);

    mk_buf_btn(s->pixels, s->w, s->h, 14, 124, 72, 24, h50  ? "[*] 50Hz" : "50 Hz",
               h50  ? 0x00105C2B : 0x0028303F, h50  ? 0x004ADE80 : 0x00FFFFFF, h50);
    mk_buf_btn(s->pixels, s->w, s->h, 94, 124, 72, 24, h100 ? "[*] 100Hz" : "100 Hz",
               h100 ? 0x00105C2B : 0x0028303F, h100 ? 0x004ADE80 : 0x00FFFFFF, h100);
    mk_buf_btn(s->pixels, s->w, s->h, 174, 124, 76, 24, h144 ? "[*] 144Hz" : "144 Hz",
               h144 ? 0x00105C2B : 0x0028303F, h144 ? 0x004ADE80 : 0x00FFFFFF, h144);
    mk_buf_btn(s->pixels, s->w, s->h, 258, 124, 72, 24, h200 ? "[*] 200Hz" : "200 Hz",
               h200 ? 0x00105C2B : 0x0028303F, h200 ? 0x004ADE80 : 0x00FFFFFF, h200);
    mk_buf_btn(s->pixels, s->w, s->h, 338, 124, 76, 24, h500 ? "[*] 500Hz" : "500 Hz",
               h500 ? 0x00105C2B : 0x0028303F, h500 ? 0x004ADE80 : 0x00FFFFFF, h500);

    /* Hardware Info Box */
    mk_buf_box(s->pixels, s->w, s->h, 10, 158, s->w - 20, 74, 0x002D3748, 0x00141820);
    mk_buf_text(s->pixels, s->w, s->h, 16, 164, "Adapter: Bochs / QEMU DISPI Linear Framebuffer (32 MB)", 0x00CBD5E1, 0x00141820);
    mk_buf_text(s->pixels, s->w, s->h, 16, 184, "Pipeline: 32 bpp TrueColor | rep movsl DMA Burst Transfer", 0x0094A3B8, 0x00141820);

    char res_buf[64];
    char sw_str[12], sh_str[12], hz_str[12];
    u32_to_str((uint32_t)cur_w, sw_str);
    u32_to_str((uint32_t)mk_screen_h(), sh_str);
    u32_to_str(cur_hz ? cur_hz : 100, hz_str);
    int ri = 0;
    const char* rp = "Mode: ";
    while (*rp) res_buf[ri++] = *rp++;
    for (int j = 0; sw_str[j]; j++) res_buf[ri++] = sw_str[j];
    res_buf[ri++] = 'x';
    for (int j = 0; sh_str[j]; j++) res_buf[ri++] = sh_str[j];
    const char* hp = " @ ";
    while (*hp) res_buf[ri++] = *hp++;
    for (int j = 0; hz_str[j]; j++) res_buf[ri++] = hz_str[j];
    const char* fz = (cur_hz == 144) ? " Hz [144 FPS Jitter-Free]" : " Hz (PIT IRQ0)";
    while (*fz) res_buf[ri++] = *fz++;
    res_buf[ri] = '\0';
    mk_buf_text(s->pixels, s->w, s->h, 16, 204, res_buf, 0x00FACC15, 0x00141820);
}

static void display_on_click(int lx, int ly) {
    /* Resolution buttons at y = 66, h = 26 */
    if (ly >= 66 && ly < 92) {
        if (lx >= 14 && lx < 106) {
            mk_switch_resolution(VBE_MODE_640x480);
        } else if (lx >= 114 && lx < 206) {
            mk_switch_resolution(VBE_MODE_800x600);
        } else if (lx >= 214 && lx < 310) {
            mk_switch_resolution(VBE_MODE_1024x768);
        } else if (lx >= 318 && lx < 414) {
            mk_switch_resolution(VBE_MODE_1920x1080);
        }
    }
    /* Timer HZ buttons at y = 124, h = 24 */
    else if (ly >= 124 && ly < 148) {
        if (lx >= 14 && lx < 86) {
            timer_init(50);
        } else if (lx >= 94 && lx < 166) {
            timer_init(100);
        } else if (lx >= 174 && lx < 250) {
            timer_init(144);
        } else if (lx >= 258 && lx < 330) {
            timer_init(200);
        } else if (lx >= 338 && lx < 414) {
            timer_init(500);
        }
    }
}

static void open_display(void) {
    if (surface_alive(id_display)) {
        mk_surface_restore(id_display);
        active_focus_id = id_display;
        return;
    }
    OPEN_APP(id_display, 430, 246, "Display Settings", paint_display);
}

/* ========================================================================= */
/* 3. Task Manager (Tasks Application)                                       */
/* ========================================================================= */
static int taskmgr_selected_pid = -1;
static char taskmgr_status_msg[48] = "Select a task to terminate.";

static void paint_tasks(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x001B202A);

    /* Header Bar */
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x002D3748, 0x00141820);
    mk_buf_text(s->pixels, s->w, s->h, 16, 12, "Task Manager — Kernel Processes & Memory", 0x0038BDF8, 0x00141820);

    /* Memory Statistics from kmalloc_stats */
    size_t mem_used = 0, mem_total = 0, mem_free = 0;
    kmalloc_stats(&mem_used, &mem_total, &mem_free);
    char mem_line[64];
    char u_str[12], t_str[12], f_str[12];
    u32_to_str((uint32_t)(mem_used / 1024u), u_str);
    u32_to_str((uint32_t)(mem_total / 1024u), t_str);
    u32_to_str((uint32_t)(mem_free / 1024u), f_str);

    int mi = 0;
    const char* mp = "RAM: ";
    while (*mp) mem_line[mi++] = *mp++;
    for (int j = 0; u_str[j]; j++) mem_line[mi++] = u_str[j];
    mem_line[mi++] = '/';
    for (int j = 0; t_str[j]; j++) mem_line[mi++] = t_str[j];
    const char* kp = " KB used (";
    while (*kp) mem_line[mi++] = *kp++;
    for (int j = 0; f_str[j]; j++) mem_line[mi++] = f_str[j];
    const char* fp = " KB free)";
    while (*fp) mem_line[mi++] = *fp++;
    mem_line[mi] = '\0';
    mk_buf_text(s->pixels, s->w, s->h, 14, 38, mem_line, 0x00E2E8F0, 0x001B202A);

    /* Memory Usage Progress Bar */
    int bar_x = 10, bar_y = 56, bar_w = s->w - 20, bar_h = 10;
    mk_buf_box(s->pixels, s->w, s->h, bar_x, bar_y, bar_w, bar_h, 0x00334155, 0x000F172A);
    uint32_t u_kb = (uint32_t)(mem_used / 1024u);
    uint32_t t_kb = (uint32_t)(mem_total / 1024u);
    if (t_kb > 0) {
        int fill_w = (int)((u_kb * (uint32_t)(bar_w - 2)) / t_kb);
        if (fill_w > bar_w - 2) fill_w = bar_w - 2;
        if (fill_w > 0) {
            uint32_t bar_col = ((u_kb * 100u) / t_kb > 85) ? 0x00EF4444 : 0x0010B981;
            mk_buf_rect(s->pixels, s->w, s->h, bar_x + 1, bar_y + 1, fill_w, bar_h - 2, bar_col);
        }
    }

    /* Process Table Header */
    int tbl_y = 72;
    mk_buf_box(s->pixels, s->w, s->h, 10, tbl_y, s->w - 20, 20, 0x00334155, 0x00242D3C);
    mk_buf_text(s->pixels, s->w, s->h, 16,  tbl_y + 2, "PID", 0x0038BDF8, 0x00242D3C);
    mk_buf_text(s->pixels, s->w, s->h, 56,  tbl_y + 2, "NAME", 0x0038BDF8, 0x00242D3C);
    mk_buf_text(s->pixels, s->w, s->h, 144, tbl_y + 2, "STATE", 0x0038BDF8, 0x00242D3C);
    mk_buf_text(s->pixels, s->w, s->h, 224, tbl_y + 2, "TICKS", 0x0038BDF8, 0x00242D3C);
    mk_buf_text(s->pixels, s->w, s->h, 304, tbl_y + 2, "STACK", 0x0038BDF8, 0x00242D3C);

    /* Process Table Rows */
    struct task* cur = sched_current();
    struct task* all = cur ? (cur - cur->id) : 0;
    int row_y = tbl_y + 22;

    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        if (!all || all[i].state == TASK_FREE) continue;

        int is_sel = (taskmgr_selected_pid == all[i].id);
        uint32_t row_bg = is_sel ? 0x000E4A7A : ((i % 2) ? 0x00202735 : 0x00181D26);
        uint32_t text_fg = is_sel ? 0x00FFFFFF : 0x00CBD5E1;

        mk_buf_rect(s->pixels, s->w, s->h, 10, row_y, s->w - 20, 18, row_bg);

        /* PID */
        char pid_str[8];
        int_to_str(all[i].id, pid_str);
        mk_buf_text(s->pixels, s->w, s->h, 16, row_y + 1, pid_str, text_fg, row_bg);

        /* Name */
        mk_buf_text(s->pixels, s->w, s->h, 56, row_y + 1, all[i].name, text_fg, row_bg);

        /* State */
        const char* st_name = "READY";
        uint32_t st_col = 0x0038BDF8;
        if (all[i].state == TASK_RUNNING) { st_name = "RUNNING"; st_col = 0x004ADE80; }
        else if (all[i].state == TASK_BLOCKED) { st_name = "BLOCKED"; st_col = 0x00FACC15; }
        else if (all[i].state == TASK_ZOMBIE) { st_name = "ZOMBIE"; st_col = 0x00F87171; }
        mk_buf_text(s->pixels, s->w, s->h, 144, row_y + 1, st_name, is_sel ? 0x00FFFFFF : st_col, row_bg);

        /* Ticks */
        char t_str2[12];
        u32_to_str(all[i].ticks_ran, t_str2);
        mk_buf_text(s->pixels, s->w, s->h, 224, row_y + 1, t_str2, text_fg, row_bg);

        /* Stack */
        mk_buf_text(s->pixels, s->w, s->h, 304, row_y + 1, "4 KB", text_fg, row_bg);

        row_y += 19;
    }

    /* Status Message */
    mk_buf_text(s->pixels, s->w, s->h, 14, s->h - 58, taskmgr_status_msg, 0x00FACC15, 0x001B202A);

    /* Action Buttons */
    mk_buf_btn(s->pixels, s->w, s->h, 12, s->h - 36, 100, 26, "End Task", 0x00C42B1C, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 120, s->h - 36, 130, 26, "+ Spawn Worker", 0x00107C10, 0x00FFFFFF, 0);
    mk_buf_btn(s->pixels, s->w, s->h, 258, s->h - 36, 80, 26, "Refresh", 0x003A4455, 0x00FFFFFF, 0);
}

static void tasks_on_click(int lx, int ly) {
    struct task* cur = sched_current();
    struct task* all = cur ? (cur - cur->id) : 0;

    /* Check process row click (y in 94..220) */
    if (ly >= 94 && ly < 220 && all) {
        int clicked_row = (ly - 94) / 19;
        int active_idx = 0;
        for (int i = 0; i < SCHED_MAX_TASKS; i++) {
            if (all[i].state != TASK_FREE) {
                if (active_idx == clicked_row) {
                    taskmgr_selected_pid = all[i].id;
                    int mi = 0;
                    const char* sp = "Selected PID ";
                    while (*sp) taskmgr_status_msg[mi++] = *sp++;
                    char ps[8];
                    int_to_str(all[i].id, ps);
                    for (int j = 0; ps[j]; j++) taskmgr_status_msg[mi++] = ps[j];
                    taskmgr_status_msg[mi++] = ' ';
                    taskmgr_status_msg[mi++] = '(';
                    for (int j = 0; all[i].name[j]; j++) taskmgr_status_msg[mi++] = all[i].name[j];
                    taskmgr_status_msg[mi++] = ')';
                    taskmgr_status_msg[mi] = '\0';
                    return;
                }
                active_idx++;
            }
        }
    }

    /* Action buttons row */
    int btn_y = 280 - 36; /* relative to window h = 280 */
    if (ly >= btn_y && ly < btn_y + 26 && all) {
        /* End Task */
        if (lx >= 12 && lx < 112) {
            if (taskmgr_selected_pid < 0) {
                int mi = 0;
                const char* m = "No task selected!";
                while (*m) taskmgr_status_msg[mi++] = *m++;
                taskmgr_status_msg[mi] = 0;
            } else if (taskmgr_selected_pid == 0) {
                int mi = 0;
                const char* m = "Cannot kill PID 0 (system)!";
                while (*m) taskmgr_status_msg[mi++] = *m++;
                taskmgr_status_msg[mi] = 0;
            } else {
                int pid = taskmgr_selected_pid;
                if (pid > 0 && pid < SCHED_MAX_TASKS && all[pid].state != TASK_FREE) {
                    __asm__ volatile ("cli");
                    if (all[pid].stack_base) {
                        kfree(all[pid].stack_base);
                        all[pid].stack_base = 0;
                    }
                    all[pid].state = TASK_FREE;
                    all[pid].name[0] = '\0';
                    __asm__ volatile ("sti");

                    int mi = 0;
                    const char* m = "Terminated PID ";
                    while (*m) taskmgr_status_msg[mi++] = *m++;
                    char ps[8];
                    int_to_str(pid, ps);
                    for (int j = 0; ps[j]; j++) taskmgr_status_msg[mi++] = ps[j];
                    taskmgr_status_msg[mi] = 0;
                    taskmgr_selected_pid = -1;
                }
            }
        }
        /* Spawn Worker */
        else if (lx >= 120 && lx < 250) {
            int new_id = sched_spawn_worker();
            if (new_id >= 0) {
                int mi = 0;
                const char* m = "Spawned worker PID ";
                while (*m) taskmgr_status_msg[mi++] = *m++;
                char ps[8];
                int_to_str(new_id, ps);
                for (int j = 0; ps[j]; j++) taskmgr_status_msg[mi++] = ps[j];
                taskmgr_status_msg[mi] = 0;
            } else {
                int mi = 0;
                const char* m = "Spawn failed (max tasks reached).";
                while (*m) taskmgr_status_msg[mi++] = *m++;
                taskmgr_status_msg[mi] = 0;
            }
        }
        /* Refresh */
        else if (lx >= 258 && lx < 338) {
            int mi = 0;
            const char* m = "Task list refreshed.";
            while (*m) taskmgr_status_msg[mi++] = *m++;
            taskmgr_status_msg[mi] = 0;
        }
    }
}

static void open_tasks(void) {
    if (surface_alive(id_tasks)) {
        mk_surface_restore(id_tasks);
        active_focus_id = id_tasks;
        return;
    }
    OPEN_APP(id_tasks, 420, 280, "Task Manager", paint_tasks);
}

/* ========================================================================= */
/* 4. Native Terminal Shell Application                                      */
/* ========================================================================= */
#define TERM_MAX_LINES 256
#define TERM_LINE_LEN  80
static char term_lines[TERM_MAX_LINES][TERM_LINE_LEN];
static uint32_t term_cols[TERM_MAX_LINES];
static int term_num_lines = 0;
static int term_cur_col = 0;

static char term_input[TERM_LINE_LEN];
static int term_in_pos = 0;
static int term_in_len = 0;
static int term_scroll_offset = 0; /* 0 = bottom, >0 = scrolled up */

#define TERM_HIST_MAX 16
static char term_hist[TERM_HIST_MAX][TERM_LINE_LEN];
static int term_hist_count = 0;
static int term_hist_idx = 0;

static void paint_term(struct mk_surface* s);

static void desktop_term_putchar(char c) {
    if (c == '\r') {
        term_cur_col = 0;
        return;
    }
    if (c == '\n') {
        if (term_num_lines < TERM_MAX_LINES) {
            term_lines[term_num_lines][term_cur_col] = '\0';
        }
        term_cur_col = 0;
        term_num_lines++;
        if (term_num_lines >= TERM_MAX_LINES) {
            for (int i = 0; i < TERM_MAX_LINES - 1; i++) {
                for (int j = 0; j < TERM_LINE_LEN; j++)
                    term_lines[i][j] = term_lines[i + 1][j];
                term_cols[i] = term_cols[i + 1];
            }
            term_num_lines = TERM_MAX_LINES - 1;
        }
        term_lines[term_num_lines][0] = '\0';
        term_cols[term_num_lines] = 0x00E2E8F0;
        term_scroll_offset = 0;
        return;
    }
    if (c == '\b') {
        if (term_cur_col > 0) {
            term_cur_col--;
            term_lines[term_num_lines][term_cur_col] = '\0';
        }
        return;
    }
    if (c == '\t') {
        int spaces = 4 - (term_cur_col % 4);
        for (int t = 0; t < spaces; t++) desktop_term_putchar(' ');
        return;
    }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
        if (term_cur_col >= TERM_LINE_LEN - 1) {
            term_lines[term_num_lines][term_cur_col] = '\0';
            term_cur_col = 0;
            term_num_lines++;
            if (term_num_lines >= TERM_MAX_LINES) {
                for (int i = 0; i < TERM_MAX_LINES - 1; i++) {
                    for (int j = 0; j < TERM_LINE_LEN; j++)
                        term_lines[i][j] = term_lines[i + 1][j];
                    term_cols[i] = term_cols[i + 1];
                }
                term_num_lines = TERM_MAX_LINES - 1;
            }
            term_lines[term_num_lines][0] = '\0';
            term_cols[term_num_lines] = 0x00E2E8F0;
        }
        term_lines[term_num_lines][term_cur_col++] = c;
        term_lines[term_num_lines][term_cur_col] = '\0';
        term_cols[term_num_lines] = 0x00E2E8F0;
    }
}

static void desktop_term_clear(void) {
    for (int i = 0; i < TERM_MAX_LINES; i++) {
        term_lines[i][0] = '\0';
        term_cols[i] = 0x00E2E8F0;
    }
    term_num_lines = 0;
    term_cur_col = 0;
    term_scroll_offset = 0;
}

static void term_append(const char* s, uint32_t col) {
    while (s && *s) desktop_term_putchar(*s++);
    if (col && term_num_lines > 0) {
        term_cols[term_num_lines - 1] = col;
    }
    desktop_term_putchar('\n');
}

static void paint_term(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x000E141D);

    /* Terminal Header Bar */
    mk_buf_box(s->pixels, s->w, s->h, 0, 0, s->w, 20, 0x001B2433, 0x00141B26);
    mk_buf_text(s->pixels, s->w, s->h, 8, 2, "MyKernel Native Shell v0.4 (i686 Protected Mode)", 0x0038BDF8, 0x00141B26);

    int max_visible = (s->h - 46) / 16;
    if (max_visible < 1) max_visible = 1;

    int total_lines = term_num_lines + (term_cur_col > 0 ? 1 : 0);

    /* Calculate start line taking scroll offset into account */
    int bottom = total_lines - term_scroll_offset;
    if (bottom < max_visible) bottom = max_visible;
    if (bottom > total_lines) bottom = total_lines;

    int start = bottom - max_visible;
    if (start < 0) start = 0;

    int y = 24;
    for (int i = start; i < bottom && i < total_lines; i++) {
        if (i < term_num_lines) {
            mk_buf_text(s->pixels, s->w, s->h, 8, y, term_lines[i], term_cols[i], 0x000E141D);
        } else {
            mk_buf_text(s->pixels, s->w, s->h, 8, y, term_lines[term_num_lines], 0x00E2E8F0, 0x000E141D);
        }
        y += 16;
    }

    /* Scrollbar on the right */
    int sb_x = s->w - 14;
    int sb_w = 12;
    int sb_top = 22;
    int sb_h = s->h - 46;
    mk_buf_box(s->pixels, s->w, s->h, sb_x, sb_top, sb_w, sb_h, 0x00263244, 0x000A0E15);
    /* Up/down arrows */
    mk_buf_btn(s->pixels, s->w, s->h, sb_x, sb_top, sb_w, 12, "^", 0x001F2937, 0x0094A3B8, 0);
    mk_buf_btn(s->pixels, s->w, s->h, sb_x, sb_top + sb_h - 12, sb_w, 12, "v", 0x001F2937, 0x0094A3B8, 0);
    /* Scroll thumb */
    if (total_lines > max_visible) {
        int track_h = sb_h - 26;
        int thumb_h = (track_h * max_visible) / total_lines;
        if (thumb_h < 8) thumb_h = 8;
        int max_scroll = total_lines - max_visible;
        int scroll_pos = max_scroll - term_scroll_offset;
        if (scroll_pos < 0) scroll_pos = 0;
        int thumb_y = sb_top + 13 + ((track_h - thumb_h) * scroll_pos) / max_scroll;
        mk_buf_rect(s->pixels, s->w, s->h, sb_x + 1, thumb_y, sb_w - 2, thumb_h, 0x0038BDF8);
    }

    /* Prompt line at bottom */
    mk_buf_box(s->pixels, s->w, s->h, 0, s->h - 22, s->w, 22, 0x001E293B, 0x000A0F17);
    char pwd[32];
    fs_pwd(pwd, sizeof(pwd));
    char prompt[TERM_LINE_LEN + 32];
    int pi = 0;
    for (int i = 0; pwd[i] && pi < 16; i++) prompt[pi++] = pwd[i];
    prompt[pi++] = '>';
    prompt[pi++] = ' ';
    for (int i = 0; i < term_in_pos && pi < TERM_LINE_LEN; i++) prompt[pi++] = term_input[i];
    prompt[pi] = '\0';

    mk_buf_text(s->pixels, s->w, s->h, 8, s->h - 18, prompt, 0x0000FF66, 0x000A0F17);

    /* Blinking Block Cursor */
    int cur_screen_x = 8 + pi * 8;
    static int cur_blink = 0;
    cur_blink++;
    if ((cur_blink / 20) % 2 == 0) {
        mk_buf_rect(s->pixels, s->w, s->h, cur_screen_x, s->h - 18, 8, 14, 0x0000FF66);
        if (term_in_pos < term_in_len) {
            mk_buf_char(s->pixels, s->w, s->h, cur_screen_x, s->h - 18, term_input[term_in_pos], 0x00000000, 0x0000FF66);
        }
    }

    /* Remainder of input after cursor */
    if (term_in_pos < term_in_len) {
        char rest[TERM_LINE_LEN];
        int ri = 0;
        for (int i = term_in_pos + 1; i < term_in_len && ri < TERM_LINE_LEN - 1; i++)
            rest[ri++] = term_input[i];
        rest[ri] = '\0';
        mk_buf_text(s->pixels, s->w, s->h, cur_screen_x + 8, s->h - 18, rest, 0x00FFFFFF, 0x000A0F17);
    }
}

static void term_exec_cmd(void) {
    char pwd[32];
    fs_pwd(pwd, sizeof(pwd));

    /* Echo prompt and command */
    for (int i = 0; pwd[i]; i++) desktop_term_putchar(pwd[i]);
    desktop_term_putchar('>');
    desktop_term_putchar(' ');
    for (int i = 0; i < term_in_len; i++) desktop_term_putchar(term_input[i]);
    desktop_term_putchar('\n');
    if (term_num_lines > 0) {
        term_cols[term_num_lines - 1] = 0x00FFFFFF;
    }

    char* cmd = term_input;
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') {
        term_in_pos = 0;
        term_in_len = 0;
        term_input[0] = '\0';
        return;
    }

    /* History */
    if (term_hist_count == 0 || !desk_streq(term_hist[term_hist_count - 1], cmd)) {
        if (term_hist_count < TERM_HIST_MAX) {
            int ci = 0;
            while (cmd[ci] && ci < TERM_LINE_LEN - 1) {
                term_hist[term_hist_count][ci] = cmd[ci];
                ci++;
            }
            term_hist[term_hist_count][ci] = '\0';
            term_hist_count++;
        } else {
            for (int i = 0; i < TERM_HIST_MAX - 1; i++) {
                for (int j = 0; j < TERM_LINE_LEN; j++)
                    term_hist[i][j] = term_hist[i + 1][j];
            }
            int ci = 0;
            while (cmd[ci] && ci < TERM_LINE_LEN - 1) {
                term_hist[TERM_HIST_MAX - 1][ci] = cmd[ci];
                ci++;
            }
            term_hist[TERM_HIST_MAX - 1][ci] = '\0';
        }
    }
    term_hist_idx = term_hist_count;

    if (desk_streq(cmd, "exit") || desk_streq(cmd, "quit")) {
        on_close_id(id_term);
        mk_surface_destroy(id_term);
        if (active_focus_id == id_term) active_focus_id = -1;
        term_in_pos = 0;
        term_in_len = 0;
        term_input[0] = '\0';
        return;
    }

    if (desk_streq(cmd, "clear")) {
        desktop_term_clear();
        term_in_pos = 0;
        term_in_len = 0;
        term_input[0] = '\0';
        return;
    }

    /* Execute command via kernel shell engine */
    terminal_set_putchar_hook(desktop_term_putchar);
    terminal_set_clear_hook(desktop_term_clear);
    shell_execute_command(cmd);
    terminal_set_putchar_hook(0);
    terminal_set_clear_hook(0);

    term_in_pos = 0;
    term_in_len = 0;
    term_input[0] = '\0';
    term_scroll_offset = 0;
}

static void term_on_click(int lx, int ly, int w, int h) {
    /* Scrollbar clicked */
    if (lx >= w - 14) {
        if (ly >= 22 && ly < 34) {
            /* Up arrow */
            int max_visible = (h - 46) / 16;
            int total_lines = term_num_lines + (term_cur_col > 0 ? 1 : 0);
            if (term_scroll_offset + 3 <= total_lines - max_visible)
                term_scroll_offset += 3;
            else if (total_lines > max_visible)
                term_scroll_offset = total_lines - max_visible;
        } else if (ly >= h - 36 && ly < h - 24) {
            /* Down arrow */
            if (term_scroll_offset >= 3)
                term_scroll_offset -= 3;
            else
                term_scroll_offset = 0;
        }
    }
}

static void open_term(void) {
    if (surface_alive(id_term)) {
        mk_surface_restore(id_term);
        active_focus_id = id_term;
        return;
    }
    if (term_num_lines == 0 && term_cur_col == 0) {
        term_append("MyKernel Shell v0.4 (i686 Protected Mode)", 0x0000FF66);
        term_append("Type 'help' for commands, 'tasks' for processes, 'exit' to close", 0x0038BDF8);
    }
    OPEN_APP(id_term, 520, 300, "Terminal", paint_term);
}

/* ========================================================================= */
/* 5. Auxiliary App Painters                                                 */
/* ========================================================================= */
static void paint_welcome(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F8FAFC);
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x000078D7, 0x000078D7);
    mk_buf_text(s->pixels, s->w, s->h, 18, 12, "Welcome to MyKernel Desktop v0.4", 0x00FFFFFF, 0x000078D7);

    mk_buf_text(s->pixels, s->w, s->h, 14, 40, "* Click desktop icons or Start menu to open apps", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 60, "* Taskbar displays open windows - click to raise/min", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 80, "* Display app: switch 640x480, 800x600, 1024x768 & HZ", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 100, "* Task Manager: inspect processes, CPU ticks & terminate", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 120, "* Drag title bars to move; click '_' to min, 'X' to close", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 146, "Press ESC to close Start menu or leave desktop", 0x000078D7, 0x00F8FAFC);
}

static void open_welcome(void) { OPEN_APP(id_welcome, 420, 180, "Welcome", paint_welcome); }

static void paint_clock(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00111827);
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x001F2937, 0x001F2937);
    mk_buf_text(s->pixels, s->w, s->h, 16, 12, "System Clock & Uptime", 0x0038BDF8, 0x001F2937);

    char line[48];
    format_uptime(line, timer_ticks(), timer_hz());
    mk_buf_box(s->pixels, s->w, s->h, 16, 42, s->w - 32, 34, 0x00374151, 0x00030712);
    mk_buf_text(s->pixels, s->w, s->h, 32, 50, line, 0x0038BDF8, 0x00030712);

    char t[32];
    u32_to_str(timer_ticks(), t);
    int i = 0;
    line[i++] = 'T'; line[i++] = 'i'; line[i++] = 'c'; line[i++] = 'k'; line[i++] = 's'; line[i++] = ':'; line[i++] = ' ';
    for (int j = 0; t[j]; j++) line[i++] = t[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 18, 86, line, 0x009CA3AF, 0x00111827);

    u32_to_str(timer_hz() ? timer_hz() : 100, t);
    i = 0;
    line[i++] = 'P'; line[i++] = 'I'; line[i++] = 'T'; line[i++] = ' '; line[i++] = 'H'; line[i++] = 'z'; line[i++] = ':'; line[i++] = ' ';
    for (int j = 0; t[j]; j++) line[i++] = t[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 18, 106, line, 0x009CA3AF, 0x00111827);
}

static void open_clock(void) { OPEN_APP(id_clock, 260, 140, "Clock", paint_clock); }

static void paint_sysinfo(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F8FAFC);
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x000078D7, 0x000078D7);
    mk_buf_text(s->pixels, s->w, s->h, 18, 12, "System Information", 0x00FFFFFF, 0x000078D7);

    mk_buf_text(s->pixels, s->w, s->h, 14, 40, "OS: MyKernel v0.4 (Protected Mode)", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 60, "Architecture: i686 (32-bit x86)", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 80, "Display: Bochs/QEMU VBE LFB", 0x001E293B, 0x00F8FAFC);

    char res[48];
    char sw[12], sh[12];
    u32_to_str((uint32_t)mk_screen_w(), sw);
    u32_to_str((uint32_t)mk_screen_h(), sh);
    int ri = 0;
    const char* rp = "Resolution: ";
    while (*rp) res[ri++] = *rp++;
    for (int j = 0; sw[j]; j++) res[ri++] = sw[j];
    res[ri++] = 'x';
    for (int j = 0; sh[j]; j++) res[ri++] = sh[j];
    const char* bp = " @ 32 bpp";
    while (*bp) res[ri++] = *bp++;
    res[ri] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 14, 100, res, 0x001E293B, 0x00F8FAFC);

    char line[48];
    int i = 0;
    const char* p = "Active Tasks: ";
    while (*p) line[i++] = *p++;
    char n[12];
    u32_to_str((uint32_t)sched_task_count(), n);
    for (int j = 0; n[j]; j++) line[i++] = n[j];
    line[i] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 14, 120, line, 0x001E293B, 0x00F8FAFC);

    size_t u = 0, t = 0, f = 0;
    kmalloc_stats(&u, &t, &f);
    char mline[48];
    char uk[12], tk[12];
    u32_to_str((uint32_t)(u / 1024u), uk);
    u32_to_str((uint32_t)(t / 1024u), tk);
    int mi = 0;
    const char* mp = "Kernel Heap: ";
    while (*mp) mline[mi++] = *mp++;
    for (int j = 0; uk[j]; j++) mline[mi++] = uk[j];
    mline[mi++] = '/';
    for (int j = 0; tk[j]; j++) mline[mi++] = tk[j];
    const char* kp = " KB";
    while (*kp) mline[mi++] = *kp++;
    mline[mi] = 0;
    mk_buf_text(s->pixels, s->w, s->h, 14, 140, mline, 0x001E293B, 0x00F8FAFC);
}

static void open_sysinfo(void) { OPEN_APP(id_sysinfo, 320, 180, "SysInfo", paint_sysinfo); }

static void paint_about(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F8FAFC);
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x004F46E5, 0x004F46E5);
    mk_buf_text(s->pixels, s->w, s->h, 18, 12, "About MyKernel", 0x00FFFFFF, 0x004F46E5);
    mk_buf_text(s->pixels, s->w, s->h, 14, 42, "Advanced Hobby OS", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 64, "Preemptive Scheduler & Dyld ELF", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 86, "Wayland-style Window Compositor", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 108, "Taskbar, Display, Tasks & Shell", 0x000078D7, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 134, "(c) 2026 MyKernel Project", 0x0064748B, 0x00F8FAFC);
}

static void open_about(void) { OPEN_APP(id_about, 320, 170, "About", paint_about); }

static void paint_notes(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00FEF08A);
    mk_buf_text(s->pixels, s->w, s->h, 12, 10, "Sticky Notes", 0x00854D0E, 0x00FEF08A);
    mk_buf_hline(s->pixels, s->w, s->h, 10, 28, s->w - 20, 0x00EAB308);
    mk_buf_text(s->pixels, s->w, s->h, 14, 38, "- python build.py to build", 0x00713F12, 0x00FEF08A);
    mk_buf_text(s->pixels, s->w, s->h, 14, 60, "- open display to change mode", 0x00713F12, 0x00FEF08A);
    mk_buf_text(s->pixels, s->w, s->h, 14, 82, "- tasks app to end worker tasks", 0x00713F12, 0x00FEF08A);
    mk_buf_text(s->pixels, s->w, s->h, 14, 104, "- native terminal supports shell", 0x00713F12, 0x00FEF08A);
    mk_buf_text(s->pixels, s->w, s->h, 14, 130, "(read-only sticky)", 0x00A16207, 0x00FEF08A);
}

static void open_notes(void) { OPEN_APP(id_notes, 280, 160, "Notes", paint_notes); }

static void paint_help(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F8FAFC);
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x002563EB, 0x002563EB);
    mk_buf_text(s->pixels, s->w, s->h, 18, 12, "Desktop Help", 0x00FFFFFF, 0x002563EB);
    mk_buf_text(s->pixels, s->w, s->h, 14, 40, "1. Click icons or Start to launch", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 62, "2. Click taskbar buttons to toggle", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 84, "3. Drag title bar to move windows", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 106, "4. Click '_' on title to minimize", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 128, "5. Click 'X' on title to close", 0x001E293B, 0x00F8FAFC);
}

static void open_help(void) { OPEN_APP(id_help, 320, 160, "Help", paint_help); }

static void paint_demo(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00111827);
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x007C3AED, 0x007C3AED);
    mk_buf_text(s->pixels, s->w, s->h, 18, 12, "Interactive Demo", 0x00FFFFFF, 0x007C3AED);
    mk_buf_text(s->pixels, s->w, s->h, 14, 42, "Shell commands: exec hello", 0x00A7F3D0, 0x00111827);
    mk_buf_text(s->pixels, s->w, s->h, 14, 66, "Shell commands: ping 127.0.0.1", 0x00A7F3D0, 0x00111827);
    mk_buf_text(s->pixels, s->w, s->h, 14, 90, "Display: dynamic resolution switch", 0x00E0E7FF, 0x00111827);
    mk_buf_text(s->pixels, s->w, s->h, 14, 114, "Taskbar: window button toggling", 0x00E0E7FF, 0x00111827);
}

static void open_demo(void) { OPEN_APP(id_demo, 340, 150, "Demo", paint_demo); }

static void paint_trash(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F8FAFC);
    mk_buf_box(s->pixels, s->w, s->h, 10, 8, s->w - 20, 24, 0x00475569, 0x00475569);
    mk_buf_text(s->pixels, s->w, s->h, 18, 12, "Recycle Bin", 0x00FFFFFF, 0x00475569);
    mk_buf_text(s->pixels, s->w, s->h, 14, 44, "Recycle Bin is currently empty.", 0x001E293B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 70, "Deleted files will appear here.", 0x0064748B, 0x00F8FAFC);
    mk_buf_text(s->pixels, s->w, s->h, 14, 96, "Anchor: Bottom-Right of Desktop", 0x000078D7, 0x00F8FAFC);
}

static void open_trash(void) { OPEN_APP(id_trash, 320, 140, "Recycle Bin", paint_trash); }

static void on_close_id(int id) {
    if (id == id_welcome) id_welcome = -1;
    if (id == id_clock)   id_clock = -1;
    if (id == id_sysinfo) id_sysinfo = -1;
    if (id == id_calc)    id_calc = -1;
    if (id == id_term)    id_term = -1;
    if (id == id_about)   id_about = -1;
    if (id == id_tasks)   id_tasks = -1;
    if (id == id_display) id_display = -1;
    if (id == id_notes)   id_notes = -1;
    if (id == id_help)    id_help = -1;
    if (id == id_trash)   id_trash = -1;
    if (id == id_demo)    id_demo = -1;
    if (id == active_focus_id) active_focus_id = -1;
}

/* ========================================================================= */
/* 6. Desktop Icons & Pixel Art Redesign                                     */
/* ========================================================================= */
#define ICON_W 68
#define ICON_H 74
#define PANEL_H 28

struct desk_icon {
    int col, row;
    int anchor; /* 0 = normal grid, 1 = bottom-right */
    const char* label;
    void (*open)(void);
};

static struct desk_icon icons[] = {
    { 0, 0, 0, "Terminal", open_term },
    { 0, 1, 0, "Tasks",    open_tasks },
    { 0, 2, 0, "Display",  open_display },
    { 0, 3, 0, "Calc",     open_calc },
    { 0, 4, 0, "Notes",    open_notes },
    { 0, 5, 0, "Welcome",  open_welcome },
    { 1, 0, 0, "Clock",    open_clock },
    { 1, 1, 0, "SysInfo",  open_sysinfo },
    { 1, 2, 0, "Help",     open_help },
    { 1, 3, 0, "About",    open_about },
    { 1, 4, 0, "Demo",     open_demo },
    { -1, -1, 1, "Recycle", open_trash },
};
#define N_ICONS ((int)(sizeof(icons) / sizeof(icons[0])))

static void icon_pos(int i, int* ox, int* oy) {
    if (icons[i].anchor == 1) {
        *ox = mk_screen_w() - ICON_W - 16;
        *oy = mk_screen_h() - PANEL_H - ICON_H - 12;
    } else {
        *ox = 24 + icons[i].col * 84;
        *oy = 20 + icons[i].row * 76;
    }
}

/* Pixel Art Drawing Functions for Desktop Icons (32x32) */
static void draw_art_terminal(int x, int y) {
    /* CRT monitor casing */
    mk_bb_fill(x + 1, y + 2, 34, 25, 0x001E293B);
    mk_bb_fill(x + 2, y + 1, 32, 27, 0x001E293B);
    /* Screen bevel */
    mk_bb_fill(x + 4, y + 3, 28, 20, 0x000F172A);
    /* Dark CRT screen */
    mk_bb_fill(x + 5, y + 4, 26, 18, 0x00070D18);
    /* Cyan scanline highlight */
    mk_bb_fill(x + 6, y + 5, 24, 1, 0x001E3A5F);
    /* Green prompt '>_' */
    mk_bb_text(x + 8, y + 8, ">_", 0x0000FF66, 0x00070D18);
    /* Small cursor */
    mk_bb_fill(x + 24, y + 10, 4, 7, 0x00FACC15);
    /* Monitor stand neck & base */
    mk_bb_fill(x + 15, y + 27, 6, 3, 0x00475569);
    mk_bb_fill(x + 10, y + 30, 16, 2, 0x0064748B);
}

static void draw_art_tasks(int x, int y) {
    /* Clipboard backing */
    mk_bb_fill(x + 3, y + 3, 30, 29, 0x001E293B);
    /* Paper sheet */
    mk_bb_fill(x + 5, y + 5, 26, 25, 0x00F8FAFC);
    /* Top metal clip */
    mk_bb_fill(x + 12, y + 1, 12, 5, 0x0094A3B8);
    mk_bb_fill(x + 14, y + 3, 8, 2, 0x00475569);
    /* 3 Performance bar charts */
    /* Bar 1: Green */
    mk_bb_fill(x + 8, y + 10, 4, 4, 0x0010B981);
    mk_bb_fill(x + 14, y + 11, 14, 2, 0x0010B981);
    /* Bar 2: Cyan */
    mk_bb_fill(x + 8, y + 17, 4, 4, 0x0006B6D4);
    mk_bb_fill(x + 14, y + 18, 15, 2, 0x0006B6D4);
    /* Bar 3: Orange */
    mk_bb_fill(x + 8, y + 24, 4, 4, 0x00F59E0B);
    mk_bb_fill(x + 14, y + 25, 11, 2, 0x00F59E0B);
}

static void draw_art_display(int x, int y) {
    /* Modern widescreen frame */
    mk_bb_fill(x + 1, y + 4, 34, 23, 0x000F172A);
    /* Screen display */
    mk_bb_fill(x + 3, y + 6, 30, 18, 0x0038BDF8); /* Blue sky */
    /* Sun */
    mk_bb_fill(x + 23, y + 8, 5, 5, 0x00FACC15);
    /* Green hills landscape */
    mk_bb_fill(x + 3, y + 16, 16, 8, 0x0022C55E);
    mk_bb_fill(x + 17, y + 14, 16, 10, 0x0016A34A);
    /* Stand */
    mk_bb_fill(x + 15, y + 27, 6, 3, 0x0064748B);
    mk_bb_fill(x + 11, y + 30, 14, 2, 0x0094A3B8);
}

static void draw_art_calc(int x, int y) {
    /* Body */
    mk_bb_fill(x + 4, y + 2, 28, 30, 0x001E293B);
    mk_bb_fill(x + 5, y + 3, 26, 28, 0x00334155);
    /* LCD display */
    mk_bb_fill(x + 7, y + 5, 22, 6, 0x006EE7B7);
    mk_bb_fill(x + 18, y + 7, 8, 2, 0x00064E3B);
    /* Keypad grid */
    /* Row 0 */
    mk_bb_fill(x + 7, y + 13, 5, 4, 0x00EF4444);
    mk_bb_fill(x + 13, y + 13, 5, 4, 0x003B82F6);
    mk_bb_fill(x + 19, y + 13, 5, 4, 0x003B82F6);
    mk_bb_fill(x + 24, y + 13, 5, 4, 0x000284C7);
    /* Row 1 */
    mk_bb_fill(x + 7, y + 19, 5, 4, 0x00E2E8F0);
    mk_bb_fill(x + 13, y + 19, 5, 4, 0x00E2E8F0);
    mk_bb_fill(x + 19, y + 19, 5, 4, 0x00E2E8F0);
    mk_bb_fill(x + 24, y + 19, 5, 4, 0x000284C7);
    /* Row 2 */
    mk_bb_fill(x + 7, y + 25, 11, 4, 0x00E2E8F0);
    mk_bb_fill(x + 19, y + 25, 10, 4, 0x0010B981);
}

static void draw_art_clock(int x, int y) {
    /* Gold circular rim */
    mk_bb_fill(x + 4, y + 2, 28, 30, 0x00D97706);
    mk_bb_fill(x + 2, y + 4, 32, 26, 0x00D97706);
    /* White dial */
    mk_bb_fill(x + 6, y + 4, 24, 26, 0x00F8FAFC);
    mk_bb_fill(x + 4, y + 6, 28, 22, 0x00F8FAFC);
    /* Hour ticks */
    mk_bb_fill(x + 17, y + 6, 2, 3, 0x001E293B);  /* 12 */
    mk_bb_fill(x + 28, y + 16, 3, 2, 0x001E293B); /* 3 */
    mk_bb_fill(x + 17, y + 25, 2, 3, 0x001E293B); /* 6 */
    mk_bb_fill(x + 5, y + 16, 3, 2, 0x001E293B);  /* 9 */
    /* Hour hand (pointing to 10) */
    mk_bb_fill(x + 14, y + 11, 4, 3, 0x000F172A);
    mk_bb_fill(x + 16, y + 13, 3, 4, 0x000F172A);
    /* Minute hand (pointing to 2) */
    mk_bb_fill(x + 19, y + 13, 6, 2, 0x000F172A);
    mk_bb_fill(x + 23, y + 11, 3, 3, 0x000F172A);
    /* Red center dot & second hand */
    mk_bb_fill(x + 17, y + 17, 2, 7, 0x00EF4444);
    mk_bb_fill(x + 16, y + 16, 4, 4, 0x00DC2626);
}

static void draw_art_sysinfo(int x, int y) {
    /* CPU chip dark body */
    mk_bb_fill(x + 4, y + 4, 28, 26, 0x000F172A);
    /* Gold pins on 4 sides */
    for (int p = 0; p < 4; p++) {
        int px = x + 7 + p * 6;
        mk_bb_fill(px, y + 1, 3, 3, 0x00F59E0B);  /* Top */
        mk_bb_fill(px, y + 30, 3, 3, 0x00F59E0B); /* Bottom */
        int py = y + 7 + p * 5;
        mk_bb_fill(x + 1, py, 3, 3, 0x00F59E0B);  /* Left */
        mk_bb_fill(x + 32, py, 3, 3, 0x00F59E0B); /* Right */
    }
    /* Silver heatspreader */
    mk_bb_fill(x + 8, y + 8, 20, 18, 0x00475569);
    mk_bb_fill(x + 9, y + 9, 18, 16, 0x0064748B);
    /* "CPU" core */
    mk_bb_text(x + 10, y + 10, "CPU", 0x0038BDF8, 0x0064748B);
}

static void draw_art_notes(int x, int y) {
    /* Yellow pad */
    mk_bb_fill(x + 5, y + 3, 26, 28, 0x00FEF08A);
    /* Folded dog-ear at bottom-right */
    mk_bb_fill(x + 24, y + 24, 7, 7, 0x00FACC15);
    mk_bb_fill(x + 23, y + 23, 1, 8, 0x00CA8A04);
    mk_bb_fill(x + 23, y + 23, 8, 1, 0x00CA8A04);
    /* Red left margin line */
    mk_bb_fill(x + 10, y + 5, 1, 24, 0x00F87171);
    /* Ruled note lines */
    mk_bb_fill(x + 13, y + 9, 14, 1, 0x00CA8A04);
    mk_bb_fill(x + 13, y + 14, 14, 1, 0x00CA8A04);
    mk_bb_fill(x + 13, y + 19, 10, 1, 0x00CA8A04);
}

static void draw_art_help(int x, int y) {
    /* Sapphire blue circular lifebuoy */
    mk_bb_fill(x + 4, y + 2, 28, 30, 0x002563EB);
    mk_bb_fill(x + 2, y + 4, 32, 26, 0x002563EB);
    /* White reflective rescue cross bands */
    mk_bb_fill(x + 15, y + 2, 6, 6, 0x00F8FAFC);
    mk_bb_fill(x + 15, y + 26, 6, 6, 0x00F8FAFC);
    mk_bb_fill(x + 2, y + 14, 6, 6, 0x00F8FAFC);
    mk_bb_fill(x + 28, y + 14, 6, 6, 0x00F8FAFC);
    /* Question mark "?" */
    mk_bb_text(x + 15, y + 10, "?", 0x00FFFFFF, 0x002563EB);
}

static void draw_art_welcome(int x, int y) {
    /* Colorful greeting window */
    mk_bb_fill(x + 3, y + 3, 30, 27, 0x00EA580C);
    mk_bb_fill(x + 5, y + 5, 26, 23, 0x0038BDF8);
    /* Golden Sun */
    mk_bb_fill(x + 14, y + 8, 8, 8, 0x00FACC15);
    /* Celebration red ribbon */
    mk_bb_fill(x + 5, y + 21, 26, 7, 0x00DC2626);
    mk_bb_text(x + 6, y + 17, "MK", 0x00FFFFFF, 0x00DC2626);
}

static void draw_art_about(int x, int y) {
    /* Indigo circular medallion */
    mk_bb_fill(x + 4, y + 2, 28, 30, 0x004F46E5);
    mk_bb_fill(x + 2, y + 4, 32, 26, 0x004F46E5);
    /* Outer highlight ring */
    mk_bb_fill(x + 6, y + 4, 24, 2, 0x00818CF8);
    mk_bb_fill(x + 4, y + 6, 2, 22, 0x00818CF8);
    /* White serif "i" information glyph */
    mk_bb_text(x + 16, y + 10, "i", 0x00FFFFFF, 0x004F46E5);
}

static void draw_art_demo(int x, int y) {
    /* Gamepad body */
    mk_bb_fill(x + 3, y + 8, 30, 18, 0x007C3AED);
    mk_bb_fill(x + 1, y + 12, 34, 10, 0x007C3AED);
    /* Grip indents */
    mk_bb_fill(x + 14, y + 20, 8, 6, 0x002B579A);
    /* D-pad cross */
    mk_bb_fill(x + 5, y + 14, 8, 3, 0x00111827);
    mk_bb_fill(x + 7, y + 12, 3, 7, 0x00111827);
    /* 4 Action buttons */
    mk_bb_fill(x + 26, y + 11, 3, 3, 0x00EF4444); /* Red */
    mk_bb_fill(x + 22, y + 14, 3, 3, 0x00FACC15); /* Yellow */
    mk_bb_fill(x + 29, y + 14, 3, 3, 0x003B82F6); /* Blue */
    mk_bb_fill(x + 26, y + 17, 3, 3, 0x0010B981); /* Green */
}

static void draw_art_trash(int x, int y) {
    /* Wastebasket mesh body */
    mk_bb_fill(x + 6, y + 8, 24, 23, 0x0064748B);
    /* Top flared rim */
    mk_bb_fill(x + 3, y + 6, 30, 3, 0x0094A3B8);
    /* Paper crumples */
    mk_bb_fill(x + 10, y + 3, 8, 4, 0x00E2E8F0);
    mk_bb_fill(x + 18, y + 4, 7, 3, 0x00CBD5E1);
    /* Wire ribs */
    mk_bb_fill(x + 11, y + 10, 2, 19, 0x00334155);
    mk_bb_fill(x + 17, y + 10, 2, 19, 0x00334155);
    mk_bb_fill(x + 23, y + 10, 2, 19, 0x00334155);
    /* Green recycle emblem */
    mk_bb_fill(x + 13, y + 15, 10, 8, 0x0016A34A);
    mk_bb_fill(x + 15, y + 17, 6, 4, 0x0064748B);
}

static void draw_icon(int idx, int x, int y, const char* label) {
    /* Icon Tile Backing / Shadow */
    mk_bb_fill(x + 12, y + 2, 38, 36, 0x00151E28);
    mk_bb_fill(x + 10, y, 38, 36, 0x00243042);
    mk_bb_fill(x + 11, y + 1, 36, 34, 0x001E2838);

    /* Render Custom Pixel Art */
    int ax = x + 11;
    int ay = y + 1;
    switch (idx) {
        case 0:  draw_art_terminal(ax, ay); break;
        case 1:  draw_art_tasks(ax, ay);    break;
        case 2:  draw_art_display(ax, ay);  break;
        case 3:  draw_art_calc(ax, ay);     break;
        case 4:  draw_art_notes(ax, ay);    break;
        case 5:  draw_art_welcome(ax, ay);  break;
        case 6:  draw_art_clock(ax, ay);    break;
        case 7:  draw_art_sysinfo(ax, ay);  break;
        case 8:  draw_art_help(ax, ay);     break;
        case 9:  draw_art_about(ax, ay);    break;
        case 10: draw_art_demo(ax, ay);     break;
        case 11: draw_art_trash(ax, ay);    break;
        default: draw_art_welcome(ax, ay);  break;
    }

    /* Centered Clean Text Plate */
    int len = 0;
    while (label[len]) len++;
    int tw = len * 8 + 8;
    int tx = x + (ICON_W - tw) / 2;
    int ty = y + 42;

    mk_bb_fill(tx, ty, tw, 18, 0x00101824);
    mk_bb_fill(tx, ty, tw, 1, 0x00334155);
    mk_bb_fill(tx, ty + 17, tw, 1, 0x000A0F17);
    mk_bb_text(tx + 4, ty + 2, label, 0x00FFFFFF, 0x00101824);
}

static int icon_at(int mx, int my) {
    for (int i = 0; i < N_ICONS; i++) {
        int x, y;
        icon_pos(i, &x, &y);
        if (mx >= x && mx < x + ICON_W && my >= y && my < y + ICON_H)
            return i;
    }
    return -1;
}

/* ========================================================================= */
/* 7. Start Menu                                                             */
/* ========================================================================= */
#define SM_X 4
#define SM_W 210
#define SM_ITEM_H 22
#define SM_NUM_APPS 11

static const char* sm_items[SM_NUM_APPS] = {
    "Terminal", "Tasks", "Display", "Calculator", "Clock",
    "SysInfo", "Notes", "Help", "Welcome", "About", "Demo"
};

static int start_menu_h(void) {
    return 28 + SM_NUM_APPS * SM_ITEM_H + 8 + 3 * SM_ITEM_H + 8;
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

    /* Shadow */
    mk_bb_fill(x + 4, y + 4, w, h, 0x00080B10);
    /* Body */
    mk_bb_fill(x, y, w, h, 0x00181D26);
    mk_bb_fill(x, y, w, 1, 0x0038BDF8);
    mk_bb_fill(x, y, w, 28, 0x000078D7);
    mk_bb_text(x + 12, y + 6, "MyKernel Applications", 0x00FFFFFF, 0x000078D7);

    int iy = y + 28;
    for (int i = 0; i < SM_NUM_APPS; i++) {
        uint32_t bg = (i % 2) ? 0x00202735 : 0x00181D26;
        mk_bb_fill(x, iy, w, SM_ITEM_H, bg);
        /* Mini color dot */
        static const uint32_t sm_cols[] = {
            0x0000FF66, 0x000078D7, 0x0000B7C3, 0x00FFB900,
            0x00E81123, 0x008812CD, 0x00F7630C, 0x00107C10,
            0x00E3008C, 0x004F46E5, 0x007C3AED
        };
        mk_bb_fill(x + 6, iy + 6, 10, 10, sm_cols[i % 11]);
        mk_bb_text(x + 24, iy + 4, sm_items[i], 0x00F0F0F0, bg);
        iy += SM_ITEM_H;
    }

    mk_bb_fill(x, iy, w, 1, 0x00374151);
    iy += 6;
    mk_bb_fill(x + 8, iy, w - 16, SM_ITEM_H - 2, 0x00374151);
    mk_bb_text(x + 16, iy + 2, "Recycle Bin", 0x00FFFFFF, 0x00374151);
    iy += SM_ITEM_H;
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
    for (int i = 0; i < SM_NUM_APPS; i++) {
        if (my >= iy && my < iy + SM_ITEM_H)
            return i;
        iy += SM_ITEM_H;
    }
    iy += 6;
    if (my >= iy && my < iy + SM_ITEM_H) return 99;  /* recycle */
    iy += SM_ITEM_H;
    if (my >= iy && my < iy + SM_ITEM_H) return 100; /* reboot */
    iy += SM_ITEM_H;
    if (my >= iy && my < iy + SM_ITEM_H) return 101; /* shutdown */
    return -2;
}

/* ========================================================================= */
/* 8. Desktop Compositor Hooks                                               */
/* ========================================================================= */
static void desktop_layer_paint(void) {
    for (int i = 0; i < N_ICONS; i++) {
        int x, y;
        icon_pos(i, &x, &y);
        if (y + ICON_H <= mk_screen_h() - PANEL_H)
            draw_icon(i, x, y, icons[i].label);
    }
}

static void desktop_overlay_paint(void) {
    draw_start_menu();
}

/* ========================================================================= */
/* 9. Main Desktop Event Loop                                                */
/* ========================================================================= */
void desktop_run(void) {
    g_desktop_running = 1;
    if (platform_is_virtualbox()) {
        terminal_writestring("desktop: needs Bochs VBE (QEMU). Not supported on VirtualBox.\n");
        g_desktop_running = 0;
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
    mk_panel_set_start_active(0);
    calc_clear();
    id_welcome = id_clock = id_sysinfo = id_calc = id_term = -1;
    id_about = id_tasks = id_display = id_notes = id_help = id_trash = id_demo = -1;
    active_focus_id = -1;

    mk_set_desktop_layer(desktop_layer_paint);
    mk_set_desktop_overlay(desktop_overlay_paint);

    /* Open default apps on startup */
    open_welcome();
    open_clock();
    if (surface_alive(id_welcome)) mk_surface_move(id_welcome, 200, 40);
    if (surface_alive(id_clock))   mk_surface_move(id_clock, 520, 40);

    mk_compose();

    int dragging = 0;
    int drag_id = -1;
    int drag_off_x = 0, drag_off_y = 0;
    int prev_buttons = 0;
    int need_redraw = 1;
    uint32_t last_clock_tick = timer_ticks();
    uint32_t last_tasks_tick = timer_ticks();

    for (;;) {
        char k;
        while (keyboard_trygetchar(&k)) {
            if (k == 0x1B) { /* ESC */
                if (start_open) {
                    start_open = 0;
                    mk_panel_set_start_active(0);
                    need_redraw = 1;
                } else {
                    goto done;
                }
            }

            int calc_target = (active_focus_id == id_calc) ||
                              (surface_alive(id_calc) && (active_focus_id <= 0 || !surface_alive(active_focus_id)));
            int term_target = (active_focus_id == id_term) ||
                              (surface_alive(id_term) && (active_focus_id <= 0 || !surface_alive(active_focus_id)));

            if (surface_alive(id_term) && active_focus_id == id_term) calc_target = 0;
            if (surface_alive(id_calc) && active_focus_id == id_calc) term_target = 0;

            if (calc_target && surface_alive(id_calc)) {
                if (k >= '0' && k <= '9')      { calc_digit(k - '0'); }
                else if (k == '+')             { calc_set_op(1); }
                else if (k == '-')             { calc_set_op(2); }
                else if (k == '*')             { calc_set_op(3); }
                else if (k == '/')             { calc_set_op(4); }
                else if (k == '=' || k == '\n' || k == '\r') { calc_equals(); }
                else if (k == '\b')            { calc_backspace(); }
                else if (k == 'c' || k == 'C') { calc_clear(); }
                paint_calc(mk_surface_get(id_calc));
                mk_surface_commit(id_calc);
                need_redraw = 1;
            } else if (term_target && surface_alive(id_term)) {
                if (k == '\n' || k == '\r') {
                    term_exec_cmd();
                } else if (k == '\b') {
                    if (term_in_pos > 0) {
                        for (int i = term_in_pos - 1; i < term_in_len - 1; i++)
                            term_input[i] = term_input[i + 1];
                        term_in_pos--;
                        term_in_len--;
                        term_input[term_in_len] = '\0';
                    }
                } else if (k == KEY_LEFT) {
                    if (term_in_pos > 0) term_in_pos--;
                } else if (k == KEY_RIGHT) {
                    if (term_in_pos < term_in_len) term_in_pos++;
                } else if (k == KEY_UP) {
                    if (term_hist_count > 0 && term_hist_idx > 0) {
                        term_hist_idx--;
                        int ci = 0;
                        while (term_hist[term_hist_idx][ci] && ci < TERM_LINE_LEN - 1) {
                            term_input[ci] = term_hist[term_hist_idx][ci];
                            ci++;
                        }
                        term_input[ci] = '\0';
                        term_in_pos = ci;
                        term_in_len = ci;
                    }
                } else if (k == KEY_DOWN) {
                    if (term_hist_idx < term_hist_count - 1) {
                        term_hist_idx++;
                        int ci = 0;
                        while (term_hist[term_hist_idx][ci] && ci < TERM_LINE_LEN - 1) {
                            term_input[ci] = term_hist[term_hist_idx][ci];
                            ci++;
                        }
                        term_input[ci] = '\0';
                        term_in_pos = ci;
                        term_in_len = ci;
                    } else if (term_hist_idx == term_hist_count - 1) {
                        term_hist_idx++;
                        term_input[0] = '\0';
                        term_in_pos = 0;
                        term_in_len = 0;
                    }
                } else if ((uint8_t)k >= ' ' && (uint8_t)k <= '~') {
                    if (term_in_len < TERM_LINE_LEN - 4) {
                        for (int i = term_in_len; i > term_in_pos; i--)
                            term_input[i] = term_input[i - 1];
                        term_input[term_in_pos++] = k;
                        term_in_len++;
                        term_input[term_in_len] = '\0';
                    }
                }
                if (surface_alive(id_term)) {
                    paint_term(mk_surface_get(id_term));
                    mk_surface_commit(id_term);
                }
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
            /* 1. Panel Hit Test */
            int panel_win_id = -1;
            int panel_hit = mk_panel_hit_test(m.x, m.y, &panel_win_id);

            if (panel_hit == MK_PANEL_HIT_START) {
                start_open = !start_open;
                mk_panel_set_start_active(start_open);
                need_redraw = 1;
            } else if (panel_hit == MK_PANEL_HIT_CLOCK) {
                start_open = 0;
                mk_panel_set_start_active(0);
                open_clock();
                need_redraw = 1;
            } else if (panel_hit == MK_PANEL_HIT_REBOOT) {
                mk_shutdown();
                gfx_restore_text();
                machine_reboot();
            } else if (panel_hit == MK_PANEL_HIT_SHUTDOWN) {
                mk_shutdown();
                gfx_restore_text();
                machine_shutdown();
            } else if (panel_hit == MK_PANEL_HIT_WINDOW) {
                start_open = 0;
                mk_panel_set_start_active(0);
                struct mk_surface* ws = mk_surface_get(panel_win_id);
                if (ws) {
                    if (ws->mapped && active_focus_id == panel_win_id) {
                        /* Active top window -> minimize! */
                        mk_surface_minimize(panel_win_id);
                        active_focus_id = -1;
                    } else {
                        /* Minimized or unfocused -> restore and bring to top! */
                        mk_surface_restore(panel_win_id);
                        active_focus_id = panel_win_id;
                    }
                    need_redraw = 1;
                }
            } else if (start_open) {
                int sm = start_menu_click(m.x, m.y);
                if (sm >= 0 && sm < SM_NUM_APPS) {
                    void (*app_launchers[SM_NUM_APPS])(void) = {
                        open_term, open_tasks, open_display, open_calc, open_clock,
                        open_sysinfo, open_notes, open_help, open_welcome, open_about, open_demo
                    };
                    app_launchers[sm]();
                    start_open = 0;
                    mk_panel_set_start_active(0);
                    need_redraw = 1;
                } else if (sm == 99) {
                    open_trash();
                    start_open = 0;
                    mk_panel_set_start_active(0);
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
                    mk_panel_set_start_active(0);
                    need_redraw = 1;
                }
            } else {
                /* 2. Window Hit Test */
                int lx, ly, on_title, on_close, on_min;
                int id = mk_hit_test_ext(m.x, m.y, &lx, &ly, &on_title, &on_close, &on_min);

                if (id >= 0) {
                    active_focus_id = id;
                    mk_surface_raise(id);
                    need_redraw = 1;

                    if (on_close) {
                        on_close_id(id);
                        mk_surface_destroy(id);
                        dragging = 0;
                        if (active_focus_id == id) active_focus_id = -1;
                    } else if (on_min) {
                        mk_surface_minimize(id);
                        dragging = 0;
                        if (active_focus_id == id) active_focus_id = -1;
                    } else if (on_title) {
                        dragging = 1;
                        drag_id = id;
                        struct mk_surface* s = mk_surface_get(id);
                        drag_off_x = m.x - s->x;
                        drag_off_y = m.y - s->y;
                    } else {
                        /* Client Area Click */
                        if (id == id_calc) {
                            calc_on_click(lx, ly);
                            if (surface_alive(id_calc)) {
                                paint_calc(mk_surface_get(id_calc));
                                mk_surface_commit(id_calc);
                            }
                        } else if (id == id_display) {
                            display_on_click(lx, ly);
                            if (surface_alive(id_display)) {
                                paint_display(mk_surface_get(id_display));
                                mk_surface_commit(id_display);
                            }
                        } else if (id == id_tasks) {
                            tasks_on_click(lx, ly);
                            if (surface_alive(id_tasks)) {
                                paint_tasks(mk_surface_get(id_tasks));
                                mk_surface_commit(id_tasks);
                            }
                        } else if (id == id_term) {
                            struct mk_surface* ts = mk_surface_get(id_term);
                            if (ts) {
                                term_on_click(lx, ly, ts->w, ts->h);
                                paint_term(ts);
                                mk_surface_commit(id_term);
                            }
                        }
                    }
                } else {
                    /* 3. Desktop Icon Hit Test */
                    int ic = icon_at(m.x, m.y);
                    if (ic >= 0) {
                        icons[ic].open();
                        need_redraw = 1;
                    }
                }
            }
        }

        if (left_up)
            dragging = 0;

        if (dragging && drag_id >= 0 && (buttons & 1)) {
            int new_x = m.x - drag_off_x;
            int new_y = m.y - drag_off_y;
            struct mk_surface* ds = mk_surface_get(drag_id);
            if (ds && (ds->x != new_x || ds->y != new_y)) {
                mk_surface_move(drag_id, new_x, new_y);
                need_redraw = 1;
            }
        }

        /* Periodic Timer Updates */
        {
            uint32_t now = timer_ticks();
            uint32_t hz = timer_hz();
            if (hz == 0) hz = 100;
            if (surface_alive(id_clock) && (now - last_clock_tick) >= hz) {
                paint_clock(mk_surface_get(id_clock));
                mk_surface_commit(id_clock);
                need_redraw = 1;
                last_clock_tick = now;
            }
            if (surface_alive(id_tasks) && (now - last_tasks_tick) >= (hz / 2)) {
                paint_tasks(mk_surface_get(id_tasks));
                mk_surface_commit(id_tasks);
                need_redraw = 1;
                last_tasks_tick = now;
            }
        }

        static int last_mx = -1, last_my = -1;
        int mouse_moved = (m.x != last_mx || m.y != last_my);
        if (mouse_moved) {
            last_mx = m.x;
            last_my = m.y;
        }

        if (need_redraw) {
            mk_compose();
            need_redraw = 0;
            last_mx = m.x;
            last_my = m.y;
        } else if (mouse_moved) {
            mk_cursor_move(m.x, m.y);
        }

        prev_buttons = buttons;
        __asm__ volatile ("hlt");
    }

done:
    g_desktop_running = 0;
    mk_set_desktop_layer(0);
    mk_set_desktop_overlay(0);
    mk_shutdown();
    gfx_restore_text();
    terminal_writestring("desktop: exited\n");
}
