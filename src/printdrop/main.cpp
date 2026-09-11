// PrintDrop — a Wi-Fi flash drive for 3D printers.
//
// The board plugs into a printer's USB port and appears as an ordinary USB
// flash drive. It also joins the local network and serves a web UI, so print
// jobs can be dropped onto the card from a desk instead of carrying a stick
// across the workshop.
//
// Target: ESP32-S3-DevKitC-1, 4 MB flash, no PSRAM.
//
// Must be built with ARDUINO_USB_CDC_ON_BOOT=0. USB.h derives
// ARDUINO_USB_ON_BOOT from it, and the core's app_main() then calls USB.begin()
// before setup() runs — freezing the USB descriptor before the MSC interface
// can be registered or the PID set. Console output goes to UART0 instead.

#include <Arduino.h>

#include "../common/reflash_hatch.h"
#include "config.h"
#include "net.h"
#include "storage.h"
#include "web.h"
#include "led.h"
#include "button.h"
#include "auth.h"
#include "ota.h"

// Serial console on UART0. The web setup portal is the normal route, but a
// headless device on a bench is far easier to provision over the wire — and it
// keeps credentials out of the source tree either way.
static void handleCommand(String line) {
    line.trim();
    if (line.isEmpty()) return;

    if (line == "BOOTLOADER") {
        Serial.println("[hatch] rebooting into download mode...");
        Serial.flush();
        delay(50);
        reflashHatchReboot();
        return;
    }

    String cmd = line, rest;
    const int sp = line.indexOf(' ');
    if (sp > 0) {
        cmd  = line.substring(0, sp);
        rest = line.substring(sp + 1);
        rest.trim();
    }
    cmd.toLowerCase();

    if (cmd == "help") {
        Serial.println("commands:");
        Serial.println("  status                 show device state");
        Serial.println("  wifi <ssid> <password> join a network and reboot");
        Serial.println("  hostname <name>        set the mDNS name and reboot");
        Serial.println("  auth <user> <pass>     set web UI login and reboot");
        Serial.println("  passwd <pass>          set password for current user");
        Serial.println("  forget                 clear Wi-Fi settings and reboot");
        Serial.println("  clear-auth             reset web auth to defaults");
        Serial.println("  factory-reset          clear Wi-Fi + auth and reboot");
        Serial.println("  reattach               drop off USB and come back");
        Serial.println("  reboot                 restart");
        Serial.println("  BOOTLOADER             reboot into flash download mode");

    } else if (cmd == "status") {
        Serial.printf("mode      : %s\n", net::isAccessPoint() ? "access point" : "station");
        Serial.printf("ssid      : %s\n", net::currentSsid().c_str());
        Serial.printf("hostname  : %s.local\n", net::hostname().c_str());
        Serial.printf("hostname2 : http://%s/  (mDNS+LLMNR)\n", net::hostname().c_str());
        Serial.printf("ip        : %s\n", net::localIp().toString().c_str());
        Serial.printf("rssi      : %d dBm\n", (int)net::rssi());
        Serial.printf("card      : %s\n", storage::cardMounted() ? "mounted" : "absent");
        Serial.printf("sd bus    : %s %u-bit @ %u Hz\n", storage::busMode(), storage::busWidth(), storage::busFrequency());
        Serial.printf("usb host  : %s\n", storage::usbHostPresent() ? "connected" : "none");
        Serial.printf("usb media : %s\n", storage::usbMediaPresent() ? "presented" : "withdrawn");
        Serial.printf("auth      : %s user=%s\n", auth::isRequired() ? "on" : "off", auth::currentUser().c_str());
        Serial.printf("version   : %s ota %s\n", ota::currentVersion().c_str(), PRINTDROP_ENABLE_OTA ? "on" : "off");

    } else if (cmd == "wifi") {
        // The password may contain spaces, so only the first token is the SSID.
        const int sp2 = rest.indexOf(' ');
        if (sp2 <= 0) { Serial.println("usage: wifi <ssid> <password>"); return; }
        net::Config c = net::config();
        c.ssid     = rest.substring(0, sp2);
        c.password = rest.substring(sp2 + 1);
        if (net::saveConfig(c)) { Serial.println("saved, restarting"); delay(300); ESP.restart(); }

    } else if (cmd == "hostname") {
        if (rest.isEmpty()) { Serial.println("usage: hostname <name>"); return; }
        String norm = net::normalizeHostname(rest);
        if (!net::isValidHostname(norm)) {
            Serial.println("invalid hostname (a-z, 0-9, hyphen, 1-63 chars, not starting/ending with -)");
            return;
        }
        {
            String cur = net::hostname();
            cur.toLowerCase(); String low = norm; low.toLowerCase();
            if (low != cur && net::isHostnameTaken(norm, 1200)) {
                String free = net::findFreeHostname(norm);
                Serial.printf("hostname '%s' already taken, try '%s'\n", norm.c_str(), free.c_str());
                return;
            }
        }
        net::Config c = net::config();
        c.hostname = norm;
        if (net::saveConfig(c)) { Serial.printf("hostname set to '%s', restarting\n", norm.c_str()); delay(300); ESP.restart(); }

    } else if (cmd == "forget") {
        net::Config c;
        net::saveConfig(c);
        Serial.println("cleared, restarting");
        delay(300);
        ESP.restart();

    } else if (cmd == "auth") {
        int sp2 = rest.indexOf(' ');
        if (sp2 <= 0) { Serial.println("usage: auth <user> <pass>"); return; }
        String user = rest.substring(0, sp2);
        String pass = rest.substring(sp2+1);
        pass.trim(); user.trim();
        if (auth::setCredentials(user, pass)) { Serial.println("auth saved, restarting"); delay(300); ESP.restart(); }
        else Serial.println("auth save failed");

    } else if (cmd == "passwd") {
        if (rest.isEmpty()) { Serial.println("usage: passwd <newPass>"); return; }
        String user = auth::currentUser();
        if (auth::setCredentials(user, rest)) { Serial.println("password saved, restarting"); delay(300); ESP.restart(); }
        else Serial.println("save failed");

    } else if (cmd == "clear-auth") {
        auth::clear();
        Serial.println("auth reset to defaults, restarting");
        delay(300); ESP.restart();

    } else if (cmd == "factory-reset") {
        net::Config c; net::saveConfig(c);
        auth::clear();
        Serial.println("factory reset — Wi-Fi + auth cleared, restarting");
        delay(300); ESP.restart();

    } else if (cmd == "reattach") {
        // A host that has ejected the LUN keeps ejecting it, and a reboot does
        // not help -- only leaving the bus and coming back does.
        Serial.println("re-attaching to the USB host");
        storage::reattachHost();

    } else if (cmd == "reboot") {
        ESP.restart();

    } else {
        Serial.printf("unknown command '%s' — try 'help'\n", cmd.c_str());
    }
}

static void pollConsole() {
    static String line;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (!line.isEmpty()) { handleCommand(line); line = ""; }
        } else if (line.length() < 160) {
            line += c;
        }
    }
}

static void onButtonShort() {
    Serial.println("[btn] short press — refreshing printer view");
    storage::refreshHostView();
}

static void onButtonLong() {
    Serial.println("[btn] long press 5s — factory reset");
    net::Config c; net::saveConfig(c);
    auth::clear();
    // Blink error pattern before reboot
    led::setError(true);
    delay(800);
    ESP.restart();
}

static void banner() {
    Serial.println();
    Serial.println("=====================================");
    Serial.println("  " PRINTDROP_NAME " " PRINTDROP_VERSION);
    Serial.println("  Kabani Tech Private Limited");
    Serial.println("=====================================");
    Serial.printf("Build: %s %s\n", __DATE__, __TIME__);
#ifdef USE_SDIO
    Serial.printf("SD bus: SDIO %d-bit CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d @ %u Hz\n",
                  SDMMC_WIDTH, SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN,
                  SDMMC_D1_PIN, SDMMC_D2_PIN, SDMMC_D3_PIN, SDMMC_FREQ);
#else
    Serial.printf("SD pins CS=%d MISO=%d MOSI=%d CLK=%d\n",
                  SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN);
#endif
#if PRINTDROP_LED_PIN >= 0
    Serial.printf("LED: GPIO %d %s, Button: GPIO %d %s\n",
                  PRINTDROP_LED_PIN, PRINTDROP_LED_ACTIVE_HIGH ? "active-high" : "active-low",
                  PRINTDROP_BUTTON_PIN, PRINTDROP_BUTTON_ACTIVE_LOW ? "active-low" : "active-high");
#endif
    Serial.printf("Auth: %s user=%s, OTA %s\n",
                  auth::isRequired() ? "on" : "off", auth::currentUser().c_str(),
                  PRINTDROP_ENABLE_OTA ? "on" : "off");
    Serial.println("Type 'help' for serial commands.");
}

void setup() {
    UART_CONSOLE.begin(115200);
    delay(1200);
    // Init auth before banner so banner can report user
    auth::begin();
    led::begin();
    button::begin(onButtonShort, onButtonLong);
    banner();

    if (!storage::begin()) {
        // Without a card there is nothing to serve, but the network side still
        // comes up so the device can be reached and diagnosed.
        Serial.println("[sd] FATAL: no usable card.");
        Serial.println("[sd] If the module has an AMS1117 regulator, VCC must be 3V3.");
#ifdef USE_SDIO
        Serial.println("[sd] Check CLK/CMD/D0-D3 against the pins above; SDIO needs pull-ups.");
#else
        Serial.println("[sd] Check CS/MISO/MOSI/CLK against the pins above.");
#endif
        led::setError(true);
        Serial.println("[sd] will keep retrying; the drive appears when a card answers.");
    } else {
        // Quick SD OTA check at boot (no block)
        String v;
        if (ota::checkSD(&v, nullptr)) {
            Serial.printf("[ota] SD update available: %s -> %s\n", PRINTDROP_VERSION, v.c_str());
        }
    }

    const bool joined = net::begin();
    web::begin();

    if (joined) {
        Serial.printf("[ready] http://%s.local  http://%s/  or  http://%s\n",
                      net::hostname().c_str(), net::hostname().c_str(), net::localIp().toString().c_str());
        Serial.printf("[ready] LLMNR: http://%s/  mDNS: http://%s.local/\n", net::hostname().c_str(), net::hostname().c_str());
    } else {
        Serial.printf("[ready] setup portal at http://%s (join \"%s\")\n",
                      net::localIp().toString().c_str(), AP_SSID_PREFIX);
    }
}

void loop() {
    storage::poll();
    web::loop();
    net::loop();
    led::loop();
    button::loop();
    // Owns the UART instead of reflashHatchPoll(), so BOOTLOADER and the
    // provisioning commands do not fight over the same input stream.
    pollConsole();

    if (web::rebootRequested()) {
        Serial.println("[sys] settings saved, restarting");
        delay(400);
        ESP.restart();
    }
}
