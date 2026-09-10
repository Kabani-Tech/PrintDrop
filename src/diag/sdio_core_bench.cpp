// Does pinning SD work to a dedicated core help?
//
// The firmware currently runs SD operations inline on whichever task calls
// them: loopTask (core 1) for the web path, the TinyUSB task (no affinity) for
// the MSC path. This sketch measures whether that costs anything, i.e. whether
// SD transfers are CPU-bound (a dedicated core would help) or DMA/latency-bound
// (it would not).
//
// Method: run the SD workload on a pinned task while a spinner ("hog") burns
// CPU on a chosen core, and compare against the same workload with no hog.
//   - hog at LOWER priority  -> how much CPU is left over *during* SD I/O
//   - hog at EQUAL priority  -> how much throughput real contention costs
//   - hog on the OTHER core  -> whether the two cores interfere at all

#include <Arduino.h>
#include <SD_MMC.h>
#include "sdmmc_cmd.h"
#include "esp_task_wdt.h"
#include "../common/reflash_hatch.h"

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
    char b[256]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof(b),fmt,a); va_end(a); out(String(b));
}
static void banner(const char* t) {
    out(""); out("=================================================="); out(String("  ")+t);
    out("==================================================");
}

// --- CPU hog ---------------------------------------------------------------
static volatile uint64_t hogCount = 0;
static volatile bool     hogRun   = false;
static void hogTask(void*) {
    while (hogRun) hogCount++;
    vTaskDelete(NULL);
}

// --- SD job ----------------------------------------------------------------
static uint8_t* buf = nullptr;
static const size_t kChunkSectors = 64;          // 32 KB
static uint32_t cursor = 65536;                  // 32 MB in, fresh region each run

struct Job {
    size_t   chunkSectors;
    uint32_t budget;
    volatile uint32_t kbps;
    volatile uint32_t ms;
    volatile bool done;
};
static Job job;

static void sdTask(void*) {
    sdmmc_card_t* c = SD_MMC_CARD;
    const uint32_t chunks = job.budget / (job.chunkSectors * 512);
    uint32_t sec = cursor; cursor += 8192;
    uint32_t t0 = millis();
    bool ok = true;
    for (uint32_t i = 0; i < chunks; ++i) {
        if (sdmmc_read_sectors(c, buf, sec, job.chunkSectors) != ESP_OK) { ok = false; break; }
        sec += job.chunkSectors;
    }
    uint32_t dt = millis() - t0;
    job.ms   = dt;
    job.kbps = (ok && dt) ? (uint32_t)(((uint64_t)chunks * job.chunkSectors * 512) / dt) : 0;
    job.done = true;
    vTaskDelete(NULL);
}

// Baseline spin rate with nothing else running, counts per ms.
static double hogBaseline = 0;

static void measureHogBaseline() {
    hogCount = 0; hogRun = true;
    xTaskCreatePinnedToCore(hogTask, "hog", 2048, NULL, 1, NULL, 1);
    uint32_t t0 = millis();
    while (millis() - t0 < 1500) vTaskDelay(10 / portTICK_PERIOD_MS);
    uint32_t dt = millis() - t0;
    hogRun = false;
    vTaskDelay(50 / portTICK_PERIOD_MS);
    hogBaseline = (double)hogCount / dt;
    outf("hog baseline (core 1, nothing else): %.0f counts/ms", hogBaseline);
}

// sdCore: where the SD task runs. hogCore: -1 for none. hogPrio relative to SD.
static void run(const char* label, size_t chunkSectors, uint32_t budget,
                int sdCore, int hogCore, int hogPrio) {
    job.chunkSectors = chunkSectors;
    job.budget = budget;
    job.done = false; job.kbps = 0; job.ms = 0;

    hogCount = 0;
    if (hogCore >= 0) {
        hogRun = true;
        xTaskCreatePinnedToCore(hogTask, "hog", 2048, NULL, hogPrio, NULL, hogCore);
    }

    xTaskCreatePinnedToCore(sdTask, "sd", 4096, NULL, 5, NULL, sdCore);
    while (!job.done) vTaskDelay(5 / portTICK_PERIOD_MS);

    uint64_t counts = hogCount;
    if (hogCore >= 0) { hogRun = false; vTaskDelay(50 / portTICK_PERIOD_MS); }

    if (hogCore >= 0 && job.ms > 0 && hogBaseline > 0) {
        double rate = (double)counts / job.ms;
        double frac = rate / hogBaseline * 100.0;
        outf("%-46s : %6u KB/s | CPU left for other work: %5.1f%%",
             label, job.kbps, frac);
    } else {
        outf("%-46s : %6u KB/s", label, job.kbps);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

static void runSweep(int cycle) {
    banner("BULK READS (32 KB per sdmmc call)");
    out("The shape a fixed multi-sector onRead/onWrite would use.");
    out("");
    run("bulk, SD core1, no hog",                    kChunkSectors, 4u<<20, 1, -1, 1);
    run("bulk, SD core0, no hog",                    kChunkSectors, 4u<<20, 0, -1, 1);
    run("bulk, SD core1, low-prio hog core1",        kChunkSectors, 4u<<20, 1,  1, 1);
    run("bulk, SD core1, low-prio hog core0 (other)",kChunkSectors, 4u<<20, 1,  0, 1);

    banner("SINGLE-SECTOR READS (what the MSC path does today)");
    out("512 B per sdmmc call -- latency-dominated, not bandwidth-dominated.");
    out("This is the path onRead/onWrite actually take.");
    out("");
    run("1-sector, SD core1, no hog",                     1, 1u<<20, 1, -1, 1);
    run("1-sector, SD core0, no hog",                     1, 1u<<20, 0, -1, 1);
    run("1-sector, SD core1, low-prio hog core1",         1, 1u<<20, 1,  1, 1);
    run("1-sector, SD core1, low-prio hog core0 (other)", 1, 1u<<20, 1,  0, 1);

    outf("--- end of cycle %d ---", cycle);
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#endif
    delay(3000);
    // The idle-task watchdog would fire when a hog monopolises a core, and
    // disableCoreNWDT() is unreliable here -- deinit the whole thing instead.
    esp_task_wdt_deinit();

    banner("SD / CORE AFFINITY TEST");
    outf("Build: %s %s", __DATE__, __TIME__);
    outf("setup() runs on core %d", xPortGetCoreID());

    buf = (uint8_t*)malloc(kChunkSectors * 512);
    if (!buf) { out("FATAL: no buffer"); return; }

    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN,
                   SDMMC_D1_PIN, SDMMC_D2_PIN, SDMMC_D3_PIN);
    if (!SD_MMC.begin("/sdcard", false, false, 40000, 5)) {
        out("FATAL: SDIO mount failed"); return;
    }
    out("mounted SDIO 4-bit @ 40 MHz");
    measureHogBaseline();
    out("");
    out("The sweep repeats forever, so a console can attach at any time.");
}

void loop() {
    static int cycle = 0;
    if (!buf) { delay(1000); return; }
    cursor = 65536;                 // bounded, comparable across cycles
    runSweep(++cycle);
    for (int i = 0; i < 100; ++i) { reflashHatchPoll(); delay(50); }
}
