#include "mkdisp.h"
#include "mkdraw.h"
#include "vbe.h"
#include "kmalloc.h"
#include "mouse.h"
#include <stddef.h>

static struct mk_surface g_surf[MK_MAX_SURFACES];
static int g_active;
static uint32_t g_wallpaper = 0x002B579A; /* Win7-ish blue */
static int g_sw, g_sh;

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

/* Frame window: title bar + border + client blit */
static void draw_window(struct mk_surface* s, int focused) {
    int fx = s->x;
    int fy = s->y;
    int fw = s->w;
    int fh = s->h + MK_TITLE_H;
    uint32_t title_bg = focused ? 0x00007ACC : 0x00666666;
    uint32_t border = 0x00222222;

    vbe_fill_rect(fx - 1, fy - 1, fw + 2, fh + 2, border);
    vbe_fill_rect(fx, fy, fw, MK_TITLE_H, title_bg);

    /* close button */
    int cx = fx + fw - 22;
    int cy = fy + 4;
    vbe_fill_rect(cx, cy, 16, 16, 0x00E81123);
    mk_fb_text(cx + 4, cy, "X", 0x00FFFFFF, 0x00E81123);

    mk_fb_text(fx + 8, fy + 4, s->title, 0x00FFFFFF, title_bg);

    /* client area */
    if (s->pixels) {
        for (int row = 0; row < s->h; row++) {
            for (int col = 0; col < s->w; col++) {
                vbe_putpixel(fx + col, fy + MK_TITLE_H + row, s->pixels[row * s->w + col]);
            }
        }
    }
}

static void draw_panel(void) {
    int y = g_sh - MK_PANEL_H;
    vbe_fill_rect(0, y, g_sw, MK_PANEL_H, 0x001F1F1F);
    vbe_fill_rect(0, y, g_sw, 1, 0x00444444);
    /* Start-like button */
    vbe_fill_rect(4, y + 4, 72, MK_PANEL_H - 8, 0x000078D7);
    mk_fb_text(12, y + 6, "MyKernel", 0x00FFFFFF, 0x000078D7);
    mk_fb_text(g_sw - 200, y + 6, "ESC = exit desktop", 0x00AAAAAA, 0x001F1F1F);
}

void mk_draw_cursor(void) {
    struct mouse_state m;
    mouse_get(&m);
    for (int row = 0; row < MK_CURSOR_H; row++) {
        for (int col = 0; col < MK_CURSOR_W; col++) {
            uint8_t v = cursor_mask[row][col];
            if (!v) continue;
            uint32_t c = (v == 1) ? 0x00FFFFFF : 0x00000000;
            vbe_putpixel(m.x + col, m.y + row, c);
        }
    }
}

void mk_compose(void) {
    if (!g_active) return;
    vbe_clear(g_wallpaper);

    /* soft gradient strip at top */
    for (int y = 0; y < 80 && y < g_sh; y++) {
        uint8_t shade = (uint8_t)(30 + y);
        vbe_fill_rect(0, y, g_sw, 1, vbe_rgb(0, shade / 2, (uint8_t)(80 + shade)));
    }

    mk_fb_text(16, 12, "MyKernel Desktop", 0x00FFFFFF, g_wallpaper);
    mk_fb_text(16, 30, "Drag title bar | click X to close window", 0x00D0D0D0, g_wallpaper);

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
    mk_draw_cursor();
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
