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
#include "shell.h"
#include "fs.h"

static int g_desktop_running = 0;

int desktop_is_running(void) {
    return g_desktop_running;
}

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
static int id_term = -1;
static int id_about = -1;
static int id_tasks = -1;
static int id_notes = -1;
static int id_help = -1;
static int id_trash = -1;
static int id_demo = -1;
static int active_focus_id = -1;

static int start_open;

/* Calculator state */
static int calc_acc = 0;
static int calc_val = 0;
static int calc_op = 0; /* op: 0 none, 1: +, 2: -, 3: *, 4: / */
static int calc_fresh = 1;
static int calc_error = 0;

static int desk_streq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
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

static void draw_calc_btn(struct mk_surface* s, int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg) {
    mk_buf_rect(s->pixels, s->w, s->h, x + 1, y + 1, w, h, 0x0010141C);
    mk_buf_rect(s->pixels, s->w, s->h, x, y, w, h, bg);
    mk_buf_hline(s->pixels, s->w, s->h, x, y, w, 0x00556677);
    mk_buf_vline(s->pixels, s->w, s->h, x, y, h, 0x00556677);
    mk_buf_hline(s->pixels, s->w, s->h, x, y + h - 1, w, 0x00151C24);
    mk_buf_vline(s->pixels, s->w, s->h, x + w - 1, y, h, 0x00151C24);
    int len = 0;
    while (label[len]) len++;
    int tx = x + (w - len * 8) / 2;
    int ty = y + (h - 16) / 2;
    mk_buf_text(s->pixels, s->w, s->h, tx, ty, label, fg, bg);
}

static void paint_calc(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00202530);

    /* LCD Screen Display */
    mk_buf_rect(s->pixels, s->w, s->h, 8, 8, s->w - 16, 44, 0x0010141A);
    mk_buf_hline(s->pixels, s->w, s->h, 8, 8, s->w - 16, 0x00334455);
    mk_buf_vline(s->pixels, s->w, s->h, 8, 8, 44, 0x00334455);
    mk_buf_hline(s->pixels, s->w, s->h, 8, 51, s->w - 16, 0x00334455);
    mk_buf_vline(s->pixels, s->w, s->h, s->w - 9, 8, 44, 0x00334455);

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

    /* Row 0: C, <-, +/-, / */
    draw_calc_btn(s, 8, 58, 46, 32, "C", 0x00D13438, 0x00FFFFFF);
    draw_calc_btn(s, 59, 58, 46, 32, "<-", 0x003A4455, 0x00FFFFFF);
    draw_calc_btn(s, 110, 58, 46, 32, "+/-", 0x003A4455, 0x00FFFFFF);
    draw_calc_btn(s, 161, 58, 47, 32, "/", 0x000078D7, 0x00FFFFFF);

    /* Row 1: 7, 8, 9, * */
    draw_calc_btn(s, 8, 96, 46, 32, "7", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 59, 96, 46, 32, "8", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 110, 96, 46, 32, "9", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 161, 96, 47, 32, "*", 0x000078D7, 0x00FFFFFF);

    /* Row 2: 4, 5, 6, - */
    draw_calc_btn(s, 8, 134, 46, 32, "4", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 59, 134, 46, 32, "5", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 110, 134, 46, 32, "6", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 161, 134, 47, 32, "-", 0x000078D7, 0x00FFFFFF);

    /* Row 3: 1, 2, 3, + */
    draw_calc_btn(s, 8, 172, 46, 32, "1", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 59, 172, 46, 32, "2", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 110, 172, 46, 32, "3", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 161, 172, 47, 32, "+", 0x000078D7, 0x00FFFFFF);

    /* Row 4: 0, = */
    draw_calc_btn(s, 8, 210, 97, 32, "0", 0x002C3240, 0x00FFFFFF);
    draw_calc_btn(s, 110, 210, 98, 32, "=", 0x00107C10, 0x00FFFFFF);
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

/* ---- Terminal Application ---- */
#define TERM_MAX_LINES 64
#define TERM_LINE_LEN  64
static char term_lines[TERM_MAX_LINES][TERM_LINE_LEN];
static uint32_t term_cols[TERM_MAX_LINES];
static int term_num_lines = 0;
static int term_cur_col = 0;
static char term_input[TERM_LINE_LEN];
static int term_in_pos = 0;

#define TERM_HIST_MAX 16
static char term_hist[TERM_HIST_MAX][TERM_LINE_LEN];
static int term_hist_count = 0;
static int term_hist_idx = 0;

static void paint_term(struct mk_surface* s);
static void on_close_id(int id);

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
        term_cols[term_num_lines] = 0x00DDDDDD;

        static uint32_t last_term_compose = 0;
        uint32_t now = timer_ticks();
        if (surface_alive(id_term) && (now - last_term_compose) >= 5) {
            last_term_compose = now;
            paint_term(mk_surface_get(id_term));
            mk_surface_commit(id_term);
            mk_compose();
        }
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
        for (int t = 0; t < 4; t++) desktop_term_putchar(' ');
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
            term_cols[term_num_lines] = 0x00DDDDDD;
        }
        term_lines[term_num_lines][term_cur_col++] = c;
        term_lines[term_num_lines][term_cur_col] = '\0';
        term_cols[term_num_lines] = 0x00DDDDDD;
    }
}

static void desktop_term_clear(void) {
    for (int i = 0; i < TERM_MAX_LINES; i++) {
        term_lines[i][0] = '\0';
        term_cols[i] = 0x00DDDDDD;
    }
    term_num_lines = 0;
    term_cur_col = 0;
}

static void term_append(const char* s, uint32_t col) {
    while (s && *s) {
        desktop_term_putchar(*s++);
    }
    if (col && term_num_lines > 0) {
        term_cols[term_num_lines - 1] = col;
    }
    desktop_term_putchar('\n');
}

static void paint_term(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x000F141C);
    /* Terminal header */
    mk_buf_rect(s->pixels, s->w, s->h, 0, 0, s->w, 20, 0x001B2230);
    mk_buf_text(s->pixels, s->w, s->h, 8, 2, "MyKernel Shell (Native Terminal)", 0x00A0C0E0, 0x001B2230);

    int max_visible = (s->h - 44) / 16;
    if (max_visible < 1) max_visible = 1;

    int total_lines = term_num_lines + (term_cur_col > 0 ? 1 : 0);
    int start = 0;
    if (total_lines > max_visible) {
        start = total_lines - max_visible;
    }

    int y = 24;
    for (int i = start; i < total_lines; i++) {
        if (i < term_num_lines) {
            mk_buf_text(s->pixels, s->w, s->h, 8, y, term_lines[i], term_cols[i], 0x000F141C);
        } else {
            /* Current in-progress line */
            mk_buf_text(s->pixels, s->w, s->h, 8, y, term_lines[term_num_lines], 0x00DDDDDD, 0x000F141C);
        }
        y += 16;
    }

    /* Prompt line */
    char pwd[48];
    fs_pwd(pwd, sizeof(pwd));
    char prompt[TERM_LINE_LEN];
    int pi = 0;
    for (int i = 0; pwd[i] && pi < 20; i++) prompt[pi++] = pwd[i];
    prompt[pi++] = '>';
    prompt[pi++] = ' ';
    for (int i = 0; i < term_in_pos && pi < TERM_LINE_LEN - 3; i++) prompt[pi++] = term_input[i];
    prompt[pi++] = '_';
    prompt[pi] = '\0';
    mk_buf_text(s->pixels, s->w, s->h, 8, s->h - 18, prompt, 0x0000FF66, 0x000F141C);
}

static void term_exec_cmd(void) {
    char pwd[48];
    fs_pwd(pwd, sizeof(pwd));

    /* Echo prompt and command into terminal output */
    for (int i = 0; pwd[i]; i++) desktop_term_putchar(pwd[i]);
    desktop_term_putchar('>');
    desktop_term_putchar(' ');
    for (int i = 0; i < term_in_pos; i++) desktop_term_putchar(term_input[i]);
    desktop_term_putchar('\n');
    if (term_num_lines > 0) {
        term_cols[term_num_lines - 1] = 0x00FFFFFF;
    }

    char* cmd = term_input;
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') {
        term_in_pos = 0;
        term_input[0] = '\0';
        return;
    }

    /* Add to command history */
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
        term_input[0] = '\0';
        return;
    }

    /* Execute via kernel shell engine */
    terminal_set_putchar_hook(desktop_term_putchar);
    terminal_set_clear_hook(desktop_term_clear);
    shell_execute_command(cmd);
    terminal_set_putchar_hook(0);
    terminal_set_clear_hook(0);

    term_in_pos = 0;
    term_input[0] = '\0';
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


static void paint_trash(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "Recycle Bin", 0, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40, "Empty (demo)", 0x00666666, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 64, "No deleted files yet.", 0, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 88, "Future: drop windows here", 0x00333333, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 120, "Icon uses screen coords:", 0, 0x00F0F0F0);
    mk_buf_text(s->pixels, s->w, s->h, 12, 140, "origin top-left, Y down", 0x00006699, 0x00F0F0F0);
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
static void open_calc(void) {
    if (surface_alive(id_calc)) {
        mk_surface_raise(id_calc);
        active_focus_id = id_calc;
        return;
    }
    OPEN_APP(id_calc, 216, 252, "Calculator", paint_calc);
    active_focus_id = id_calc;
}

static void open_term(void) {
    if (surface_alive(id_term)) {
        mk_surface_raise(id_term);
        active_focus_id = id_term;
        return;
    }
    if (term_num_lines == 0 && term_cur_col == 0) {
        term_append("MyKernel Shell v0.3 (i686 Protected Mode)", 0x0000FF66);
        term_append("Type 'help' for commands, 'exit' to close", 0x0088CCFF);
    }
    OPEN_APP(id_term, 480, 270, "Terminal", paint_term);
    active_focus_id = id_term;
}

static void open_about(void)   { OPEN_APP(id_about, 300, 170, "About", paint_about); }
static void open_tasks(void)   { OPEN_APP(id_tasks, 300, 160, "Tasks", paint_tasks); }
static void open_notes(void)   { OPEN_APP(id_notes, 280, 160, "Notes", paint_notes); }
static void open_help(void)    { OPEN_APP(id_help, 320, 170, "Help", paint_help); }

static void paint_demo(struct mk_surface* s) {
    if (!s || !s->pixels) return;
    mk_buf_fill(s->pixels, s->w, s->h, 0x001A1A2E);
    mk_buf_text(s->pixels, s->w, s->h, 12, 12, "Demo App", 0x00FFFFFF, 0x001A1A2E);
    mk_buf_text(s->pixels, s->w, s->h, 12, 40, "Ring3 + icons demo", 0x00AAAAFF, 0x001A1A2E);
    mk_buf_text(s->pixels, s->w, s->h, 12, 64, "Shell: exec hello", 0x00AAFFAA, 0x001A1A2E);
    mk_buf_text(s->pixels, s->w, s->h, 12, 88, "Shell: ping 127.0.0.1", 0x00AAFFAA, 0x001A1A2E);
    mk_buf_text(s->pixels, s->w, s->h, 12, 112, "Two windows on start", 0x00FFFFFF, 0x001A1A2E);
}
static void open_demo(void) { OPEN_APP(id_demo, 340, 160, "Demo", paint_demo); }
static void open_trash(void)   { OPEN_APP(id_trash, 300, 170, "Recycle Bin", paint_trash); }

static void on_close_id(int id) {
    if (id == id_welcome) id_welcome = -1;
    if (id == id_clock) id_clock = -1;
    if (id == id_sysinfo) id_sysinfo = -1;
    if (id == id_calc) id_calc = -1;
    if (id == id_term) id_term = -1;
    if (id == id_about) id_about = -1;
    if (id == id_tasks) id_tasks = -1;
    if (id == id_notes) id_notes = -1;
    if (id == id_help) id_help = -1;
    if (id == id_trash) id_trash = -1;
    if (id == id_demo) id_demo = -1;
    if (id == active_focus_id) active_focus_id = -1;
}

/* ---- desktop icons ----
 * Coordinate system (screen / VBE framebuffer):
 *   (0,0) = top-left of the screen
 *   X grows right, Y grows down
 *   mouse_get() and mk_hit_test() use the same space
 *   panel occupies y in [screen_h - MK_PANEL_H, screen_h)
 *   icon hitbox ≈ [x, x+ICON_W) x [y, y+ICON_H)
 */
#define ICON_W 60
#define ICON_H 70
#define PANEL_H 28

struct desk_icon {
    int x, y;           /* top-left of icon body; -1,-1 = auto place */
    int anchor;         /* 0=fixed xy, 1=bottom-right (Recycle) */
    const char* label;
    void (*open)(void);
};

static struct desk_icon icons[] = {
    { 20,  48,  0, "Clock",    open_clock },
    { 20,  118, 0, "SysInfo",  open_sysinfo },
    { 20,  188, 0, "Calc",     open_calc },
    { 20,  258, 0, "Terminal", open_term },
    { 20,  328, 0, "About",    open_about },
    { 100, 48,  0, "Tasks",    open_tasks },
    { 100, 118, 0, "Notes",    open_notes },
    { 100, 188, 0, "Help",     open_help },
    { 100, 258, 0, "Welcome",  open_welcome },
    { 100, 328, 0, "Demo",     open_demo },
    { -1,  -1,  1, "Recycle",  open_trash }, /* bottom-right */
};
#define N_ICONS ((int)(sizeof(icons) / sizeof(icons[0])))

static void icon_pos(int i, int* ox, int* oy) {
    if (icons[i].anchor == 1) {
        /* bottom-right above taskbar, classic Recycle Bin */
        *ox = mk_screen_w() - ICON_W - 16;
        *oy = mk_screen_h() - PANEL_H - ICON_H - 12;
    } else {
        *ox = icons[i].x;
        *oy = icons[i].y;
    }
}

static uint32_t icon_color(int i) {
    static const uint32_t cols[] = {
        0x00E81123, 0x000078D7, 0x00107C10, 0x00FFB900,
        0x008812CD, 0x00F7630C, 0x0000B7C3, 0x00E3008C
    };
    return cols[i % 8];
}

static void draw_icon(int idx, int x, int y, const char* label) {
    int is_trash = (icons[idx].anchor == 1);
    uint32_t c = is_trash ? 0x00666666 : icon_color(idx);
    /* shadow */
    mk_bb_fill(x + 3, y + 3, 56, 48, 0x00202020);
    /* body */
    mk_bb_fill(x, y, 56, 48, 0x00FFFFFF);
    mk_bb_fill(x + 3, y + 3, 50, 42, c);
    if (is_trash) {
        /* simple bin glyph: lid + can */
        mk_bb_fill(x + 14, y + 10, 28, 4, 0x00DDDDDD);
        mk_bb_fill(x + 24, y + 6, 8, 6, 0x00DDDDDD);
        mk_bb_fill(x + 16, y + 16, 24, 22, 0x00AAAAAA);
        mk_bb_fill(x + 20, y + 20, 4, 14, 0x00444444);
        mk_bb_fill(x + 26, y + 20, 4, 14, 0x00444444);
        mk_bb_fill(x + 32, y + 20, 4, 14, 0x00444444);
    } else if (label[0] == 'T' && label[1] == 'e') {
        mk_bb_text(x + 18, y + 14, ">_", 0x0000FF66, c);
    } else {
        char badge[2] = { label[0], 0 };
        mk_bb_text(x + 22, y + 14, badge, 0x00FFFFFF, c);
    }
    mk_bb_fill(x - 2, y + 52, 64, 16, 0x00000000);
    mk_bb_text(x + 2, y + 54, label, 0x00FFFFFF, 0x00000000);
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

/* ---- Start menu ---- */
#define SM_X 4
#define SM_W 200
#define SM_ITEM_H 22

static int start_menu_h(void) {
    /* 9 apps + separator + reboot + shutdown + empty */
    return 28 + 9 * SM_ITEM_H + 8 + 3 * SM_ITEM_H + 8;
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
        "Clock", "SysInfo", "Calculator", "Terminal", "About",
        "Tasks", "Notes", "Help", "Welcome"
    };
    int iy = y + 28;
    for (int i = 0; i < 9; i++) {
        uint32_t bg = (i % 2) ? 0x00252525 : 0x001E1E1E;
        mk_bb_fill(x, iy, w, SM_ITEM_H, bg);
        mk_bb_fill(x + 4, iy + 4, 14, 14, icon_color(i));
        mk_bb_text(x + 24, iy + 4, items[i], 0x00F0F0F0, bg);
        iy += SM_ITEM_H;
    }
    mk_bb_fill(x, iy, w, 1, 0x00444444);
    iy += 6;
    mk_bb_fill(x + 8, iy, w - 16, SM_ITEM_H - 2, 0x00555555);
    mk_bb_text(x + 16, iy + 2, "Recycle Bin", 0x00FFFFFF, 0x00555555);
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
    for (int i = 0; i < 9; i++) {
        if (my >= iy && my < iy + SM_ITEM_H)
            return i;
        iy += SM_ITEM_H;
    }
    iy += 6;
    if (my >= iy && my < iy + SM_ITEM_H) return 99;  /* recycle */
    iy += SM_ITEM_H;
    if (my >= iy && my < iy + SM_ITEM_H) return 100;
    iy += SM_ITEM_H;
    if (my >= iy && my < iy + SM_ITEM_H) return 101;
    return -2;
}

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
    calc_clear();
    id_welcome = id_clock = id_sysinfo = id_calc = id_term = -1;
    id_about = id_tasks = id_notes = id_help = id_trash = id_demo = -1;
    active_focus_id = -1;

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

            int calc_target = (active_focus_id == id_calc) || (surface_alive(id_calc) && (active_focus_id <= 0 || !surface_alive(active_focus_id)));
            int term_target = (active_focus_id == id_term) || (surface_alive(id_term) && (active_focus_id <= 0 || !surface_alive(active_focus_id)));

            if (surface_alive(id_term) && active_focus_id == id_term) calc_target = 0;
            if (surface_alive(id_calc) && active_focus_id == id_calc) term_target = 0;

            if (calc_target && surface_alive(id_calc)) {
                if (k >= '0' && k <= '9') { calc_digit(k - '0'); }
                else if (k == '+') { calc_set_op(1); }
                else if (k == '-') { calc_set_op(2); }
                else if (k == '*') { calc_set_op(3); }
                else if (k == '/') { calc_set_op(4); }
                else if (k == '=' || k == '\n' || k == '\r') { calc_equals(); }
                else if (k == '\b') { calc_backspace(); }
                else if (k == 'c' || k == 'C') { calc_clear(); }
                paint_calc(mk_surface_get(id_calc));
                mk_surface_commit(id_calc);
                need_redraw = 1;
            } else if (term_target && surface_alive(id_term)) {
                if (k == '\n' || k == '\r') {
                    term_exec_cmd();
                } else if (k == '\b') {
                    if (term_in_pos > 0) {
                        term_input[--term_in_pos] = '\0';
                    }
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
                    } else if (term_hist_idx == term_hist_count - 1) {
                        term_hist_idx++;
                        term_input[0] = '\0';
                        term_in_pos = 0;
                    }
                } else if ((uint8_t)k >= ' ' && (uint8_t)k <= '~') {
                    if (term_in_pos < TERM_LINE_LEN - 4) {
                        term_input[term_in_pos++] = k;
                        term_input[term_in_pos] = '\0';
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
                if (sm >= 0 && sm <= 8) {
                    void (*ops[9])(void) = {
                        open_clock, open_sysinfo, open_calc, open_term, open_about,
                        open_tasks, open_notes, open_help, open_welcome
                    };
                    ops[sm]();
                    start_open = 0;
                    need_redraw = 1;
                } else if (sm == 99) {
                    open_trash();
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
                /* 1. Hit test top-down for windows */
                int lx, ly, on_title, on_close;
                int id = mk_hit_test(m.x, m.y, &lx, &ly, &on_title, &on_close);
                if (id >= 0) {
                    active_focus_id = id;
                    mk_surface_raise(id);
                    need_redraw = 1;
                    if (on_close) {
                        on_close_id(id);
                        mk_surface_destroy(id);
                        dragging = 0;
                        if (active_focus_id == id) active_focus_id = -1;
                    } else if (on_title) {
                        dragging = 1;
                        drag_id = id;
                        struct mk_surface* s = mk_surface_get(id);
                        drag_off_x = m.x - s->x;
                        drag_off_y = m.y - s->y;
                    } else {
                        /* Client area click inside window */
                        if (id == id_calc) {
                            calc_on_click(lx, ly);
                            if (surface_alive(id_calc)) {
                                paint_calc(mk_surface_get(id_calc));
                                mk_surface_commit(id_calc);
                            }
                        }
                    }
                } else {
                    /* 2. No window was clicked: check desktop icons! */
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
    g_desktop_running = 0;
    mk_set_desktop_layer(0);
    mk_set_desktop_overlay(0);
    mk_shutdown();
    gfx_restore_text();
    terminal_writestring("desktop: exited\n");
}
