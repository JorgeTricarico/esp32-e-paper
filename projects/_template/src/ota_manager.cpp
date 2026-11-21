#include "ota_manager.h"

uint16_t calcNextCheckinMinutes(uint16_t quietCount) {
    if (quietCount < 6) {
        return 10;
    }
    if (quietCount >= OTA_STABLE_QUIET_LIMIT) {
        return 0;
    }
    uint32_t mins = 20;
    uint16_t expSteps = quietCount - 6;
    for (uint16_t i = 0; i < expSteps; i++) {
        mins *= 2;
        if (mins >= 720) return 720;
    }
    return (uint16_t)(mins > 720 ? 720 : mins);
}

#ifndef UNIT_TEST_HOST
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

static Preferences s_prefs;

static uint16_t readQuietCount() {
    s_prefs.begin("ota_base", true);
    uint16_t q = s_prefs.getUShort("ota_quiet", 0);
    s_prefs.end();
    return q;
}

void otaEnsureFirmwareEpoch() {
    String curMd5 = ESP.getSketchMD5();
    s_prefs.begin("ota_base", false);
    String savedMd5 = s_prefs.getString("ota_md5", "");
    if (curMd5.length() > 0 && savedMd5 != curMd5) {
        s_prefs.putString("ota_md5", curMd5);
        s_prefs.putUShort("ota_quiet", 0);
    }
    s_prefs.end();
}

uint16_t otaGetNextCheckinMinutes() {
    return calcNextCheckinMinutes(readQuietCount());
}

void otaResetQuietCounter() {
    s_prefs.begin("ota_base", false);
    s_prefs.putUShort("ota_quiet", 0);
    s_prefs.end();
}

void otaIncrementQuietCounter() {
    uint16_t q = readQuietCount();
    if (q < OTA_STABLE_QUIET_LIMIT) {
        q++;
        s_prefs.begin("ota_base", false);
        s_prefs.putUShort("ota_quiet", q);
        s_prefs.end();
    }
}

static bool connectKnownWifi(uint32_t timeoutMs = 6000) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFiMulti wifiMulti;
    wifiMulti.addAP("TeleCentro-5618", "RMZ5GDZKRDM3");
    wifiMulti.addAP("Telecentro-d1fc", "MMLNTJFTY2W5");
    wifiMulti.addAP("Jorge-Moto_G32", "asdfghjk");
    wifiMulti.addAP("MOVISTAR-WIFI6-0E70", "Lacarra3424");
    wifiMulti.addAP("Jorge_Moto_G32", "asdfghjk");

    uint32_t t0 = millis();
    while ((millis() - t0) < timeoutMs) {
        if (wifiMulti.run(2500) == WL_CONNECTED) return true;
        delay(100);
    }
    return (WiFi.status() == WL_CONNECTED);
}

static bool probeOtaServer(uint32_t waitMs = 1100) {
    WiFiUDP udp;
    if (!udp.begin(OTA_UDP_DISCOVERY_PORT)) return false;

    IPAddress bcastGlobal(255, 255, 255, 255);
    IPAddress bcastSubnet = WiFi.broadcastIP();
    String hello = String("ESP32_OTA_CHECK ") + WiFi.localIP().toString();

    for (int attempt = 0; attempt < 2; attempt++) {
        udp.beginPacket(bcastSubnet, OTA_UDP_DISCOVERY_PORT);
        udp.write((const uint8_t *)hello.c_str(), hello.length());
        udp.endPacket();

        udp.beginPacket(bcastGlobal, OTA_UDP_DISCOVERY_PORT);
        udp.write((const uint8_t *)hello.c_str(), hello.length());
        udp.endPacket();

        uint32_t t0 = millis();
        while ((millis() - t0) < (waitMs / 2)) {
            int pktSize = udp.parsePacket();
            if (pktSize > 0) {
                char buf[64];
                int len = udp.read(buf, sizeof(buf) - 1);
                if (len > 0) {
                    buf[len] = '\0';
                    if (strncmp(buf, "OTA_READY", 9) == 0) {
                        udp.stop();
                        return true;
                    }
                }
            }
            delay(20);
        }
    }
    udp.stop();
    return false;
}

void otaRunCheckinWindow() {
    if (!connectKnownWifi(6000)) {
        otaIncrementQuietCounter();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    if (!probeOtaServer(1100)) {
        otaIncrementQuietCounter();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    otaResetQuietCounter();
    ArduinoOTA.setHostname("esp32-paper");
    ArduinoOTA.onEnd([]() { otaResetQuietCounter(); });
    ArduinoOTA.begin();

    uint32_t t0 = millis();
    while ((millis() - t0) < OTA_WAIT_WINDOW_MS) {
        ArduinoOTA.handle();
        delay(10);
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}
#endif
