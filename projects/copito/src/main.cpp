#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "epaper_driver.h"
#include "audio_es8311.h"
#include "wifi_portal.h"
#include "ai_engine.h"
#include "story_cascade.h"
#include "sensor_shtc3.h"
#include "copito_tools.h"
#include "copito_memory.h"
#include "text_paginator.h"
#include "copito_log.h"
#include "battery_monitor.h"
#include "rtc_pcf85063.h"
#include "voice_bank.h"
#include <Preferences.h>
#include <esp_system.h>

static const char *TAG = "MAIN";

// Paginador de texto para respuestas largas
TextPaginator paginator;

// Global Assistant State
static AssistantState currentState = STATE_BOOT;
static unsigned long lastInteractionTime = 0;
static unsigned long buttonPressStartTime = 0;
static bool buttonIsPressed = false;

// Resetea el timeout de sleep (lo llaman tareas largas: musica, timer).
void copitoPokeActivity() { lastInteractionTime = millis(); }

// USB de datos a la PC: operator bool() de USBCDC = enumerado por el host.
// OJO: con solo cargador (sin datos) da false aunque haya 5V (eso lo dice
// batteryMonitor.isCharging). Así se distingue "conectó bien a la PC".
bool copitoUsbDataConnected() { return (bool)Serial; }

// Mutex recursivo para el bus I2C compartido (ES8311, SHTC3, PCF85063)
static SemaphoreHandle_t s_i2cMutex = nullptr;
bool copitoI2cLock(uint32_t timeoutMs) {
    if (!s_i2cMutex) {
        s_i2cMutex = xSemaphoreCreateRecursiveMutex();
    }
    if (!s_i2cMutex) return true;
    return xSemaphoreTakeRecursive(s_i2cMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
void copitoI2cUnlock() {
    if (s_i2cMutex) xSemaphoreGiveRecursive(s_i2cMutex);
}

// Forward Declarations
void changeState(AssistantState newState);
bool copitoVoiceBusy(); // voz/música/cuento en curso (guardia pre-flash y hold)
void processVoiceInteraction();
void triggerVoiceInteraction();
void checkButtonEvents();
void enterSleepMode(bool manual = false);
void lipSyncCallback(bool mouthOpen);
static void echoSpeakTask(void *pv);

// FreeRTOS Dedicated Voice Interaction Task (48 KB Stack to eliminate stack canary watchpoint)
static TaskHandle_t voiceTaskHandle = NULL;
static volatile bool voiceTaskRunning = false;

// State & Power Tracking
static const uint32_t INACTIVITY_SLEEP_TIMEOUT_MS = 120000; // 2 minutes auto-sleep
static bool showingHappyFace = false;
// Pantalla fija: el texto/dibujo de la respuesta queda hasta hablar de nuevo (e-paper gasta 0)
bool g_textHold = false;
// Carga del cuento en cascada: mientras esta en true el loop() anima el timer
// pixel a pixel (parcial). Lo prende quien genera la parte 1, lo apaga al mostrarla.
bool g_storyLoading = false;
// Eco staged ("dime X"): frase guardada lista en pantalla; BOOT la dice.
// No se habla solo, no es hora, no va a la IA. "" = nada pendiente.
String g_echoPending = "";
// Cancelación total (POWER): corta grabación, etapas pendientes (transcribe/IA),
// TTS, música y cuento, en silencio. Lo miran: el callback de grabación, las
// etapas de processVoiceInteraction, streamTtsToSpeaker (vía copitoVoiceCancelled)
// y los loops de audio/música (vía g_ttsStopRequested/g_musicStopRequested).
// Va acá arriba porque checkButtonEvents (botones) se define antes que la voz.
static volatile bool g_voiceCancel = false;
bool copitoVoiceCancelled() { return g_voiceCancel; }
static void requestFullCancel(const char *why) {
    g_voiceCancel = true;
    g_ttsStopRequested = true;
    g_musicStopRequested = true;
    g_echoPending = ""; // POWER también descarta el eco staged
    storyRequestExit(); // no-op si no hay cuento activo
    CLOGI(TAG, "Cancelación total por %s (POWER)", why);
}
// Cara completa pendiente: la pide quien mostró texto/dibujo parcial y una
// cara debe taparlo después. Sin esto la cara parcial dibuja sobre el
// fantasma del texto y queda pálida/mezclada ("flujos que vuelven al inicio").
static bool g_faceNeedsFull = false;
// Hold con vencimiento: el texto fijado se libera solo a los 3 min y vuelve
// la cara (full). Sin esto el equipo quedaba despierto para siempre tras cada
// respuesta (el sleep está gated por !g_textHold) = batería + pantalla fija.
static unsigned long s_holdStartMs = 0;
static bool s_holdPrev = false;
static const unsigned long TEXT_HOLD_TIMEOUT_MS = 180000;
static int g_lastClockMin = -1;
// Diagnóstico de boot (visible en /status y /api/selftest): causa del último
// reset + contador persistente. BROWNOUT/PANIC/WDT en subida = pista directa
// cuando "se tildó / se apagó solo / no despierta".
uint32_t g_bootCount = 0;
const char *g_resetReason = "?";

void setup() {
    // 1. Native USB CDC Serial Initialization
    Serial.begin(115200);
    delay(500);
    copitoLog.begin(); // bitácora en RAM + recarga /logs.txt ANTES del primer log
    CLOGI(TAG, "boot: bitacora web lista (%u entradas previas)", copitoLog.count());
    CLOGI(TAG, "\n==========================================");
    CLOGI(TAG, "   KRAKEN AI - ROBOT COMPANION");
    CLOGI(TAG, "   Placa: Waveshare ESP32-S3-ePaper-1.54");
    CLOGI(TAG, "==========================================");

    // 1b. Diagnóstico de boot: causa de reset + contador persistente en NVS.
    {
        esp_reset_reason_t rr = esp_reset_reason();
        switch (rr) {
            case ESP_RST_POWERON:   g_resetReason = "POWERON"; break;
            case ESP_RST_SW:        g_resetReason = "SW"; break;
            case ESP_RST_PANIC:     g_resetReason = "PANIC"; break;
            case ESP_RST_INT_WDT:   g_resetReason = "INT_WDT"; break;
            case ESP_RST_TASK_WDT:  g_resetReason = "TASK_WDT"; break;
            case ESP_RST_WDT:       g_resetReason = "WDT"; break;
            case ESP_RST_DEEPSLEEP: g_resetReason = "DEEPSLEEP"; break;
            case ESP_RST_BROWNOUT:  g_resetReason = "BROWNOUT"; break;
            case ESP_RST_SDIO:      g_resetReason = "SDIO"; break;
            default:                g_resetReason = "UNKNOWN"; break;
        }
        Preferences bp;
        if (bp.begin("assistant", false)) {
            g_bootCount = bp.getUInt("boot_cnt", 0) + 1;
            bp.putUInt("boot_cnt", g_bootCount);
            bp.end();
        }
        if (rr == ESP_RST_BROWNOUT || rr == ESP_RST_PANIC ||
            rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) {
            CLOGW(TAG, "boot #%u tras reset SOSPECHOSO (%s): revisar alimentación/carga",
                  g_bootCount, g_resetReason);
        } else {
            CLOGI(TAG, "boot #%u (reset %s)", g_bootCount, g_resetReason);
        }
    }

    // 2. Hardware Button & LED Setup (BOOT = GPIO 0, PWR = GPIO 18, LED = GPIO 3)
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
    pinMode(PIN_PWR_BUTTON, INPUT_PULLUP);
    gpio_reset_pin((gpio_num_t)PIN_USER_LED);
    pinMode(PIN_USER_LED, OUTPUT);
    digitalWrite(PIN_USER_LED, USER_LED_ON);
    delay(100);
    digitalWrite(PIN_USER_LED, USER_LED_OFF);

    // 3. Enable Board Peripheral Power Rail (GPIO 17 = HIGH)
    pinMode(PIN_MAIN_PWR, OUTPUT);
    digitalWrite(PIN_MAIN_PWR, MAIN_PWR_ON);
    delay(50); // Power rail stabilization delay

    // Check Wakeup Cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool wokeFromSleep = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wakeup_reason == ESP_SLEEP_WAKEUP_EXT1);
    bool wokeByTimer = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);

    // 3.5 Network Configuration & Load Config (NVS / LittleFS)
    netManager.begin();

    // Hold post-OTA: si quedó minutes pendientes en NVS (p.ej. se flasheó por
    // WiFi y la placa reinició), se rearma el deadline desde ahora.
    {
        uint16_t holdMin = netManager.getConfig().ota_hold_min;
        if (holdMin > 0) {
            netManager.beginStayAwake(holdMin);
            CLOGI(TAG, "OTA hold activo: despierto por %u min (sobrevive al reboot)", holdMin);
        }
    }

    // 4. E-Paper Display Setup (Active LOW power gate on GPIO 6)
    if (!epd.begin()) {
        CLOGE(TAG, "Fallo al inicializar pantalla E-Ink!");
    } else {
        epd.showFace(FACE_IDLE, 0, true);
    }

    // 5. Audio Engine & ES8311 Setup (Active LOW gate on GPIO 42, PA on GPIO 46)
    if (!audio.begin(100)) {
        CLOGE(TAG, "Fallo al inicializar codec de audio ES8311!");
    } else {
        audio.setVolume(netManager.getConfig().volume);
        audio.setPlaybackSpeed(netManager.getConfig().tts_speed);
        if (!wokeFromSleep && !wokeByTimer) {
            audio.playStartupSound(); // Melodía alegre ascendente de encendido
        } else if (wokeFromSleep && !wokeByTimer) {
            audio.playWakeupSound();  // Pip-pip amigable al despertar
        }
        // Check-in por timer: silencioso (nadie lo está escuchando a las 3 AM)
    }

    // 5.4 Battery Monitor Setup
    batteryMonitor.begin();

    // 5.5 SHTC3 Ambient Sensor Setup (I2C @ 0x70)
    if (envSensor.begin()) {
        float initT = 0, initH = 0;
        envSensor.readSample(initT, initH);
    }

    // 5.5b PCF85063 RTC Setup (I2C @ 0x51) - Hora offline inmediata en 0 ms
    if (rtcDriver.begin()) {
        if (rtcDriver.syncSystemTimeFromRtc()) {
            CLOGI(TAG, "Hora del sistema inicializada offline desde RTC PCF85063");
        } else {
            CLOGW(TAG, "RTC PCF85063 detectado, pendiente de primera sincronizacion NTP");
        }
    }

    // 5.6 Copito long/short-term memory (NVS + PSRAM ring)
    copitoMem.begin();

    // Check if user is holding BOOT button on startup to force rescue portal
    if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
        CLOGI(TAG, "Boton BOOT presionado al arranque: Entrando en Modo Portal Cautivo");
        changeState(STATE_PORTAL_CONFIG);
        return;
    }

    // 7. Connect to Known WiFi Networks
    changeState(STATE_WIFI_CONNECTING);
    bool connected = netManager.connectKnownNetworks(12000);

    if (connected) {
        if (!wokeByTimer) audio.playSuccessSound(); // Trino alegre de WiFi conectado
        netManager.startWebServerSTA();
        CopitoTools::syncNtp(); // hora real para modo reloj y TopBar (no bloquea si falla)
        // Pull-OTA: si hay firmware staged en el Pi3, flashear solo (no retorna si flashea)
        netManager.pollStagedFirmware();
        if (wokeByTimer && netManager.getConfig().checkin_min > 0 && netManager.stayAwakeLeftMin() == 0) {
            // Check-in OTA: ventana WiFi breve para un posible flash, luego a dormir
            netManager.enterCheckinMode(netManager.getConfig().checkin_window_s);
            CLOGI(TAG, "Check-in OTA: ventana de %u s, luego a dormir",
                  netManager.getConfig().checkin_window_s);
        }
        changeState(STATE_IDLE);
    } else if (wokeByTimer) {
        // Check-in sin red: no hay nada que atender, directo a dormir (sin portal)
        CLOGW(TAG, "Check-in sin WiFi: volviendo a dormir");
        enterSleepMode();
    } else {
        CLOGW(TAG, "No se pudo conectar a ninguna red conocida. Iniciando Portal de Rescate...");
        audio.playErrorSound();
        changeState(STATE_PORTAL_CONFIG);
    }

    lastInteractionTime = millis();
}

void loop() {
    // 1. Handle Web Server (Captive Portal in AP mode, or Dashboard in STA mode)
    netManager.handlePortal();
    if (netManager.isPortalActive()) {
        checkButtonEvents();
        delay(5);
        return;
    }

    // 2. Monitor Hardware Button Actions
    checkButtonEvents();

    // 2b. Carga del cuento: timer pixel a pixel (PARCIAL, no barre la pantalla).
    // Corre en el loop (Core1) mientras la parte 1 se genera en la voiceTask.
    {
        static unsigned long s_storyTickMs = 0;
        static uint16_t s_storyStep = 0;
        if (g_storyLoading) {
            if (millis() - s_storyTickMs >= 260) {
                s_storyTickMs = millis();
                epd.updateStoryLoading(s_storyStep++);
            }
        } else {
            s_storyStep = 0;
        }
    }

    // 3. USB CDC Serial Commands
    if (Serial.available()) {
        char cmd = Serial.read();
        lastInteractionTime = millis();
        if (cmd == 't' || cmd == 'T') {
            CLOGI(TAG, "Comando Serial 't': Forzando interacción de voz...");
            triggerVoiceInteraction();
        } else if (cmd == 'r' || cmd == 'R') {
            CLOGI(TAG, "Comando Serial 'r': Refresco forzado (Copito IDLE)");
            epd.showFace(FACE_IDLE, 0, true);
        } else if (cmd == 'c' || cmd == 'C') {
            CLOGI(TAG, "Comando Serial 'c': Probando sonido de campana");
            audio.playChimeUp();
        } else if (cmd == 'p' || cmd == 'P') {
            CLOGI(TAG, "Comando Serial 'p': Activando portal cautivo");
            changeState(STATE_PORTAL_CONFIG);
        } else if (cmd == 's' || cmd == 'S') {
            CLOGI(TAG, "Comando Serial 's': Forzando modo reposo (Sleep)");
            enterSleepMode();
        } else if (cmd == 'h' || cmd == 'H' || cmd == 'g' || cmd == 'G') {
            CLOGI(TAG, "Comando Serial 'h': Mostrando cara feliz");
            epd.showFace(FACE_HAPPY, 0, true);
            audio.playSuccessSound();
        } else if (cmd == 'f' || cmd == 'F') {
            CLOGI(TAG, "Comando Serial 'f': Test de Fallback Dibujo + Texto");
            epd.showFallbackText(1, "SOY KRAKEN. HABLA FUERTE Y CLARO.", true);
        } else if (cmd == 'u' || cmd == 'U') {
            CLOGI(TAG, "Comando Serial 'u': Modo Conectado a PC");
            epd.showFace(FACE_USB_PC, 0, true);
        }
    }

    // 4. Periodic SHTC3 Ambient Sensor Refresh (every 20s; en reloj cada
    // 5 min: el footer usa caché y el sensor en sleep gasta ~0 entre lecturas)
    static unsigned long lastSensorRead = 0;
    unsigned long sensorIntervalMs = (currentState == STATE_CLOCK) ? 300000UL : 20000UL;
    if (millis() - lastSensorRead > sensorIntervalMs) {
        lastSensorRead = millis();
        float t = 0.0f, h = 0.0f;
        envSensor.readSample(t, h);
    }

    // 4a. Periodic NTP resync check (cada 5s hasta sincronizar, luego cada 24h:
    // el RTC PCF85063 aguanta la deriva entre syncs sin gasto de radio)
    static unsigned long lastNtpCheck = 0;
    static unsigned long ntpInterval = 5000;
    if (netManager.isConnected() && (millis() - lastNtpCheck > ntpInterval)) {
        lastNtpCheck = millis();
        if (!g_ntpSynced) {
            CopitoTools::syncNtp();
            ntpInterval = g_ntpSynced ? 86400000UL : 5000;
        } else {
            ntpInterval = 86400000UL;
        }
    }

    // 4a-bis. Testigo USB de datos: flanco de copitoUsbDataConnected().
    // Al enchufar a la PC se avisa en pantalla (IDLE) y en bitácora; al
    // desenchufar se restaura la cara. En reloj/clima no se pisa la pantalla:
    // el header "USB " se actualiza solo en el próximo minuto (footerChanged).
    {
        static bool prevUsb = false;
        static bool usbInit = false;
        bool usb = copitoUsbDataConnected();
        if (!usbInit) { prevUsb = usb; usbInit = true; }
        else if (usb != prevUsb) {
            prevUsb = usb;
            CLOGI(TAG, usb ? "USB datos CONECTADO a PC (tridente en TopBar)"
                           : "USB datos desconectado (quedo solo carga/bateria)");
            if (currentState == STATE_IDLE && !voiceTaskRunning && !g_textHold && !g_storyLoading) {
                // showFace es async por Core 0: no corta audio ni bloquea.
                epd.showFace(usb ? FACE_USB_PC : FACE_IDLE, 0, !usb);
            }
        }
    }

    // 4b. Manejo del Modo Reloj de bajo consumo:
    // - Solo se toca la pantalla cuando cambia el minuto (alineado al segundo
    //   00) y solo los dígitos que cambian (ventana parcial, no full-screen).
    // - Sensores/batería/fecha con caché: si no cambian, no se re-renderizan.
    // - La hora se consulta como máximo 1 vez/s y cada 4 s lejos del cambio.
    if (currentState == STATE_CLOCK) {
        static unsigned long lastFullClockRefresh = 0;
        static unsigned long lastClockPoll = 0;
        static int shownHour = -1, shownMin = -1;
        static char shownDate[32] = "";
        static float shownTemp = -999.0f;
        static float shownHum = -999.0f;
        static int shownBatt = -1;
        static unsigned long lastClockBattRead = 0;
        static int clockPartialCount = 0;
        static int lastTmSec = -1;
        // BOOT con respuesta inmediata (poll a ~10 Hz sin consultar la hora)
        if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
            CLOGI(TAG, "Saliendo de Modo Reloj por pulsacion de boton BOOT");
            shownMin = -1; shownHour = -1; shownDate[0] = '\0';
            shownTemp = -999.0f; shownHum = -999.0f; shownBatt = -1;
            lastTmSec = -1; clockPartialCount = 0;
            audio.playChimeUp(); // powerOn() interno restaura el PA
            changeState(STATE_IDLE);
            delay(300);
            return;
        }
        // Lejos del cambio de minuto no vale la pena consultar la hora
        // (getLocalTime + posible I2C al RTC). Cerca (<10 s) se afina a 1 s.
        // Contrato eco-leve: hora >= 1000 ms entre lecturas, BOOT a 10 Hz.
        unsigned long pollInterval = 1000;
        if (lastTmSec >= 0) {
            unsigned long elapsed = (millis() - lastClockPoll) / 1000;
            int estSec = lastTmSec + (int)elapsed;
            if (estSec < 50) pollInterval = 4000; // faltan >10 s: 0.25 Hz basta
        }
        if (millis() - lastClockPoll < pollInterval) { delay(100); return; }
        lastClockPoll = millis();
        struct tm tm;
        bool hasTime = getLocalTime(&tm, 20);
        if (!hasTime) {
            hasTime = rtcDriver.readTime(tm);
        }
        if (!hasTime) { delay(100); return; }
        lastTmSec = tm.tm_sec;

        if (tm.tm_min != g_lastClockMin || shownMin < 0) {
            g_lastClockMin = tm.tm_min;
            const char *dias[] = {"DOMINGO", "LUNES", "MARTES", "MIERCOLES", "JUEVES", "VIERNES", "SABADO"};
            const char *meses[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
            int wday = (tm.tm_wday >= 0 && tm.tm_wday < 7) ? tm.tm_wday : 0;
            int mon  = (tm.tm_mon  >= 0 && tm.tm_mon  < 12) ? tm.tm_mon  : 0;
            char dateStr[32];
            snprintf(dateStr, sizeof(dateStr), "%s, %02d %s", dias[wday], tm.tm_mday, meses[mon]);
            // Testigo USB en modo reloj (no tiene TopBar): "USB " delante de
            // la fecha. Entra en la caché/comparación, así que enchufar o
            // desenchufar refresca el header solo (footerChanged).
            if (copitoUsbDataConnected()) {
                char withUsb[32];
                snprintf(withUsb, sizeof(withUsb), "USB %s", dateStr);
                strncpy(dateStr, withUsb, sizeof(dateStr) - 1);
                dateStr[sizeof(dateStr) - 1] = '\0';
            }

            // Primera pasada tras entrar (el FULL ya lo hizo changeState):
            // solo se inicializa la caché, sin re-render.
            if (shownMin < 0) {
                shownHour = tm.tm_hour; shownMin = tm.tm_min;
                strncpy(shownDate, dateStr, sizeof(shownDate) - 1);
                shownDate[sizeof(shownDate) - 1] = '\0';
                shownTemp = g_ambientSensorValid ? g_ambientTempC : -99.0f;
                shownHum = g_ambientSensorValid ? g_ambientHum : -1.0f;
                shownBatt = batteryMonitor.getStatus().percentage;
                lastClockBattRead = millis();
                lastFullClockRefresh = millis();
                clockPartialCount = 0;
                delay(100);
                return;
            }

            float newTemp = g_ambientSensorValid ? g_ambientTempC : -99.0f;
            float newHum = g_ambientSensorValid ? g_ambientHum : -1.0f;
            // Batería: lectura cara (8x ADC + filtro) como máximo cada 10 min
            // o si pasó mucho desde la última; el % solo se muestra si cambia.
            int newBatt = shownBatt;
            if (shownBatt < 0 || millis() - lastClockBattRead > 600000UL) {
                newBatt = batteryMonitor.getStatus().percentage;
                lastClockBattRead = millis();
            }
            bool dateChanged = (strcmp(dateStr, shownDate) != 0);
            bool tempChanged = (fabsf(newTemp - shownTemp) >= 0.5f);
            bool humChanged = (fabsf(newHum - shownHum) >= 5.0f);
            bool battChanged = (newBatt != shownBatt && shownBatt >= 0) ||
                               (shownBatt < 0);
            // Cambios chicos de temp/hum se acumulan en caché sin refrescar
            // el footer (evita un full-screen parcial por 0.1 C de deriva).
            bool footerChanged = dateChanged || battChanged;
            if (tempChanged || humChanged) {
                // Solo refrescar footer si el salto es visible (>=1 C / >=10%)
                if (fabsf(newTemp - shownTemp) >= 1.0f ||
                    fabsf(newHum - shownHum) >= 10.0f || shownTemp < -40.0f) {
                    footerChanged = true;
                }
                shownTemp = newTemp;
                shownHum = newHum;
            }

            bool forceFull = (millis() - lastFullClockRefresh > 900000UL); // 15 min anti-ghosting
            if (forceFull) {
                lastFullClockRefresh = millis();
                clockPartialCount = 0;
            } else if (++clockPartialCount >= 20) {
                // La ventana ensucia mucho menos que el partial completo:
                // el FULL de limpieza puede espaciarse al doble (20 min).
                forceFull = true;
                lastFullClockRefresh = millis();
                clockPartialCount = 0;
            }

            bool ok = false;
            if (forceFull || footerChanged) {
                if (footerChanged) {
                    strncpy(shownDate, dateStr, sizeof(shownDate) - 1);
                    shownDate[sizeof(shownDate) - 1] = '\0';
                    shownBatt = newBatt;
                }
                epd.showClockFace(tm.tm_hour, tm.tm_min, newTemp, newHum,
                                  newBatt >= 0 ? newBatt : batteryMonitor.getStatus().percentage,
                                  dateStr, forceFull);
                ok = true;
            } else {
                // Caso común (1 dígito/min): solo la ventana de dígitos.
                ok = epd.showClockMinutesWindowed(shownHour, shownMin,
                                                  tm.tm_hour, tm.tm_min);
                if (!ok) {
                    epd.showClockFace(tm.tm_hour, tm.tm_min, shownTemp, shownHum,
                                      shownBatt, shownDate, false);
                    ok = true;
                }
            }
            if (ok) { shownHour = tm.tm_hour; shownMin = tm.tm_min; }
        }
        delay(100);
        return;
    }

    // 4c. Manejo del Modo Clima Full-Screen (200x200 Weather Station)
    if (currentState == STATE_WEATHER) {
        if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
            CLOGI(TAG, "Saliendo de Modo Clima por pulsacion de boton BOOT");
            audio.playChimeUp();
            changeState(STATE_IDLE);
            delay(300);
            return;
        }

        static unsigned long lastWeatherUpdate = 0;
        static unsigned long lastFullWeatherRefresh = 0;
        if (millis() - lastWeatherUpdate > 30000UL) { // Actualiza cada 30 segundos
            lastWeatherUpdate = millis();
            struct tm tm;
            bool hasTime = getLocalTime(&tm, 20);
            if (!hasTime) {
                hasTime = rtcDriver.readTime(tm);
            }
            bool forceFull = (millis() - lastFullWeatherRefresh > 900000UL); // Cada 15 min full refresh
            if (forceFull) lastFullWeatherRefresh = millis();
            // Además: full cada 10 parciales seguidos (ver reloj: anti-lavado).
            static int weatherPartialCount = 0;
            if (!forceFull) {
                if (++weatherPartialCount >= 10) { forceFull = true; weatherPartialCount = 0; }
            } else {
                weatherPartialCount = 0;
            }

            int bPct = batteryMonitor.getStatus().percentage;

            epd.showWeatherScreen(hasTime ? tm.tm_hour : -1, hasTime ? tm.tm_min : -1,
                                  g_ambientSensorValid ? g_ambientTempC : -99.0f,
                                  g_ambientSensorValid ? g_ambientHum : -1.0f,
                                  bPct, nullptr, forceFull);
        }
        delay(100); // plan leve: el clima solo actúa cada 30 s (BOOT sigue a 10 Hz)
        return;
    }

    // 4d. Vencimiento del texto fijado: 3 min después se libera solo y vuelve
    // la cara (full) para que el equipo pueda dormir y no quede fijo.
    // (Hojear con BOOT rearma el timer.)
    // El cuento retiene su pantalla mientras narra (2026-09-09: un relato
    // largo superaba los 3 min y el idle pisaba el texto con la cara).
    if (g_textHold && !s_holdPrev) { s_holdPrev = true; s_holdStartMs = millis(); }
    if (!g_textHold) s_holdPrev = false;
    if (g_textHold && !voiceTaskRunning && !copitoVoiceBusy() && (millis() - s_holdStartMs > TEXT_HOLD_TIMEOUT_MS)) {
        CLOGI(TAG, "Texto fijado vencido (3 min): liberando y volviendo a cara idle");
        g_textHold = false;
        s_holdPrev = false;
        g_faceNeedsFull = true;
        changeState(STATE_IDLE);
    }

    // 5. Idle animations: natural micro-movements (glance left/right, subtle blink)
    // Pausadas mientras hay texto fijado en pantalla (g_textHold) para no borrarlo,
    // y con USB a la PC (la cara USB de aviso queda fija y gasta 0; al
    // desenchufar el watcher 4a-bis restaura IDLE con FULL).
    if (currentState == STATE_IDLE && !voiceTaskRunning && !g_textHold && !copitoUsbDataConnected()) {
        static unsigned long lastAnimTime = 0;
        static int animFrame = 0;
        if (millis() - lastAnimTime > 6000) {
            animFrame = (animFrame + 1) % 8;
            epd.showFace(FACE_IDLE, animFrame);
            lastAnimTime = millis();
        }

        // 5. Automatic Inactivity Sleep (configurable, 0=disabled)
        // Pausado con OTA hold activo o en ventana check-in (la guardia decide)
        uint32_t sleepMs = (uint32_t)netManager.getConfig().sleep_timeout_min * 60000UL;
        if (sleepMs > 0 && netManager.stayAwakeLeftMin() == 0 && !netManager.inCheckinMode()
            && (millis() - lastInteractionTime > sleepMs)) {
            CLOGI(TAG, "Inactividad detectada. Entrando en modo reposo (Deep Sleep)...");
            enterSleepMode();
        }
    }

    // 6. Guardia autónoma: fin de ventana check-in -> a dormir.
    // No interrumpe una interacción de voz en curso ni un hold activo.
    if (netManager.inCheckinMode() && !voiceTaskRunning
        && netManager.stayAwakeLeftMin() == 0 && netManager.checkinExpired()) {
        CLOGI(TAG, "Ventana check-in cumplida (%u s). Durmiendo hasta el proximo ciclo...",
              netManager.getConfig().checkin_window_s);
        enterSleepMode();
        return;
    }

    delay(20);
}

void enterSleepMode(bool manual) {
    CLOGI(TAG, "=== ENTRANDO EN MODO REPOSO (DEEP SLEEP, %s) ===",
          manual ? "MANUAL sin timer" : "AUTO con check-in");
    // Persistir hold restante (minutos) para que sobreviva al deep-sleep
    netManager.persistHoldForSleep();
    // Cortar actividad que impediría dormir limpio (la RAM se pierde igual,
    // pero evita dejar I2S/posteos/SPI a medias durante la melodía de bajada)
    requestFullCancel("sleep");
    g_storyLoading = false;
    unsigned long waitVoiceStart = millis();
    while (copitoVoiceBusy() && (millis() - waitVoiceStart < 1500)) {
        delay(20);
    }
    // 1. Play gentle descending sleep melody
    audio.playSleepSound();

    // 2. Show peaceful sleeping face with Zzz & battery status SYNCHRONOUSLY
    // Blocks until the full e-Paper waveform latch is 100% complete so no partial/black artifacts occur!
    epd.showFaceDirect(FACE_SLEEP, 0, true);
    
    // Safely command e-Paper controller into hardware deep sleep before turning off power rails
    epd.sleep();

    // 3. Power down all active peripheral circuits to reach microampere consumption
    digitalWrite(PIN_USER_LED, USER_LED_OFF);
    digitalWrite(PIN_AUDIO_PA, AUDIO_PA_OFF);
    digitalWrite(PIN_AUDIO_PWR, AUDIO_PWR_OFF);
    digitalWrite(PIN_EPD_PWR, EPD_PWR_OFF);
    digitalWrite(PIN_MAIN_PWR, MAIN_PWR_OFF);

    // 4. Configure Wake-up on BOOT button (GPIO 0 low level)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BOOT_BUTTON, 0);

    if (manual) {
        // Dormido manual por PWR (noche): gasto cero. Radio apagada del todo
        // y SIN timer: solo BOOT despierta. Vale desde cualquier estado
        // (incluido STATE_CLOCK: checkButtonEvents lo atiende antes del bloque
        // de reloj, así que el hold de 2 s llega igual).
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    } else {
        // 4b. Check-in periódico con backoff adaptativo: sin novedades se alarga,
        // con actividad reciente despierta rápido (5 min) por si hay más flashes.
        netManager.noteQuietCycle();
        uint16_t nextMin = netManager.nextCheckinMin();
        if (nextMin > 0) {
            esp_sleep_enable_timer_wakeup((uint64_t)nextMin * 60ULL * 1000000ULL);
        }
    }

    // 5. Enter ultra-low power deep sleep (E-Ink keeps image indefinitely at 0 uA!)
    esp_deep_sleep_start();
}

// === Plan leve de energía (ECO): radio según carga ===
// Reposo (idle/reloj/clima): modem-sleep WiFi. El portal, un hold, el
// check-in o una voz en curso mantienen radio full para no degradar latencia.
// Carga (TTS/streaming/OTA/música): radio full.
// NOTA 2026-09-09: el escalado de CPU a 80 MHz se retiró (colgaba el equipo:
// con OPI PSRAM el loop moría tras el cambio y solo sobrevivía el ping).
// Reevaluar con medidor USB y pruebas de estabilidad antes de reintentarlo.
void powerBoost() {
    WiFi.setSleep(false);
}

void powerRelax() {
    bool needFull = netManager.isPortalActive()
        || netManager.stayAwakeLeftMin() > 0
        || netManager.inCheckinMode()
        || voiceTaskRunning;
    WiFi.setSleep(needFull ? false : true);
}

// Guardia pre-flash (la usa /update y pull-OTA): true si hay voz/música en
// curso. Flashear con el I2S/posteando audio brickió el equipo el 2026-09-09.
// El cuento también bloquea: ilustra/narra durante minutos (aunque haya
// silencios entre partes, un flash a mitad rompe el relato).
bool copitoVoiceBusy() {
    return voiceTaskRunning || g_speaking || g_musicPlaying || storyCascadeActive();
}

void changeState(AssistantState newState) {
    currentState = newState;
    lastInteractionTime = millis();

    // Plan leve: perfil de energía según estado (ver powerBoost/powerRelax).
    if (newState == STATE_IDLE || newState == STATE_CLOCK || newState == STATE_WEATHER) {
        powerRelax();
    } else if (newState == STATE_SPEAKING) {
        powerBoost(); // streamTtsToSpeaker también lo asegura por sí mismo
    }

    // Turn ON LED while listening / recording, turn OFF in all other states
    if (currentState == STATE_LISTENING) {
        digitalWrite(PIN_USER_LED, USER_LED_ON);
    } else {
        digitalWrite(PIN_USER_LED, USER_LED_OFF);
    }

    switch (currentState) {
        case STATE_WIFI_CONNECTING:
            epd.showFace(FACE_WIFI_CONNECTING, 0, true);
            break;
        case STATE_IDLE:
            if (!g_textHold) {
                epd.showFace(FACE_IDLE, 0, g_faceNeedsFull);
                g_faceNeedsFull = false;
            }
            break;
        case STATE_LISTENING:
            // FULL siempre al empezar a escuchar: venimos de soltar el texto
            // fijado y el parcial dibujaría la cara sobre su fantasma.
            epd.showFace(FACE_LISTENING, 0, true);
            audio.playListeningSound(); // Tono amigable y claro para avisar que escucha
            break;
        case STATE_THINKING:
            epd.showFace(FACE_THINKING, 0);
            audio.playThinkingSound();  // Clicks suaves de procesamiento
            break;
        case STATE_SPEAKING:
            // Preservar en pantalla el texto de respuesta y los dibujos/iconos
            break;
        case STATE_ERROR:
            epd.showFace(FACE_ERROR, 0, true);
            audio.playErrorSound();     // Tono oh-oh suave
            break;
        case STATE_PORTAL_CONFIG:
            netManager.startCaptivePortal();
            epd.showFace(FACE_PORTAL, 0, true);
            audio.playTone(440, 70);
            delay(30);
            audio.playTone(659, 70);
            delay(30);
            audio.playTone(880, 100);
            break;
        case STATE_CLOCK: {
            CLOGI(TAG, "=== ENTRANDO EN MODO RELOJ FIJO ===");
            // Gating: el PA del altavoz es el mayor consumidor en reposo; el
            // codec queda alimentado (sus registros se perderían con corte de
            // raíl y el próximo chime sonaría mal). playChimeUp() lo restaura.
            digitalWrite(PIN_USER_LED, USER_LED_OFF);
            digitalWrite(PIN_AUDIO_PA, AUDIO_PA_OFF);
            struct tm tm;
            bool hasTime = getLocalTime(&tm, 500);
            if (!hasTime) {
                hasTime = rtcDriver.readTime(tm);
            }
            char dateStr[32] = "KRAKEN RELOJ";
            if (hasTime) {
                g_lastClockMin = tm.tm_min;
                const char *dias[] = {"DOMINGO", "LUNES", "MARTES", "MIERCOLES", "JUEVES", "VIERNES", "SABADO"};
                const char *meses[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
                int wday = (tm.tm_wday >= 0 && tm.tm_wday < 7) ? tm.tm_wday : 0;
                int mon  = (tm.tm_mon  >= 0 && tm.tm_mon  < 12) ? tm.tm_mon  : 0;
                snprintf(dateStr, sizeof(dateStr), "%s, %02d %s", dias[wday], tm.tm_mday, meses[mon]);
                // Testigo USB en el FULL inicial del reloj (ver bloque loop).
                if (copitoUsbDataConnected()) {
                    char withUsb[32];
                    snprintf(withUsb, sizeof(withUsb), "USB %s", dateStr);
                    strncpy(dateStr, withUsb, sizeof(dateStr) - 1);
                    dateStr[sizeof(dateStr) - 1] = '\0';
                }
            } else {
                g_lastClockMin = -1;
            }
            int bPct = batteryMonitor.getStatus().percentage;
            epd.showClockFace(hasTime ? tm.tm_hour : 0, hasTime ? tm.tm_min : 0,
                              g_ambientSensorValid ? g_ambientTempC : -99.0f,
                              g_ambientSensorValid ? g_ambientHum : -1.0f,
                              bPct, dateStr, true);
            break;
        }
        case STATE_WEATHER: {
            CLOGI(TAG, "=== ENTRANDO EN MODO CLIMA FULL-SCREEN ===");
            struct tm tm;
            bool hasTime = getLocalTime(&tm, 500);
            if (!hasTime) {
                hasTime = rtcDriver.readTime(tm);
            }
            int bPct = batteryMonitor.getStatus().percentage;
            epd.showWeatherScreen(hasTime ? tm.tm_hour : -1, hasTime ? tm.tm_min : -1,
                              g_ambientSensorValid ? g_ambientTempC : -99.0f,
                              g_ambientSensorValid ? g_ambientHum : -1.0f,
                              bPct, nullptr, true);
            break;
        }
        default:
            break;
    }
}

// Volumen durante musica (botones): cambia, guarda y lo muestra en pantalla.
static void musicBumpVolume(int delta) {
    AppConfig &cfg = netManager.getConfig();
    int v = (int)cfg.volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    audio.setVolume((uint8_t)v);
    cfg.volume = (uint8_t)v;
    Preferences pr;
    pr.begin("assistant", false);
    pr.putUChar("volume", (uint8_t)v);
    pr.end();
    char st[16];
    snprintf(st, sizeof st, "Vol %d%%", v);
    const char *t = g_musicEsp32Title.length() > 0 ? g_musicEsp32Title.c_str() : "Musica";
    epd.showMusicScreen(t, st);
    CLOGI(TAG, "Musica: volumen %d%% (boton)", v);
}

void checkButtonEvents() {
    // 1. Check BOOT Button (GPIO 0)
    bool rawBoot = (digitalRead(PIN_BOOT_BUTTON) == LOW);

    if (rawBoot && !buttonIsPressed) {
        buttonIsPressed = true;
        buttonPressStartTime = millis();
    } else if (!rawBoot && buttonIsPressed) {
        buttonIsPressed = false;
        unsigned long duration = millis() - buttonPressStartTime;
        lastInteractionTime = millis();

        // MODO MUSICA: BOOT click = subir volumen, BOOT mantenido 1.5s = frenar.
        // (Sin chime de confirmacion: se mezclaria con el tema.)
        if (g_musicPlaying) {
            if (duration >= 1500) {
                CLOGI(TAG, "Musica: stop por BOOT mantenido");
                g_musicStopRequested = true;
            } else if (duration > 50) {
                musicBumpVolume(+10);
            }
            return;
        }

        // MODO CUENTO EN CASCADA: BOOT = siguiente parte (corta la voz y avanza
        // a la imagen que sigue; al aparecer empieza a contarla).
        if (storyCascadeActive()) {
            if (duration > 50) {
                CLOGI(TAG, "Cuento: BOOT avanza a la siguiente parte");
                storyRequestAdvance();
            }
            return;
        }

        // HABLANDO: BOOT toque = callar (no superponer nada: el chime o una
        // voz nueva sobre el TTS lo relentizaba y deformaba el tono).
        // La tarea de voz sigue ocupada: otro toque para hablar de nuevo.
        if (g_speaking) {
            if (duration > 50) {
                CLOGI(TAG, "Habla interrumpida por BOOT: callando");
                g_ttsStopRequested = true;
            }
            return;
        }

        // ECO STAGED: BOOT dice la frase guardada ("dime X" no habla solo).
        // Va antes que paginación/portal: el eco pendiente manda.
        if (g_echoPending.length() > 0) {
            if (duration > 50 && !voiceTaskRunning) {
                String *sp = new String(g_echoPending);
                g_echoPending = "";
                if (sp) {
                    CLOGI(TAG, "Eco por BOOT (%u letras)", (unsigned)sp->length());
                    if (xTaskCreatePinnedToCore(echoSpeakTask, "echoSay", 20480, sp, 2, NULL, 1) != pdPASS) {
                        delete sp;
                        CLOGW(TAG, "Eco: no se pudo crear tarea");
                    }
                }
            }
            return;
        }

        if (duration >= BOOT_LONG_PRESS_MS) {
            // Long Press (> 4s) -> Force Rescue Portal
            CLOGI(TAG, "Boton sostenido > 4s: Activando Portal de Rescate");
            changeState(STATE_PORTAL_CONFIG);
        } else if (duration > 50) {
            // Click normal (50ms - 3999ms)
            if (currentState == STATE_PORTAL_CONFIG) {
                CLOGI(TAG, "Boton BOOT en Portal: Intentando conectar WiFi y salir a reposo");
                if (netManager.connectKnownNetworks(6000)) {
                    changeState(STATE_IDLE);
                    triggerVoiceInteraction();
                    return;
                }
            }
            if (currentState == STATE_CLOCK || currentState == STATE_WEATHER) {
                CLOGI(TAG, "Saliendo de Modo Fijo para iniciar interaccion por voz");
                changeState(STATE_IDLE);
            }

            // Paginación inteligente: si hay texto fijado con más de una página y NO es la última:
            // el botón avanza la página para que el usuario pueda seguir leyendo sin cortar a voz.
            if (g_textHold && paginator.getTotalPages() > 1 && !paginator.isLastPage()) {
                if (paginator.nextPage()) {
                    CLOGI(TAG, "Boton BOOT avanza a pagina %d/%d (lectura continuada)",
                          paginator.getCurrentPage() + 1, paginator.getTotalPages());
                    if (!g_speaking) audio.playChimeUp(); // nunca chime sobre la voz
                    epd.renderPaginatorPage(paginator, "KRAKEN AI", true);
                    s_holdStartMs = millis(); // seguir leyendo rearma el vencimiento
                    return; // No activar micrófono, seguir leyendo!
                }
            }

            // Si llegamos aquí: o no hay texto en hold o ya estamos en la última página.
            // Permitir hablar otra vez!
            g_textHold = false;
            if (!voiceTaskRunning) {
                CLOGI(TAG, "Boton BOOT presionado (%lu ms): iniciando interacción de voz", duration);
                triggerVoiceInteraction();
            } else {
                CLOGW(TAG, "Interaccion de voz ya en curso (voiceTaskRunning=true)");
            }
        }
    }

    // 2. Check PWR Button (GPIO 18) if pressed
    static unsigned long pwrPressStart = 0;
    static bool pwrPressed = false;
    bool rawPwr = (digitalRead(PIN_PWR_BUTTON) == LOW);

    if (rawPwr && !pwrPressed) {
        pwrPressed = true;
        pwrPressStart = millis();
    } else if (!rawPwr && pwrPressed) {
        pwrPressed = false;
        unsigned long dur = millis() - pwrPressStart;
        lastInteractionTime = millis();

        // MODO MUSICA: PWR click = bajar volumen, PWR mantenido 1.5s = frenar
        // (en vez de dormir: frenar primero, dormir despues si se quiere).
        if (g_musicPlaying) {
            if (dur >= 1500) {
                CLOGI(TAG, "Musica: stop por PWR mantenido");
                g_musicStopRequested = true;
            } else if (dur > 50) {
                musicBumpVolume(-10);
            }
            return;
        }

        // MODO CUENTO: 1er POWER frena la voz (queda en la imagen, pausado),
        // 2do POWER sale a 0. BOOT sigue avanzando partes. PWR largo sale.
        if (storyCascadeActive()) {
            if (dur >= 1500) {
                CLOGI(TAG, "Cuento: salida por PWR mantenido");
                storyRequestExit();
            } else if (dur > 50) {
                if (g_speaking) {
                    CLOGI(TAG, "Cuento: 1er POWER frena la voz (pausado)");
                    storyRequestPause();
                } else {
                    CLOGI(TAG, "Cuento: 2do POWER sale a 0");
                    storyRequestExit();
                }
            }
            return;
        }

        if (dur >= 2000) {
            // Hold PWR for 2s -> Sleep manual (noche): gasto cero, solo BOOT
            enterSleepMode(true);
        } else if (dur > 50) {
            // 1er POWER con algo activo (grabando/pensando/hablando/música) =
            // cancelar TODO en silencio. El 2do POWER (ya en reposo) vuelve a 0.
            if (voiceTaskRunning || g_speaking || g_musicPlaying) {
                requestFullCancel("voz/musica en curso");
                return;
            }
            // Toggle between Idle face and Super Happy Kraken Face
            showingHappyFace = !showingHappyFace;
            g_textHold = false; // el usuario pidio otra cara: soltar texto fijado
            audio.playSuccessSound();
            if (showingHappyFace) {
                epd.showFace(FACE_HAPPY, 0, true);
            } else {
                epd.showFace(FACE_IDLE, 0, true);
            }
        }
    }
}

void lipSyncCallback(bool mouthOpen) {
    if (currentState != STATE_SPEAKING) {
        currentState = STATE_SPEAKING;
    }
    // No-op: mantenemos en pantalla el texto de respuesta y los iconos de herramientas
}

void voiceInteractionTask(void *pvParameters) {
    voiceTaskRunning = true;
    processVoiceInteraction();
    voiceTaskRunning = false;
    voiceTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Habla del eco staged (BOOT): no bloquea el loop de botones/portal.
static void echoSpeakTask(void *pv) {
    String *sp = (String *)pv;
    if (sp && sp->length() > 0) {
        changeState(STATE_SPEAKING);
        aiEngine.streamTtsToSpeaker(*sp, nullptr);
        g_lastSpoken = *sp;
        copitoMem.addTurn("(eco BOOT)", *sp);
        changeState(STATE_IDLE);
        lastInteractionTime = millis();
    }
    if (sp) delete sp;
    vTaskDelete(NULL);
}

void triggerVoiceInteraction() {
    if (voiceTaskRunning) {
        CLOGW(TAG, "Interacción de voz ya en curso, ignorando nueva solicitud");
        return;
    }
    g_voiceCancel = false; // nueva interacción: limpia un cancel previo
    g_echoPending = ""; // grabar nuevo pisa el eco staged anterior
    g_textHold = false; // nueva charla: se libera la pantalla fija anterior
    g_faceNeedsFull = true; // la cara de LISTENING debe limpiar el fantasma del texto

    CLOGI(TAG, "Creando tarea FreeRTOS dedicada voiceTask (48 KB de stack en Core 1)...");
    BaseType_t res = xTaskCreatePinnedToCore(
        voiceInteractionTask,
        "voiceTask",
        49152,         // 48 KB Stack: 6x loopTask! Previene stack canary watchpoints permanentemente
        NULL,
        2,             // Prioridad 2 (superior a loopTask que corre en 1)
        &voiceTaskHandle,
        1              // Core 1
    );

    if (res != pdPASS) {
        CLOGW(TAG, "Fallo al crear voiceTask con 48KB, reintentando con 32KB...");
        res = xTaskCreatePinnedToCore(
            voiceInteractionTask,
            "voiceTask",
            32768,
            NULL,
            2,
            &voiceTaskHandle,
            1
        );
        if (res != pdPASS) {
            CLOGE(TAG, "Fallo critico al crear voiceTask!");
        }
    }
}

static bool g_buttonStopReleased = false;

static void resetButtonStopCheck() {
    g_buttonStopReleased = false;
}

static bool checkButtonStop() {
    if (digitalRead(PIN_BOOT_BUTTON) == HIGH) {
        g_buttonStopReleased = true;
    } else if (g_buttonStopReleased && digitalRead(PIN_BOOT_BUTTON) == LOW) {
        CLOGI(TAG, "2do click en BOOT detectado: fin manual de habla");
        return true;
    }
    // POWER durante la grabación = cancelar todo (BOOT-habla + POWER = basta).
    // Se marca el flag para que el flujo salga directo en silencio (sin nudge).
    if (digitalRead(PIN_PWR_BUTTON) == LOW) {
        CLOGI(TAG, "POWER durante grabación: cancelando");
        g_voiceCancel = true;
        return true;
    }
    return false;
}

// Eco staged ("dime X" solo repite): guarda la frase y la deja lista en
// pantalla; BOOT la dice. Sin hablar solo, sin hora, sin IA.
static bool stageEchoAndFinish(const String &userSaid, const String &phrase) {
    g_echoPending = phrase;
    String scr = phrase.length() > 90 ? phrase.substring(0, 90) : phrase;
    epd.showFallbackText(0, scr.c_str(), true);
    g_textHold = true;
    g_lastSpoken = phrase;
    copitoMem.addTurn(userSaid, phrase);
    changeState(STATE_IDLE);
    lastInteractionTime = millis();
    CLOGI(TAG, "Eco staged (%u letras): BOOT lo dice", (unsigned)phrase.length());
    return true;
}

void processVoiceInteraction() {
    lastInteractionTime = millis();

    if (!netManager.isConnected()) {
        // Guardia offline: sin gastar grabacion ni API. Portal sigue disponible con hold 4s.
        CLOGW(TAG, "Sin WiFi: respuesta local sin llamar a la IA.");
        if (!playBankPhrase("sin_internet")) audio.playErrorSound();
        epd.showFallbackText(0, "SIN INTERNET. REVISA TU WIFI.", true);
        delay(2500);
        g_faceNeedsFull = true; // el IDLE debe limpiar el texto del error
        changeState(STATE_IDLE);
        return;
    }

    CLOGI(TAG, "--- INICIANDO INTERACCIÓN POR VOZ INTELIGENTE ---");

    // 1. State: LISTENING (Rostro atento + Chime suave bajo)
    changeState(STATE_LISTENING);

    // 2. Intelligent Recording:
    //    - If no speech begins within 2.4s -> fast exit without calling AI
    //    - If user stops speaking for 1.2s -> automatic cutoff
    //    - If user clicks BOOT 2nd time -> immediate manual cutoff
    //    - Max duration: 8.5s
    resetButtonStopCheck();
    uint8_t *wavData = nullptr;
    size_t wavSize = 0;
    const AppConfig &cfg = netManager.getConfig();
    uint32_t maxRecMs       = (uint32_t)cfg.max_rec_seconds * 1000;
    uint32_t silenceStartMs = (cfg.silence_start_ms >= 1500) ? cfg.silence_start_ms : 3000;
    uint32_t silenceEndMs   = (cfg.silence_end_ms >= 1500) ? cfg.silence_end_ms : 2200;
    if (maxRecMs < 3000 || maxRecMs > 35000) maxRecMs = 20000;
    bool recOk = audio.recordWav(&wavData, &wavSize, maxRecMs, checkButtonStop, silenceStartMs, silenceEndMs);

    if (!recOk || wavSize < 1600) {
        if (g_voiceCancel) {
            // Cancel pedido por el usuario (POWER en grabación): salida directa
            // en silencio, sin nudge de "no te escuché" ni demoras.
            CLOGI(TAG, "Grabación cancelada por usuario: a reposo en silencio");
            audio.freeWavBuffer(wavData);
            changeState(STATE_IDLE);
            return;
        }
        // Guardia silencio: no hay voz transcribible -> NO llamar a la IA (ahorra API).
        // Nudge rotativo estilo Google VUI: 1ro leve, 2do con ejemplo. Sin repetir igual.
        static int s_silenceCount = 0;
        const char *nudge = (s_silenceCount % 2 == 0)
            ? "NO TE ESCUCHE. TOCA BOOT Y HABLA."
            : "HABLA CERCA DE MI. EJ: HOLA KRAKEN.";
        s_silenceCount++;
        CLOGI(TAG, "Sin voz detectada o cancelado por silencio inicial. Volviendo a reposo.");
        audio.freeWavBuffer(wavData);
        // Banco de voz primero (habla de verdad); chime solo si no hay archivos.
        if (!playBankPhrase((s_silenceCount % 2 == 1) ? "no_escucho1" : "no_escucho2")) {
            audio.playCancelSound(); // Muted soft chime
        }
        epd.showFallbackText(0, nudge, false); // parcial: rapido y sin parpadeo
        delay(2500);
        g_faceNeedsFull = true; // el IDLE debe limpiar el nudge parcial
        changeState(STATE_IDLE);
        return;
    }

    // 3. State: THINKING
    changeState(STATE_THINKING);

    // 3a. Pre-transcripcion para comandos locales SIN IA (musica Pi3 / ESP32, etc.).
    // Si el usuario pidio musica, se ejecuta directo y NO se llama a Gemini.
    String preTranscript;
    bool havePre = aiEngine.transcribeGroqWhisper(wavData, wavSize, preTranscript) && preTranscript.length() > 0;
    if (havePre) {
        // Eco staged ("dime X" solo repite, ni hora ni IA): se guarda y BOOT
        // lo dice. Va primero para que nada lo pise.
        String echoPhrase;
        if (aiEngine.extractEchoPhrase(preTranscript, echoPhrase)) {
            audio.freeWavBuffer(wavData);
            stageEchoAndFinish(preTranscript, echoPhrase);
            CLOGI(TAG, "--- ECO STAGED (sin IA) ---");
            return;
        }
        // 3a1. Comandos de SISTEMA sin IA (volumen, radio, hora, repeti, timer).
        // Van primero: son locales y prioritarios sobre musica/IA.
        String sysSpoken, sysScreen, sysUrl, sysTitle;
        if (aiEngine.tryLocalSystemCommand(preTranscript, sysSpoken, sysScreen, sysUrl, sysTitle)) {
            CLOGI(TAG, "Comando SISTEMA local: voz='%s' pantalla='%s' url='%s'",
                  sysSpoken.c_str(), sysScreen.c_str(), sysUrl.c_str());
            audio.freeWavBuffer(wavData);
            if (sysUrl.length() > 0) {
                // Radio en Copito: stremear directo (sin TTS previo).
                epd.showFallbackText(6, "Sintonizando...", true); // icono MUSICA
                g_textHold = true;
                changeState(STATE_SPEAKING);
                g_musicStopRequested = false; // inicio explícito: limpia un "para" previo
                bool okR = aiEngine.streamPcmUrlOnEsp32(sysUrl, sysTitle);
                String doneScreen = okR ? String("♪ ") + sysTitle : String("Sin señal");
                if (doneScreen.length() > 90) doneScreen = doneScreen.substring(0, 90);
                epd.showFallbackText(6, doneScreen.c_str(), true);
                if (!okR) {
                    aiEngine.streamTtsToSpeaker("Sin señal de radio.", nullptr);
                }
                g_lastSpoken = doneScreen;
                copitoMem.addTurn(preTranscript, okR ? doneScreen : String("radio fallo"));
                changeState(STATE_IDLE);
                lastInteractionTime = millis();
                CLOGI(TAG, "--- INTERACCIÓN RADIO FINALIZADA (sin IA) ---");
                return;
            }
            epd.showFallbackText(0, sysScreen.c_str(), true);
            g_textHold = true;
            changeState(STATE_SPEAKING);
            aiEngine.streamTtsToSpeaker(sysSpoken, nullptr);
            copitoMem.addTurn(preTranscript, sysSpoken);
            changeState(STATE_IDLE);
            lastInteractionTime = millis();
            CLOGI(TAG, "--- INTERACCIÓN SISTEMA FINALIZADA (sin IA) ---");
            return;
        }
        String musicSpoken, musicScreen, esp32Query;
        if (aiEngine.tryLocalMusicCommand(preTranscript, musicSpoken, musicScreen, esp32Query)) {
            CLOGI(TAG, "Comando MUSICA local: voz='%s' pantalla='%s' esp32q='%s'",
                  musicSpoken.c_str(), musicScreen.c_str(), esp32Query.c_str());
            audio.freeWavBuffer(wavData);
            if (esp32Query.length() > 0) {
                // "sona/pon/pone X": suena EN Kraken (stream PCM del Pi3). Sin TTS previo:
                // la pantalla avisa mientras se busca/resuelve (~10-15s).
                epd.showFallbackText(6, "Buscando...", true); // icono MUSICA
                g_textHold = true;
                changeState(STATE_SPEAKING);
                String playedTitle;
                g_musicStopRequested = false; // inicio explícito: limpia un "para" previo
                bool okPlay = aiEngine.streamMusicOnEsp32(esp32Query, playedTitle);
                String doneScreen = okPlay ? String("♪ ") + playedTitle : String("Sin audio");
                if (doneScreen.length() > 90) doneScreen = doneScreen.substring(0, 90);
                epd.showFallbackText(6, doneScreen.c_str(), true);
                if (!okPlay) {
                    // Nunca silencio: explicar por voz que fallo (Pi3 caido? sin YouTube?)
                    aiEngine.streamTtsToSpeaker(
                        String("No pude conseguir ") + esp32Query + ".", nullptr);
                }
                g_lastSpoken = doneScreen;
                copitoMem.addTurn(preTranscript, okPlay ? doneScreen : String("musica en Copito fallo"));
                changeState(STATE_IDLE);
                lastInteractionTime = millis();
                CLOGI(TAG, "--- INTERACCIÓN MUSICA-ESP32 FINALIZADA (sin IA) ---");
                return;
            }
            epd.showFallbackText(6, musicScreen.c_str(), true); // icono MUSICA
            g_textHold = true;
            changeState(STATE_SPEAKING);
            aiEngine.streamTtsToSpeaker(musicSpoken, nullptr);
            copitoMem.addTurn(preTranscript, musicSpoken);
            changeState(STATE_IDLE);
            lastInteractionTime = millis();
            CLOGI(TAG, "--- INTERACCIÓN MUSICA FINALIZADA (sin IA) ---");
            return;
        }
    }

    String aiAnswer;
    // 3a3. REFERENCIA REAL (logo/escudo/simbolo/bandera/foto): Wikipedia primero.
    // La IA generativa INVENTA los emblemas; esto muestra la imagen posta y
    // habla el resumen real, sin pasar por la IA. Si falla, se sigue a Gemini.
    if (havePre) {
        String refSubject;
        if (aiEngine.isReferenceImageRequest(preTranscript, refSubject)) {
            CLOGI(TAG, "Referencia real pedida: '%s'", refSubject.c_str());
            String refSummary;
            bool refImg = false;
            if (aiEngine.fetchWikipediaReference(refSubject, refSummary, refImg)) {
                audio.freeWavBuffer(wavData);
                String say = refSummary.length() > 0
                    ? String("Segun Wikipedia: ") + refSummary
                    : String("Te lo muestro en pantalla.");
                if (!refImg) {
                    // Sin foto JPG: el resumen igual se muestra como texto normal
                    paginator.setText(say.c_str(), 2);
                    if (paginator.getTotalPages() > 10) paginator.setText(say.c_str(), 1);
                    epd.renderPaginatorPage(paginator, "KRAKEN AI", true);
                }
                // (Con foto, la pantalla ya la muestra: no taparla con texto.)
                g_textHold = true;
                changeState(STATE_SPEAKING);
                aiEngine.streamTtsToSpeaker(say, nullptr);
                g_lastSpoken = say.length() > 300 ? say.substring(0, 300) : say;
                copitoMem.addTurn(preTranscript, say);
                changeState(STATE_IDLE);
                lastInteractionTime = millis();
                CLOGI(TAG, "--- INTERACCIÓN REFERENCIA FINALIZADA (sin IA) ---");
                return;
            }
            CLOGW(TAG, "Referencia sin datos, se sigue a Gemini como antes");
        }
    }
    // POWER durante "pensando": lo ya transcripto se descarta, la IA no habla después.
    if (g_voiceCancel) {
        CLOGI(TAG, "Cancelado antes de la IA: a reposo en silencio");
        audio.freeWavBuffer(wavData);
        changeState(STATE_IDLE);
        return;
    }
    bool aiSuccess = aiEngine.queryGeminiAudio(wavData, wavSize, aiAnswer, havePre ? preTranscript : String(""));
    audio.freeWavBuffer(wavData);

    if (!aiSuccess || aiAnswer.length() == 0) {
        CLOGE(TAG, "Fallo en la consulta a Gemini!");
        // La IA falló (429/cuota/caída) pero si pedía dibujo se intenta directo
        // igual: la nube de imágenes no depende de Gemini.
        String dt = havePre ? preTranscript : g_lastVoiceTranscript;
        if (dt.length() > 2 && CopitoTools::hasDrawingIntent(dt)) {
            CLOGI(TAG, "IA caída pero pedía dibujo: intento directo a la nube...");
            if (aiEngine.fetchAndDisplayCloudImage(dt)) {
                g_textHold = true;
                changeState(STATE_SPEAKING);
                aiEngine.streamTtsToSpeaker("Ahi va tu dibujo.", nullptr);
                copitoMem.addTurn(dt, "[dibujo directo sin IA]");
                changeState(STATE_IDLE);
                lastInteractionTime = millis();
                CLOGI(TAG, "--- DIBUJO DIRECTO FINALIZADO (sin IA) ---");
                return;
            }
        }
        changeState(STATE_ERROR);
        delay(2500);
        g_faceNeedsFull = true; // el IDLE debe limpiar la cara de error
        changeState(STATE_IDLE);
        return;
    }
    // POWER durante la IA: la respuesta se descarta sin mostrar ni hablar.
    if (g_voiceCancel) {
        CLOGI(TAG, "Cancelado tras la IA: respuesta descartada en silencio");
        changeState(STATE_IDLE);
        return;
    }
    // Sin pre-transcript (Groq falló) pero Gemini transcribió: el eco vale
    // igual. Si no, "dime X" lo contestaba la IA en vez de repetirlo.
    if (!havePre && g_lastVoiceTranscript.length() > 0) {
        String echoPhrase;
        if (aiEngine.extractEchoPhrase(g_lastVoiceTranscript, echoPhrase)) {
            stageEchoAndFinish(g_lastVoiceTranscript, echoPhrase);
            CLOGI(TAG, "--- ECO STAGED POST-IA (sin hablar IA) ---");
            return;
        }
    }

    // 3. Parse AI agent tags: [DRAW:...] [ICON:...] [FACE:...] [REMEMBER:...] [TOOL:...]
    CLOGI(TAG, "[IA-RAW] '%s'", aiAnswer.c_str());
    CopitoTags tags;
    CopitoTools::parseTags(aiAnswer, tags);
    CLOGI(TAG, "[TAGS] sketchLen=%d, mode='%s', text='%s', draw=%d, face=%d",
          tags.sketchCommands.length(), tags.mode.c_str(), tags.showText.c_str(), tags.drawType, tags.hasFace);
    int drawType = tags.drawType;
    String cleanAnswer = aiAnswer; // ya sin tags y con tools resueltos
    cleanAnswer.trim();

    // Guardar dato largo si la IA pidio recordar
    if (tags.rememberFact.length() > 0) {
        copitoMem.saveFact(tags.rememberFact);
        CLOGI(TAG, "Memoria larga guardada: %s", tags.rememberFact.c_str());
    }

    // 4. Texto de pantalla y voz (TEXT fija resumen corto; la voz dice lo largo).
    int iconToDraw = tags.drawType; // 0=Mini-Kraken (o Mini-Copito segun pack), 1=Corazon, 2=Estrella, 3=Sol, etc.
    String screenText = (tags.showText.length() > 0) ? tags.showText : cleanAnswer;

    // Detección proactiva de intenciones (transcripción de Whisper + respuesta de la IA)
    auto stripSpanishAccents = [](const String &src) -> String {
        String s = src;
        s.toLowerCase();
        s.replace("á", "a");
        s.replace("é", "e");
        s.replace("í", "i");
        s.replace("ó", "o");
        s.replace("ú", "u");
        s.replace("ü", "u");
        s.replace("ñ", "n");
        return s;
    };

    String userText = g_lastVoiceTranscript;
    userText.toLowerCase();
    String replyLower = cleanAnswer;
    replyLower.toLowerCase();
    String normUser = stripSpanishAccents(userText);
    String normReply = stripSpanishAccents(cleanAnswer);
    String combined = normUser + " " + normReply;

    // 1. Detección MODO RELOJ
    if (tags.mode != "RELOJ") {
        if (userText.indexOf("modo reloj") >= 0 || userText.indexOf("pon el reloj") >= 0 ||
            userText.indexOf("activa el reloj") >= 0 || userText.indexOf("pantalla de reloj") >= 0 ||
            userText.indexOf("pone el reloj") >= 0 || userText.indexOf("pasa a reloj") >= 0 ||
            normUser.indexOf("modo reloj") >= 0 || normUser.indexOf("pone el reloj") >= 0 ||
            replyLower.indexOf("modo reloj") >= 0 || replyLower.indexOf("activando el reloj") >= 0 ||
            replyLower.indexOf("pongo el reloj") >= 0 || normReply.indexOf("modo reloj") >= 0) {
            tags.mode = "RELOJ";
        }
    }

    // 2. Detección MODO CLIMA
    if (tags.mode != "CLIMA") {
        if (userText.indexOf("modo clima") >= 0 || userText.indexOf("pon el clima") >= 0 ||
            userText.indexOf("activa el clima") >= 0 || userText.indexOf("pantalla de clima") >= 0 ||
            userText.indexOf("estacion meteorologica") >= 0 || userText.indexOf("estacion de clima") >= 0 ||
            userText.indexOf("pone el clima") >= 0 || replyLower.indexOf("modo clima") >= 0 ||
            replyLower.indexOf("estacion meteorologica") >= 0 || normUser.indexOf("modo clima") >= 0 ||
            normReply.indexOf("modo clima") >= 0) {
            tags.mode = "CLIMA";
        }
    }

    // 2b. Detección MODO CUENTO explícito: el cuento manda (2026-09-10: un
    // "cuento de un conejito" disparaba dibujo por los sustantivos visuales y
    // encima se vocalizaba el sketch). Con pedido explícito no hay dibujo.
    if (tags.mode != "CUENTO" && aiIsStoryRequest(userText)) {
        tags.mode = "CUENTO";
        CLOGI(TAG, "MODO CUENTO forzado por pedido explícito (sin dibujo)");
    }

    // 3. Detección DIBUJO / SKETCH
    bool wantsDraw = false;
    // Petición por voz del usuario
    if (userText.indexOf("dibuja") >= 0 || userText.indexOf("dibujame") >= 0 ||
        userText.indexOf("haz un dibujo") >= 0 || userText.indexOf("hazme un dibujo") >= 0 ||
        userText.indexOf("ilustra") >= 0 || normUser.indexOf("dibuja") >= 0 ||
        normUser.indexOf("dibujame") >= 0 || normUser.indexOf("hace un dibujo") >= 0 ||
        normUser.indexOf("haceme un dibujo") >= 0 || normUser.indexOf("mostrame un dibujo") >= 0 ||
        normUser.indexOf("boceto") >= 0 || normUser.indexOf("grafica") >= 0 ||
        CopitoTools::hasDrawingIntent(userText) || CopitoTools::hasDrawingIntent(normUser)) {
        wantsDraw = true;
    }
    // O respuesta de la IA indicando dibujo o evasión complaciente ("Ahí va en pantalla...")
    if (!wantsDraw) {
        if (replyLower.indexOf("aqui tienes el dibujo") >= 0 || replyLower.indexOf("te dibuje") >= 0 ||
            normReply.indexOf("dibuj") >= 0 || normReply.indexOf("ilustr") >= 0 ||
            normReply.indexOf("boceto") >= 0 || normReply.indexOf("sketch") >= 0 ||
            normReply.indexOf("te pinte") >= 0 || normReply.indexOf("te hice un dibujo") >= 0 ||
            normReply.indexOf("en pantalla") >= 0 || normReply.indexOf("en el display") >= 0 ||
            normReply.indexOf("ahi va") >= 0 || normReply.indexOf("ahi tenes") >= 0 ||
            normReply.indexOf("aca tenes") >= 0 || normReply.indexOf("ahi tienes") >= 0 ||
            CopitoTools::hasDrawingIntent(cleanAnswer) || CopitoTools::hasDrawingIntent(normReply)) {
            wantsDraw = true;
        }
    }

    // El cuento nunca dibuja (aunque el texto mencione cosas dibujables).
    if (wantsDraw && tags.mode != "CUENTO") {
        CLOGI(TAG, "Intención de dibujo detectada. Priorizando dibujo libre real en la nube...");
        String prompt = (userText.length() > 0) ? userText : combined;
        bool cloudDrawn = aiEngine.fetchAndDisplayCloudImage(prompt);
        if (cloudDrawn) {
            tags.sketchCommands = ""; // Descartar cualquier muñeco de palitos del LLM
        } else if (tags.sketchCommands.length() == 0) {
            CLOGI(TAG, "Nube directa no disponible. Usando generador procedural fallback...");
            CopitoTools::generateFallbackSketch(combined, tags.sketchCommands);
        }
    }

    // 3b. Modos locales post-respuesta (0 llamadas extra: la IA ya clasifico con 1 llamada).
    // RELOJ = tarjeta hora fija. CLIMA = datos reales de sensores. CUENTO = icono libro fijo.
    // DORMIDO = duerme tras hablar. Todo configurable tambien desde la web (/api/action).
    String voiceText = cleanAnswer;
    // Cache para "repeti": lo ultimo dicho por la IA (top 300 chars).
    g_lastSpoken = voiceText.length() > 300 ? voiceText.substring(0, 300) : voiceText;
    bool wantSleep = false;
    // Cuento clásico (cascada no disponible): narra lento y sin progreso.
    // Se setea en la rama CUENTO y se consume abajo en la locución.
    bool classicStory = false;
    if (tags.mode == "RELOJ") {
        CLOGI(TAG, "Activando MODO RELOJ FIJO tras respuesta");
        changeState(STATE_CLOCK);
        aiEngine.streamTtsToSpeaker(voiceText, nullptr);
        copitoMem.addTurn("(voz)", voiceText);
        return;
    } else if (tags.mode == "CLIMA") {
        CLOGI(TAG, "Activando MODO CLIMA FULL-SCREEN tras respuesta");
        changeState(STATE_WEATHER);
        String tpl = "Ahora mismo hay [TOOL:GET_TEMP] con [TOOL:GET_HUM] de humedad.";
        CopitoTools::resolveToolTags(tpl);
        aiEngine.streamTtsToSpeaker(tpl, nullptr);
        copitoMem.addTurn("(voz)", tpl);
        return;
    } else if (tags.mode == "CUENTO") {
        if (iconToDraw == 0) iconToDraw = 8; // LIBRO si la IA no eligio icono
        // Cascada ilustrada: 3-6 partes con dibujo + voz narrador + precarga.
        // Si no sale (JSON roto, sin red), se sigue abajo con el clasico largo.
        CLOGI(TAG, "Cuento en cascada: intentando partes ilustradas...");
        if (playStoryCascade(userText)) {
            g_lastSpoken = "Te conte un cuento en cascada.";
            copitoMem.addTurn(userText, "[cuento en cascada]");
            changeState(STATE_IDLE);
            lastInteractionTime = millis();
            CLOGI(TAG, "--- CUENTO EN CASCADA FINALIZADO ---");
            return;
        }
        CLOGW(TAG, "Cascada no disponible, sigo con cuento clasico");
        // Stub de cuota ("Aquí va."): no es un cuento; mensaje honesto.
        if (voiceText.length() < STORY_MIN_CHARS) {
            CLOGW(TAG, "Cuento clasico stub (%u letras), fallback honesto", (unsigned)voiceText.length());
            voiceText = STORY_FALLBACK_TEXT;
            screenText = STORY_FALLBACK_TEXT;
        }
        classicStory = true; // el clasico también narra lento y sin progreso
        aiEngine.storyTtsResetPref(); // cada cuento redescubre su voz
    } else if (tags.mode == "DORMIDO") {
        wantSleep = true;
    }
    // 4. Boceto Vectorial [SKETCH: ...] o Paginación Inteligente Adaptativa
    int totalP = 1;
    if (tags.sketchCommands.length() > 0) {
        CLOGI(TAG, "Boceto vectorial detectado: '%s'", tags.sketchCommands.c_str());
        String cap = (tags.showText.length() > 0) ? tags.showText : cleanAnswer;
        if (cap.length() > 28) {
            cap = cap.substring(0, 28);
        }
        epd.showVectorSketch(tags.sketchCommands.c_str(), cap.c_str(), true);
        g_textHold = true;
    } else if (!g_textHold) {
        // Adaptación automática: primero escala 2X (letra grande); solo si pasa
        // de 10 páginas se baja a 1X (un cuento largo se hojea con BOOT igual).
        paginator.setText(screenText.c_str(), 2);
        if (paginator.getTotalPages() > 10) {
            // Si con 2X excede 2 páginas (texto muy largo), usamos escala 1X
            paginator.setText(screenText.c_str(), 1);
        }
        totalP = paginator.getTotalPages();
        int fontScale = paginator.getFontScale();
        CLOGI(TAG, "Texto adaptativo: %d paginas calculadas (Escala: %dX)", totalP, fontScale);

        epd.renderPaginatorPage(paginator, "KRAKEN AI", true);
        g_textHold = true;
    }

    CLOGI(TAG, "Mostrando en pantalla: '%s' (Icono: %d, Pags: %d)", screenText.c_str(), iconToDraw, totalP);

    // 5. Reproducir locución por audio TTS en paralelo
    changeState(STATE_SPEAKING);
    // Clásico de cuento: voz narradora lenta + sin cartel de progreso (el
    // texto ya está en pantalla y la e-paper no parpadea por bloque).
    bool ttsOk = classicStory ? aiEngine.narrateStoryPart(voiceText, nullptr)
                              : aiEngine.streamTtsToSpeaker(voiceText, nullptr);

    if (!ttsOk && !g_ttsStopRequested) {
        CLOGW(TAG, "TTS de audio no disponible. Mostrando respuesta en pantalla.");
        audio.playChimeUp();
    }

    // 5b. Guardar turno en memoria corta (para proxima conversacion)
    {
        String userShort = "(voz)";
        String asShort = voiceText;
        if (asShort.length() > 240) asShort = asShort.substring(0, 240);
        copitoMem.addTurn(userShort, asShort);
    }

    // 5b2. Modo DORMIDO: tras hablar, a dormir directo (sin espera de lectura)
    if (wantSleep) {
        g_textHold = false;
        enterSleepMode();
        return; // enterSleepMode no retorna (deep sleep)
    }

    // 6. Return to IDLE (el texto queda fijado en g_textHold; BOOT permite hojear hasta el final)
    changeState(STATE_IDLE);
    lastInteractionTime = millis();
    CLOGI(TAG, "--- INTERACCIÓN FINALIZADA CON ÉXITO ---");
}
