/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: HID report descriptor parser and report decoder (inside
 * openinput; usbhid.c uses it). Input fields on the keyboard, button,
 * generic desktop (X, Y, wheel) and consumer pages; variable and array
 * fields, relative and absolute, report IDs. */

#ifndef OPENINPUT_HIDPARSE_H
#define OPENINPUT_HIDPARSE_H

#include "openinput/input.h"

#define HID_MAX_FIELDS 24
#define HID_MAX_USAGES 16
#define HID_MAX_COUNT  32

struct hid_field {
    oa_u8 report_id;
    oa_u16 bit_offset;      /* after the report ID byte */
    oa_u8 bit_size;
    oa_u8 count;
    oa_u16 page;
    oa_u8 nusages;          /* listed usages; otherwise usage_min..max */
    oa_u16 usages[HID_MAX_USAGES];
    oa_u16 usage_min, usage_max;
    oa_s32 logical_min, logical_max;
    oa_u8 variable, relative;
    oa_s32 last[HID_MAX_COUNT];
};

struct hid_desc {
    struct hid_field fields[HID_MAX_FIELDS];
    unsigned nfields;
    int uses_ids;
    oa_u16 max_report;      /* bytes, with the report ID */
    oa_u8 led_report_id;    /* output report with the LEDs */
    int has_leds;
    int keys, pointer, absolute; /* what the device reports */
};

/* 0 when the descriptor gave at least one field we handle. */
int hid_parse(struct hid_desc *d, const oa_u8 *desc, unsigned len);
/* Decode one input report into events from `src`. */
void hid_report(struct hid_desc *d, struct oi_source *src, const oa_u8 *r, unsigned len);

#endif
