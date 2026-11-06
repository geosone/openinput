/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: PS/2 keyboard and mouse on the 8042 controller (polled, port
 * I/O). The keyboard runs in set 2 with the controller translating to set
 * 1; the mouse in stream mode, with the IntelliMouse wheel when it has
 * one. */

#include "openaspi/native/host.h"
#include "openinput/input.h"

#define DATA   0x60u
#define STATUS 0x64u
#define CMD    0x64u

#define ST_OUT  0x01u /* output buffer full */
#define ST_IN   0x02u /* input buffer full */
#define ST_AUX  0x20u /* the byte is from the mouse */

#define ACK 0xFAu

struct ps2 {
    struct oi_source kbd, mouse;
    int have_kbd, have_mouse;
    /* keyboard decoder */
    int e0, e1;
    /* mouse packets */
    unsigned packet_len, got;
    oa_u8 packet[4];
    oa_u8 buttons;
};

static struct ps2 ps2;

static int wait_in(void)
{
    oa_u64 end = oa_host_time_us() + 100000u;

    while (oa_host_inb(STATUS) & ST_IN) {
        if (oa_host_time_us() > end) {
            return -1;
        }
    }
    return 0;
}

/* A byte from the controller or a device; -1 after `ms`. */
static int read_byte(oa_u32 ms, int *aux)
{
    oa_u64 end = oa_host_time_us() + (oa_u64)ms * 1000u;
    oa_u8 st;

    for (;;) {
        st = oa_host_inb(STATUS);
        if (st & ST_OUT) {
            if (aux) {
                *aux = (st & ST_AUX) != 0;
            }
            return oa_host_inb(DATA);
        }
        if (oa_host_time_us() > end) {
            return -1;
        }
    }
}

static void flush(void)
{
    unsigned i;

    for (i = 0; i < 64u && (oa_host_inb(STATUS) & ST_OUT); i++) {
        (void)oa_host_inb(DATA);
        oa_host_delay_us(100);
    }
}

static int command(oa_u8 c)
{
    if (wait_in() < 0) {
        return -1;
    }
    oa_host_outb(CMD, c);
    return 0;
}

static int write_data(oa_u8 v)
{
    if (wait_in() < 0) {
        return -1;
    }
    oa_host_outb(DATA, v);
    return 0;
}

static void feed_key(oa_u8 b);
static void feed_mouse(oa_u8 b);

/* Send a byte to the keyboard (mouse: `aux`) and wait for its ACK; bytes
 * that come in between go to the decoders. */
static int device_cmd(int aux, oa_u8 v)
{
    unsigned tries;
    int b, from_aux;

    for (tries = 0; tries < 3u; tries++) {
        if (aux && command(0xD4u) < 0) {
            return -1;
        }
        if (write_data(v) < 0) {
            return -1;
        }
        for (;;) {
            b = read_byte(200, &from_aux);
            if (b < 0) {
                return -1;
            }
            if (from_aux == aux && b == ACK) {
                return 0;
            }
            if (from_aux == aux && b == 0xFEu) {
                break; /* resend */
            }
            if (from_aux) {
                feed_mouse((oa_u8)b);
            } else {
                feed_key((oa_u8)b);
            }
        }
    }
    return -1;
}

/* --- keyboard ---------------------------------------------------------- */

static void feed_key(oa_u8 b)
{
    oa_u16 usage;

    if (!ps2.have_kbd) {
        return;
    }
    if (ps2.e1) {
        /* Pause: E1 1D 45 E1 9D C5, make only - five bytes after the E1 */
        if (++ps2.e1 == 3) {
            oi_emit(&ps2.kbd, OI_PAGE_KEYBOARD, 0x48u, 1);
            oi_emit(&ps2.kbd, OI_PAGE_KEYBOARD, 0x48u, 0);
        } else if (ps2.e1 == 6) {
            ps2.e1 = 0;
        }
        return;
    }
    if (b == 0xE1u) {
        ps2.e1 = 1;
        return;
    }
    if (b == 0xE0u) {
        ps2.e0 = 1;
        return;
    }
    if (b == ACK || b == 0xFEu || b == 0x00u || b == 0xFFu) {
        ps2.e0 = 0;
        return; /* acknowledgements, overrun */
    }
    usage = oi_set1_to_usage(b, ps2.e0);
    ps2.e0 = 0;
    if (usage) {
        oi_emit(&ps2.kbd, OI_PAGE_KEYBOARD, usage, (b & 0x80u) ? 0 : 1);
    }
}

static void kbd_leds(struct oi_source *src, unsigned leds)
{
    oa_u8 v = 0;

    (void)src;
    if (leds & OI_LED_SCROLL) {
        v |= 1u;
    }
    if (leds & OI_LED_NUM) {
        v |= 2u;
    }
    if (leds & OI_LED_CAPS) {
        v |= 4u;
    }
    if (device_cmd(0, 0xEDu) == 0) {
        device_cmd(0, v);
    }
}

/* --- mouse ------------------------------------------------------------- */

static void feed_mouse(oa_u8 b)
{
    unsigned i;
    oa_u8 changed;
    oa_s32 dx, dy;

    if (!ps2.have_mouse) {
        return;
    }
    if (ps2.got == 0u && !(b & 0x08u)) {
        return; /* out of step: the first byte has bit 3 set */
    }
    ps2.packet[ps2.got++] = b;
    if (ps2.got < ps2.packet_len) {
        return;
    }
    ps2.got = 0;
    b = ps2.packet[0];
    changed = (oa_u8)((b ^ ps2.buttons) & 7u);
    /* PS/2: bit 0 left, 1 right, 2 middle - as HID buttons 1, 2, 3 */
    for (i = 0; i < 3u; i++) {
        if (changed & (1u << i)) {
            oi_emit(&ps2.mouse, OI_PAGE_BUTTON, (oa_u16)(i + 1u), (b >> i) & 1u);
        }
    }
    ps2.buttons = b;
    if (b & 0xC0u) {
        return; /* overflow */
    }
    dx = (oa_s32)ps2.packet[1] - ((b & 0x10u) ? 256 : 0);
    dy = (oa_s32)ps2.packet[2] - ((b & 0x20u) ? 256 : 0);
    if (dx) {
        oi_emit(&ps2.mouse, OI_PAGE_DESKTOP, OI_DESKTOP_X, dx);
    }
    if (dy) {
        oi_emit(&ps2.mouse, OI_PAGE_DESKTOP, OI_DESKTOP_Y, -dy); /* PS/2: up is positive */
    }
    if (ps2.packet_len == 4u && (ps2.packet[3] & 0x0Fu)) {
        oa_s32 z = ps2.packet[3] & 0x0Fu;
        if (z & 8) {
            z -= 16;
        }
        oi_emit(&ps2.mouse, OI_PAGE_DESKTOP, OI_DESKTOP_WHEEL, -z);
    }
}

static void ps2_poll(struct oi_source *src)
{
    unsigned i;
    oa_u8 st;

    (void)src;
    for (i = 0; i < 64u; i++) {
        st = oa_host_inb(STATUS);
        if (!(st & ST_OUT)) {
            return;
        }
        if (st & ST_AUX) {
            feed_mouse(oa_host_inb(DATA));
        } else {
            feed_key(oa_host_inb(DATA));
        }
    }
}

static void null_poll(struct oi_source *src)
{
    (void)src; /* the keyboard source polls for both */
}

static int mouse_init(void)
{
    int b, id;
    static const oa_u8 rates[3] = { 200, 100, 80 };
    unsigned i;

    command(0xA8u); /* enable the aux port */
    if (device_cmd(1, 0xF6u) < 0) { /* defaults */
        return -1;
    }
    for (i = 0; i < 3u; i++) { /* IntelliMouse knock */
        if (device_cmd(1, 0xF3u) < 0 || device_cmd(1, rates[i]) < 0) {
            return -1;
        }
    }
    if (device_cmd(1, 0xF2u) < 0) {
        return -1;
    }
    b = read_byte(100, 0);
    id = b < 0 ? 0 : b;
    ps2.packet_len = id == 3 || id == 4 ? 4u : 3u;
    if (device_cmd(1, 0xF3u) < 0 || device_cmd(1, 100) < 0 || device_cmd(1, 0xF4u) < 0) {
        return -1;
    }
    return 0;
}

int oi_ps2_init(void)
{
    int cfg, n = 0;

    if (!oa_host_has_port_io() || oa_host_inb(STATUS) == 0xFFu) {
        return 0; /* no controller */
    }
    command(0xADu); /* both ports off while we set up */
    command(0xA7u);
    flush();
    if (command(0x20u) < 0 || (cfg = read_byte(50, 0)) < 0) {
        return 0;
    }
    /* no IRQs, translation on, both clocks on */
    cfg = (cfg & ~0x33) | 0x40;
    command(0x60u);
    write_data((oa_u8)cfg);
    command(0xAEu);
    flush();

    ps2.have_kbd = 1; /* let device_cmd's decoders see bytes */
    if (device_cmd(0, 0xF5u) == 0 &&            /* stop scanning */
        device_cmd(0, 0xF0u) == 0 && device_cmd(0, 2u) == 0 && /* set 2 */
        device_cmd(0, 0xF4u) == 0) {            /* scanning on */
        ps2.kbd.poll = ps2_poll;
        ps2.kbd.set_leds = kbd_leds;
        if (oi_register(&ps2.kbd, "PS/2 keyboard") == 0) {
            n++;
        }
    } else {
        ps2.have_kbd = 0;
    }
    ps2.have_mouse = 1;
    if (mouse_init() == 0) {
        ps2.mouse.poll = n ? null_poll : ps2_poll;
        if (oi_register(&ps2.mouse, "PS/2 mouse") == 0) {
            n++;
        }
    } else {
        ps2.have_mouse = 0;
        command(0xA7u);
    }
    flush();
    return n;
}
