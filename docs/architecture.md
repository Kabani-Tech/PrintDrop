# Architecture

PrintDrop is one device wearing two hats: a USB mass storage device for the
printer, and a Wi-Fi file server for everyone else. Both want the same SD card,
and that is the whole design problem.

## Module layout

```
src/printdrop/
  main.cpp        startup, serial console, button/LED
  config.h        pins, bus, auth, OTA, LED, timeouts
  storage.cpp     SD card and USB mass storage arbitration (SPI/SDIO)
  net.cpp         Wi-Fi provisioning, mDNS + LLMNR, static IP
  web.cpp         HTTP server + WebSocket (port 81) + JSON file API
  auth.cpp        SHA-256 Basic auth, NVS store
  ota.cpp         HTTP + SD-card OTA (Update)
  ws.cpp          WebSocket broadcaster (progress/status)
  led.cpp         heartbeat / activity LED
  button.cpp      eject + factory-reset button
src/diag/         SD and USB diagnostics
src/legacy/       USB mass storage only, no networking
src/common/       shared helpers (reflash hatch)
data/             web UI, flashed to LittleFS (now WS + auth + OTA)
```

## Sharing the card

A USB host caches the FAT. If the ESP32 writes to the card while the printer has
it mounted, the host's cached allocation table goes stale and the next write
from either side corrupts the filesystem. The reverse is also true — the ESP32's
own FATFS cache goes stale if the host writes sectors underneath it.

`storage.cpp` enforces three rules:

**1. One mutex.** Every SD access, from either side, is serialised. The MSC
read/write callbacks and the HTTP handlers contend for the same lock.

**2. Withdraw before writing.** Before the ESP32 modifies the card, the media is
withdrawn from the USB host (`msc.mediaPresent(false)`), and re-presented
afterwards. The host sees a card removal, drops its cache, and re-reads the FAT
— which is what makes an uploaded file appear in the printer's file list without
a reboot.

**3. Remount when the host writes.** MSC write callbacks set a flag. Before the
ESP32 next trusts its own view of the filesystem, it tears down and remounts
FATFS.

Any code path that touches the card goes through `storage::Guard`:

```cpp
storage::Guard g(/*forWrite=*/true);
if (!g.ok()) return sendError(503, "Card busy");
// ... card is exclusively ours, and the host cannot see it ...
```

### Not stalling the printer

MSC callbacks run on the TinyUSB task. They take the mutex with a **2 second
timeout and fail the transfer** rather than block indefinitely:

```cpp
if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(kMscLockTimeoutMs)) != pdTRUE) return -1;
```

This matters: a 20 MB upload holds the write lock for around 80 seconds. Without
the timeout, a printer mid-job would block on a read for the whole upload. With
it, the host sees the media as absent (because the write path withdrew it) and
does not issue reads at all.

## Networking

Credentials live in NVS via `Preferences`, never in the source tree. On boot:

1. No stored SSID, or the stored network cannot be joined within 20 s →
   raise a WPA2 access point (`PrintDrop-Setup`) serving the same UI.
2. Otherwise join as a station, optionally with a static IP.

Either way mDNS (`http://printdrop.local`) **and** LLMNR (`http://printdrop`, single-label for Windows) are started on the same hostname via a minimal UDP 5355 responder (`net.cpp:74`). See `docs/discovery.md`.

A serial console on UART0 (`help`, `status`, `wifi`, `hostname`, `auth`/`passwd`/`clear-auth`/`factory-reset`, `forget`, `reboot`) provides headless provisioning without putting secrets in the repo. `auth` and `ota` state are also in `status` (`auth {user}` `ota {version}` `discovery {mdns,llmnr}`).

## Web layer

A synchronous `WebServer` on port 80 + a `WebSocketsServer` on port 81. The UI is static files from LittleFS, pre-compressed copies preferred when present; everything else is a small JSON API (protected by Basic auth when `PRINTDROP_AUTH_REQUIRED=1`).

| Endpoint | Method | Auth | Notes |
|---|---|---|---|
| `/api/status` | GET | — | device, card, USB, network, `auth`, `discovery`, `ota`, `wsPort` |
| `/api/list` | GET | Basic | directory listing |
| `/api/upload` | POST | Basic | multipart; `?path=` + WS progress `ws://:81` |
| `/api/download` | GET | Basic | streams a file |
| `/api/delete`, `/api/mkdir`, `/api/rename` | POST | Basic | form-encoded |
| `/api/eject` | POST | Basic | withdraw and re-present the media |
| `/api/wifi/scan`, `/api/wifi` | GET / POST | Basic | provisioning |
| `/api/auth/status` | GET | — | `{required,user}` |
| `/api/auth/set` | POST | Basic | `user`+`pass` → SHA-256 NVS |
| `/api/ota/status` | GET | Basic | `{current,sdAvailable,sdVersion}` |
| `/api/ota` | POST | Basic | raw `.bin` → `Update` + reboot |
| `/api/ota/sd` | POST | Basic | flash `SD:/firmware.bin` per `firmware.json` |

WS on `:81` broadcasts `{"type":"progress",...}` and `{"type":"status",data:{...}}`. Auth is HTTP Basic, SHA-256 hex in NVS (`auth_user`/`auth_hash`), seeded from `platformio.ini` `PRINTDROP_AUTH_*` (`docs/auth.md`). LED idle/activity/error blink and button eject/factory-reset are handled in `main.cpp` + `led.cpp`/`button.cpp`.

Client-supplied paths go through `safePath()`, which rejects anything
containing `..` and normalises separators.

Uploads are driven by repeated callbacks, so the SD lock is acquired on
`UPLOAD_FILE_START` and released on `UPLOAD_FILE_END` or `UPLOAD_FILE_ABORTED`
rather than scoped to one function. An aborted upload deletes its partial file
— a truncated `.gcode` that looks like a valid print job is worse than no file.

Query-string arguments are parsed before the multipart body
(`WebServer::_parseRequest`), which is why `?path=` is readable inside the
upload callback.

## Build environments

| Env | Sources | USB mode | Purpose |
|---|---|---|---|
| `printdrop` | `src/printdrop/` | TinyUSB | The product — **SDIO 4-bit** (`feat/sdio` default) |
| `printdrop_spi` | `src/printdrop/` | TinyUSB | The product — **SPI legacy** (4-wire, no re-wire) |
| `msc` | `src/legacy/` | TinyUSB | USB mass storage only, no networking |
| `ramdisk` | `src/diag/msc_ramdisk.cpp` | TinyUSB | RAM-backed FAT12 volume; proves USB MSC without the SD card |
| `diag` | `src/diag/sd_diag.cpp` | Serial/JTAG | SPI speed sweep, geometry, MBR dump, root listing |
| `diag_sdio` | `src/diag/sd_diag.cpp` | Serial/JTAG | **SDIO 4-bit** bring-up, bus width + throughput sweep |
| `scan` | `src/diag/sd_scan.cpp` | Serial/JTAG | Pin health, line voltages, pin-permutation sweep |

The diagnostic environments deliberately keep `ARDUINO_USB_MODE=1` so the native
port stays a serial/JTAG device and the board remains trivially flashable while
hardware is being investigated.

`ramdisk` is the key isolation tool: if it produces a drive on the host, the USB
path is proven good and any remaining fault is on the SD side.

## Partitions

4 MB flash — dual OTA slots so HTTP/SD update does not brick the stick.

| Partition | Offset | Size | Holds |
|---|---|---|---|
| `nvs` | `0x9000` | 20 KB | Wi-Fi, hostname, `auth_*` |
| `otadata` | `0xE000` | 8 KB | OTA slot selection |
| `app0` | `0x10000` | 1344 KB | firmware ota_0 (~980 KB used) |
| `app1` | `0x160000` | 1344 KB | firmware ota_1 |
| `spiffs` (LittleFS) | `0x2B0000` | 1280 KB | web UI (~62 KB used) |
| `coredump` | `0x3F0000` | 64 KB | crash dumps |

`partitions_printdrop_ota.csv` (`platformio.ini:25` per-env) is the OTA layout. The `LittleFS` partition keeps label `spiffs` because that is what `LittleFS.begin()` looks for and what `uploadfs` targets. `printdrop_spi` retains the old single-app `partitions_printdrop.csv` for bring-up without OTA. Disable OTA on tight builds with `-D PRINTDROP_ENABLE_OTA=0` (`config.h:135`).

## Measured performance

All measured on an ESP32-S3-DevKitC-1 with a 32 GB SDHC card.

| Path | SDIO 4-bit @ 40 MHz | SPI @ 20 MHz | Bounded by |
|---|---|---|---|
| USB read (uncached) | **1 016 KB/s** | 485 KB/s | USB Full-Speed — at the ceiling |
| USB write | **~535 KB/s** | 248 KB/s | card program time + FAT metadata |
| Raw SD (32 KB per command) | **~16 000 KB/s** | ~910 KB/s | the card |
| Raw SD (one command per sector) | ~1 600 KB/s | — | per-command latency |
| Wi-Fi upload (web UI) | ~200 KB/s | ~200 KB/s | HTTP multipart path |
| Wi-Fi download (web UI) | ~500 KB/s | ~500 KB/s | serialised send loop |

**The card is not the bottleneck, and on SPI it never really was either.**
Moving from SPI to SDIO raised raw card throughput ~17× (910 → 16 000 KB/s) and
moved USB read by 3% (485 → 500 KB/s). The wall was always somewhere else. Two
walls, in fact:

* **USB is Full-Speed.** The ESP32-S3's USB OTG peripheral has no High-Speed
  PHY; the host negotiates 12 Mbit/s, so ~1.2 MB/s is the hard ceiling for
  anything crossing USB. Read now sits at 1 016 KB/s, i.e. essentially there.
  Going faster is a silicon change (the ESP32-P4 has a High-Speed PHY), not a
  firmware one.
* **The Wi-Fi paths are bounded in software**, not by the radio or the card:
  the synchronous `WebServer`'s multipart parser and its 1360-byte blocking
  send loop. SDIO does not touch either, which is why the web UI numbers did
  not move.

What *did* move the USB numbers was transfer size. `onRead`/`onWrite` used to
loop one single-sector SDMMC command per 512 bytes even though TinyUSB hands
over 4 KB at a time; serving the whole request with one command doubled both
directions (read 500 → 1 016 KB/s, write 258 → ~535 KB/s). Per-command latency,
not bandwidth, was the cost.

Pinning storage work to a dedicated core was measured and does **not** help:
throughput moves under 2% when the same core is saturated, and 74–89% of that
core is idle during SD I/O — the transfers are DMA/latency-bound, not
CPU-bound. See `pio run -e core_sdio` and
[hardware.md](hardware.md#sdio-clocks).

Verified end to end over USB: a 2 MB write survives a SHA-256 round trip
(`6EA73B45…C562` identical both sides), and the ESP32's own directory listing
matches what the host sees.
