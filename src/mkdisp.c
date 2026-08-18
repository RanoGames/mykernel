#include "mkdisp.h"
#include "mkdraw.h"
#include "vbe.h"
#include "gfx.h"
#include "kmalloc.h"
#include "mouse.h"
#include <stddef.h>

static struct mk_surface g_surf[MK_MAX_SURFACES];
static int g_active;
static uint32_t g_wallpaper = 0x002B579A; /* Win7-ish blue */
static int g_sw, g_sh;
/* Off-screen compose buffer (fixed VA, not kmalloc — up to 1024x768x4) */
#define MK_BACK_BASE 0x00700000u
#define MK_BACK_MAX  (1024u * 768u * 4u)
static uint32_t* g_back;
static int g_back_ok;

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
    return id;
}

void mk_surface_destroy(int id) {
    if (id < 0 || id >= MK_MAX_SURFACES || !g_surf[id].used) return;
    if (g_surf[id].pixels) kfree(g_surf[id].pixels);
    g_surf[id].pixels = 0;
    g_surf[id].used = 0;
    g_surf[id].mapped = 0;
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
    volatile uint32_t* fb = (volatile uint32_t*)(uint32_t)vi->lfb;
    uint32_t n = (uint32_t)g_sw * (uint32_t)g_sh;
    /* single blast — no tearing mid-frame */
    for (uint32_t i = 0; i < n; i++)
        fb[i] = g_back[i];
}

static void draw_window(struct mk_surface* s, int focused) {
    int fx = s->x;
    int fy = s->y;
    int fw = s->w;
    int fh = s->h + MK_TITLE_H;
    uint32_t title_bg = focused ? 0x00007ACC : 0x00666666;
    uint32_t border = 0x00222222;

    bb_fill(fx - 1, fy - 1, fw + 2, fh + 2, border);
    bb_fill(fx, fy, fw, MK_TITLE_H, title_bg);

    int cx = fx + fw - 22;
    int cy = fy + 4;
    bb_fill(cx, cy, 16, 16, 0x00E81123);
    bb_text(cx + 4, cy, "X", 0x00FFFFFF, 0x00E81123);
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

static void draw_panel(void) {
    int y = g_sh - MK_PANEL_H;
    bb_fill(0, y, g_sw, MK_PANEL_H, 0x001F1F1F);
    bb_fill(0, y, g_sw, 1, 0x00444444);
    bb_fill(4, y + 4, 72, MK_PANEL_H - 8, 0x000078D7);
    bb_text(12, y + 6, "MyKernel", 0x00FFFFFF, 0x000078D7);
    bb_text(g_sw - 200, y + 6, "ESC = exit desktop", 0x00AAAAAA, 0x001F1F1F);
}

static void cursor_restore_under(void) {
    if (!cursor_under_valid || !g_active) return;
    for (int row = 0; row < MK_CURSOR_H; row++) {
        for (int col = 0; col < MK_CURSOR_W; col++) {
            bb_put(cursor_ux + col, cursor_uy + row,
                   cursor_under[row * MK_CURSOR_W + col]);
            if (!g_back_ok)
                vbe_putpixel(cursor_ux + col, cursor_uy + row,
                             cursor_under[row * MK_CURSOR_W + col]);
        }
    }
    cursor_under_valid = 0;
}

static void cursor_save_under(int x, int y) {
    if (!g_active) return;
    for (int row = 0; row < MK_CURSOR_H; row++) {
        for (int col = 0; col < MK_CURSOR_W; col++) {
            uint32_t pix = 0;
            int px = x + col, py = y + row;
            if ((unsigned)px < (unsigned)g_sw && (unsigned)py < (unsigned)g_sh) {
                if (g_back_ok)
                    pix = g_back[py * g_sw + px];
                else {
                    const struct vbe_info* vi = vbe_get_info();
                    if (vi && vi->lfb)
                        pix = ((volatile uint32_t*)(uint32_t)vi->lfb)[py * vi->width + px];
                }
            }
            cursor_under[row * MK_CURSOR_W + col] = pix;
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
            uint32_t c = (v == 1) ? 0x00FFFFFF : 0x00000000;
            if (g_back_ok) {
                bb_put(x + col, y + row, c);
                /* also paint to LFB for live cursor without full flip */
                vbe_putpixel(x + col, y + row, c);
            } else {
                vbe_putpixel(x + col, y + row, c);
            }
        }
    }
}

void mk_draw_cursor(void) {
    struct mouse_state m;
    mouse_get(&m);
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
    /* restore old underlay on LFB (backbuffer not flipped for cursor-only moves) */
    if (cursor_under_valid) {
        for (int row = 0; row < MK_CURSOR_H; row++)
            for (int col = 0; col < MK_CURSOR_W; col++)
                vbe_putpixel(cursor_ux + col, cursor_uy + row,
                             cursor_under[row * MK_CURSOR_W + col]);
        cursor_under_valid = 0;
    }
    cursor_save_under(x, y);
    cursor_paint_at(x, y);
}

void mk_compose(void) {
    if (!g_active) return;
    cursor_under_valid = 0;

    /* paint full frame into backbuffer */
    bb_fill(0, 0, g_sw, g_sh, g_wallpaper);
    for (int y = 0; y < 80 && y < g_sh; y++) {
        uint8_t shade = (uint8_t)(30 + y);
        bb_fill(0, y, g_sw, 1, vbe_rgb(0, shade / 2, (uint8_t)(80 + shade)));
    }
    bb_text(16, 12, "MyKernel Desktop", 0x00FFFFFF, g_wallpaper);
    bb_text(16, 30, "Drag title bar | click X to close window", 0x00D0D0D0, g_wallpaper);

    int top = -1;
    for (int i = 0; i < MK_MAX_SURFACES; i++)
        if (g_surf[i].used && g_surf[i].mapped)
            top = i;

    for (int i = 0; i < MK_MAX_SURFACES; i++) {
        if (!g_surf[i].used || !g_surf[i].mapped) continue;
        draw_window(&g_surf[i], i == top);
        g_surf[i].dirty = 0;
    }
    draw_panel();

    /* cursor into backbuffer, then one flip */
    {
        struct mouse_state m;
        mouse_get(&m);
        cursor_save_under(m.x, m.y);
        for (int row = 0; row < MK_CURSOR_H; row++) {
            for (int col = 0; col < MK_CURSOR_W; col++) {
                uint8_t v = cursor_mask[row][col];
                if (!v) continue;
                bb_put(m.x + col, m.y + row, (v == 1) ? 0x00FFFFFF : 0x00000000);
            }
        }
    }

    if (g_back_ok)
        flip_to_lfb();
    else {
        /* fallback already painted via bb_* -> vbe */
    }
}

int mk_hit_test(int sx, int sy, int* local_x, int* local_y, int* on_title, int* on_close) {
    if (local_x) *local_x = 0;
    if (local_y) *local_y = 0;
    if (on_title) *on_title = 0;
    if (on_close) *on_close = 0;

    for (int i = MK_MAX_SURFACES - 1; i >= 0; i--) {
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
            if (sx >= cx && sx < cx + 16 && sy >= cy && sy < cy + 16)
                if (on_close) *on_close = 1;
        } else {
            if (local_x) *local_x = sx - x0;
            if (local_y) *local_y = sy - (y0 + MK_TITLE_H);
        }
        return i;
    }
    return -1;
}
