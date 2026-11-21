#include <Arduino.h>
#include <esp_sleep.h>
#include "driver/gpio.h"
#include "board_pins.h"
#include "ota_manager.h"

void enterDeepSleepWithOtaCheckin() {
    digitalWrite(PIN_USER_LED, HIGH);
    digitalWrite(PIN_EPD_PWR, HIGH);
    digitalWrite(PIN_AUDIO_PWR, HIGH);
    digitalWrite(PIN_MAIN_PWR, LOW);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_BOOT, 0);
    esp_sleep_enable_ext1_wakeup((1ULL << PIN_BTN_PWR), ESP_EXT1_WAKEUP_ANY_LOW);

    uint16_t nextMin = otaGetNextCheckinMinutes();
    if (nextMin > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)nextMin * 60ULL * 1000000ULL);
    }
    esp_deep_sleep_start();
}

void setup() {
    // Enable main peripheral rail
    pinMode(PIN_MAIN_PWR, OUTPUT);
    digitalWrite(PIN_MAIN_PWR, HIGH);

    Serial.begin(115200);
    delay(150);

    // Check-in OTA silencioso al despertar por timer (sin encender pantalla)
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        otaRunCheckinWindow();
        enterDeepSleepWithOtaCheckin();
        return;
    }

    // Solo resetea la ventana de check-in (cada 10 min la 1ra hora) si cambio el firmware
    otaEnsureFirmwareEpoch();

    // GPIO 3 is MTDO (JTAG) by default on ESP32-S3; reset before using as LED
    gpio_reset_pin((gpio_num_t)PIN_USER_LED);
    pinMode(PIN_USER_LED, OUTPUT);
    digitalWrite(PIN_USER_LED, HIGH); // Active LOW -> OFF

    Serial.println("[TEMPLATE] Waveshare ESP32-S3-ePaper-1.54 listo (con OTA eco).");
}

void loop() {
    digitalWrite(PIN_USER_LED, LOW);
    delay(250);
    digitalWrite(PIN_USER_LED, HIGH);
    delay(1750);
}
