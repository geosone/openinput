/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: HID report descriptor parser and report decoder. */

#include <string.h>

#include "hidparse.h"

struct globals {
    oa_u16 page;
    oa_s32 lmin, lmax;
    oa_u32 lmax_u;      /* the maximum read unsigned */
    oa_u32 size, count;
    oa_u8 id;
};

static oa_u32 item_u(const oa_u8 *p, unsigned n)
{
    oa_u32 v = 0;
    unsigned i;

    for (i = 0; i < n; i++) {
        v |= (oa_u32)p[i] << (8u * i);
    }
    return v;
}

static oa_s32 item_s(const oa_u8 *p, unsigned n)
{
    oa_u32 v = item_u(p, n);

    if (n == 1u) {
        return (oa_s8)v;
    }
    if (n == 2u) {
        return (oa_s16)v;
    }
    return (oa_s32)v;
}

static int wanted(oa_u16 page)
{
    return page == OI_PAGE_KEYBOARD || page == OI_PAGE_BUTTON || page == OI_PAGE_DESKTOP ||
           page == OI_PAGE_CONSUMER;
}

int hid_parse(struct hid_desc *d, const oa_u8 *desc, unsigned len)
{
    struct globals g, stack[4];
    unsigned sp = 0, pos = 0, i;
    oa_u16 usages[HID_MAX_USAGES];
    unsigned nusages = 0;
    oa_u32 umin = 0, umax = 0;
    int have_min = 0;
    oa_u16 bits[256];  /* input bits per report ID */

    memset(d, 0, sizeof(*d));
    memset(&g, 0, sizeof(g));
    memset(bits, 0, sizeof(bits));
    while (pos < len) {
        oa_u8 pre = desc[pos];
        unsigned n = pre & 3u, type = (pre >> 2) & 3u, tag = pre >> 4;
        const oa_u8 *data;
        oa_u32 u;

        if (pre == 0xFEu) { /* long item */
            if (pos + 1u >= len) {
                break;
            }
            pos += 3u + desc[pos + 1];
            continue;
        }
        if (n == 3u) {
            n = 4;
        }
        if (pos + 1u + n > len) {
            break;
        }
        data = desc + pos + 1;
        u = item_u(data, n);
        pos += 1u + n;
        if (type == 1u) { /* global */
            switch (tag) {
            case 0: g.page = (oa_u16)u; break;
            case 1: g.lmin = item_s(data, n); break;
            case 2: g.lmax = item_s(data, n); g.lmax_u = u; break;
            case 7: g.size = u; break;
            case 8: g.id = (oa_u8)u; d->uses_ids = 1; break;
            case 9: g.count = u; break;
            case 10: if (sp < 4u) { stack[sp++] = g; } break;
            case 11: if (sp) { g = stack[--sp]; } break;
            }
            continue;
        }
        if (type == 2u) { /* local */
            if (tag == 0u && nusages < HID_MAX_USAGES) {
                usages[nusages++] = (oa_u16)u; /* extended usages (4 bytes) keep the low half */
            } else if (tag == 1u) {
                umin = u;
                have_min = 1;
            } else if (tag == 2u) {
                umax = u;
            }
            continue;
        }
        if (type == 0u && (tag == 8u || tag == 9u)) { /* input, output */
            oa_u32 total = g.size * g.count;
            if (tag == 9u) { /* output: only the LEDs */
                if (g.page == 0x08u) {
                    d->has_leds = 1;
                    d->led_report_id = g.id;
                }
            } else if (!(u & 1u) && g.size && g.size <= 32u && g.count && wanted(g.page) &&
                       d->nfields < HID_MAX_FIELDS) { /* data, not constant */
                struct hid_field *f = &d->fields[d->nfields++];
                f->report_id = g.id;
                f->bit_offset = bits[g.id];
                f->bit_size = (oa_u8)g.size;
                f->count = (oa_u8)(g.count > HID_MAX_COUNT ? HID_MAX_COUNT : g.count);
                f->page = g.page;
                f->variable = (u & 2u) != 0;
                f->relative = (u & 4u) != 0;
                f->logical_min = g.lmin;
                f->logical_max = g.lmax;
                if (g.lmin >= 0 && g.lmax < g.lmin) {
                    f->logical_max = (oa_s32)g.lmax_u; /* "Logical Maximum 255" in one byte */
                }
                if (have_min) {
                    f->usage_min = (oa_u16)umin;
                    f->usage_max = (oa_u16)umax;
                } else {
                    f->nusages = (oa_u8)nusages;
                    for (i = 0; i < nusages; i++) {
                        f->usages[i] = usages[i];
                    }
                    if (nusages) {
                        f->usage_min = usages[0];
                        f->usage_max = usages[nusages - 1u];
                    }
                }
                if (!f->variable) {
                    for (i = 0; i < HID_MAX_COUNT; i++) {
                        f->last[i] = f->logical_min - 1; /* nothing pressed */
                    }
                }
                if (g.page == OI_PAGE_KEYBOARD || g.page == OI_PAGE_CONSUMER) {
                    d->keys = 1;
                } else if (g.page == OI_PAGE_DESKTOP || g.page == OI_PAGE_BUTTON) {
                    d->pointer = 1;
                    if (g.page == OI_PAGE_DESKTOP && !f->relative) {
                        d->absolute = 1;
                    }
                }
            }
            if (tag == 8u) {
                bits[g.id] = (oa_u16)(bits[g.id] + total);
                if ((bits[g.id] + 7u) / 8u + (d->uses_ids ? 1u : 0u) > d->max_report) {
                    d->max_report = (oa_u16)((bits[g.id] + 7u) / 8u + (d->uses_ids ? 1u : 0u));
                }
            }
        }
        if (type == 0u) { /* every main item ends the local items */
            nusages = 0;
            have_min = 0;
            umin = umax = 0;
        }
    }
    return d->nfields ? 0 : -1;
}

static oa_s32 field_value(const struct hid_field *f, const oa_u8 *r, unsigned len, unsigned i)
{
    oa_u32 bit = f->bit_offset + (oa_u32)f->bit_size * i, v = 0, k;

    for (k = 0; k < f->bit_size; k++, bit++) {
        if (bit / 8u < len && (r[bit / 8u] >> (bit % 8u)) & 1u) {
            v |= 1ul << k;
        }
    }
    if (f->logical_min < 0 && f->bit_size < 32u && (v & (1ul << (f->bit_size - 1u)))) {
        return (oa_s32)(v | ~((1ul << f->bit_size) - 1u)); /* sign */
    }
    return (oa_s32)v;
}

static oa_u16 usage_at(const struct hid_field *f, unsigned i)
{
    if (f->nusages) {
        return f->usages[i < f->nusages ? i : f->nusages - 1u];
    }
    return (oa_u16)(f->usage_min + i);
}

/* The usage an array field value stands for; 0 for none or out of range. */
static oa_u16 array_usage(const struct hid_field *f, oa_s32 v)
{
    if (v < f->logical_min || v > f->logical_max) {
        return 0;
    }
    return usage_at(f, (unsigned)(v - f->logical_min));
}

static void emit_key(struct oi_source *src, oa_u16 page, oa_u16 usage, int down)
{
    if (page == OI_PAGE_KEYBOARD && usage < 4u) {
        return; /* no key, error codes */
    }
    oi_emit(src, page, usage, down);
}

void hid_report(struct hid_desc *d, struct oi_source *src, const oa_u8 *r, unsigned len)
{
    unsigned fi, i, j;
    oa_u8 id = 0;

    if (d->uses_ids) {
        if (len == 0u) {
            return;
        }
        id = r[0];
        r++;
        len--;
    }
    for (fi = 0; fi < d->nfields; fi++) {
        struct hid_field *f = &d->fields[fi];
        oa_s32 now[HID_MAX_COUNT];
        if (f->report_id != id) {
            continue;
        }
        for (i = 0; i < f->count; i++) {
            now[i] = field_value(f, r, len, i);
        }
        if (!f->variable) { /* array: the usages pressed now */
            oa_u16 unow[HID_MAX_COUNT], ulast[HID_MAX_COUNT];
            int rollover = 0;
            for (i = 0; i < f->count; i++) {
                unow[i] = array_usage(f, now[i]);
                ulast[i] = array_usage(f, f->last[i]);
                rollover |= f->page == OI_PAGE_KEYBOARD && unow[i] == 1u;
            }
            if (rollover) {
                continue; /* ErrorRollOver: keep the last state */
            }
            for (i = 0; i < f->count; i++) {
                int released = ulast[i] != 0u, pressed = unow[i] != 0u;
                for (j = 0; j < f->count; j++) {
                    released &= unow[j] != ulast[i];
                    pressed &= ulast[j] != unow[i];
                }
                if (released) {
                    emit_key(src, f->page, ulast[i], 0);
                }
                if (pressed) {
                    emit_key(src, f->page, unow[i], 1);
                }
            }
        } else {
            for (i = 0; i < f->count; i++) {
                oa_u16 u = usage_at(f, i);
                if (f->page == OI_PAGE_DESKTOP) {
                    if (u != OI_DESKTOP_X && u != OI_DESKTOP_Y && u != OI_DESKTOP_WHEEL) {
                        continue;
                    }
                    if (f->relative) {
                        if (now[i]) {
                            oi_emit(src, OI_PAGE_DESKTOP, u, now[i]);
                        }
                    } else if (now[i] != f->last[i] && f->logical_max > f->logical_min) {
                        oa_s32 v = now[i] < f->logical_min ? f->logical_min :
                                   now[i] > f->logical_max ? f->logical_max : now[i];
                        oi_emit_abs(src, OI_PAGE_DESKTOP, u,
                                    (oa_s32)(((oa_s64)(v - f->logical_min) * OI_ABS_MAX) /
                                             (f->logical_max - f->logical_min)));
                    }
                } else if (f->relative) {
                    if (now[i]) {
                        oi_emit(src, f->page, u, now[i]); /* consumer AC Pan and the like */
                    }
                } else if ((now[i] != 0) != (f->last[i] != 0)) {
                    emit_key(src, f->page, u, now[i] != 0); /* modifiers, buttons */
                }
            }
        }
        for (i = 0; i < f->count; i++) {
            f->last[i] = now[i];
        }
    }
}
