#pragma once

// Arbitrates the SD card between the USB host and the Wi-Fi side.
//
// The problem this solves: a USB host caches the FAT. If the ESP32 writes to
// the card while a host has it mounted, the host's cached allocation table goes
// stale and the next write from either side corrupts the filesystem. The
// converse is also true -- the ESP32's own FATFS cache goes stale if the host
// writes sectors underneath it.
//
// The rules enforced here:
//   * Every SD access, from either side, is serialised by one mutex.
//   * Before the ESP32 modifies the card, the media is withdrawn from the USB
//     host, so the host stops issuing reads and re-reads the FAT afterwards.
//   * If the host has written any sector, the ESP32 remounts FATFS before
//     trusting its own view of the filesystem again.

#include <Arduino.h>
#include <stdint.h>

namespace storage {

bool begin();
// Call from loop(). Retries a card that was absent or has stopped answering,
// and starts USB if the card only turned up after boot. Without it, a card
// that fails at any point stays failed until the board is power-cycled.
void poll();
bool cardMounted();

uint64_t totalBytes();
uint64_t usedBytes();
uint64_t freeBytes();
uint32_t sectorCount();
uint32_t sectorSize();
// Bus clock currently in use (SPI Hz or SDMMC Hz). Kept as spiFrequency()
// for backwards compatibility; prefer busFrequency().
uint32_t spiFrequency();
uint32_t busFrequency();
const char* busMode();      // "sdio-4bit", "sdio-1bit", or "spi"
uint32_t busWidth();        // 1 or 4 (SDIO), or 1 (SPI)

// True while a USB host is attached and not suspended.
bool usbHostPresent();
// True while the card is being offered to that host.
bool usbMediaPresent();

// Withdraw and re-present the media so the host re-reads the FAT. This is what
// makes a freshly uploaded file show up in the printer's file list.
void refreshHostView();

// Drop off the USB bus and come back, forcing the host to rediscover the drive.
// The only recovery once a host has ejected the LUN, and the only signal some
// hosts act on at all.
void reattachHost();

// Acquire before touching the filesystem. `forWrite` additionally withdraws the
// media from the USB host for the duration. Returns false on timeout, in which
// case the lock is NOT held and must not be released.
bool lock(bool forWrite, uint32_t timeoutMs = 15000);
void unlock();

// RAII wrapper for the common case.
class Guard {
public:
    explicit Guard(bool forWrite, uint32_t timeoutMs = 15000)
        : held_(lock(forWrite, timeoutMs)) {}
    ~Guard() { if (held_) unlock(); }
    bool ok() const { return held_; }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
private:
    bool held_;
};

}  // namespace storage
