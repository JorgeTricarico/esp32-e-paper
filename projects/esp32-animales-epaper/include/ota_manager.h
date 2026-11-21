#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>

#define OTA_UDP_DISCOVERY_PORT 32320
#define OTA_WAIT_WINDOW_MS     180000UL  // 3 min conectado esperando push OTA si hay update
#define OTA_STABLE_QUIET_LIMIT 14        // Tras ~2 dias sin cambios, apaga check-in automatico

/**
 * Calcula el proximo intervalo de check-in en minutos:
 * - Primera hora (quietCount < 6): cada 10 minutos (6 * 10 = 60 min).
 * - Decaimiento exponencial: 20, 40, 80, 160, 320, 720, 720, 720 min.
 * - Software estable (quietCount >= 14): retorna 0 (desactiva timer WiFi).
 */
uint16_t calcNextCheckinMinutes(uint16_t quietCount);

#ifndef UNIT_TEST_HOST
#include <Arduino.h>

// Verifica en NVS si el firmware cambio (ESP.getSketchMD5()).
// Solo resetea el contador ota_quiet a 0 cuando hay un compilable nuevo.
void otaEnsureFirmwareEpoch();

// Ejecuta un check-in ultra-rapido (~1-3s).
// Si hay servidor OTA esperando en la LAN, mantiene WiFi y ArduinoOTA activos
// hasta completar el flash o vencer OTA_WAIT_WINDOW_MS.
void otaRunCheckinWindow();

uint16_t otaGetNextCheckinMinutes();
void otaResetQuietCounter();
void otaIncrementQuietCounter();
#endif

#endif // OTA_MANAGER_H
