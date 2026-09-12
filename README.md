<div align="center">

<picture>
  <source srcset="assets/printdrop_banner_dark.webp" media="(prefers-color-scheme: dark)">
  <img src="assets/printdrop_banner.webp" alt="PrintDrop" width="620">
</picture>

**A Wi-Fi flash drive for 3D printers.**
Drop a print job from your desk instead of walking a USB stick to the machine.

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D?logo=arduino&logoColor=white)](https://github.com/espressif/arduino-esp32)
[![Build](https://img.shields.io/badge/build-PlatformIO-FF7F00?logo=platformio&logoColor=white)](https://platformio.org/)
[![USB](https://img.shields.io/badge/USB-TinyUSB%20MSC-336791)](https://github.com/hathach/tinyusb)
[![UI](https://img.shields.io/badge/UI-LittleFS%20hosted-4B32C3)](data/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-working%20on%20hardware-brightgreen)](docs/)
[![Issues](https://img.shields.io/github/issues/Akash97p/PrintDrop)](https://github.com/Akash97p/PrintDrop/issues)
[![Last commit](https://img.shields.io/github/last-commit/Akash97p/PrintDrop)](https://github.com/Akash97p/PrintDrop/commits)
[![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-db61a2?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/Akash97p)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/akash97p)

</div>

---

PrintDrop plugs into a 3D printer's USB port and appears as an ordinary USB
flash drive — no printer firmware changes, no cloud service, no vendor account.
At the same time it joins the local network and serves a web UI, so anyone can
drag a `.gcode` file onto it from their desk and then walk over and press print.

It is a **flash drive that happens to be reachable over Wi-Fi**. The printer
sees nothing unusual: a USB mass storage device with a FAT32 volume. An SD card
is simply the storage medium behind it.

Built for printers that only accept USB media and have no networking of their
own.

Working on hardware: the card enumerates as a USB drive, the web UI serves from
LittleFS, and files uploaded over Wi-Fi appear to the USB host without a
reboot.

All figures below are **measured on hardware** — an ESP32-S3-DevKitC-1 with a
32 GB SDHC card on a 3.3 V-native breakout, SDIO 4-bit at 40 MHz.

| Measurement | Value | Notes |
|---|---|---|
| Read (USB, uncached) — SDIO 4-bit | **1 016 KB/s** | at the USB Full-Speed ceiling |
| Write (USB) — SDIO 4-bit | **~535 KB/s** | |
| Read (USB, uncached) — SPI | 485 KB/s | 4-wire SPI @ 20 MHz, for comparison |
| Write (USB) — SPI | 248 KB/s | |
| Upload over Wi-Fi (web UI) | ~200 KB/s | bounded by the HTTP path, not the card |
| Download over Wi-Fi (web UI) | ~500 KB/s | same |
| Raw SD — SDIO 4-bit @ 40 MHz | ~16 000 KB/s | 32 KB per command (~1 600 KB/s at one command per sector) |
| Raw SD — SPI @ 20 MHz | ~910 KB/s | |
| USB link | Full Speed, 12 Mbit/s | **~1.2 MB/s hard ceiling** — the ESP32-S3 has no High-Speed PHY |
| SD clock — SDIO | 40 MHz, 4-bit | only 40 and 20 MHz are usable, see [hardware.md](docs/hardware.md#sdio-clocks) |
| SD clock — SPI | 20 MHz (verified clean to 25 MHz) | legacy bus |
| Web UI size | 62 KB, served from LittleFS | |
| Card tested | 32 GB SDHC, FAT32 | |

**What actually bounds this.** The card is not the bottleneck on any path — it
has roughly 16 MB/s available and nothing uses more than a fraction of it.
USB transfers are bounded by the ESP32-S3's **USB Full-Speed** peripheral
(12 Mbit/s ≈ 1.2 MB/s); reads already sit at that ceiling, so faster USB needs
different silicon, not firmware. The Wi-Fi figures are bounded by the HTTP
upload/download path in software. Earlier versions of this table carried
projected SDIO figures of ~3 200 KB/s read and ~2 000 KB/s write; those were
never achievable over USB Full-Speed and have been replaced with measurements.

## How the USB and Wi-Fi sides coexist

This is the part that matters, and the part that is easy to get wrong.

A USB host caches the FAT. If the ESP32 writes to the card while the printer
has it mounted, the host's cached allocation table goes stale and the next
write from either side corrupts the filesystem. The reverse is also true — the
ESP32's own FATFS cache goes stale if the host writes sectors underneath it.

`src/printdrop/storage.cpp` enforces four rules:

1. **The host cannot write.** The card is offered to USB as write-protected
   (`USB_READ_ONLY`, on by default). A host that cannot write cannot cache dirty
   filesystem metadata, and so cannot flush a stale copy back over an upload.
   The cost: files can no longer be dragged onto the card over USB. The web UI
   is the only writer, which for a print-drop box is the intended flow anyway.
2. **One mutex.** Every SD access, from either side, is serialised.
3. **Withdraw before writing.** Before the ESP32 modifies the card, the media is
   withdrawn from the USB host, with UNIT ATTENTION raised so the host is told
   the medium may have changed, then re-presented afterwards. The printer sees a
   card removal and re-reads its file list, so uploads appear without a reboot.
4. **Remount when the host writes.** MSC write callbacks set a flag; the ESP32
   remounts FATFS before trusting its own view of the filesystem again. This
   only matters with rule 1 turned off.

MSC callbacks take the mutex with a short timeout and fail the transfer rather
than stall the USB task, so a slow Wi-Fi upload can never hang the printer.

### Why rule 1 exists

Rules 2 to 4 are not enough on their own, and that is measurable rather than
theoretical. USB mass storage gives the device no way to invalidate a cache the
host has already taken; withdrawing the medium is a hint a host may ignore, and
macOS ignores it. With the volume mounted read-write, a file uploaded over Wi-Fi
stayed invisible for as long as it stayed mounted, and the host's stale
allocation table was later written back over it — the file was gone from the
card and `fsck` found 51 orphaned clusters. Another upload read back at the
right size with different contents, because the host had written into the
clusters the device had allocated. Full evidence in
[`docs/bugs.md`](docs/bugs.md).

Write protection removes that rather than narrowing it. **Setting
`USB_READ_ONLY=0` puts you back in the state described above**, which is only
reasonable if nothing will write from the host side while PrintDrop is running.

The full design is in [`docs/architecture.md`](docs/architecture.md).

## Hardware

- ESP32-S3-DevKitC-1 (configured for a **4 MB flash, no PSRAM** board)
- microSD breakout — **SDIO 4-bit** (recommended, 6 wires), **SPI** (legacy, 4 wires)
- A **FAT32** card — exFAT will not mount
- Printer on the native USB port; the UART bridge carries the console

### Wiring

**SDIO 4-bit (recommended, 6 wires):**

| Signal | GPIO | Notes |
|--------|------|-------|
| CLK    | 40   | |
| CMD    | 14   | 10 k pull-up |
| D0     | 39   | 10 k pull-up |
| D1     | 12   | 10 k pull-up |
| D2     | 13   | 10 k pull-up |
| D3     | 15   | 10 k pull-up |

Reuses the four SPI pins plus two new data lines — an SPI-wired board migrates with two jumpers. Requires a **3.3 V-native microSD breakout** (no AMS1117/LC125) with pull-ups on CMD/D0-D3. Jumpers at 20 MHz are stable; 40 MHz wants short, equal-length wires. 1-bit mode (`SDMMC_WIDTH=1`) works with only CLK/CMD/D0 for bring-up.

**SPI — `main` legacy (4 wires):**

| Signal | GPIO |
|--------|------|
| CS     | 12   |
| MISO   | 39   |
| MOSI   | 14   |
| CLK    | 40   |

Build with `pio run -e printdrop_spi` to stay on SPI. See [`docs/hardware.md`](docs/hardware.md) for both wirings, power notes, and measured clocks.

> **Power the breakout from 3V3.** AMS1117/LC125 modules also run on 5 V, but their pull-ups then reference the 5 V rail and drive the ESP32's GPIOs above 3.3 V, outside the SoC's absolute maximum. SDIO must use a 3.3 V-native breakout.

## Flashing

```bash
pio run -e printdrop -t upload      # firmware
pio run -e printdrop -t uploadfs    # web UI into LittleFS
```

Both are needed on a first install. See [Reflashing](#reflashing) for how to get
the board into download mode on this hardware.

## First run

With no credentials stored, PrintDrop raises its own access point:

| | |
|---|---|
| Network | `PrintDrop-Setup` |
| Password | `printdrop` |
| Setup page | `http://192.168.4.1` |

Join it, open the page, and set the Wi-Fi network and hostname under
**Settings**. The device reboots and reappears on the office network.

Credentials are stored in NVS and never enter the source tree.

### Provisioning over serial instead

Faster on a bench, and the only option if the AP is inconvenient:

```
wifi <ssid> <password>     join a network and reboot
hostname <name>            set the mDNS name and reboot
status                     show device state
forget                     clear Wi-Fi settings
help                       list commands
```

Send these as lines to the UART bridge (`COM5`) at 115200 baud.

## Finding it on the network

PrintDrop advertises itself over mDNS, so `http://printdrop.local` works on most
machines out of the box.

For a fixed address, either set a static IP under **Settings → Network**, or
reserve one on the DHCP server and point a DNS entry at it. With AdGuard Home,
add a DNS rewrite from e.g. `printdrop.office.lan` to the reserved address.

## Using it

- **Upload** — drag files anywhere onto the page, or use *Choose files*.
  Progress is shown per file. A 20 MB job takes roughly **100 s** over Wi-Fi at
  the current ~200 KB/s; copying the same file over USB takes ~37 s, and the
  printer reads it back at ~1 016 KB/s. The limit is the HTTP path in firmware,
  not the card and not the network.
  (The per-file rate and ETA in the UI are currently unreliable — they time only
  the card write, not the transfer. See [bugs.md](docs/bugs.md).)
- **Browse** — grid or list view, sorted by name or size, with folder
  navigation. Print jobs get their own icon colour so they stand out.
- **Eject / refresh printer view** — forces the printer to re-read the card.
  Uploads do this automatically; the button is for when a printer needs nudging.

## The experience

| Feature | What it does | See |
|---|---|---|
| **WebSocket progress** | `ws://<host>:81/` pushes upload progress & status (polling stays as fallback) | `src/printdrop/ws.*`, `data/app.js` |
| **LED + button** | LED idle 2 s blink / activity fast blink / error double-blink; button short = eject, long 5 s = factory reset (clears Wi-Fi + login) | `config.h:84`, `docs/hardware.md` |
| **Discovery** | mDNS `http://printdrop.local` + LLMNR `http://printdrop` (Windows bare name) | `docs/discovery.md` |
| **Web UI auth** | HTTP Basic, SHA-256 in NVS, seed from `platformio.ini` `PRINTDROP_AUTH_*`, set via serial `auth` or web `Settings` | `docs/auth.md` |
| **OTA** | `POST /api/ota` (bin upload) + SD `firmware.bin`+`firmware.json` popup, dual OTA slots on 4 MB | `docs/ota.md`, `partitions_printdrop_ota.csv` |

LED `GPIO 38` active-high, button `GPIO 4` active-low with pull-up by default — override with `-D PRINTDROP_LED_PIN` etc. `src/printdrop/config.h:84`/`platformio.ini:150`.

## Build environments

| Env | Purpose | Console |
|---|---|---|
| `printdrop` | The product — **SDIO 4-bit**, USB drive plus Wi-Fi web UI | UART0 (`COM5`) |
| `printdrop_spi` | The product — **SPI legacy** (4-wire) | UART0 (`COM5`) |
| `msc` | USB mass storage only, no networking | UART0 (`COM5`) |
| `ramdisk` | RAM-backed FAT12 volume; proves USB MSC without the SD card | UART0 (`COM5`) |
| `diag` | SPI speed sweep, card geometry, MBR dump, root listing | USB/JTAG (`COM11`) |
| `diag_sdio` | **SDIO 4-bit bring-up** — bus width test, throughput sweep | USB/JTAG (`COM11`) |
| `scan` | Pin health, line voltages, pin-permutation sweep | USB/JTAG (`COM11`) |

Troubleshooting order: `scan` when the card is not detected, `diag` when it
mounts but misbehaves, `ramdisk` to prove the USB path independently.

## Reflashing

Firmware that owns the native USB port replaces the USB-Serial-JTAG device, so
`esptool` cannot reach the board there. Every firmware watches UART0 for the
word `BOOTLOADER` and reboots into download mode:

```powershell
"BOOTLOADER" | Out-File -Encoding ascii COM5
```

On this board **RTS drives EN**, so opening or closing `COM5` resets the chip
and knocks it straight back out of download mode. Any tool used between the
hatch and the flash must hold RTS and DTR deasserted.

Manual fallback: hold **BOOT**, tap **RESET**, release **BOOT**.

## Layout

```
src/printdrop/    the product: storage arbitration, networking, HTTP API
src/diag/         SD and USB diagnostics
src/legacy/       USB mass storage only
src/common/       shared helpers
data/             web UI, flashed to LittleFS
docs/             architecture, hardware, bugs, flashing notes
assets/           branding
website/          GitHub Pages source (Next.js, statically exported)
```

## Partitions

4 MB flash, single app image — there is no room for two OTA slots.

| Partition | Offset | Size |
|---|---|---|
| `nvs` | `0x9000` | 20 KB |
| `app0` | `0x10000` | 2688 KB |
| `spiffs` (LittleFS) | `0x2B0000` | 1280 KB |
| `coredump` | `0x3F0000` | 64 KB |

## Gotchas

Three details stop the USB drive appearing at all, and none produce a useful
error message:

- **`ARDUINO_USB_MODE` must be `0`.** The stock board definition hard-codes `1`,
  which compiles `USBMSC` out entirely. Override through
  `board_build.extra_flags` — appending to `build_flags` only yields a macro
  redefinition.
- **`ARDUINO_USB_CDC_ON_BOOT` must be `0`.** It implies `ARDUINO_USB_ON_BOOT`,
  which makes the core call `USB.begin()` in `app_main()` *before* `setup()` —
  freezing the USB descriptor before the sketch can add its MSC interface.
- **SD cards must be identified at ≤400 kHz** before the clock is raised.

These and the rest of the investigation are written up in [`docs/bugs.md`](docs/bugs.md).

## Documentation

| | |
|---|---|
| [docs/architecture.md](docs/architecture.md) | How the pieces fit: USB/Wi-Fi arbitration, module layout, partitions, performance |
| [docs/hardware.md](docs/hardware.md) | The board as measured, wiring, power requirements, verified SPI clocks, LED/button |
| [docs/bugs.md](docs/bugs.md) | Every fault found during the port and its root cause |
| [docs/flashing.md](docs/flashing.md) | Getting this board into download mode |
| [docs/sdio.md](docs/sdio.md) | SDIO 4-bit migration (feat/sdio) |
| [docs/auth.md](docs/auth.md) | Web UI login, NVS, Basic auth |
| [docs/discovery.md](docs/discovery.md) | mDNS + LLMNR (`printdrop.local` / `printdrop`) |
| [docs/ota.md](docs/ota.md) | Dual OTA slots, HTTP + SD-card update |

## Contributing

Build instructions, the branch model, and the hardware traps worth knowing about
are in [CONTRIBUTING.md](CONTRIBUTING.md). Bug reports are most useful with the
UART0 boot log and, for card problems, the output of the `diag` or `scan`
environment.

## Support

PrintDrop is free and MIT licensed. If it saved you a walk across the workshop,
you can [sponsor the project](https://github.com/sponsors/Akash97p) or
[buy me a coffee](https://buymeacoffee.com/akash97p).

<a href="https://buymeacoffee.com/akash97p" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="46" width="163"></a>

GitHub strips `<iframe>` from README markdown, so the sponsor card only renders
on the [project site](https://kabani-tech.github.io/PrintDrop/#support). Here it
is the badge above, plus the repository's own Sponsor button from
[`.github/FUNDING.yml`](.github/FUNDING.yml).

## Credits

Made by Akash P | CTO, [Kabani Tech Private Limited](https://kabanitech.com)

MIT licensed — see [LICENSE](LICENSE). The USB mass storage layer derives from
the `Esp32-USB-Stick` project for the M5Stack Cardputer.
