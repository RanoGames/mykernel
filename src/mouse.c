/* mouse.c — PS/2 mouse (IRQ12) */

#include "mouse.h"
#include "isr.h"
#include "io.h"
#include <stdint.h>

#define KBD_DATA 0x60
#define KBD_CMD  0x64
#define KBD_STAT 0x64

static volatile int mx, my;
static volatile int mbuttons;
static volatile int mdx, mdy, mdz;
static int bound_w = 320, bound_h = 200;
static int has_wheel = 0;
static uint8_t mouse_dev_id = 0;

int mouse_has_wheel(void) {
    return has_wheel;
}

#define EVQ 32
static struct mouse_event evq[EVQ];
static volatile int ev_head, ev_tail;
static int prev_buttons;

static void mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(KBD_STAT) & 2)) return;
}

static void mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(KBD_STAT) & 1) return;
}

static void mouse_write(uint8_t b) {
    mouse_wait_write();
    outb(KBD_CMD, 0xD4);
    mouse_wait_write();
    outb(KBD_DATA, b);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(KBD_DATA);
}

static void ev_push(const struct mouse_event* e) {
    int n = (ev_head + 1) % EVQ;
    if (n == ev_tail) return;
    evq[ev_head] = *e;
    ev_head = n;
}

static void mouse_irq_handler(struct registers* regs) {
    (void)regs;
    static uint8_t cycle;
    static uint8_t packet[4];

    uint8_t data = inb(KBD_DATA);

    if (cycle == 0 && !(data & 0x08))
        return; /* resync */

    packet[cycle++] = data;
    int packet_size = has_wheel ? 4 : 3;
    if (cycle < packet_size) return;
    cycle = 0;

    int dx = (int)(int8_t)packet[1];
    int dy = (int)(int8_t)packet[2];
    /* Y in PS/2 is inverted relative to screen */
    dy = -dy;

    int dz = 0;
    if (has_wheel) {
        int8_t raw_z = (int8_t)packet[3];
        if (mouse_dev_id == 4) {
            raw_z = (int8_t)(packet[3] & 0x0F);
            if (raw_z & 0x08) raw_z |= 0xF0;
        }
        if (raw_z > 0) dz = 1;
        else if (raw_z < 0) dz = -1;
    }

    int buttons = packet[0] & 0x07;

    mx += dx;
    my += dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= bound_w) mx = bound_w - 1;
    if (my >= bound_h) my = bound_h - 1;

    mdx += dx;
    mdy += dy;
    mdz += dz;
    mbuttons = buttons;

    struct mouse_event ev;
    ev.x = mx;
    ev.y = my;
    ev.buttons = buttons;
    ev.dz = 0;

    if (dx || dy) {
        ev.type = 0;
        ev.button = 0;
        ev.pressed = 0;
        ev_push(&ev);
    }

    int changed = buttons ^ prev_buttons;
    if (changed) {
        for (int b = 0; b < 3; b++) {
            if (changed & (1 << b)) {
                ev.type = 1;
                ev.button = b;
                ev.pressed = (buttons & (1 << b)) ? 1 : 0;
                ev_push(&ev);
            }
        }
        prev_buttons = buttons;
    }

    if (dz != 0) {
        ev.type = 2; /* Wheel event */
        ev.button = 0;
        ev.pressed = 0;
        ev.dz = dz;
        ev_push(&ev);
    }
}

void mouse_init(void) {
    mx = 160;
    my = 100;
    mbuttons = 0;
    mdx = mdy = mdz = 0;
    ev_head = ev_tail = 0;
    prev_buttons = 0;
    has_wheel = 0;
    mouse_dev_id = 0;

    /* enable auxiliary device */
    mouse_wait_write();
    outb(KBD_CMD, 0xA8);

    /* status */
    mouse_wait_write();
    outb(KBD_CMD, 0x20);
    mouse_wait_read();
    uint8_t status = inb(KBD_DATA);
    status |= 0x02;  /* enable IRQ12 */
    status &= ~0x20; /* disable clock inhibit? */
    mouse_wait_write();
    outb(KBD_CMD, 0x60);
    mouse_wait_write();
    outb(KBD_DATA, status);

    /* use default settings */
    mouse_write(0xF6);
    mouse_read(); /* ACK */

    /* IntelliMouse wheel probe sequence: set sample rates 200, 100, 80 */
    mouse_write(0xF3); mouse_read();
    mouse_write(200);  mouse_read();
    mouse_write(0xF3); mouse_read();
    mouse_write(100);  mouse_read();
    mouse_write(0xF3); mouse_read();
    mouse_write(80);   mouse_read();

    /* Query device ID */
    mouse_write(0xF2); mouse_read(); /* ACK */
    mouse_dev_id = mouse_read();
    if (mouse_dev_id == 3 || mouse_dev_id == 4) {
        has_wheel = 1;
    }

    /* enable streaming */
    mouse_write(0xF4);
    mouse_read();

    irq_register_handler(12, mouse_irq_handler);
    irq_unmask(12);
}

void mouse_get(struct mouse_state* out) {
    out->x = mx;
    out->y = my;
    out->dx = mdx;
    out->dy = mdy;
    out->dz = mdz;
    out->buttons = mbuttons;
    mdx = 0;
    mdy = 0;
    mdz = 0;
}

int mouse_poll_event(struct mouse_event* ev) {
    if (ev_head == ev_tail) return 0;
    *ev = evq[ev_tail];
    ev_tail = (ev_tail + 1) % EVQ;
    return 1;
}

void mouse_set_bounds(int w, int h) {
    bound_w = w;
    bound_h = h;
    if (mx >= w) mx = w - 1;
    if (my >= h) my = h - 1;
}
