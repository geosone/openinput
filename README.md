# openinput

openinput turns keyboards and mice into one stream of events in HID usages, whatever the device is. It is C90, polled, and runs on the host services of [OpenASPI](https://github.com/geosone/OpenASPI)'s native layer (`openaspi/native/host.h`) and the USB stack [eusbcore](https://github.com/geosone/eusbcore). It is written for the GDOS kernel of [GonWare](https://gitea.disconnected-by-peer.at/GonSoft/GonWare).

## Events

Every event is `{page, usage, value, source}` after the USB-IF HID Usage Tables:

| Page | Usages | Value |
|---|---|---|
| 07h keyboard | key codes, 04h = A … E0h–E7h modifiers | 1 pressed, 0 released |
| 09h button | 1 left, 2 right, 3 middle … | 1 pressed, 0 released |
| 01h generic desktop | 30h X, 31h Y, 38h wheel | relative: X right, Y down, wheel away from the user positive; with `OI_EV_ABSOLUTE` a position 0–32767 (tablets, the pointer of a virtual machine) |
| 0Ch consumer | media keys | 1 pressed, 0 released |

Layouts, key repeat, lock keys and the pointer are the embedding system's business; it sets the keyboard LEDs with `oi_set_leds()`.

## Sources

| File | |
|---|---|
| `src/usbhid.c` | USB HID on eusbcore's interrupt pipes: the report protocol through the device's report descriptor when it describes keys, buttons or axes, otherwise keyboards and mice in the boot protocol; keyboard LEDs through SET_REPORT |
| `src/hidparse.c` | report descriptor parser and decoder: input fields on the keyboard, button, generic desktop and consumer pages, variable and array, relative and absolute, report IDs, push/pop |
| `src/ps2.c` | PS/2 keyboard and mouse on the 8042 (x86, port I/O): the keyboard in set 2 with the controller translating to set 1, the mouse in stream mode with the IntelliMouse wheel |
| `src/sermouse.c` | serial mice on a 16550 UART: Microsoft, Logitech (third button) and IntelliMouse (wheel) recognised by their ID; Mouse Systems on request |
| `src/virtio.c` | virtio input (virtio 1.0 over PCI): keyboard, mouse, tablet; LEDs on the status queue |
| `src/tables.c` | scancode set 1 ↔ keyboard usage, after Microsoft's Keyboard Scan Code Specification (the reverse direction for DOS, INT 9); Linux input key codes → usage for virtio |
| `src/event.c` | sources and the event queue |

Planned: a DOS build with game port joysticks (201h) and USB gamepads.

## Use

```c
oi_ps2_init();                          /* x86 */
oi_serial_mouse_init(0x2F8, OI_SERIAL_AUTO); /* COM2 */
oi_virtio_add(pci);                     /* each PCI 1AF4h:1052h */
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

Tested in QEMU through GDOS on x86-64, i686 and AArch64: USB keyboard and mouse (report protocol) on xHCI, EHCI, OHCI and UHCI, behind a hub and behind an EHCI's UHCI companion; the USB tablet; virtio keyboard, mouse and tablet; on x86 also the PS/2 keyboard and mouse and a serial mouse (Logitech, QEMU's msmouse). Untested: the boot protocol fallback (QEMU's devices all have report descriptors), Mouse Systems mice.

## License

GPL version 2 or later.
