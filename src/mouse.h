/* mouse.h — PS/2 мышь + события */

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

struct mouse_state {
    int x, y;           /* 0..319, 0..199 в графике */
    int dx, dy;         /* дельта с прошлого опроса */
    int buttons;        /* bit0=left bit1=right bit2=middle */
};

struct mouse_event {
    int type;           /* 0=motion 1=button */
    int x, y;
    int buttons;
    int button;         /* какая кнопка изменилась */
    int pressed;        /* 1 down 0 up */
};

void mouse_init(void);
void mouse_get(struct mouse_state* out);
int  mouse_poll_event(struct mouse_event* ev); /* 1 если есть событие */
void mouse_set_bounds(int w, int h);

#endif
