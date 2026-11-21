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

void otaEnsureFirmwareEpoch();
void otaRunCheckinWindow();
uint16_t otaGetNextCheckinMinutes();
void otaResetQuietCounter();
void otaIncrementQuietCounter();
#endif

#endif // OTA_MANAGER_H
