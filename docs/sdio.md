# SDIO 4-bit

PrintDrop drives the card through the ESP32-S3's native **SDMMC host in 4-bit
SDIO mode**, rather than the legacy 4-wire **SPI** driver.

## Why

SPI caps at ~910 KB/s raw. SDIO 4-bit at 40 MHz measures **~16 000 KB/s** raw
on the same card and breakout — roughly 17× the bus bandwidth.

What that bought end-to-end is more modest, and the reason is worth stating
plainly: **the card was never the bottleneck.** USB read went 485 → 500 KB/s on
the bus change alone. The real gains came afterwards, from issuing one SDMMC
command per MSC request instead of one per 512-byte sector (read 500 →
1 016 KB/s, write 258 → ~535 KB/s), and reads now sit at the ESP32-S3's USB
Full-Speed ceiling of ~1.2 MB/s.

The Wi-Fi upload and download paths are unchanged by any of this (~200 and
~500 KB/s); they are bounded by the synchronous `WebServer` in firmware.

Headroom on the card is therefore large and mostly unused — which is a good
position to be in, but it does mean SDIO is not a throughput fix on its own.

See [`docs/hardware.md`](hardware.md#sdio-clocks) for the
sweep and [`docs/architecture.md`](architecture.md#measured-performance) for the
bottleneck note.

## Wiring

SDIO reuses the four SPI pins plus two new data lines, so an SPI-wired board
migrates with two jumpers:

| Signal | GPIO | SDIO notes |
|--------|------|------------|
| CLK    | 40   | |
| CMD    | 14   | 10 kΩ pull-up to 3V3 |
| D0     | 39   | 10 kΩ pull-up to 3V3 |
| D1     | 12   | 10 kΩ pull-up to 3V3 |
| D2     | 13   | 10 kΩ pull-up to 3V3 |
| D3     | 15   | 10 kΩ pull-up to 3V3 |

Legacy SPI (4 wires): `CS=12 MISO=39 MOSI=14 CLK=40`.

SDIO **requires a 3.3 V-native microSD breakout** — no AMS1117/LC125. Modules
with an AMS1117 reference their pull-ups to the 5 V rail and drive the ESP32
pins above the 3.6 V absolute maximum (see `docs/hardware.md` power section).
The breakout must have the four pull-ups above; the ESP32's internal
pull-ups are weaker and not sufficient for SDIO. Bring-up can use 1-bit mode
(`SDMMC_WIDTH=1`, only CLK/CMD/D0) before wiring D1-D3.

```
# feat/sdio — SDIO is the default
pio run -e printdrop        # SDIO 4-bit @ 40 MHz
pio run -e bench_sdio       # SDIO bus width + clock + throughput sweep
pio run -e core_sdio        # is SD work CPU-bound or DMA-bound?

# legacy — SPI without re-wiring
pio run -e printdrop_spi    # SPI @ 20 MHz
pio run -e diag             # SPI speed sweep
```

## Software

### `src/printdrop/config.h`

* Keeps the SPI pins (`SD_*_PIN`, `SD_SPI_FREQ`) for the `printdrop_spi`
  environment.
* Adds the SDIO pins (`SDMMC_*_PIN`), bus width (`SDMMC_WIDTH`, 1 or 4) and
  clock (`SDMMC_FREQ`, Hz, default 40 MHz). All are overridable from
  `platformio.ini` `build_flags`.

### `src/printdrop/storage.cpp`

* `#ifdef USE_SDIO` selects `SD_MMC` + `sdmmc_read_sectors`/`sdmmc_write_sectors`
  via the SDMMC host; otherwise `SPI` + `SD` (`SDFS`).
* The Arduino 2.0 `SDMMCFS` does not expose `readRAW`/`sectorSize`/`numSectors`,
  so the branch accesses the underlying `sdmmc_card_t` (via a private-member
  hack `SDMMCHack::_card`) and calls the IDF `sdmmc_*` sector API directly.
  A future core that adds those accessors will let the hack be removed.
* `mountCard()` for SDIO sets the pins with `SD_MMC.setPins()`, then walks a
  frequency ladder (`SDMMC_FREQ` → 20 → 10 → 4 → 1 MHz), calling
  `SD_MMC.begin("/sdcard", mode1bit, false, freqKhz)` and verifying sector 0's
  `55 AA` MBR signature before trusting the bus. The SPI path retains its
  400 kHz cold-identification ladder.
* `spiFrequency()` is kept as an alias; new code should call `busFrequency()`
  / `busWidth()` / `busMode()` (`"sdio-4bit"`, `"sdio-1bit"`, `"spi"`).
* The three arbitration rules (`one mutex`, `withdraw before writing`, `remount
  when host writes`) and the `2 s` MSC lock timeout are unchanged — the bus
  is an implementation detail to the rest of the firmware.

### `src/printdrop/web.cpp` + `main.cpp`

* `web.cpp` introduces `SD_FS` (`SD_MMC` or `SD`) so `open`/`exists`/`remove`
  etc. are bus-agnostic, and extends `/api/status` with `busHz`/`busMode`/
  `busWidth` (`spiHz` is kept for compatibility).
* `main.cpp` banner and `status` console command report the active bus.

### `platformio.ini`

* `[env]` adds the six SDIO pin definitions.
* `[env:printdrop]` defines `USE_SDIO` + `SDMMC_WIDTH=4` (now SDIO).
* `[env:printdrop_spi]` is the SPI legacy snapshot (`ARDUINO_USB_MODE=0`).
* `[env:bench_sdio]` sweeps bus width, clock and throughput.
* `[env:core_sdio]` tests whether a dedicated storage core would help.
* `[env:diag_sdio]` predates both and is **misleading**: it builds
  `src/diag/sd_diag.cpp`, which is SPI-only, so its `-D USE_SDIO` has no effect
  and it never touches the SDMMC host. Use `bench_sdio`.

## Known SDMMC hazards

Only **40 MHz and 20 MHz** are usable. Every other clock either runs at
~192 KB/s while reporting the requested frequency, or aborts the boot outright.
Both failure modes and their measurements are documented in
[`hardware.md`](hardware.md#sdio-clocks). The lower rungs of `mountCard()`'s
frequency ladder (10 / 4 / 1 MHz) are affected and have not yet been removed.

## Testing plan

1. `pio run -e bench_sdio -t upload` — verify the probe passes at 20 MHz 1-bit
   before wiring D1-D3, then at 20 MHz 4-bit, then at 40 MHz 4-bit, and check
   the throughput sweep against the table in `hardware.md`.
2. `pio run -e printdrop -t upload` — check the `SD bus: SDIO 4-bit ...`
   banner, the `status` command, and that the card enumerates.
3. Copy a large file to the mounted volume over USB and read it back; verify
   SHA-256 matches. Expect ~1 016 KB/s read and ~535 KB/s write.
4. Upload the same file via `http://printdrop.local` — expect ~200 KB/s, i.e.
   ~100 s for 20 MB. This path is bounded by the HTTP stack, not the card.
5. During upload, confirm the printer's file list withdraws and reappears
   without a reboot, and that a concurrent USB read does not stall (short mutex
   timeout).
6. `pio run -e printdrop_spi` — regression: SPI still enumerates on the same
   hardware with only the four original wires.

## Rollback

SPI is not removed. `pio run -e printdrop_spi` builds the legacy driver
without re-wiring.
