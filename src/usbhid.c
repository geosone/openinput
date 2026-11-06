/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: USB HID devices on eusbcore's interrupt pipes. The report
 * protocol with the device's report descriptor (hidparse.c) when it
 * describes keys, buttons or axes we know - tablets, media keys, more than
 * three buttons; otherwise keyboards and mice in the boot protocol. */

#include <string.h>

#include "eusbcore/usb.h"
#include "hidparse.h"
#include "openinput/input.h"

#define MAX_HID 8

#define HID_SET_IDLE     0x0Au
#define HID_SET_PROTOCOL 0x0Bu
#define HID_SET_REPORT   0x09u
#define REQ_CLASS_IFACE_OUT 0x21u
#define REQ_STD_IFACE_IN    0x81u
#define GET_DESCRIPTOR      0x06u

struct hid {
    struct oi_source src;
    struct usb_intr pipe;
    oa_u8 iface;
    int keyboard;
    int report;              /* report protocol through `desc` */
    oa_u8 last[8];
    oa_u8 buttons;
    struct hid_desc desc;
    char name[24];
};

static struct hid hids[MAX_HID];
static unsigned nhids;

static void keyboard_report(struct hid *h, const oa_u8 *r, oa_u16 n)
{
    unsigned i, j;
    oa_u8 changed;

    if (n < 3u) {
        return;
    }
    if (r[2] == 0x01u) {
        return; /* ErrorRollOver: keep the last state */
    }
    changed = (oa_u8)(r[0] ^ h->last[0]);
    for (i = 0; i < 8u; i++) {
        if (changed & (1u << i)) {
            oi_emit(&h->src, OI_PAGE_KEYBOARD, (oa_u16)(0xE0u + i), (r[0] >> i) & 1u);
        }
    }
    for (i = 2; i < 8u; i++) { /* released */
        oa_u8 k = h->last[i];
        int found = 0;
        if (k < 4u) {
            continue;
        }
        for (j = 2; j < n && j < 8u; j++) {
            found |= r[j] == k;
        }
        if (!found) {
            oi_emit(&h->src, OI_PAGE_KEYBOARD, k, 0);
        }
    }
    for (j = 2; j < n && j < 8u; j++) { /* pressed */
        oa_u8 k = r[j];
        int found = 0;
        if (k < 4u) {
            continue;
        }
        for (i = 2; i < 8u; i++) {
            found |= h->last[i] == k;
        }
        if (!found) {
            oi_emit(&h->src, OI_PAGE_KEYBOARD, k, 1);
        }
    }
    memset(h->last, 0, sizeof(h->last));
    memcpy(h->last, r, n < 8u ? n : 8u);
}

static void mouse_report(struct hid *h, const oa_u8 *r, oa_u16 n)
{
    unsigned i;
    oa_u8 changed;

    if (n < 3u) {
        return;
    }
    changed = (oa_u8)((r[0] ^ h->buttons) & 7u);
    for (i = 0; i < 3u; i++) {
        if (changed & (1u << i)) {
            oi_emit(&h->src, OI_PAGE_BUTTON, (oa_u16)(i + 1u), (r[0] >> i) & 1u);
        }
    }
    h->buttons = r[0];
    if (r[1]) {
        oi_emit(&h->src, OI_PAGE_DESKTOP, OI_DESKTOP_X, (oa_s8)r[1]);
    }
    if (r[2]) {
        oi_emit(&h->src, OI_PAGE_DESKTOP, OI_DESKTOP_Y, (oa_s8)r[2]);
    }
    if (n >= 4u && r[3]) {
        oi_emit(&h->src, OI_PAGE_DESKTOP, OI_DESKTOP_WHEEL, (oa_s8)r[3]);
    }
}

static void hid_poll(struct oi_source *src)
{
    struct hid *h = src->priv;
    oa_u8 r[64];
    oa_u16 n;
    unsigned guard;

    for (guard = 0; guard < 4u; guard++) {
        if (usb_intr_poll(&h->pipe, r, &n) != 1) {
            return;
        }
        if (h->report) {
            hid_report(&h->desc, &h->src, r, n);
        } else if (h->keyboard) {
            keyboard_report(h, r, n);
        } else {
            mouse_report(h, r, n);
        }
    }
}

static void hid_leds(struct oi_source *src, unsigned leds)
{
    struct hid *h = src->priv;
    oa_u8 v[2];
    oa_u16 got, id = h->report ? h->desc.led_report_id : 0u;

    v[0] = (oa_u8)id;
    v[1] = (oa_u8)(leds & 7u);
    usb_control(h->pipe.dev, REQ_CLASS_IFACE_OUT, HID_SET_REPORT, (oa_u16)(0x0200u | id), h->iface,
                id ? v : v + 1, id ? 2u : 1u, &got);
}

static void set_name(struct hid *h, const char *s)
{
    unsigned i;

    for (i = 0; s[i] && i < sizeof(h->name) - 1u; i++) {
        h->name[i] = s[i];
    }
    h->name[i] = 0;
}

/* Interface `iface` of `d` (HID class, boot `protocol` or 0) with its
 * interrupt IN endpoint; `rdlen`: report descriptor length (0: unknown). */
static void attach(struct usb_dev *d, oa_u8 iface, oa_u8 protocol, oa_u8 ep, oa_u16 rdlen)
{
    struct hid *h = &hids[nhids];
    static oa_u8 rd[1024];
    oa_u16 got = 0, len = 8;

    memset(h, 0, sizeof(*h));
    h->iface = iface;
    h->keyboard = protocol == 1u;
    if (rdlen && rdlen <= sizeof(rd) &&
        usb_control(d, REQ_STD_IFACE_IN, GET_DESCRIPTOR, 0x2200u, iface, rd, rdlen, &got) == USB_OK &&
        hid_parse(&h->desc, rd, got) == 0 && h->desc.max_report <= 64u) {
        h->report = 1;
        h->keyboard = h->desc.keys;
        len = h->desc.max_report;
        usb_control(d, REQ_CLASS_IFACE_OUT, HID_SET_PROTOCOL, 1, iface, 0, 0, &got); /* report */
    } else if (protocol == 1u || protocol == 2u) {
        usb_control(d, REQ_CLASS_IFACE_OUT, HID_SET_PROTOCOL, 0, iface, 0, 0, &got); /* boot */
    } else {
        return; /* nothing we understand */
    }
    usb_control(d, REQ_CLASS_IFACE_OUT, HID_SET_IDLE, 0, iface, 0, 0, &got); /* on change */
    if (usb_intr_open(&h->pipe, d, ep, len) < 0) {
        return;
    }
    set_name(h, h->report && h->desc.absolute ? "USB tablet" :
                h->keyboard ? "USB keyboard" : "USB mouse");
    h->src.poll = hid_poll;
    h->src.set_leds = h->keyboard && (!h->report || h->desc.has_leds) ? hid_leds : 0;
    h->src.priv = h;
    if (oi_register(&h->src, h->name) == 0) {
        nhids++;
    }
}

int oi_usb_attach(void)
{
    unsigned i, before = nhids;

    for (i = 0; i < usb_device_count() && nhids < MAX_HID; i++) {
        struct usb_dev *d = usb_device(i);
        const oa_u8 *c = d->config_desc;
        oa_u16 pos = 0, len = d->config_len;
        int hid = 0;
        oa_u8 iface = 0, protocol = 0;
        oa_u16 rdlen = 0;

        while (pos + 2u <= len && c[pos] >= 2u && pos + c[pos] <= len && nhids < MAX_HID) {
            const oa_u8 *x = c + pos;
            if (x[1] == 4u && x[0] >= 9u) { /* interface */
                hid = x[5] == 3u;
                iface = x[2];
                protocol = x[6] == 1u ? x[7] : 0u; /* boot interface subclass */
                rdlen = 0;
            } else if (x[1] == 0x21u && x[0] >= 9u && hid) { /* HID descriptor */
                rdlen = (oa_u16)(x[7] | (x[8] << 8));
            } else if (x[1] == 5u && x[0] >= 7u && hid && (x[2] & 0x80u) && (x[3] & 3u) == 3u) {
                attach(d, iface, protocol, x[2], rdlen);
                hid = 0; /* one pipe per interface */
            }
            pos = (oa_u16)(pos + c[pos]);
        }
    }
    return (int)(nhids - before);
}
