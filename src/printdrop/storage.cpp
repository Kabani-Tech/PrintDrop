#include "storage.h"

#include <USB.h>
#include <USBMSC.h>
#include <esp32-hal-tinyusb.h>   // TUSB_CLASS_* — not pulled in by USB.h
#ifdef USE_SDIO
#include <SD_MMC.h>
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
// ---------------------------------------------------------------------------
// SD_MMC Arduino 2.0.x does not expose raw block access or geometry.
// The underlying sdmmc_card_t is kept in the protected _card member,
// so we expose it via a minimal subclass hack. This avoids forking the
// library and stays compatible with future core updates that may add a
// public card() accessor.
class SDMMCHack : public fs::SDMMCFS {
public:
    sdmmc_card_t* getCard() { return _card; }
};
#define SD_MMC_CARD (reinterpret_cast<SDMMCHack*>(&SD_MMC)->getCard())
// One SDMMC command per call, however many sectors. Issuing a command per
// 512-byte sector costs ~10x: measured 1.6 MB/s single-sector vs 16 MB/s at
// 32 KB per command on the same bus. The IDF bounces the buffer internally if
// it is not DMA-capable or word-aligned, so callers need not pre-align.
inline bool sdioReadRAW(uint8_t* buf, uint32_t sector, uint32_t count = 1) {
    sdmmc_card_t* c = SD_MMC_CARD;
    if (!c) return false;
    return sdmmc_read_sectors(c, buf, sector, count) == ESP_OK;
}
inline bool sdioWriteRAW(uint8_t* buf, uint32_t sector, uint32_t count = 1) {
    sdmmc_card_t* c = SD_MMC_CARD;
    if (!c) return false;
    return sdmmc_write_sectors(c, buf, sector, count) == ESP_OK;
}
#else
#include <SPI.h>
#include <SD.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "led.h"

namespace storage {
namespace {

USBMSC      msc;
#ifdef USE_SDIO
// SDIO uses the ESP32-S3 SDMMC host — no SPIClass needed.
#else
SPIClass    sdSPI;
#endif
SemaphoreHandle_t sdMutex = nullptr;

bool     mounted        = false;
uint32_t secSize        = 0;
uint32_t secCount       = 0;
uint32_t activeFreq     = 0;
uint32_t activeWidth    = 0;

volatile bool hostWrote      = false;   // host changed sectors under our FATFS
volatile bool hostAttached   = false;
volatile bool mediaOffered   = false;
bool          usbStarted     = false;   // USB.begin() actually ran

// How many nested write-locks have withdrawn the media, so the outermost one
// restores it.
int  mediaWithdrawnDepth = 0;

void log(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
}

// --- SD mounting -----------------------------------------------------------

#ifdef USE_SDIO

// SDIO 4-bit via the SDMMC host. The driver handles the 400 kHz
// identification internally, so we only validate the link at each frequency
// step and keep the fastest that survives a probe read.
bool mountCard() {
    const bool mode1bit = (SDMMC_WIDTH == 1);
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN,
                   mode1bit ? -1 : SDMMC_D1_PIN,
                   mode1bit ? -1 : SDMMC_D2_PIN,
                   mode1bit ? -1 : SDMMC_D3_PIN);

    // Ladder in kHz (SD_MMC API takes kHz). 40 MHz is SDHC high-speed max;
    // 20 MHz is conservative on jumper wiring and still ~4x SPI.
    const uint32_t ladderKhz[] = {
        SDMMC_FREQ / 1000,
        20000,
        10000,
        4000,
        1000,
    };

    for (uint32_t fKhz : ladderKhz) {
        if (SD_MMC.begin("/sdcard", mode1bit, false, fKhz, 5)) {
            // Verify before trusting: a marginal clock mounts but serves corrupt
            // sectors. Use the raw sdmmc path — SD_MMC Arduino 2.0.x has no
            // readRAW/sectorSize/numSectors.
            uint8_t probe[512];
            if (sdioReadRAW(probe, 0) && probe[510] == 0x55 && probe[511] == 0xAA) {
                sdmmc_card_t* c = SD_MMC_CARD;
                activeFreq  = fKhz * 1000;
                activeWidth = mode1bit ? 1 : 4;
                secSize     = c ? (uint32_t)c->csd.sector_size : 512;
                secCount    = c ? (uint32_t)c->csd.capacity : (uint32_t)(SD_MMC.cardSize() / secSize);
                log("[sd] SDIO %u-bit mounted at %u Hz, %u sectors x %u bytes",
                    activeWidth, activeFreq, secCount, secSize);
                return true;
            }
            log("[sd] SDIO %u-bit %u kHz mounted but probe failed, stepping down",
                mode1bit ? 1 : 4, fKhz);
            SD_MMC.end();
        } else {
            log("[sd] SDIO %u-bit %u kHz mount failed, stepping down",
                mode1bit ? 1 : 4, fKhz);
        }
        delay(200);
    }

    log("[sd] SDIO mount failed at all frequencies");
    return false;
}

void endBus() {
    SD_MMC.end();
}

#else  // SPI

// The SD specification requires identification at <=400 kHz; only afterwards
// may the clock rise. SD.begin() runs that sequence at whatever frequency it is
// handed, so a cold card must be mounted slowly first.
bool mountCard() {
    sdSPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    bool identified = false;
    for (int attempt = 1; attempt <= 5 && !identified; ++attempt) {
        if (SD.begin(SD_CS_PIN, sdSPI, 400000)) {
            identified = true;
            break;
        }
        SD.end();
        delay(400);
    }
    if (!identified) {
        log("[sd] identification failed at 400 kHz");
        return false;
    }

    const uint32_t ladder[] = {SD_SPI_FREQ, 10000000, 4000000, 1000000};
    for (uint32_t f : ladder) {
        SD.end();
        if (!SD.begin(SD_CS_PIN, sdSPI, f)) continue;
        // Verify before trusting: a marginal clock mounts fine and then serves
        // corrupt sectors.
        uint8_t probe[512];
        if (SD.readRAW(probe, 0) && probe[510] == 0x55 && probe[511] == 0xAA) {
            activeFreq = f;
            activeWidth = 1;
            secSize    = SD.sectorSize();
            secCount   = SD.numSectors();
            log("[sd] SPI mounted at %u Hz, %u sectors x %u bytes", f, secCount, secSize);
            return true;
        }
    }

    SD.end();
    if (SD.begin(SD_CS_PIN, sdSPI, 400000)) {
        activeFreq = 400000;
        activeWidth = 1;
        secSize    = SD.sectorSize();
        secCount   = SD.numSectors();
        log("[sd] SPI fell back to 400 kHz");
        return true;
    }
    return false;
}

void endBus() {
    SD.end();
    sdSPI.end();
}

#endif

// Re-read the filesystem if the USB host has changed it underneath us.
void refreshMountIfStale() {
    if (!hostWrote) return;
    hostWrote = false;
    log("[sd] host wrote sectors, remounting FATFS");
#ifdef USE_SDIO
    SD_MMC.end();
#else
    SD.end();
    sdSPI.end();
#endif
    mounted = mountCard();
}

// --- Write protection ------------------------------------------------------

#if USB_READ_ONLY
// TinyUSB declares this weak and the Arduino core never defines it, so this is
// the entire write-protect switch. MODE SENSE then reports the medium as
// protected, which makes hosts mount the volume read-only and stop writing
// metadata to it, and any WRITE10 that arrives regardless is refused with
// DATA PROTECT 7/27/00.
//
// This is not caution for its own sake. A host that writes caches FAT metadata
// the device has no way to invalidate, and writes it back later over whatever
// the ESP32 has changed in the meantime: measured against macOS, an uploaded
// file vanished and 51 orphaned clusters were left behind, and one upload read
// back at the right size with the wrong contents. Withdrawing the medium is a
// hint a host may ignore. Write protection is not.
extern "C" bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return false;
}
#endif

// --- USB MSC callbacks -----------------------------------------------------
// These run on the TinyUSB task. They must never block for long, so they take
// the mutex with a short timeout and fail the transfer rather than stall USB.

const uint32_t kMscLockTimeoutMs = 2000;

int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!mounted || secSize == 0) return -1;
    if (offset != 0 || (bufsize % secSize) != 0) return -1;
    const uint32_t count = bufsize / secSize;
    if (lba + count > secCount) return -1;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(kMscLockTimeoutMs)) != pdTRUE) return -1;
    led::setActivity(true);
    int32_t result = bufsize;
#ifdef USE_SDIO
    // The host asks for up to CONFIG_TINYUSB_MSC_BUFSIZE (4 KB = 8 sectors) at
    // a time; serve it with a single command.
    if (!sdioReadRAW(reinterpret_cast<uint8_t*>(buffer), lba, count)) {
        result = -1;
    }
#else
    // SDFS exposes only single-sector access, so the SPI path keeps the loop.
    for (uint32_t i = 0; i < count; ++i) {
        if (!SD.readRAW(reinterpret_cast<uint8_t*>(buffer) + i * secSize, lba + i)) {
            result = -1;
            break;
        }
    }
#endif
    led::setActivity(false);
    xSemaphoreGive(sdMutex);
    return result;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!mounted || secSize == 0) return -1;
    if (offset != 0 || (bufsize % secSize) != 0) return -1;
    const uint32_t count = bufsize / secSize;
    if (lba + count > secCount) return -1;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(kMscLockTimeoutMs)) != pdTRUE) return -1;
    led::setActivity(true);
    int32_t result = bufsize;
#ifdef USE_SDIO
    // One multi-sector write is also one card program cycle, rather than `count`
    // of them -- this matters more on writes than on reads.
    if (!sdioWriteRAW(buffer, lba, count)) {
        result = -1;
    }
#else
    for (uint32_t i = 0; i < count; ++i) {
        if (!SD.writeRAW(buffer + i * secSize, lba + i)) {
            result = -1;
            break;
        }
    }
#endif
    // Our cached FATFS view is now suspect regardless of success.
    hostWrote = true;
    led::setActivity(false);
    xSemaphoreGive(sdMutex);
    return result;
}

bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    if (load_eject && !start) {
        log("[usb] host ejected the media");
    }
    return true;
}

void onUsbEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT: hostAttached = true;  log("[usb] host attached"); break;
        case ARDUINO_USB_STOPPED_EVENT: hostAttached = false; log("[usb] host gone");     break;
        case ARDUINO_USB_SUSPEND_EVENT: hostAttached = false; log("[usb] suspended");     break;
        case ARDUINO_USB_RESUME_EVENT:  hostAttached = true;  log("[usb] resumed");       break;
        default: break;
    }
}

void setMedia(bool present) {
    if (mediaOffered == present) return;
    mediaOffered = present;
    msc.mediaPresent(present);
}

// Withdrawing the media reports MEDIUM NOT PRESENT (2/3A/00) correctly, but
// nothing in TinyUSB 0.16 ever raises UNIT ATTENTION 28h/00 -- "not ready to
// ready transition, medium may have changed" -- when it comes back. A host that
// kept the volume mounted therefore has no protocol-level reason to drop its
// cached FAT, and macOS demonstrably does not: an uploaded file stayed
// invisible for as long as the volume stayed mounted, and the host's stale
// allocation table was later written back over it, losing the file and leaving
// orphaned clusters behind. See docs/bugs.md.
//
// TinyUSB substitutes its default MEDIUM NOT PRESENT only while sense_key is
// still 0, so setting ours first makes the next *failing* command carry UNIT
// ATTENTION instead. It therefore has to be set while the medium is still
// withdrawn, and the host needs a poll in that window to collect it.
void signalMediaChanged() {
    tud_msc_set_sense(0, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00);
}

// The hammer, for when UNIT ATTENTION is not honoured: drop off the bus
// entirely. A host cannot hold a cache for a device that is not there, and it
// is also the only way back after a host has ejected the LUN -- macOS keeps
// ejecting it otherwise, and not even a firmware reboot brings it back.
void reattachUsb() {
    // begin() bails out before USB.begin() when no card mounts, and TinyUSB is
    // not safe to poke at before it is initialised. A board sitting there with
    // no card must not be crashable from an HTTP request.
    if (!usbStarted) {
        log("[usb] reattach ignored: USB was never started (no card at boot)");
        return;
    }
    log("[usb] detaching to force the host to rediscover the drive");
    tud_disconnect();
    delay(USB_DETACH_MS);
    tud_connect();
}

}  // namespace

// --- Public API ------------------------------------------------------------

bool begin() {
    sdMutex = xSemaphoreCreateMutex();
    if (!sdMutex) return false;

    mounted = mountCard();
    if (!mounted) return false;
    if (secSize == 0 || secCount == 0) {
        log("[sd] card reports zero geometry");
        return false;
    }

    USB.onEvent(onUsbEvent);

    USB.VID(0x303A);
    USB.PID(0x4003);
    USB.productName(PRINTDROP_NAME);
    USB.manufacturerName("Kabani Tech");
    // Single-function device: let the host classify from the interface
    // descriptor rather than announcing an IAD composite it is not.
    USB.usbClass(TUSB_CLASS_UNSPECIFIED);
    USB.usbSubClass(0);
    USB.usbProtocol(0);

    msc.vendorID("PrntDrop");
    msc.productID("SD Card");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    mediaOffered = true;
    msc.begin(secCount, secSize);

    USB.begin();
    usbStarted = true;
    log("[usb] mass storage started (%s %u-bit @ %u Hz)", busMode(), busWidth(), busFrequency());
    return true;
}

bool     cardMounted()    { return mounted; }
uint32_t sectorCount()    { return secCount; }
uint32_t sectorSize()     { return secSize; }
uint32_t spiFrequency()   { return activeFreq; }
uint32_t busFrequency()   { return activeFreq; }
uint32_t busWidth()       { return activeWidth; }
const char* busMode() {
#ifdef USE_SDIO
    return (SDMMC_WIDTH == 1) ? "sdio-1bit" : "sdio-4bit";
#else
    return "spi";
#endif
}
bool     usbHostPresent() { return hostAttached; }
bool     usbMediaPresent(){ return mediaOffered; }

uint64_t totalBytes() {
    Guard g(false, 3000);
    if (!g.ok()) return 0;
#ifdef USE_SDIO
    return SD_MMC.totalBytes();
#else
    return SD.totalBytes();
#endif
}
uint64_t usedBytes() {
    Guard g(false, 3000);
    if (!g.ok()) return 0;
#ifdef USE_SDIO
    return SD_MMC.usedBytes();
#else
    return SD.usedBytes();
#endif
}
uint64_t freeBytes() {
    Guard g(false, 3000);
    if (!g.ok()) return 0;
#ifdef USE_SDIO
    const uint64_t total = SD_MMC.totalBytes();
    const uint64_t used  = SD_MMC.usedBytes();
#else
    const uint64_t total = SD.totalBytes();
    const uint64_t used  = SD.usedBytes();
#endif
    return total > used ? total - used : 0;
}

bool lock(bool forWrite, uint32_t timeoutMs) {
    if (!sdMutex) return false;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;

    led::setActivity(true);
    if (forWrite) {
        // Withdraw the card so the host stops reading and drops its FAT cache.
        if (mediaWithdrawnDepth++ == 0) setMedia(false);
    }
    refreshMountIfStale();
    return true;
}

void unlock() {
    if (!sdMutex) return;
    if (mediaWithdrawnDepth > 0 && --mediaWithdrawnDepth == 0) {
        // Re-present it; the host re-reads the FAT and sees our changes.
        setMedia(true);
    }
    led::setActivity(false);
    xSemaphoreGive(sdMutex);
}

void refreshHostView() {
    if (!sdMutex) return;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return;
    setMedia(false);
    xSemaphoreGive(sdMutex);
    // Give the host time to notice the removal before offering it back.
    // 1500 ms is chosen to survive hosts that poll only every 500-1000 ms
    // (Windows, many printers). The previous 600 ms was missed when the
    // upload was tiny (50-byte file) and the whole offline window was short.
    delay(1500);
    // Raise the media-changed condition with the medium still withdrawn, and
    // leave it withdrawn long enough for the host to collect it on a poll.
    signalMediaChanged();
    delay(USB_MEDIA_CHANGED_MS);
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        setMedia(true);
        xSemaphoreGive(sdMutex);
    }
    log("[usb] media re-presented to host");

#if USB_FORCE_REATTACH
    // Measured: macOS ignores the removal and the UNIT ATTENTION alike while it
    // holds the volume mounted. Detaching from the bus is the only signal it
    // acts on. It costs the host a "disk not ejected properly" complaint, which
    // is the honest description of what just happened.
    reattachUsb();
#endif
}

void reattachHost() {
    reattachUsb();
}

}  // namespace storage
