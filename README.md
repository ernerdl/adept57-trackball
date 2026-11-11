# adept57-trackball

A 3D-printed trackball for a **57.15 mm ball** (a standard No. 57 pool ball), built
**without a custom PCB and without soldering a sensor**: a stock gaming-mouse
board provides the optical sensor, and an **RP2040-Zero** sits between it and the
computer as a USB bridge.

The case is a derivative of the [Ploopy Adept Trackball](https://github.com/ploopyco/adept-trackball),
scaled up 1.27× and reworked for this build. All credit for the original case
design goes to Ploopy — see [hardware/NOTICE.md](hardware/NOTICE.md).

> Türkçe özet aşağıda.

## How it works

```
 ball ─► Logitech G403 board (PMW3366) ──USB──► RP2040-Zero ──USB──► computer
                                                   ▲
                               6 buttons (GPIO) ───┘
```

The RP2040 runs two USB stacks at once:

- **USB host** (bit-banged with [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), on core 1)
  reads the reports coming from the mouse board;
- **USB device** (native port) presents itself to the computer as a single mouse
  ("Trackball Bridge").

Movement from the mouse board is merged with six extra buttons wired to GPIO
pins, then sent on as one HID report. The computer never sees the mouse board
directly.

## Bill of materials

| Part | Qty | Notes |
|------|-----|-------|
| Waveshare RP2040-Zero | 1 | USB bridge |
| Logitech G403 (wired) main board | 1 | PMW3366 sensor; the stock USB cable is cut and soldered to the Zero |
| Omron D2FC-F-7N switch | 6 | Buttons |
| MR63ZZ bearing (3×6×2.5 mm) | 3 | Ball supports |
| No. 57 pool ball, 57.15 mm | 1 | Sanded matte, see *Ball care* |
| M3×12 self-tapping screw | 4 | Top shell ↔ bottom shell |
| Printed dowels Ø3.05×9.8 mm | 3 | Bearing axles (`hardware/stl/bearing-dowel-3.05x9.8-x4.stl`) |
| Thin stranded wire (AWG 28–30) | — | Buttons and USB pigtail |

## Wiring

| Signal | RP2040-Zero pin | Notes |
|--------|-----------------|-------|
| Mouse board 5 V (red) | 5V | |
| Mouse board GND (black) + cable shield | GND | |
| Mouse board D+ (green) | **GP28** | D+ and D− must be **adjacent** pins (PIO-USB requirement) |
| Mouse board D− (white) | **GP29** | Keep the pair twisted and short (≤10 cm), soldered — no clips |
| Big button, left | GP26 | Each switch: NO → GPIO, COM → common GND |
| Big button, right | GP6 | |
| Top row, far right | GP10 | |
| Top row, middle right | GP7 | |
| Top row, middle left | GP13 | |
| Top row, far left | GP14 | |

Buttons are active-low with the internal pull-ups enabled. Daisy-chain all six
COM pins and run a single wire to GND.

## Firmware

Features:

- Buttons remappable **live** from a browser page (`firmware/tools/ayar.html`,
  Chrome / WebHID), saved to flash; settings survive firmware updates.
- Actions: left/right/middle click, back/forward, **drag-scroll** (hold or toggle,
  vertical + horizontal with an axis "rail"), **drag lock**, an **Fn layer**,
  **two-button chords**, pointer speed, keyboard shortcuts and media keys.
- **Buttonless updates** (`firmware/tools/flash.sh`): a HID feature report puts
  the board into the bootloader, so the case never has to be opened.
- Robustness: hardware watchdog, armed **before clock setup** so a boot that
  stalls (crystal/PLL, flash, core 1) recovers by itself instead of staying
  dead until replugged; a supervisor that re-initialises the USB host if the
  mouse board does not enumerate; the host is started only after the mouse
  board pulls D+ high (avoids a power-on race).
- **On-device event log** kept in RAM that survives resets (boot reason, host
  connect/disconnect, lock changes), read with `firmware/tools/gunluk.sh`.

### Build

Requirements: [pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.1.x with
TinyUSB, Pico-PIO-USB cloned into
`pico-sdk/lib/tinyusb/hw/mcu/raspberry_pi/Pico-PIO-USB`, and the Arm GNU
toolchain (tested with 14.2).

```bash
cd firmware
cmake -B build -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_TOOLCHAIN_PATH=/path/to/arm-toolchain
cmake --build build -j8
```

### Flash

- **First time:** hold BOOTSEL while plugging in the RP2040-Zero, then copy
  `build/trackball_bridge.uf2` to the `RPI-RP2` drive.
- **Afterwards (macOS):** `firmware/tools/flash.sh` — no buttons needed.

The settings page works in Chrome on any OS. `flash.sh` and `gunluk.sh` are
macOS scripts (Swift + IOKit).

`firmware/tools/usb-kaydedici.sh` is an optional macOS helper that appends every
USB connect/disconnect the kernel reports to `~/Library/Logs/usb-olaylari.log`
(run it from a LaunchAgent). Useful when chasing intermittent dropouts, because
macOS keeps its own log for only about two days.

> **USB IDs:** the firmware uses the Raspberry Pi vendor ID (0x2E8A) with
> product ID 0x107B for personal use. If you distribute devices, request your
> own product ID.

## Printing

- **Top shell** (`hardware/stl/top-shell-57.stl`): print **upside down** (top
  face on the bed). This orientation needs no supports and keeps the button
  surfaces clean — printing it right side up puts support marks exactly on the
  button pads.
- **Bottom shell** (`hardware/stl/bottom-shell-57.stl`): print flat, no supports.
- 0.2 mm layers recommended for the bottom shell (switch seats differ by ~0.26 mm).

## Assembly notes

- Mount the G403 board so the lens sits about **1 mm below the ball**.
  Touching the ball stops tracking.
- Solder the switch wires before placing the switches, trim the legs, and route
  the wires through the channels in the bottom shell.
- Bearings: press the MR63ZZ onto the printed dowels and drop them into the
  three pockets. A flicked ball should spin freely for a few turns.

## Ball care

The ball is sanded matte so the sensor can track it. A matte surface collects
skin oil, which leaves shiny patches where the cursor skips. **Wipe the ball with
isopropyl alcohol** every few weeks, and blow dust off the sensor lens.

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Pointer skips on some parts of the ball | Dirty ball or dust on the lens — clean with IPA |
| Buttons work but the ball doesn't | Mouse board did not enumerate; the supervisor retries automatically. Check the D+/D− solder joints if it persists |
| Nothing works, device shows up but has no HID interface | Firmware hang; the watchdog recovers within 2 s |
| Device vanishes from USB after a burst of resets | A boot that stalled before the watchdog was armed; fixed by the early watchdog (log event 14 records each rescue) |
| Pointer grows huge and stutters (macOS) | macOS "Shake mouse pointer to locate" — turn it off in Accessibility |

Run `firmware/tools/gunluk.sh` to see the device's own event log.

---

## Türkçe özet

57,15 mm'lik bilardo topu (No:57) kullanan, 3D baskı bir trackball. **Özel PCB
yok, sensör lehimi yok:** Logitech G403 mouse'un kartı sensör olarak
kullanılıyor, araya giren **RP2040-Zero** hem mouse kartını USB host olarak
dinliyor hem de bilgisayara tek bir mouse olarak görünüyor. Altı ek buton
doğrudan Zero'nun GPIO pinlerine bağlı.

- **Kasa:** [Ploopy Adept](https://github.com/ploopyco/adept-trackball)
  tasarımından türetildi (1,27× büyütme, MR63ZZ rulman yuvaları, switch cepleri,
  Zero yatağı, kablo kanalları). Lisansı **CERN-OHL-S-2.0**.
- **Firmware:** sıfırdan yazıldı, Ploopy kodu içermez. Lisansı **GPLv3**.
- Tuşlar Chrome'da açılan ayar sayfasından canlı olarak atanır, ayarlar
  kalıcıdır. Güncelleme kasayı açmadan `flash.sh` ile yapılır.
- Bağlantı şeması, malzeme listesi ve baskı notları yukarıda (İngilizce).
- Atlama olursa topu izopropil alkolle sil, sensörün tozunu üfle.
