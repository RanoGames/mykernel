/* settings.h — системные настройки + сохранение на FAT */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

struct system_settings {
    int sound_enabled;
    int gfx_mode;
    int gfx_use_vbe;
};

void settings_init(void);
struct system_settings* settings_get(void);
int settings_load_fat(void);
int settings_save_fat(void);
void settings_menu(void);

#endif
