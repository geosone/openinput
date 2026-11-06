/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: keyboards, mice and (later) game controllers as one stream of
 * events in HID usages (USB-IF HID Usage Tables), whatever the device:
 * USB HID (on eusbcore), PS/2 (8042), later serial mice and virtio-input.
 * Polled, C90, on OpenASPI's native host services. Layouts, key repeat and
 * the pointer are the embedding system's business. */

#ifndef OPENINPUT_INPUT_H
#define OPENINPUT_INPUT_H

#include "openaspi/types.h"

/* Usage pages */
#define OI_PAGE_DESKTOP  0x01u /* X 30h, Y 31h, wheel 38h */
#define OI_PAGE_KEYBOARD 0x07u
#define OI_PAGE_BUTTON   0x09u /* 1 left, 2 right, 3 middle */
#define OI_PAGE_CONSUMER 0x0Cu

#define OI_DESKTOP_X     0x30u
#define OI_DESKTOP_Y     0x31u
#define OI_DESKTOP_WHEEL 0x38u

/* Keyboard page, the usages the embedding system is most likely to need. */
#define OI_KEY_A          0x04u /* ... Z 1Dh */
#define OI_KEY_1          0x1Eu /* ... 9 26h, 0 27h */
#define OI_KEY_ENTER      0x28u
#define OI_KEY_ESCAPE     0x29u
#define OI_KEY_BACKSPACE  0x2Au
#define OI_KEY_TAB        0x2Bu
#define OI_KEY_SPACE      0x2Cu
#define OI_KEY_CAPS_LOCK  0x39u
#define OI_KEY_F1         0x3Au /* ... F12 45h */
#define OI_KEY_SCROLL_LOCK 0x47u
#define OI_KEY_NUM_LOCK   0x53u
#define OI_KEY_NON_US_BACKSLASH 0x64u /* ISO key left of Z */
#define OI_KEY_LEFT_CTRL  0xE0u
#define OI_KEY_LEFT_SHIFT 0xE1u
#define OI_KEY_LEFT_ALT   0xE2u
#define OI_KEY_LEFT_GUI   0xE3u
#define OI_KEY_RIGHT_CTRL 0xE4u
#define OI_KEY_RIGHT_SHIFT 0xE5u
#define OI_KEY_RIGHT_ALT  0xE6u /* AltGr */
#define OI_KEY_RIGHT_GUI  0xE7u

/* One event. Keys and buttons: value 1 pressed, 0 released. Axes: relative
 * movement (X right, Y down, wheel away from the user positive), or with
 * OI_EV_ABSOLUTE a position from 0 to OI_ABS_MAX (tablets, the pointer of
 * a virtual machine). */
struct oi_event {
    oa_u16 page;
    oa_u16 usage;
    oa_s32 value;
    oa_u8 source;  /* index of the source, oi_source_name() */
    oa_u8 flags;   /* OI_EV_* */
};

#define OI_EV_ABSOLUTE 0x01u
#define OI_ABS_MAX     32767

/* Keyboard LEDs, as the bits of the HID LED report. */
#define OI_LED_NUM    0x01u
#define OI_LED_CAPS   0x02u
#define OI_LED_SCROLL 0x04u

/* Find the sources: PS/2 keyboard and mouse on the 8042 (x86, port I/O),
 * USB boot protocol keyboards and mice among the devices eusbcore
 * enumerated. Each returns the number of sources it added. */
int oi_ps2_init(void);
int oi_usb_attach(void);
/* A virtio input device (keyboard, mouse, tablet: PCI 1AF4h:1052h) at PCI
 * address `pci`; 1 when it was added. */
int oi_virtio_add(oa_u32 pci);
#define OI_VIRTIO_VENDOR 0x1AF4u
#define OI_VIRTIO_INPUT  0x1052u
/* A serial mouse on the 16550 UART at I/O port `port` (COM2: 2F8h):
 * Microsoft, Logitech (3 buttons) and IntelliMouse (wheel) are recognised
 * by their ID; OI_SERIAL_MOUSE_SYSTEMS forces the Mouse Systems protocol,
 * which has none. 1 when a mouse was found. */
#define OI_SERIAL_AUTO          0
#define OI_SERIAL_MOUSE_SYSTEMS 1
int oi_serial_mouse_init(oa_u16 port, int protocol);

/* Poll every source; then take the queued events one by one (1, or 0 when
 * the queue is empty). */
void oi_poll(void);
int oi_read(struct oi_event *ev);

/* Set the LEDs of every keyboard. */
void oi_set_leds(unsigned leds);

unsigned oi_source_count(void);
const char *oi_source_name(unsigned source);

/* Scancode set 1 to keyboard usage (0: none). `e0`: after an E0h prefix.
 * And back, for systems that hand set 1 codes on (DOS, INT 9): the code,
 * with bit 8 set when it needs E0h, 0 when there is none. */
oa_u16 oi_set1_to_usage(oa_u8 code, int e0);
oa_u16 oi_usage_to_set1(oa_u16 usage);
/* Linux input event key code (virtio-input) to keyboard usage (0: none). */
oa_u16 oi_evdev_to_usage(oa_u16 code);

/* --- for the source drivers ---------------------------------------- */

struct oi_source {
    const char *name;
    void (*poll)(struct oi_source *src);
    void (*set_leds)(struct oi_source *src, unsigned leds); /* NULL: no LEDs */
    void *priv;
    oa_u8 index;
};

/* Register a source (0, or -1 when the table is full). */
int oi_register(struct oi_source *src, const char *name);
/* Queue an event from `src`; oi_emit_abs for an absolute position. */
void oi_emit(struct oi_source *src, oa_u16 page, oa_u16 usage, oa_s32 value);
void oi_emit_abs(struct oi_source *src, oa_u16 page, oa_u16 usage, oa_s32 value);

#endif
