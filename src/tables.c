/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Scancode set 1 <-> HID keyboard usages, after Microsoft's Keyboard Scan
 * Code Specification (the set 1 column; an 8042 in translation mode turns
 * the keyboard's set 2 into it), and Linux input key codes (virtio-input),
 * which are set 1 codes up to 88 (F12). */

#include "openinput/input.h"

/* set 1 codes 00h-7Fh without prefix */
static const oa_u8 set1[128] = {
    /* 00 */ 0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    /* 08 */ 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,
    /* 10 */ 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C,
    /* 18 */ 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16,
    /* 20 */ 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33,
    /* 28 */ 0x34, 0x35, 0xE1, 0x31, 0x1D, 0x1B, 0x06, 0x19,
    /* 30 */ 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x55,
    /* 38 */ 0xE2, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
    /* 40 */ 0x3F, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5F,
    /* 48 */ 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59,
    /* 50 */ 0x5A, 0x5B, 0x62, 0x63, 0x46, 0x00, 0x64, 0x44,
    /* 58 */ 0x45, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 60 */ 0x00, 0x00, 0x00, 0x00, 0x68, 0x69, 0x6A, 0x6B,
    /* 68 */ 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x00,
    /* 70 */ 0x88, 0x00, 0x00, 0x87, 0x00, 0x00, 0x73, 0x00,
    /* 78 */ 0x00, 0x8A, 0x00, 0x8B, 0x00, 0x89, 0x85, 0x00,
};

/* set 1 codes after E0h */
static const oa_u8 e0[][2] = {
    { 0x1C, 0x58 }, /* keypad Enter */
    { 0x1D, 0xE4 }, /* right Ctrl */
    { 0x35, 0x54 }, /* keypad / */
    { 0x37, 0x46 }, /* Print Screen */
    { 0x38, 0xE6 }, /* right Alt */
    { 0x46, 0x48 }, /* Ctrl+Break: Pause */
    { 0x47, 0x4A }, /* Home */
    { 0x48, 0x52 }, /* Up */
    { 0x49, 0x4B }, /* Page Up */
    { 0x4B, 0x50 }, /* Left */
    { 0x4D, 0x4F }, /* Right */
    { 0x4F, 0x4D }, /* End */
    { 0x50, 0x51 }, /* Down */
    { 0x51, 0x4E }, /* Page Down */
    { 0x52, 0x49 }, /* Insert */
    { 0x53, 0x4C }, /* Delete */
    { 0x5B, 0xE3 }, /* left GUI */
    { 0x5C, 0xE7 }, /* right GUI */
    { 0x5D, 0x65 }, /* Application */
    { 0x5E, 0x66 }, /* Power */
};

#define NE0 (sizeof(e0) / sizeof(e0[0]))

oa_u16 oi_set1_to_usage(oa_u8 code, int prefix)
{
    unsigned i;

    code &= 0x7Fu;
    if (!prefix) {
        return set1[code];
    }
    for (i = 0; i < NE0; i++) {
        if (e0[i][0] == code) {
            return e0[i][1];
        }
    }
    return 0; /* E0 2A and the like: fake shifts */
}

oa_u16 oi_usage_to_set1(oa_u16 usage)
{
    unsigned i;

    if (usage == 0u) {
        return 0;
    }
    /* E0 first: Print Screen is E0 37 (54h is Alt+SysRq), Pause gives
     * E0 46 (Ctrl+Break; the real Pause sequence starts with E1) */
    for (i = 0; i < NE0; i++) {
        if (e0[i][1] == usage) {
            return (oa_u16)(0x100u | e0[i][0]);
        }
    }
    for (i = 0; i < 128u; i++) {
        if (set1[i] == usage) {
            return (oa_u16)i;
        }
    }
    return 0;
}

/* Linux key codes above 88 */
static const oa_u16 evdev[][2] = {
    { 89, 0x87 },  /* KEY_RO */
    { 92, 0x8A },  /* KEY_HENKAN */
    { 93, 0x88 },  /* KEY_KATAKANAHIRAGANA */
    { 94, 0x8B },  /* KEY_MUHENKAN */
    { 96, 0x58 },  /* KEY_KPENTER */
    { 97, 0xE4 },  /* KEY_RIGHTCTRL */
    { 98, 0x54 },  /* KEY_KPSLASH */
    { 99, 0x46 },  /* KEY_SYSRQ: Print Screen */
    { 100, 0xE6 }, /* KEY_RIGHTALT */
    { 102, 0x4A }, /* KEY_HOME */
    { 103, 0x52 }, /* KEY_UP */
    { 104, 0x4B }, /* KEY_PAGEUP */
    { 105, 0x50 }, /* KEY_LEFT */
    { 106, 0x4F }, /* KEY_RIGHT */
    { 107, 0x4D }, /* KEY_END */
    { 108, 0x51 }, /* KEY_DOWN */
    { 109, 0x4E }, /* KEY_PAGEDOWN */
    { 110, 0x49 }, /* KEY_INSERT */
    { 111, 0x4C }, /* KEY_DELETE */
    { 113, 0x7F }, /* KEY_MUTE */
    { 114, 0x81 }, /* KEY_VOLUMEDOWN */
    { 115, 0x80 }, /* KEY_VOLUMEUP */
    { 116, 0x66 }, /* KEY_POWER */
    { 117, 0x67 }, /* KEY_KPEQUAL */
    { 119, 0x48 }, /* KEY_PAUSE */
    { 121, 0x85 }, /* KEY_KPCOMMA */
    { 124, 0x89 }, /* KEY_YEN */
    { 125, 0xE3 }, /* KEY_LEFTMETA */
    { 126, 0xE7 }, /* KEY_RIGHTMETA */
    { 127, 0x65 }, /* KEY_COMPOSE: Application */
};

oa_u16 oi_evdev_to_usage(oa_u16 code)
{
    unsigned i;

    if (code >= 183u && code <= 194u) {
        return (oa_u16)(0x68u + (code - 183u)); /* F13-F24 */
    }
    if (code <= 88u) {
        return set1[code];
    }
    for (i = 0; i < sizeof(evdev) / sizeof(evdev[0]); i++) {
        if (evdev[i][0] == code) {
            return evdev[i][1];
        }
    }
    return 0;
}
