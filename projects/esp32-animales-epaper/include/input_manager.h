#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <stdint.h>
#include "app_state.h"

#define BTN_DEBOUNCE_MS      50UL
#define BTN_PWR_SHORT_MAX_MS 1500UL
#define BTN_PWR_LONG_MS      3000UL

/**
 * Detector de pulsaciones con debounce (50 ms) y distincion corta/larga.
 * Desacoplado de GPIO para permitir pruebas unitarias en host.
 */
class ButtonDebouncer {
public:
    explicit ButtonDebouncer(bool detectLongPress = false);

    // Actualiza con lectura cruda (true = presionado, activo LOW en hardware)
    // y timestamp actual en ms. Retorna evento si se disparo en este tick.
    AppEventType update(bool rawPressed, uint32_t nowMs, bool isPwrButton);

private:
    bool longPressEnabled;
    bool stableState;
    bool lastRawState;
    uint32_t lastBounceMs;
    uint32_t pressStartMs;
    bool longFired;
};

#endif // INPUT_MANAGER_H
