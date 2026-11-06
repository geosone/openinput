# openinput

openinput turns keyboards and mice into one stream of events in HID usages, whatever the device is. It is C90, polled, and runs on the host services of [OpenASPI](https://github.com/geosone/OpenASPI)'s native layer (`openaspi/native/host.h`) and the USB stack [eusbcore](https://github.com/geosone/eusbcore). It is written for the GDOS kernel of [GonWare](https://gitea.disconnected-by-peer.at/GonSoft/GonWare).

## Events

Every event is `{page, usage, value, source}` after the USB-IF HID Usage Tables:

| Page | Usages | Value |
|---|---|---|
| 07h keyboard | key codes, 04h = A … E0h–E7h modifiers | 1 pressed, 0 released |
| 09h button | 1 left, 2 right, 3 middle | 1 pressed, 0 released |
| 01h generic desktop | 30h X, 31h Y, 38h wheel | relative: X right, Y down, wheel away from the user positive |

Layouts, key repeat, lock keys and the pointer are the embedding system's business; it sets the keyboard LEDs with `oi_set_leds()`.

## Sources

| File | |
|---|---|
| `src/usbhid.c` | USB keyboards and mice in the boot protocol (HID class, subclass 1) on eusbcore's interrupt pipes; keyboard LEDs through SET_REPORT |
| `src/ps2.c` | PS/2 keyboard and mouse on the 8042 (x86, port I/O): the keyboard in set 2 with the controller translating to set 1, the mouse in stream mode with the IntelliMouse wheel |
| `src/tables.c` | scancode set 1 ↔ keyboard usage, after Microsoft's Keyboard Scan Code Specification; the reverse direction is for systems that hand set 1 codes on (DOS, INT 9) |
| `src/event.c` | sources and the event queue |

Planned: serial mice (Microsoft, Mouse Systems, Logitech), virtio-input (QEMU on ARM64, evdev codes), report descriptor parsing beyond the boot protocol; later a DOS build with game port joysticks (201h) and USB gamepads.

## Use

```c
oi_ps2_init();              /* x86 */
usb_add(...); usb_enumerate();
oi_usb_attach();            /* HID devices among eusbcore's */
for (;;) {
    struct oi_event e;
    oi_poll();
    while (oi_read(&e))
        handle(&e);
}
```

CMake builds it as `OpenInput::input` on top of `EUSBCORE::core` and `OpenASPI::host`. A parent project's targets take precedence; otherwise the submodule `third_party/eusbcore` (or `../eusbcore`) and `OPENASPI_SOURCE_DIR` (or `../OpenAspi`) are used:

```sh
git submodule update --init --depth 1
cmake -S . -B build -G Ninja && ninja -C build
```

Tested in QEMU through GDOS: PS/2 keyboard and mouse; USB keyboard and mouse on xHCI, EHCI, OHCI and UHCI, behind a hub and behind an EHCI's UHCI companion, on x86-64, i686 and AArch64.

## License

GPL version 2 or later.
