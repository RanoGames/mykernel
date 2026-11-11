#include "mkdisp.h"
#include "mkdraw.h"
#include "vbe.h"
#include "gfx.h"
#include "kmalloc.h"
#include "mouse.h"
#include "timer.h"
#include <stddef.h>

static int g_start_active = 0;

void mk_panel_set_start_active(int active) {
    g_start_active = active;
}

static struct mk_surface g_surf[MK_MAX_SURFACES];
static int g_active;
static void (*g_desktop_layer)(void) = 0;
static void (*g_desktop_overlay)(void) = 0;
static uint32_t g_wallpaper = 0x002B579A; /* Win7-ish blue */
static int g_sw, g_sh;
/* Off-screen compose buffer (placed at 32 MiB to avoid collision with dyld libs @ 6-14 MiB and mmap arena) */
/* Supports up to 1080p: 1920x1080x4 = 8,294,400 bytes */
#ifndef VBE_MODE_1920x1080
#define VBE_MODE_1920x1080 3
#endif

#define MK_BACK_BASE 0x02000000u
#define MK_BACK_MAX  (1920u * 1080u * 4u)
static uint32_t* g_back;
static int g_back_ok;

/* Window Z-order: 0 is bottom-most, g_z_count-1 is top-most / focused */
static int g_z_order[MK_MAX_SURFACES];
static int g_z_count = 0;

/* Cursor underlay to avoid full-screen redraw on mouse move */
static uint32_t cursor_under[MK_CURSOR_W * MK_CURSOR_H];
static int cursor_ux = -1, cursor_uy = -1;
static int cursor_under_valid = 0;

/* Simple arrow cursor mask (1=fg, 2=outline, 0=empty) */
static const uint8_t cursor_mask[MK_CURSOR_H][MK_CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,1,1,1,1,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

int mk_screen_w(void) { return g_sw; }
int mk_screen_h(void) { return g_sh; }
int mk_is_active(void) { return g_active; }

void mk_set_wallpaper_color(uint32_t c) { g_wallpaper = c; }

int mk_init(int vbe_mode_id) {
    if (vbe_set_mode(vbe_mode_id) != 0)
        return -1;
    const struct vbe_info* vi = vbe_get_info();
    if (!vi || !vi->active) return -1;
    g_sw = (int)vi->width;
    g_sh = (int)vi->height;
    mouse_set_bounds(g_sw - 1, g_sh - 1);
    g_z_count = 0;
    for (int i = 0; i < MK_MAX_SURFACES; i++) {
        g_surf[i].used = 0;
        g_surf[i].pixels = 0;
    }
    {
        uint32_t need = (uint32_t)g_sw * (uint32_t)g_sh * 4u;
        g_back = 0;
        g_back_ok = 0;
        if (need > 0 && need <= MK_BACK_MAX) {
            g_back = (uint32_t*)MK_BACK_BASE;
            for (uint32_t i = 0; i < need / 4u; i++) g_back[i] = 0;
            g_back_ok = 1;
        }
    }
    g_active = 1;
    return 0;
}

void mk_shutdown(void) {
    for (int i = 0; i < MK_MAX_SURFACES; i++) {
        if (g_surf[i].used && g_surf[i].pixels) {
            kfree(g_surf[i].pixels);
            g_surf[i].pixels = 0;
        }
        g_surf[i].used = 0;
    }
    g_active = 0;
    vbe_disable();
}

static void copy_title(char* dst, const char* src, size_t n) {
    size_t i = 0;
    if (!src) { dst[0] = '\0'; return; }
    while (src[i] && i + 1 < n) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

int mk_surface_create(int w, int h, const char* title) {
    if (w < 40 || h < 20 || w > g_sw || h > g_sh - MK_TITLE_H - MK_PANEL_H)
        return -1;
    int id = -1;
    for (int i = 0; i < MK_MAX_SURFACES; i++) {
        if (!g_surf[i].used) { id = i; break; }
    }
    if (id < 0) return -1;

    size_t bytes = (size_t)w * (size_t)h * 4u;
    uint32_t* pix = (uint32_t*)kmalloc(bytes);
    if (!pix) return -1;

    g_surf[id].used = 1;
    g_surf[id].w = w;
    g_surf[id].h = h;
    g_surf[id].x = 40 + id * 24;
    g_surf[id].y = 40 + id * 24;
    g_surf[id].mapped = 1;
    g_surf[id].pixels = pix;
    g_surf[id].dirty = 1;
    copy_title(g_surf[id].title, title ? title : "Window", sizeof(g_surf[id].title));
    mk_buf_fill(pix, w, h, 0x00C0C0C0);
    g_z_order[g_z_count++] = id;
    return id;
}

void mk_surface_destroy(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    if (g_surf[id].pixels) kfree(g_surf[id].pixels);
    g_surf[id].pixels = 0;
    g_surf[id].used = 0;
    g_surf[id].mapped = 0;

    int idx = -1;
    for (int i = 0; i < g_z_count; i++) {
        if (g_z_order[i] == id) { idx = i; break; }
    }
    if (idx >= 0) {
        for (int i = idx; i < g_z_count - 1; i++)
            g_z_order[i] = g_z_order[i + 1];
        g_z_count--;
    }
}

void mk_surface_raise(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    int idx = -1;
    for (int i = 0; i < g_z_count; i++) {
        if (g_z_order[i] == id) { idx = i; break; }
    }
    if (idx >= 0) {
        for (int i = idx; i < g_z_count - 1; i++)
            g_z_order[i] = g_z_order[i + 1];
        g_z_order[g_z_count - 1] = id;
    }
}

void mk_surface_minimize(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    g_surf[id].mapped = 0;
}

void mk_surface_restore(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    g_surf[id].mapped = 1;
    mk_surface_raise(id);
}

int mk_surface_is_minimized(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return 0;
    return !g_surf[id].mapped;
}

int mk_switch_resolution(int vbe_mode_id) {
    if (vbe_set_mode(vbe_mode_id) != 0)
        return -1;
    const struct vbe_info* vi = vbe_get_info();
    if (!vi || !vi->active) return -1;
    g_sw = (int)vi->width;
    g_sh = (int)vi->height;
    mouse_set_bounds(g_sw - 1, g_sh - 1);

    uint32_t need = (uint32_t)g_sw * (uint32_t)g_sh * 4u;
    if (need > 0 && need <= MK_BACK_MAX) {
        void* dst = (void*)g_back;
        uint32_t count = need / 4u;
        uint32_t val = 0;
        __asm__ volatile (
            "cld\n\t"
            "rep stosl\n\t"
            : "+D"(dst), "+c"(count)
            : "a"(val)
            : "memory"
        );
        g_back_ok = 1;
    }

    for (int i = 0; i < MK_MAX_SURFACES; i++) {
        if (g_surf[i].used) {
            if (g_surf[i].x + g_surf[i].w > g_sw) {
                g_surf[i].x = (g_sw > g_surf[i].w) ? (g_sw - g_surf[i].w) : 0;
            }
            if (g_surf[i].y + MK_TITLE_H + g_surf[i].h > g_sh - MK_PANEL_H) {
                int max_y = g_sh - MK_PANEL_H - MK_TITLE_H - g_surf[i].h;
                g_surf[i].y = (max_y > 0) ? max_y : 0;
            }
            g_surf[i].dirty = 1;
        }
    }
    cursor_under_valid = 0;
    mk_compose();
    return 0;
}

void mk_surface_move(int id, int x, int y) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + g_surf[id].w > g_sw) x = g_sw - g_surf[id].w;
    if (y + MK_TITLE_H + g_surf[id].h > g_sh - MK_PANEL_H)
        y = g_sh - MK_PANEL_H - MK_TITLE_H - g_surf[id].h;
    if (y < 0) y = 0;
    g_surf[id].x = x;
    g_surf[id].y = y;
    g_surf[id].dirty = 1;
}

void mk_surface_set_title(int id, const char* title) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    copy_title(g_surf[id].title, title, sizeof(g_surf[id].title));
}

void mk_surface_commit(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    g_surf[id].dirty = 1;
}

struct mk_surface* mk_surface_get(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return 0;
    return &g_surf[id];
}


/* ---- draw into backbuffer (or LFB if no back) ---- */

static inline void bb_put(int x, int y, uint32_t c) {
    if ((unsigned)x >= (unsigned)g_sw || (unsigned)y >= (unsigned)g_sh) return;
    if (g_back_ok)
        g_back[y * g_sw + x] = c;
    else
        vbe_putpixel(x, y, c);
}

static void bb_fill(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= (unsigned)g_sh) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if ((unsigned)xx >= (unsigned)g_sw) continue;
            bb_put(xx, yy, c);
        }
    }
}

static void bb_char(int x, int y, char ch, uint32_t fg, uint32_t bg) {
    const uint8_t* g = gfx_font_glyph((unsigned char)ch);
    for (int row = 0; row < 16; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++)
            bb_put(x + col, y + row, (bits & (0x80 >> col)) ? fg : bg);
    }
}

static void bb_text(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (s && *s) {
        if (*s == '\n') { cx = x; y += 16; s++; continue; }
        bb_char(cx, y, *s, fg, bg);
        cx += 8;
        s++;
    }
}

static void flip_to_lfb(void) {
    if (!g_back_ok || !g_active) return;
    const struct vbe_info* vi = vbe_get_info();
    if (!vi || !vi->active || !vi->lfb) return;
    void* dst = (void*)(uint32_t)vi->lfb;
    const void* src = (const void*)g_back;
    uint32_t count = (uint32_t)g_sw * (uint32_t)g_sh;

    /* High-speed burst DMA/MMIO transfer to LFB via hardware rep movsl */
    __asm__ volatile (
        "cld\n\t"
        "rep movsl\n\t"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}

static void draw_window(struct mk_surface* s, int focused) {
    int fx = s->x;
    int fy = s->y;
    int fw = s->w;
    int fh = s->h + MK_TITLE_H;
    uint32_t title_bg = focused ? 0x000078D7 : 0x003A404D;
    uint32_t border = 0x001B1E24;

    bb_fill(fx - 1, fy - 1, fw + 2, fh + 2, border);
    bb_fill(fx, fy, fw, MK_TITLE_H, title_bg);

    /* Minimize button '_' */
    int mx = fx + fw - 42;
    int my = fy + 4;
    bb_fill(mx, my, 16, 16, focused ? 0x001F5A96 : 0x002B303C);
    bb_fill(mx + 3, my + 11, 10, 2, 0x00FFFFFF);

    /* Close button 'X' */
    int cx = fx + fw - 22;
    int cy = fy + 4;
    bb_fill(cx, cy, 16, 16, 0x00E81123);
    bb_text(cx + 4, cy, "X", 0x00FFFFFF, 0x00E81123);

    /* Title text */
    bb_text(fx + 8, fy + 4, s->title, 0x00FFFFFF, title_bg);

    if (s->pixels) {
        for (int row = 0; row < s->h; row++) {
            int yy = fy + MK_TITLE_H + row;
            if ((unsigned)yy >= (unsigned)g_sh) continue;
            for (int col = 0; col < s->w; col++) {
                int xx = fx + col;
                if ((unsigned)xx >= (unsigned)g_sw) continue;
                bb_put(xx, yy, s->pixels[row * s->w + col]);
            }
        }
    }
}


/* Panel geometry & hit-testing */
#define PANEL_BTN_H 22

void mk_panel_power_rects(int* reboot_x, int* shut_x, int* by, int* bw, int* bh) {
    int py = g_sh - MK_PANEL_H;
    int clk_x = g_sw - 76 - 4;
    int sx = clk_x - 68 - 4;
    int rx = sx - 58 - 4;
    if (by) *by = py + 3;
    if (bw) *bw = 58;
    if (bh) *bh = PANEL_BTN_H;
    if (reboot_x) *reboot_x = rx;
    if (shut_x) *shut_x = sx;
}

static void draw_panel(void) {
    int py = g_sh - MK_PANEL_H;
    bb_fill(0, py, g_sw, MK_PANEL_H, 0x00181A20);
    bb_fill(0, py, g_sw, 1, 0x00333A48);

    /* 1. Start button */
    int st_x = 4, st_y = py + 3, st_w = 78, st_h = 22;
    uint32_t st_bg = g_start_active ? 0x000E3F66 : 0x000078D7;
    bb_fill(st_x, st_y, st_w, st_h, st_bg);
    bb_fill(st_x, st_y, st_w, 1, g_start_active ? 0x0008233A : 0x0038A6F0);
    bb_fill(st_x, st_y + st_h - 1, st_w, 1, g_start_active ? 0x00266B9E : 0x00004578);

    /* Windows 4-color squares icon */
    bb_fill(st_x + 6,  st_y + 5, 5, 5, 0x00E81123);
    bb_fill(st_x + 12, st_y + 5, 5, 5, 0x00107C10);
    bb_fill(st_x + 6,  st_y + 11, 5, 5, 0x000078D7);
    bb_fill(st_x + 12, st_y + 11, 5, 5, 0x00FFB900);
    bb_text(st_x + 24, st_y + 3, "Start", 0x00FFFFFF, st_bg);

    /* 2. Tray Clock */
    int clk_w = 76, clk_h = 22;
    int clk_x = g_sw - clk_w - 4;
    int clk_y = py + 3;
    bb_fill(clk_x, clk_y, clk_w, clk_h, 0x0010141C);
    bb_fill(clk_x, clk_y, clk_w, 1, 0x000A0D12);
    bb_fill(clk_x, clk_y, 1, clk_h, 0x000A0D12);
    bb_fill(clk_x, clk_y + clk_h - 1, clk_w, 1, 0x002D3748);
    bb_fill(clk_x + clk_w - 1, clk_y, 1, clk_h, 0x002D3748);

    /* Format uptime into HH:MM:SS */
    uint32_t hz = timer_hz();
    if (hz == 0) hz = 100;
    uint32_t sec = timer_ticks() / hz;
    uint32_t m = (sec / 60) % 60;
    uint32_t s = sec % 60;
    uint32_t h = (sec / 3600) % 24;
    char clk_str[10];
    clk_str[0] = (char)('0' + (h / 10));
    clk_str[1] = (char)('0' + (h % 10));
    clk_str[2] = ':';
    clk_str[3] = (char)('0' + (m / 10));
    clk_str[4] = (char)('0' + (m % 10));
    clk_str[5] = ':';
    clk_str[6] = (char)('0' + (s / 10));
    clk_str[7] = (char)('0' + (s % 10));
    clk_str[8] = '\0';
    bb_text(clk_x + 6, clk_y + 3, clk_str, 0x0038BDF8, 0x0010141C);

    /* 3. Power buttons */
    int shut_w = 68, shut_h = 22;
    int shut_x = clk_x - shut_w - 4;
    int shut_y = py + 3;
    bb_fill(shut_x, shut_y, shut_w, shut_h, 0x00C42B1C);
    bb_fill(shut_x, shut_y, shut_w, 1, 0x00E85D4E);
    bb_text(shut_x + 4, shut_y + 3, "Shutdown", 0x00FFFFFF, 0x00C42B1C);

    int reb_w = 58, reb_h = 22;
    int reb_x = shut_x - reb_w - 4;
    int reb_y = py + 3;
    bb_fill(reb_x, reb_y, reb_w, reb_h, 0x00C19C00);
    bb_fill(reb_x, reb_y, reb_w, 1, 0x00F0C830);
    bb_text(reb_x + 5, reb_y + 3, "Reboot", 0x00FFFFFF, 0x00C19C00);

    /* 4. Taskbar Window Buttons */
    int win_start_x = st_x + st_w + 6;
    int win_end_x = reb_x - 6;
    int win_avail_w = win_end_x - win_start_x;
    int open_count = 0;
    for (int i = 0; i < MK_MAX_SURFACES; i++) {
        if (g_surf[i].used) open_count++;
    }

    if (open_count > 0 && win_avail_w > 40) {
        int bw = (win_avail_w - (open_count - 1) * 4) / open_count;
        if (bw > 120) bw = 120;
        if (bw < 44) bw = 44;
        int top_id = (g_z_count > 0) ? g_z_order[g_z_count - 1] : -1;
        int cur_x = win_start_x;

        for (int i = 0; i < MK_MAX_SURFACES; i++) {
            if (!g_surf[i].used) continue;
            int is_top = (i == top_id && g_surf[i].mapped);
            int is_min = (!g_surf[i].mapped);

            uint32_t btn_bg = is_top ? 0x00264F78 : (is_min ? 0x0014161C : 0x00262B35);
            uint32_t btn_fg = is_top ? 0x00FFFFFF : (is_min ? 0x00788296 : 0x00CBD5E1);

            bb_fill(cur_x, py + 3, bw, 22, btn_bg);
            bb_fill(cur_x, py + 3, bw, 1, is_top ? 0x0038BDF8 : 0x003E4657);
            bb_fill(cur_x, py + 24, bw, 1, 0x00101216);

            if (is_top) {
                bb_fill(cur_x + 2, py + 23, bw - 4, 2, 0x0038BDF8);
            }

            /* Small app dot badge */
            uint32_t dot_color = is_top ? 0x0038BDF8 : (is_min ? 0x00556070 : 0x00A0AEC0);
            bb_fill(cur_x + 5, py + 9, 6, 6, dot_color);

            /* Truncate title to fit button width */
            int max_chars = (bw - 18) / 8;
            if (max_chars > 0) {
                char tbuf[16];
                int ti = 0;
                while (g_surf[i].title[ti] && ti < max_chars && ti < 15) {
                    tbuf[ti] = g_surf[i].title[ti];
                    ti++;
                }
                tbuf[ti] = '\0';
                bb_text(cur_x + 14, py + 3, tbuf, btn_fg, btn_bg);
            }

            cur_x += bw + 4;
        }
    }
}

int mk_panel_hit_test(int mx, int my, int* window_id) {
    int py = g_sh - MK_PANEL_H;
    if (my < py || my >= g_sh) return MK_PANEL_HIT_NONE;

    /* Start button */
    if (mx >= 4 && mx < 4 + 78) return MK_PANEL_HIT_START;

    /* Clock */
    int clk_w = 76;
    int clk_x = g_sw - clk_w - 4;
    if (mx >= clk_x && mx < clk_x + clk_w) return MK_PANEL_HIT_CLOCK;

    /* Shutdown */
    int shut_w = 68;
    int shut_x = clk_x - shut_w - 4;
    if (mx >= shut_x && mx < shut_x + shut_w) return MK_PANEL_HIT_SHUTDOWN;

    /* Reboot */
    int reb_w = 58;
    int reb_x = shut_x - reb_w - 4;
    if (mx >= reb_x && mx < reb_x + reb_w) return MK_PANEL_HIT_REBOOT;

    /* Window buttons */
    int win_start_x = 4 + 78 + 6;
    int win_end_x = reb_x - 6;
    int win_avail_w = win_end_x - win_start_x;
    int open_count = 0;
    for (int i = 0; i < MK_MAX_SURFACES; i++) {
        if (g_surf[i].used) open_count++;
    }

    if (open_count > 0 && mx >= win_start_x && mx < win_end_x) {
        int bw = (win_avail_w - (open_count - 1) * 4) / open_count;
        if (bw > 120) bw = 120;
        if (bw < 44) bw = 44;
        int cur_x = win_start_x;
        for (int i = 0; i < MK_MAX_SURFACES; i++) {
            if (!g_surf[i].used) continue;
            if (mx >= cur_x && mx < cur_x + bw) {
                if (window_id) *window_id = i;
                return MK_PANEL_HIT_WINDOW;
            }
            cur_x += bw + 4;
        }
    }

    return MK_PANEL_HIT_NONE;
}

/* ---- cursor: ONLY on LFB (never baked into backbuffer) ---- */

static uint32_t lfb_get(int x, int y) {
    const struct vbe_info* vi = vbe_get_info();
    if (!vi || !vi->active || !vi->lfb) return 0;
    if ((unsigned)x >= vi->width || (unsigned)y >= vi->height) return 0;
    volatile uint32_t* fb = (volatile uint32_t*)(uint32_t)vi->lfb;
    return fb[y * vi->width + x];
}

static void lfb_put(int x, int y, uint32_t c) {
    vbe_putpixel(x, y, c);
}

static void cursor_restore_under(void) {
    if (!cursor_under_valid || !g_active) return;
    for (int row = 0; row < MK_CURSOR_H; row++) {
        for (int col = 0; col < MK_CURSOR_W; col++) {
            if (!cursor_mask[row][col]) continue;
            lfb_put(cursor_ux + col, cursor_uy + row,
                    cursor_under[row * MK_CURSOR_W + col]);
        }
    }
    cursor_under_valid = 0;
}

static void cursor_save_under(int x, int y) {
    if (!g_active) return;
    for (int row = 0; row < MK_CURSOR_H; row++) {
        int py = y + row;
        for (int col = 0; col < MK_CURSOR_W; col++) {
            int px = x + col;
            if (g_back_ok && (unsigned)px < (unsigned)g_sw && (unsigned)py < (unsigned)g_sh) {
                cursor_under[row * MK_CURSOR_W + col] = g_back[py * g_sw + px];
            } else {
                cursor_under[row * MK_CURSOR_W + col] = lfb_get(px, py);
            }
        }
    }
    cursor_ux = x;
    cursor_uy = y;
    cursor_under_valid = 1;
}

static void cursor_paint_at(int x, int y) {
    for (int row = 0; row < MK_CURSOR_H; row++) {
        for (int col = 0; col < MK_CURSOR_W; col++) {
            uint8_t v = cursor_mask[row][col];
            if (!v) continue;
            lfb_put(x + col, y + row, (v == 1) ? 0x00FFFFFF : 0x00000000);
        }
    }
}

void mk_draw_cursor(void) {
    struct mouse_state m;
    mouse_get(&m);
    cursor_restore_under();
    cursor_save_under(m.x, m.y);
    cursor_paint_at(m.x, m.y);
}

void mk_cursor_invalidate(void) {
    cursor_under_valid = 0;
}

void mk_cursor_move(int x, int y) {
    if (!g_active) return;
    if (cursor_under_valid && cursor_ux == x && cursor_uy == y)
        return;
    cursor_restore_under();
    cursor_save_under(x, y);
    cursor_paint_at(x, y);
}


void mk_bb_fill(int x, int y, int w, int h, uint32_t color) {
    bb_fill(x, y, w, h, color);
}
void mk_bb_text(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    bb_text(x, y, s, fg, bg);
}
void mk_set_desktop_layer(void (*paint)(void)) {
    g_desktop_layer = paint;
}
void mk_set_desktop_overlay(void (*paint)(void)) {
    g_desktop_overlay = paint;
}

void mk_compose(void) {
    if (!g_active) return;

    /* Drop cursor underlay — full frame will replace LFB */
    cursor_under_valid = 0;

    /* Wallpaper */
    bb_fill(0, 0, g_sw, g_sh, g_wallpaper);
    for (int y = 0; y < 100 && y < g_sh; y++) {
        uint8_t shade = (uint8_t)(40 + y / 2);
        bb_fill(0, y, g_sw, 1, vbe_rgb(20, (uint8_t)(60 + shade / 3), (uint8_t)(120 + shade / 2)));
    }

    /* Desktop icons (under windows) */
    if (g_desktop_layer)
        g_desktop_layer();

    /* Windows in Z-order: 0 is bottom, g_z_count-1 is top/focused */
    int top = (g_z_count > 0) ? g_z_order[g_z_count - 1] : -1;

    for (int z = 0; z < g_z_count; z++) {
        int i = g_z_order[z];
        if (!g_surf[i].used || !g_surf[i].mapped) continue;
        draw_window(&g_surf[i], i == top);
        g_surf[i].dirty = 0;
    }

    draw_panel();

    /* Start menu etc. on top of panel/windows */
    if (g_desktop_overlay)
        g_desktop_overlay();

    if (g_back_ok)
        flip_to_lfb();

    /* Cursor only on LFB after flip */
    {
        struct mouse_state m;
        mouse_get(&m);
        cursor_save_under(m.x, m.y);
        cursor_paint_at(m.x, m.y);
    }
}

int mk_hit_test_ext(int sx, int sy, int* local_x, int* local_y, int* on_title, int* on_close, int* on_min) {
    if (local_x) *local_x = 0;
    if (local_y) *local_y = 0;
    if (on_title) *on_title = 0;
    if (on_close) *on_close = 0;
    if (on_min) *on_min = 0;

    for (int z = g_z_count - 1; z >= 0; z--) {
        int i = g_z_order[z];
        struct mk_surface* s = &g_surf[i];
        if (!s->used || !s->mapped) continue;
        int x0 = s->x, y0 = s->y;
        int x1 = s->x + s->w;
        int y1 = s->y + MK_TITLE_H + s->h;
        if (sx < x0 || sx >= x1 || sy < y0 || sy >= y1) continue;

        if (sy < y0 + MK_TITLE_H) {
            if (on_title) *on_title = 1;
            int cx = x0 + s->w - 22;
            int cy = y0 + 4;
            if (sx >= cx && sx < cx + 16 && sy >= cy && sy < cy + 16) {
                if (on_close) *on_close = 1;
            }
            int mx = x0 + s->w - 42;
            if (sx >= mx && sx < mx + 16 && sy >= cy && sy < cy + 16) {
                if (on_min) *on_min = 1;
            }
        } else {
            if (local_x) *local_x = sx - x0;
            if (local_y) *local_y = sy - (y0 + MK_TITLE_H);
        }
        return i;
    }
    return -1;
}

int mk_hit_test(int sx, int sy, int* local_x, int* local_y, int* on_title, int* on_close) {
    return mk_hit_test_ext(sx, sy, local_x, local_y, on_title, on_close, 0);
}
