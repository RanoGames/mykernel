/* settings.c — меню настроек, SETTINGS.CFG на FAT32 */

#include "settings.h"
#include "vbe.h"
#include "fat32.h"
#include "vga.h"
#include "platform.h"
#include "gfx.h"
#include "keyboard.h"
#include "sound.h"
#include <stddef.h>

static struct system_settings g_cfg = {
    .sound_enabled = 1,
    .gfx_mode = 1,
    .gfx_use_vbe = 1,
};

void settings_init(void) {
    g_cfg.sound_enabled = 1;
    g_cfg.gfx_mode = VBE_MODE_800x600;
    g_cfg.gfx_use_vbe = 1;
}

struct system_settings* settings_get(void) {
    return &g_cfg;
}

static int parse_int_after(const char* line, const char* key, int* out) {
    size_t klen = 0;
    while (key[klen]) klen++;
    size_t i = 0;
    while (key[i] && line[i] == key[i]) i++;
    if (i != klen || line[i] != '=')
        return 0;
    i++;
    int neg = 0;
    if (line[i] == '-') { neg = 1; i++; }
    int v = 0, any = 0;
    while (line[i] >= '0' && line[i] <= '9') {
        v = v * 10 + (line[i] - '0');
        i++;
        any = 1;
    }
    if (!any) return 0;
    *out = neg ? -v : v;
    return 1;
}

static void apply_cfg_text(const char* text) {
    const char* p = text;
    while (*p) {
        char line[64];
        size_t n = 0;
        while (*p && *p != '\n' && n < sizeof(line) - 1)
            line[n++] = *p++;
        line[n] = '\0';
        if (*p == '\n') p++;
        int v;
        if (parse_int_after(line, "sound", &v))
            g_cfg.sound_enabled = v ? 1 : 0;
        else if (parse_int_after(line, "gfx_mode", &v)) {
            if (v >= 0 && v <= 2) g_cfg.gfx_mode = v;
        } else if (parse_int_after(line, "use_vbe", &v))
            g_cfg.gfx_use_vbe = v ? 1 : 0;
    }
}

static void build_cfg_text(char* buf, size_t max) {
    size_t o = 0;
#define PUT(s) do { const char* _p = (s); while (*_p && o + 1 < max) buf[o++] = *_p++; } while (0)
    PUT("sound=");
    if (o + 1 < max) buf[o++] = g_cfg.sound_enabled ? '1' : '0';
    PUT("\ngfx_mode=");
    if (o + 1 < max) buf[o++] = (char)('0' + (g_cfg.gfx_mode % 10));
    PUT("\nuse_vbe=");
    if (o + 1 < max) buf[o++] = g_cfg.gfx_use_vbe ? '1' : '0';
    PUT("\n");
    buf[o] = '\0';
#undef PUT
}

int settings_load_fat(void) {
    if (!fat32_is_mounted()) {
        terminal_writestring("settings: FAT not mounted (fatmount first)\n");
        return -1;
    }
    char buf[512];
    size_t n = 0;
    enum fat_result r = fat32_read_file("SETTINGS.CFG", buf, sizeof(buf) - 1, &n);
    if (r != FAT_OK) {
        terminal_writestring("settings: SETTINGS.CFG not found (save first)\n");
        return -1;
    }
    buf[n] = '\0';
    apply_cfg_text(buf);
    terminal_writestring("settings: loaded from SETTINGS.CFG\n");
    return 0;
}

int settings_save_fat(void) {
    if (!fat32_is_mounted()) {
        terminal_writestring("settings: FAT not mounted. Run: fatmount\n");
        return -1;
    }
    char buf[256];
    build_cfg_text(buf, sizeof(buf));
    enum fat_result r = fat32_write("SETTINGS.CFG", buf);
    if (r != FAT_OK) {
        terminal_writestring("settings: FAT write failed: ");
        terminal_writestring(fat_strerror(r));
        terminal_putchar('\n');
        return -1;
    }
    terminal_writestring("settings: saved to SETTINGS.CFG on FAT\n");
    return 0;
}

static void print_mode_name(int m) {
    if (m == 0) terminal_writestring("640x480");
    else if (m == 1) terminal_writestring("800x600");
    else if (m == 2) terminal_writestring("1024x768");
    else terminal_writestring("?");
}

static void show_status(void) {
    terminal_writestring("--- current ---\n");
    terminal_writestring("  sound: ");
    terminal_writestring(g_cfg.sound_enabled ? "on\n" : "off\n");
    terminal_writestring("  gfx:   ");
    print_mode_name(g_cfg.gfx_mode);
    terminal_writestring(g_cfg.gfx_use_vbe ? " (VBE)\n" : " (Mode13 prefer)\n");
}


static void kbd_drain(void) {
    char junk;
    int n = 0;
    while (keyboard_trygetchar(&junk) && n++ < 64)
        ;
}

/* Wait for a single menu key; ignore junk/autorepeat/serial noise */
static char settings_wait_key(void) {
    for (;;) {
        kbd_drain();
        char c = keyboard_getchar();
        if (c >= '0' && c <= '9') return c;
        if (c == 'q' || c == 'Q') return c;
        /* ignore everything else — do not tight-loop without blocking:
         * getchar already blocked once */
    }
}

void settings_menu(void) {
    /* Restore text only if we were in graphics/FB — full CRTC rewrite is slow */
    terminal_disable_fb();
    if (gfx_is_graphics() || platform_is_virtualbox()) {
        /* VBox: cheap path — just ensure text buffer is writable */
        if (gfx_is_graphics())
            gfx_restore_text();
    }
    kbd_drain();

    terminal_writestring("\n=== MyKernel Settings ===\n");
    if (platform_is_virtualbox()) {
        terminal_writestring("Host: VirtualBox detected\n");
        terminal_writestring("  VBE/DISPI: not available — use Mode13 for games\n");
        terminal_writestring("  desktop: needs QEMU (Bochs VBE)\n");
        terminal_writestring("  sound: PC speaker often silent in VBox\n");
        g_cfg.gfx_use_vbe = 0;
    } else {
        terminal_writestring("VBE DISPI: ");
        terminal_writestring(vbe_probe() ? "yes (QEMU)\n" : "no\n");
    }
    show_status();
    terminal_writestring(
        "  1) Toggle sound\n"
        "  2) System res: 640x480 (VBE console)\n"
        "  3) System res: 800x600 (VBE console)\n"
        "  4) System res: 1024x768 (VBE console)\n"
        "  5) Prefer VBE + enable FB console\n"
        "  6) Prefer Mode13 + text 80x25\n"
        "  7) Test VBE now (demo)\n"
        "  8) Save to FAT (SETTINGS.CFG)\n"
        "  9) Load from FAT\n"
        "  0) Exit\n"
    );

    for (;;) {
        terminal_writestring("settings> ");
        char c = settings_wait_key();
        if (c == '0' || c == 'q' || c == 'Q') {
            terminal_putchar('\n');
            break;
        }
        terminal_putchar(c);
        terminal_putchar('\n');
        kbd_drain();

        if (c == '1') {
            g_cfg.sound_enabled = !g_cfg.sound_enabled;
            terminal_writestring(g_cfg.sound_enabled ? "sound on\n" : "sound off\n");
            if (g_cfg.sound_enabled) sound_beep_ok();
        } else if (c == '2' || c == '3' || c == '4' || c == '5') {
            if (platform_is_virtualbox()) {
                terminal_writestring("VBE console not supported on VirtualBox.\n");
                terminal_writestring("Use option 6 (Mode13) or run under QEMU.\n");
                g_cfg.gfx_use_vbe = 0;
            } else if (c == '2') {
                g_cfg.gfx_mode = 0;
                g_cfg.gfx_use_vbe = 1;
                if (terminal_enable_fb(0) == 0)
                    terminal_writestring("system console: 640x480 VBE\n");
                else {
                    g_cfg.gfx_use_vbe = 0;
                    terminal_writestring("VBE failed\n");
                }
            } else if (c == '3') {
                g_cfg.gfx_mode = 1;
                g_cfg.gfx_use_vbe = 1;
                if (terminal_enable_fb(1) == 0)
                    terminal_writestring("system console: 800x600 VBE\n");
                else {
                    g_cfg.gfx_use_vbe = 0;
                    terminal_writestring("VBE failed\n");
                }
            } else if (c == '4') {
                g_cfg.gfx_mode = 2;
                g_cfg.gfx_use_vbe = 1;
                if (terminal_enable_fb(2) == 0)
                    terminal_writestring("system console: 1024x768 VBE\n");
                else {
                    g_cfg.gfx_use_vbe = 0;
                    terminal_writestring("VBE failed\n");
                }
            } else {
                g_cfg.gfx_use_vbe = 1;
                if (terminal_enable_fb(g_cfg.gfx_mode) == 0)
                    terminal_writestring("prefer VBE + FB console on\n");
                else {
                    g_cfg.gfx_use_vbe = 0;
                    terminal_writestring("VBE failed\n");
                }
            }
        } else if (c == '6') {
            g_cfg.gfx_use_vbe = 0;
            terminal_disable_fb();
            gfx_restore_text();
            terminal_writestring("prefer Mode13, text 80x25 restored\n");
        } else if (c == '7') {
            if (platform_is_virtualbox())
                terminal_writestring("VBE demo skipped on VirtualBox\n");
            else
                vbe_demo();
            gfx_restore_text();
        } else if (c == '8') {
            settings_save_fat();
        } else if (c == '9') {
            settings_load_fat();
        }
        show_status();
        kbd_drain();
    }
}
