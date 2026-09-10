// SDIO throughput benchmark for ESP32-S3-DevKitC-1.
//
// sd_diag.cpp measures the SPI bus. Nothing in src/diag/ actually exercised the
// SDMMC host, so the numbers in README.md for SDIO were projections. This sketch
// measures them.
//
// Leaves USB in Serial/JTAG mode so the board stays flashable while testing.

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include "sdmmc_cmd.h"
#include "../common/reflash_hatch.h"

#ifndef SDMMC_CLK_PIN
#define SDMMC_CLK_PIN 40
#endif
#ifndef SDMMC_CMD_PIN
#define SDMMC_CMD_PIN 14
#endif
#ifndef SDMMC_D0_PIN
#define SDMMC_D0_PIN 39
#endif
#ifndef SDMMC_D1_PIN
#define SDMMC_D1_PIN 12
#endif
#ifndef SDMMC_D2_PIN
#define SDMMC_D2_PIN 13
#endif
#ifndef SDMMC_D3_PIN
#define SDMMC_D3_PIN 15
#endif

// Same private-member hack storage.cpp uses: Arduino 2.0.x SDMMCFS exposes no
// raw block access or geometry.
class SDMMCHack : public fs::SDMMCFS {
public:
    sdmmc_card_t* getCard() { return _card; }
};
#define SD_MMC_CARD (reinterpret_cast<SDMMCHack*>(&SD_MMC)->getCard())

static void out(const String& s) {
    Serial.println(s);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.println(s);
#endif
}

static void outf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    out(String(buf));
}

static void banner(const char* title) {
    out("");
    out("==================================================");
    out(String("  ") + title);
    out("==================================================");
}

// --- benchmark ---------------------------------------------------------------

// 32 KB transfer buffer: 64 sectors per sdmmc call. Large enough that per-command
// overhead stops dominating, small enough for a 4 MB-heap board with no PSRAM.
static const size_t kChunkSectors = 64;
static const size_t kChunkBytes   = kChunkSectors * 512;
static uint8_t*     buf           = nullptr;

// Byte budgets per measurement. Bulk gets more (it is fast); the single-sector
// pass is deliberately smaller because it is 10x slower and would otherwise
// dominate the sweep at low clocks.
static const uint32_t kBulkBytes   = 4u * 1024 * 1024;
static const uint32_t kSingleBytes = 1u * 1024 * 1024;
static const uint32_t kWriteBytes  = 2u * 1024 * 1024;

// Every measurement reads a *fresh* region so the card's internal read-ahead
// cache cannot inflate a later run. Starts 32 MB in, past the FAT and any short
// files. Read-only: this never writes to raw sectors.
static uint32_t benchCursor = 65536;   // 32 MB in
static const uint32_t kRegionSectors = 8192;   // 4 MB per measurement region

struct Result {
    uint32_t freqKhz;
    uint8_t  width;
    bool     mounted;
    bool     verified;
    uint32_t bulkKBps;     // 32 KB per sdmmc call
    uint32_t singleKBps;   // 1 sector per call -- same shape as the SPI sweep
    uint32_t fileWriteKBps;
};

static uint8_t reference[512];
static bool    haveReference = false;

// Sequential raw read, `chunkSectors` per sdmmc_read_sectors call.
static uint32_t measureRead(sdmmc_card_t* card, size_t chunkSectors, uint32_t budget) {
    const uint32_t chunks = budget / (chunkSectors * 512);
    uint32_t sector = benchCursor;
    benchCursor += kRegionSectors;      // never re-read the same region
    uint32_t t0 = millis();
    for (uint32_t i = 0; i < chunks; ++i) {
        if (sdmmc_read_sectors(card, buf, sector, chunkSectors) != ESP_OK) return 0;
        sector += chunkSectors;
    }
    uint32_t dt = millis() - t0;
    if (!dt) return 0;
    return (uint32_t)(((uint64_t)chunks * chunkSectors * 512) / dt);   // bytes/ms == KB/s
}

// Write through the filesystem rather than raw sectors: a raw write benchmark
// would have to pick a scratch LBA range, and getting that wrong destroys the
// user's card. This costs some FAT overhead but is representative of the path
// an upload actually takes.
static uint32_t measureFileWrite() {
    const uint32_t bytes = kWriteBytes;
    File f = SD_MMC.open("/.pd_bench.tmp", FILE_WRITE);
    if (!f) return 0;
    memset(buf, 0xA5, kChunkBytes);
    uint32_t t0 = millis();
    uint32_t written = 0;
    while (written < bytes) {
        size_t n = f.write(buf, kChunkBytes);
        if (n != kChunkBytes) { f.close(); SD_MMC.remove("/.pd_bench.tmp"); return 0; }
        written += n;
    }
    f.flush();
    uint32_t dt = millis() - t0;
    f.close();
    SD_MMC.remove("/.pd_bench.tmp");
    if (!dt) return 0;
    return written / dt;
}

static Result benchAt(uint32_t freqKhz, uint8_t width) {
    Result r = {freqKhz, width, false, false, 0, 0, 0};
    const bool mode1bit = (width == 1);

    SD_MMC.end();
    delay(150);
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN,
                   mode1bit ? -1 : SDMMC_D1_PIN,
                   mode1bit ? -1 : SDMMC_D2_PIN,
                   mode1bit ? -1 : SDMMC_D3_PIN);

    if (!SD_MMC.begin("/sdcard", mode1bit, false, freqKhz, 5)) {
        outf("%2u-bit %6u kHz : mount FAILED", width, freqKhz);
        return r;
    }
    r.mounted = true;

    sdmmc_card_t* card = SD_MMC_CARD;
    if (!card) {
        outf("%2u-bit %6u kHz : mounted but no card handle", width, freqKhz);
        return r;
    }

    // Verify the link before trusting any number from it: a marginal clock
    // mounts happily and returns corrupt data.
    uint8_t probe[512];
    if (sdmmc_read_sectors(card, probe, 0, 1) != ESP_OK) {
        outf("%2u-bit %6u kHz : sector 0 read FAILED", width, freqKhz);
        return r;
    }
    if (probe[510] != 0x55 || probe[511] != 0xAA) {
        outf("%2u-bit %6u kHz : no 55 AA signature -- unreliable", width, freqKhz);
        return r;
    }
    if (!haveReference) {
        memcpy(reference, probe, 512);
        haveReference = true;
    } else if (memcmp(reference, probe, 512) != 0) {
        outf("%2u-bit %6u kHz : DATA MISMATCH vs reference -- unreliable", width, freqKhz);
        return r;
    }
    r.verified = true;

    r.bulkKBps      = measureRead(card, kChunkSectors, kBulkBytes);
    r.singleKBps    = measureRead(card, 1, kSingleBytes);
    r.fileWriteKBps = measureFileWrite();

    outf("%2u-bit %6u kHz (host settled on %6u kHz) : OK  read %5u KB/s (32K chunks) | %5u KB/s (1 sector) | write %5u KB/s",
         width, freqKhz, (unsigned)card->max_freq_khz,
         r.bulkKBps, r.singleKBps, r.fileWriteKBps);
    return r;
}

static void reportCard() {
    sdmmc_card_t* card = SD_MMC_CARD;
    if (!card) return;
    banner("CARD");
    outf("Name          : %s", card->cid.name);
    outf("Type          : %s", (card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC");
    outf("Speed         : %u kHz (max %u kHz)",
         (unsigned)(card->max_freq_khz), (unsigned)(card->csd.tr_speed / 1000));
    outf("Bus width     : %u-bit", (unsigned)card->log_bus_width);
    outf("Sector size   : %u bytes", (unsigned)card->csd.sector_size);
    outf("Sector count  : %u", (unsigned)card->csd.capacity);
    outf("Capacity      : %llu MB",
         ((uint64_t)card->csd.capacity * card->csd.sector_size) / (1024ULL * 1024ULL));
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#endif
    delay(3000);

    banner("ESP32-S3 SDIO THROUGHPUT BENCHMARK");
    out("Build: " __DATE__ " " __TIME__);
    outf("Pins: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d",
         SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN,
         SDMMC_D1_PIN, SDMMC_D2_PIN, SDMMC_D3_PIN);
    outf("Free heap: %u bytes", (unsigned)ESP.getFreeHeap());

    buf = (uint8_t*)malloc(kChunkBytes);
    if (!buf) {
        out("FATAL: could not allocate the transfer buffer.");
        return;
    }

    banner("CLOCK MAP");
    out("Requested clock vs. the clock the SDMMC host actually programmed, with a");
    out("short read at each. A large gap means the ladder in storage.cpp is");
    out("stepping down to something far slower than it thinks.");
    out("");
    // Ordered most-important-first: these are exactly the rungs of the fallback
    // ladder in storage.cpp mountCard(). Anything that is not one of the IDF's
    // standard frequencies (40000 / 20000 / 400) risks the host rounding *up*
    // and tripping an abort:
    //   sdmmc_init_host_frequency: card->max_freq_khz <= card->host.max_freq_khz
    // 32000 and 25000 both do this and boot-loop the board, so they are omitted.
    const uint32_t mapFreqs[] = {40000, 20000, 10000, 4000, 1000, 16000, 8000};
    for (uint32_t f : mapFreqs) {
        SD_MMC.end();
        delay(120);
        SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN,
                       SDMMC_D1_PIN, SDMMC_D2_PIN, SDMMC_D3_PIN);
        if (!SD_MMC.begin("/sdcard", false, false, f, 5)) {
            outf("  requested %6u kHz : mount FAILED", f);
            continue;
        }
        sdmmc_card_t* c = SD_MMC_CARD;
        if (!c) { outf("  requested %6u kHz : no card handle", f); continue; }
        uint32_t kb = measureRead(c, kChunkSectors, 512u * 1024);
        outf("  requested %6u kHz -> host %6u kHz : %5u KB/s",
             f, (unsigned)c->max_freq_khz, kb);
    }

    banner("SWEEP");
    out("Each mount is verified against a reference read of sector 0 before its");
    out("throughput is believed. Reads are raw sdmmc_read_sectors from 32 MB in;");
    out("the write figure goes through FATFS (see measureFileWrite).");
    out("");

    Result results[8];
    int n = 0;
    // 4-bit is the shipping configuration. 40 and 20 MHz carry the claims;
    // 10 MHz runs twice because the first sweep produced an implausible outlier.
    results[n++] = benchAt(40000, 4);
    results[n++] = benchAt(20000, 4);
    results[n++] = benchAt(10000, 4);
    results[n++] = benchAt(10000, 4);
    results[n++] = benchAt(40000, 4);   // repeat: confirms 40 MHz is reproducible
    results[n++] = benchAt(20000, 1);

    reportCard();

    banner("SUMMARY");
    out("width  clock      read(bulk)  read(1sec)   write");
    for (int i = 0; i < n; ++i) {
        Result& r = results[i];
        if (!r.verified) {
            outf("%u-bit  %6u kHz  --  not verified", r.width, r.freqKhz);
            continue;
        }
        outf("%u-bit  %6u kHz  %6u KB/s  %6u KB/s  %6u KB/s",
             r.width, r.freqKhz, r.bulkKBps, r.singleKBps, r.fileWriteKBps);
    }

    banner("BENCHMARK COMPLETE");
}

void loop() {
    reflashHatchPoll();
    delay(50);
}
