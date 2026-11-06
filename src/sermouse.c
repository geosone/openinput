/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: serial mice on a 16550 UART (polled, port I/O).
 *
 * Microsoft protocol (1200 baud, 7N1): with DTR and RTS raised the mouse
 * sends its ID - "M" for two buttons, "M3" for Logitech's third button (a
 * fourth byte when it changes), "MZ" for the IntelliMouse wheel (a fourth
 * byte with middle button and wheel). Packets: three bytes, the first
 * with bit 6 set: 1 L R Y7 Y6 X7 X6, then X5-X0, Y5-Y0; Y down positive.
 * Mouse Systems (1200 baud, 8N1, no ID): five bytes, 10000LMR (buttons
 * active low), X1, Y1, X2, Y2 with Y up positive. */

#include "openaspi/native/host.h"
#include "openinput/input.h"

#define RBR 0u
#define THR 0u
#define DLL 0u
#define IER 1u
#define DLM 1u
#define FCR 2u
#define LCR 3u
#define MCR 4u
#define LSR 5u
#define SCR 7u

#define MAX_MICE 2

enum { MS, MS3, MSWHEEL, MOUSESYS };

struct smouse {
    struct oi_source src;
    oa_u16 port;
    int proto;
    oa_u8 packet[5];
    unsigned got;
    oa_u8 buttons;
    char name[24];
};

static struct smouse mice[MAX_MICE];
static unsigned nmice;

static void buttons(struct smouse *m, oa_u8 b)
{
    unsigned i;
    oa_u8 changed = (oa_u8)(b ^ m->buttons);

    for (i = 0; i < 3u; i++) {
        if (changed & (1u << i)) {
            oi_emit(&m->src, OI_PAGE_BUTTON, (oa_u16)(i + 1u), (b >> i) & 1u);
        }
    }
    m->buttons = b;
}

static void motion(struct smouse *m, oa_s32 dx, oa_s32 dy)
{
    if (dx) {
        oi_emit(&m->src, OI_PAGE_DESKTOP, OI_DESKTOP_X, dx);
    }
    if (dy) {
        oi_emit(&m->src, OI_PAGE_DESKTOP, OI_DESKTOP_Y, dy);
    }
}

static void feed(struct smouse *m, oa_u8 c)
{
    oa_u8 *p = m->packet;

    if (m->proto == MOUSESYS) {
        if (m->got == 0u && (c & 0xF8u) != 0x80u) {
            return;
        }
        p[m->got++] = c;
        if (m->got < 5u) {
            return;
        }
        m->got = 0;
        /* L bit 2, M bit 1, R bit 0, active low; HID: L 1, R 2, M 3 */
        buttons(m, (oa_u8)((!(p[0] & 4u) ? 1u : 0u) | (!(p[0] & 1u) ? 2u : 0u) |
                           (!(p[0] & 2u) ? 4u : 0u)));
        motion(m, (oa_s32)(oa_s8)p[1] + (oa_s8)p[3], -((oa_s32)(oa_s8)p[2] + (oa_s8)p[4]));
        return;
    }
    c &= 0x7Fu;
    if (c & 0x40u) { /* first byte of a packet */
        p[0] = c;
        m->got = 1;
        return;
    }
    if (m->got == 0u || m->got > 3u) {
        /* an extension byte after a complete packet */
        if (m->got == 4u && m->proto == MS3) {
            buttons(m, (oa_u8)((m->buttons & 3u) | ((c & 0x20u) ? 4u : 0u)));
        } else if (m->got == 4u && m->proto == MSWHEEL) {
            oa_s32 z = c & 0x0Fu;
            buttons(m, (oa_u8)((m->buttons & 3u) | ((c & 0x10u) ? 4u : 0u)));
            if (z & 8) {
                z -= 16;
            }
            if (z) {
                oi_emit(&m->src, OI_PAGE_DESKTOP, OI_DESKTOP_WHEEL, -z);
            }
        }
        m->got = 0;
        return;
    }
    p[m->got++] = c;
    if (m->got == 3u) {
        oa_s8 dx = (oa_s8)(((p[0] & 0x03u) << 6) | (p[1] & 0x3Fu));
        oa_s8 dy = (oa_s8)(((p[0] & 0x0Cu) << 4) | (p[2] & 0x3Fu));
        buttons(m, (oa_u8)((m->buttons & 4u) | ((p[0] & 0x20u) ? 1u : 0u) | ((p[0] & 0x10u) ? 2u : 0u)));
        motion(m, dx, dy);
        m->got = 4; /* an extension byte may follow */
    }
}

static void s_poll(struct oi_source *src)
{
    struct smouse *m = src->priv;
    unsigned i;

    for (i = 0; i < 64u && (oa_host_inb((oa_u16)(m->port + LSR)) & 1u); i++) {
        feed(m, oa_host_inb((oa_u16)(m->port + RBR)));
    }
}

int oi_serial_mouse_init(oa_u16 port, int protocol)
{
    struct smouse *m;
    oa_u64 end;
    char id[4];
    unsigned n = 0;

    if (nmice == MAX_MICE || !oa_host_has_port_io()) {
        return 0;
    }
    /* is there a UART? */
    oa_host_outb((oa_u16)(port + SCR), 0x5Au);
    if (oa_host_inb((oa_u16)(port + SCR)) != 0x5Au) {
        return 0;
    }
    m = &mice[nmice];
    m->port = port;
    m->got = 0;
    m->buttons = 0;
    oa_host_outb((oa_u16)(port + IER), 0);
    oa_host_outb((oa_u16)(port + LCR), 0x80u);  /* divisor: 115200 / 96 = 1200 baud */
    oa_host_outb((oa_u16)(port + DLL), 96);
    oa_host_outb((oa_u16)(port + DLM), 0);
    oa_host_outb((oa_u16)(port + LCR), protocol == OI_SERIAL_MOUSE_SYSTEMS ? 0x03u : 0x02u);
    oa_host_outb((oa_u16)(port + FCR), 0x07u);  /* FIFO on, cleared */
    oa_host_outb((oa_u16)(port + MCR), 0);      /* power off ... */
    oa_host_delay_us(200000);
    while (oa_host_inb((oa_u16)(port + LSR)) & 1u) {
        (void)oa_host_inb((oa_u16)(port + RBR));
    }
    oa_host_outb((oa_u16)(port + MCR), 0x0Bu);  /* ... and on: DTR, RTS, OUT2 */
    if (protocol == OI_SERIAL_MOUSE_SYSTEMS) {
        m->proto = MOUSESYS;
    } else {
        /* collect the ID for 300 ms, then let the line go quiet: what is
         * left of it must not be taken for a packet */
        end = oa_host_time_us() + 300000u;
        while (oa_host_time_us() < end) {
            if (oa_host_inb((oa_u16)(port + LSR)) & 1u) {
                char c = (char)(oa_host_inb((oa_u16)(port + RBR)) & 0x7Fu);
                if ((n == 0u && c != 'M') || n == sizeof(id)) {
                    continue;
                }
                id[n++] = c;
                end = oa_host_time_us() + 50000u; /* until 50 ms of silence */
            }
        }
        if (n == 0u) {
            return 0; /* nothing answered */
        }
        m->proto = n > 1u && id[1] == '3' ? MS3 : n > 1u && id[1] == 'Z' ? MSWHEEL : MS;
    }
    {
        static const char *const names[] = { "serial mouse", "serial mouse (Logitech)",
                                             "serial mouse (wheel)", "serial mouse (Mouse Systems)" };
        const char *s = names[m->proto];
        unsigned i;
        for (i = 0; s[i] && i < sizeof(m->name) - 1u; i++) {
            m->name[i] = s[i];
        }
        m->name[i] = 0;
    }
    m->src.poll = s_poll;
    m->src.set_leds = 0;
    m->src.priv = m;
    if (oi_register(&m->src, m->name) < 0) {
        return 0;
    }
    nmice++;
    return 1;
}
