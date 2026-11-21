#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"

struct WifiCredential {
    char ssid[64];
    char pass[64];
};

#define MAX_WIFI_NETWORKS 10

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // Configuration Management
    void loadConfig();
    void saveConfig();
    AppConfig& getConfig() { return config; }

    // Multi-network dynamic management (N networks without destructive overwrite)
    int getNetworkCount() const { return networkCount; }
    const WifiCredential* getNetwork(int index) const;
    bool addOrUpdateNetwork(const char *ssid, const char *pass, bool persist = true);
    bool removeNetwork(const char *ssid);
    void loadNetworks();
    void saveNetworks();

    // Connection Routines with RSSI roaming
    bool begin();
    bool connectKnownNetworks(uint32_t timeoutMs = 15000);
    bool isConnected();

    // Rescue Captive Portal & STA Dashboard
    void startCaptivePortal();
    void startWebServerSTA();
    void handlePortal();
    bool isPortalActive() const { return portalActive; }

    // Guardia autónoma: check-in periódico + hold post-OTA
    // Backoff adaptativo: tras actividad (OTA/hold) despierta cada 5 min; cada
    // ciclo sin novedades duplica hasta el tope checkin_min. 0 = desactivado.
    void enterCheckinMode(uint16_t windowS);
    bool inCheckinMode() const { return checkinMode; }
    bool checkinExpired() const;
    void beginStayAwake(uint16_t minutes); // hold que sobrevive al reboot post-OTA
    uint32_t stayAwakeLeftMin();           // minutos restantes de hold (0 = libre)
    void persistHoldForSleep();            // guarda hold restante antes de dormir
    void noteActivity();                   // hubo flash/hold: vuelve a intervalo rápido
    void noteQuietCycle();                 // ciclo sin novedades: alarga el próximo
    uint16_t nextCheckinMin() const;       // intervalo efectivo para el próximo sleep
    uint16_t quietCount() const { return checkinQuiet; }

    // Pull-OTA: si hay firmware staged en el Pi3 (scp via SSH) con MD5 distinto
    // al sketch en ejecucion, lo descarga y flashea solo (LAN o funnel).
    // Se invoca en cada boot con WiFi. Devuelve 0=nada, 1=flasheado (reinicia), 2=error.
    int pollStagedFirmware();

private:
    AppConfig config;
    Preferences prefs;
    WiFiMulti wifiMulti;
    DNSServer dnsServer;
    WebServer *server;
    bool portalActive;
    WifiCredential networks[MAX_WIFI_NETWORKS];
    int networkCount = 0;

    void setupWebServer();
    void handleRoot();
    void handleSave();
    void handleSavePrompt();
    void handleSkills();
    void handleApiChat();
    void handleApiSay();
    void handleApiStory();
    void handleApiSensor();
    void handleApiAction();
    void handleApiLogs();
    void handleApiLogsClear();
    void handleApiSelftest();
    void handleApiAudioProbe();
    void handleApiConfigBackup();
    void handleApiConfigRestore();
    void configToJson(JsonDocument &doc);
    int configFromJson(const JsonObjectConst &o);
    void writeConfigBackupFile();
    void handleApiOtaHold();
    void handleApiBitmap();
    void handleApiScreen();
    void handleApiBankList();
    void handleBankDone();
    void handleBankUpload();

  private:
    bool checkinMode = false;
    uint32_t holdUntilMs = 0;
    uint32_t checkinUntilMs = 0;
    uint16_t checkinQuiet = 0; // ciclos seguidos sin actividad (backoff)
    void handleScan();
    void handleOtaUpdate();
    void handleOtaUpload();
    void handleNotFound();
};

extern NetworkManager netManager;

#endif // WIFI_PORTAL_H
