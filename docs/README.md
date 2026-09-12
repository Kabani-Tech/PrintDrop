# PrintDrop documentation

Reference and background for the firmware. Start with the
[project README](../README.md) for what PrintDrop is and how to use it, and
[CONTRIBUTING.md](../CONTRIBUTING.md) for how to build and submit changes.

| Document | What's in it |
|---|---|
| [architecture.md](architecture.md) | How the pieces fit: USB/Wi-Fi arbitration, why the card is offered read-only, how two other projects solved the same problem, module layout, partition map, build environments, measured performance |
| [hardware.md](hardware.md) | The board as measured, SD wiring, power requirements, verified SPI clocks |
| [bugs.md](bugs.md) | Every fault found during the port and its root cause — why the code is shaped the way it is |
| [flashing.md](flashing.md) | Getting this board into download mode, and why it is more awkward than it should be |

`platformio.ini.original` is the upstream Cardputer build configuration, kept
for reference against the current one.

## Branding assets

| File | Used by | Notes |
|---|---|---|
| `assets/printdrop_logo.webp` | source of truth | Full lockup, 1536×1024, transparent |
| `assets/printdrop_banner.webp` | README, site hero | Lockup on a dark ground, 1200 px wide |
| `data/logo.webp` | web UI, site favicon | Icon only, 256×256, flashed to the device |

The wordmark sets "Print" in white, so the full lockup **needs a dark
background** — on white, half of it disappears. That is why the banner is
pre-flattened onto the brand navy and why the site commits to a dark theme. The
square icon carries its own dark tile, so it is safe on any background and is
the one used in the UI.

## Why these notes exist

The port from an M5Stack Cardputer to a bare ESP32-S3-DevKitC-1 turned up
several faults that produce no useful error message — the device simply does
nothing. Each is recorded with the evidence that identified it, so the same
symptom does not have to be re-diagnosed from scratch.
