#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#ifdef ENABLE_SD_FALLBACK
#include <SD.h>
#include <SPI.h>
#endif

#include "board_pins.h"
#include "epd_driver.h"
#include "audio_es8311.h"
#include "app_state.h"
#include "input_manager.h"
#include "ota_manager.h"
#include "assets/animals_gfx.h"
#include <WiFi.h>

// Colas FreeRTOS entre tareas
static QueueHandle_t s_eventQueue = nullptr;
static QueueHandle_t s_audioQueue = nullptr;
static SemaphoreHandle_t s_epdReadySem = nullptr;
static volatile bool s_abortAudio = false;
static volatile bool s_sleepRequested = false;

struct AppEventMsg {
    AppEventType type;
    int8_t letterIdx;
};

struct AudioCmd {
    uint8_t animalIndex;
    bool spellLetters;
};

static inline bool sendAppEvent(AppEventType type, int8_t letterIdx = -1, TickType_t waitTicks = 0) {
    AppEventMsg msg = {type, letterIdx};
    return (xQueueSend(s_eventQueue, &msg, waitTicks) == pdTRUE);
}

static void abortableDelayMs(uint32_t ms) {
    uint32_t elapsed = 0;
    while (elapsed < ms && !s_abortAudio) {
        uint32_t step = (ms - elapsed > 15) ? 15 : (ms - elapsed);
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }
}

static uint8_t sampleBatteryLevel() {
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += (uint32_t)analogReadMilliVolts(PIN_VBAT_ADC) * 2;
        delayMicroseconds(150);
    }
    uint32_t mv = sum / 4;
    if (mv < 2500 || mv > 5000) return 3; // USB sin bateria o lectura fuera de rango
    if (mv >= 3950) return 3;
    if (mv >= 3750) return 2;
    if (mv >= 3580) return 1;
    return 0; // Bateria baja (< 3.58V): pila vacia para avisar carga
}

static void enterDeepSleep(bool drawSleepMsg) {
    log_i("Entrando en Deep Sleep (drawSleepMsg=%d)...", drawSleepMsg);

    s_sleepRequested = true;
    s_abortAudio = true;
    audio.requestAbort();
    if (s_epdReadySem) xSemaphoreGive(s_epdReadySem);

    if (drawSleepMsg) {
        epd.drawSleepScreen();
        epd.displayFull();
        vTaskDelay(pdMS_TO_TICKS(1800));
        epd.drawCoverScreen();
        epd.displayFull();
    }

    epd.sleep();
    audio.powerOff();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Esperar a que el usuario suelte PWR y BOOT antes de dormir para evitar re-wakeup inmediato
    uint32_t waitStart = millis();
    while ((digitalRead(PIN_BTN_PWR) == LOW || digitalRead(PIN_BTN_BOOT) == LOW) &&
           (millis() - waitStart) < 3000) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    gpio_reset_pin((gpio_num_t)PIN_USER_LED);
    pinMode(PIN_USER_LED, OUTPUT);
    digitalWrite(PIN_USER_LED, HIGH); // LED apagado (active LOW)

    digitalWrite(PIN_EPD_PWR, EPD_PWR_OFF);
    digitalWrite(PIN_MAIN_PWR, MAIN_PWR_OFF);

    // Despertar con BOOT (GPIO 0), PWR (GPIO 18) o timer de check-in OTA
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_BOOT, 0);
    esp_sleep_enable_ext1_wakeup((1ULL << PIN_BTN_PWR), ESP_EXT1_WAKEUP_ANY_LOW);

    uint16_t nextMin = otaGetNextCheckinMinutes();
    if (nextMin > 0) {
        log_i("Proximo check-in OTA en %u min", nextMin);
        esp_sleep_enable_timer_wakeup((uint64_t)nextMin * 60ULL * 1000000ULL);
    } else {
        log_i("Firmware estable: check-in OTA automatico desactivado");
    }

    esp_deep_sleep_start();
}

// ==============================================================================
// CORE 0: task_audio — Inicializa ES8311 y reproduce WAVs sin trabar UI/SPI
// ==============================================================================
static void taskAudio(void *pvParameters) {
    if (!audio.begin(98)) {
        log_e("Fallo inicializando AudioEs8311 en Core 0");
    }

    AudioCmd cmd;
    while (true) {
        if (xQueueReceive(s_audioQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (!s_sleepRequested) {
                s_abortAudio = false;
                audio.clearAbort();
            }
            if (cmd.animalIndex < ANIMAL_COUNT && !s_abortAudio) {
                const AnimalAsset &asset = ANIMALS[cmd.animalIndex];
                log_i("Reproduciendo %s...", asset.audio_path);
                if (cmd.spellLetters) {
                    // Mantener el amplificador encendido desde el sonido inicial para que
                    // la primera letra no sufra el soft-start del NS4150B.
                    audio.beginSession();
                }
                audio.playWavFile(asset.audio_path);

                if (cmd.spellLetters && !s_abortAudio) {
                    abortableDelayMs(220);
                    for (uint8_t i = 0; i < asset.letter_count && !s_abortAudio; i++) {
                        char letterPath[32];
                        snprintf(letterPath, sizeof(letterPath), "/letters/%c.wav", asset.letter_keys[i]);
                        if (!LittleFS.exists(letterPath)) {
                            log_w("Letra faltante, omitiendo: %s", letterPath);
                            continue;
                        }
                        if (s_epdReadySem) xSemaphoreTake(s_epdReadySem, 0);
                        sendAppEvent(EVT_SPELL_LETTER, (int8_t)i, portMAX_DELAY);
                        if (s_epdReadySem) xSemaphoreTake(s_epdReadySem, pdMS_TO_TICKS(600));
                        if (s_abortAudio) break;

                        if (audio.playWavFile(letterPath) > 0 && !s_abortAudio) {
                            abortableDelayMs(50);
                        }
                    }
                    if (!s_abortAudio) {
                        // Resaltar toda la palabra completa (-2) al decir el nombre entero
                        if (s_epdReadySem) xSemaphoreTake(s_epdReadySem, 0);
                        sendAppEvent(EVT_SPELL_LETTER, -2, portMAX_DELAY);
                        if (s_epdReadySem) xSemaphoreTake(s_epdReadySem, pdMS_TO_TICKS(600));
                        if (!s_abortAudio) {
                            abortableDelayMs(80);
                            char namePath[32];
                            snprintf(namePath, sizeof(namePath), "/names/%s.wav", asset.id);
                            audio.playWavFile(namePath);
                        }

                        // Limpiar resaltado y repetir el sonido del animal al terminar la palabra
                        if (!s_abortAudio) {
                            if (s_epdReadySem) xSemaphoreTake(s_epdReadySem, 0);
                            sendAppEvent(EVT_SPELL_LETTER, -1, portMAX_DELAY);
                            if (s_epdReadySem) xSemaphoreTake(s_epdReadySem, pdMS_TO_TICKS(600));
                            if (!s_abortAudio) {
                                abortableDelayMs(140);
                                audio.playWavFile(asset.audio_path);
                            }
                        }
                    }
                    audio.endSession();
                }
            }
            if (!s_abortAudio) {
                sendAppEvent(EVT_AUDIO_DONE, -1, portMAX_DELAY);
            }
        }
    }
}

// ==============================================================================
// CORE 1: task_input — Lectura de botones PWR y BOOT con debounce 50 ms
// ==============================================================================
static void taskInput(void *pvParameters) {
    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
    pinMode(PIN_BTN_PWR, INPUT_PULLUP);

    ButtonDebouncer pwrBtn(true);
    ButtonDebouncer bootBtn(true);

    while (true) {
        uint32_t now = millis();
        bool rawPwr = (digitalRead(PIN_BTN_PWR) == LOW);
        bool rawBoot = (digitalRead(PIN_BTN_BOOT) == LOW);
        if (rawPwr && rawBoot) {
            rawPwr = false;
            rawBoot = false;
        }

        AppEventType evtPwr = pwrBtn.update(rawPwr, now, true);
        if (evtPwr != EVT_NONE) {
            sendAppEvent(evtPwr);
        }

        AppEventType evtBoot = bootBtn.update(rawBoot, now, false);
        if (evtBoot != EVT_NONE) {
            sendAppEvent(evtBoot);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==============================================================================
// CORE 1: task_display — Maquina de estados, framebuffer y refrescos EPD
// ==============================================================================
static void taskDisplay(void *pvParameters) {
    AppStateMachine fsm(ANIMAL_COUNT);
    uint32_t lastActivityMs = millis();
    uint32_t lastAwakeOtaMs = millis();
    uint32_t lastBattCheckMs = millis();
    uint32_t animStartMs = 0;
    bool animTimerSent = false;

    while (true) {
        AppEventMsg msg = {EVT_NONE, -1};
        if (xQueueReceive(s_eventQueue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            AppEventType evt = msg.type;
            if (evt == EVT_NEXT_ANIMAL || evt == EVT_PLAY_ANIMAL ||
                evt == EVT_TOGGLE_SPELL_MODE || evt == EVT_POWER_OFF) {
                lastActivityMs = millis();
            }

            if (evt == EVT_SPELL_LETTER) {
                epd.drawLetterHighlight(fsm.getCurrentAnimal(), msg.letterIdx);
                epd.displayPartial();
                if (s_epdReadySem) {
                    xSemaphoreGive(s_epdReadySem);
                }
                continue;
            }

            if (evt == EVT_NEXT_ANIMAL && fsm.getState() == STATE_ANIMATING) {
                // Cancelar de inmediato el audio/deletreo en curso para pasar al siguiente animal
                s_abortAudio = true;
                audio.requestAbort();
                if (s_epdReadySem) xSemaphoreGive(s_epdReadySem);
            }

            StepResult res = fsm.handleEvent(evt);
            log_i("EVT=%u -> animal=%u (%s) action=%u",
                  (unsigned)evt, (unsigned)res.animalIndex,
                  ANIMALS[res.animalIndex].id, (unsigned)res.displayAction);

            if (res.triggerAudio) {
                AudioCmd cmd = {res.animalIndex, fsm.isSpellModeEnabled()};
                xQueueSend(s_audioQueue, &cmd, 0);
                animStartMs = millis();
                animTimerSent = false;
            }

            switch (res.displayAction) {
                case DISP_FULL_ANIMAL:
                    epd.setBatteryLevel(sampleBatteryLevel());
                    epd.drawAnimalScreen(res.animalIndex, false);
                    epd.displayFull();
                    break;

                case DISP_PARTIAL_ANIMAL:
                    if ((millis() - lastBattCheckMs) >= 300000UL) {
                        lastBattCheckMs = millis();
                        epd.setBatteryLevel(sampleBatteryLevel());
                    }
                    epd.drawAnimalScreen(res.animalIndex, false);
                    epd.displayPartial();
                    break;

                case DISP_PARTIAL_TOGGLE_SPELL:
                    epd.setSpellMode(fsm.isSpellModeEnabled());
                    epd.drawAnimalScreen(res.animalIndex, false);
                    epd.displayPartial();
                    break;

                case DISP_PARTIAL_SPRITE_B:
                    epd.drawAnimalSpriteOnly(res.animalIndex, true);
                    epd.displayPartialWindow(GFX_ZONE2_X, GFX_ZONE2_Y, GFX_ZONE2_W, GFX_ZONE2_H);
                    break;

                case DISP_PARTIAL_SPRITE_A:
                    epd.drawAnimalSpriteOnly(res.animalIndex, false);
                    epd.displayPartialWindow(GFX_ZONE2_X, GFX_ZONE2_Y, GFX_ZONE2_W, GFX_ZONE2_H);
                    break;

                case DISP_SLEEP_SCREEN:
                    enterDeepSleep(true);
                    break;

                case DISP_DEEP_SLEEP_KEEP_SCREEN:
                    enterDeepSleep(false);
                    break;

                case DISP_NONE:
                default:
                    break;
            }
        }

        // Control de duracion minima de Frame B (~700 ms)
        if (fsm.getState() == STATE_ANIMATING && !animTimerSent) {
            if ((millis() - animStartMs) >= ANIM_MIN_DURATION_MS) {
                if (sendAppEvent(EVT_ANIM_TIMER_DONE)) {
                    animTimerSent = true;
                }
            }
        }

        // Ahorro de energia: 3 minutos de inactividad -> Deep Sleep
        if (fsm.getState() == STATE_IDLE && (millis() - lastActivityMs) >= INACTIVITY_TIMEOUT_MS) {
            sendAppEvent(EVT_INACTIVITY_TIMEOUT);
        }

        // Si estuvo encendido durante la primera hora (intervalo=10 min), chequear OTA tambien despierto
        if (fsm.getState() == STATE_IDLE && (millis() - lastAwakeOtaMs) >= 600000UL) {
            lastAwakeOtaMs = millis();
            if (otaGetNextCheckinMinutes() == 10) {
                otaRunCheckinWindow();
            }
        }
    }
}

void setup() {
    // 1. Retener energia de bateria/perifericos
    pinMode(PIN_MAIN_PWR, OUTPUT);
    digitalWrite(PIN_MAIN_PWR, MAIN_PWR_ON);

    Serial.begin(115200);
    delay(150);

    // Si desperto por timer de check-in OTA, NO tocar pantalla ni audio:
    // solo chequear si hay update en 1s y volver a dormir con gasto minimo.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        log_i("=== Wakeup por timer: Check-in OTA silencioso ===");
        otaRunCheckinWindow();

        pinMode(PIN_EPD_PWR, OUTPUT);
        pinMode(PIN_AUDIO_PWR, OUTPUT);
        digitalWrite(PIN_EPD_PWR, EPD_PWR_OFF);
        digitalWrite(PIN_AUDIO_PWR, AUDIO_PWR_OFF);
        digitalWrite(PIN_MAIN_PWR, MAIN_PWR_OFF);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_BOOT, 0);
        esp_sleep_enable_ext1_wakeup((1ULL << PIN_BTN_PWR), ESP_EXT1_WAKEUP_ANY_LOW);
        uint16_t nextMin = otaGetNextCheckinMinutes();
        if (nextMin > 0) {
            esp_sleep_enable_timer_wakeup((uint64_t)nextMin * 60ULL * 1000000ULL);
        }
        esp_deep_sleep_start();
        return;
    }

    // Solo reinicia la ventana de 10 min si el compilable (MD5) cambio
    otaEnsureFirmwareEpoch();
    log_i("=== Libro de Animales Interactivo (ESP32-S3-ePaper-1.54) ===");

    // 2. Montar LittleFS (y SD opcional)
    if (!LittleFS.begin(true)) {
        log_e("Error montando LittleFS!");
    } else {
        log_i("LittleFS montado correctamente");
    }

#ifdef ENABLE_SD_FALLBACK
    SPIClass sdSpi(HSPI);
    sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (SD.begin(PIN_SD_CS, sdSpi)) {
        log_i("Tarjeta TF/SD montada como fallback");
    }
#endif

    // 3. Inicializar pantalla e-Paper con 1 Refresco Completo inicial
    pinMode(PIN_VBAT_ADC, INPUT);
    if (!epd.begin()) {
        log_e("Fallo inicializando EpdDriver!");
    } else {
        epd.setBatteryLevel(sampleBatteryLevel());
        epd.drawAnimalScreen(0, false);
        epd.displayFull();
    }

    // 4. Crear colas, semaforo de sync y tareas Dual-Core FreeRTOS
    s_eventQueue = xQueueCreate(24, sizeof(AppEventMsg));
    s_audioQueue = xQueueCreate(4, sizeof(AudioCmd));
    s_epdReadySem = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(taskAudio, "task_audio", 6144, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(taskInput, "task_input", 3072, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(taskDisplay, "task_display", 6144, nullptr, 2, nullptr, 1);
}

void loop() {
    // Todas las operaciones corren en las tareas FreeRTOS
    vTaskDelay(pdMS_TO_TICKS(1000));
}
