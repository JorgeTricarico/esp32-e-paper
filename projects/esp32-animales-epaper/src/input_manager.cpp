#include "input_manager.h"

ButtonDebouncer::ButtonDebouncer(bool detectLongPress)
    : longPressEnabled(detectLongPress),
      stableState(false),
      lastRawState(false),
      lastBounceMs(0),
      pressStartMs(0),
      longFired(false) {}

AppEventType ButtonDebouncer::update(bool rawPressed, uint32_t nowMs, bool isPwrButton) {
    if (rawPressed != lastRawState) {
        lastBounceMs = nowMs;
        lastRawState = rawPressed;
    }

    if ((nowMs - lastBounceMs) < BTN_DEBOUNCE_MS) {
        return EVT_NONE;
    }

    if (rawPressed != stableState) {
        stableState = rawPressed;
        if (stableState) {
            // Flanco de bajada (presionado)
            pressStartMs = nowMs;
            longFired = false;
            if (!isPwrButton && !longPressEnabled) {
                return EVT_PLAY_ANIMAL;
            }
        } else {
            // Flanco de subida (soltado)
            if (!longFired) {
                uint32_t heldMs = nowMs - pressStartMs;
                if (heldMs < BTN_PWR_SHORT_MAX_MS) {
                    return isPwrButton ? EVT_NEXT_ANIMAL : EVT_PLAY_ANIMAL;
                }
            }
        }
    } else if (stableState && longPressEnabled && !longFired) {
        if ((nowMs - pressStartMs) >= BTN_PWR_LONG_MS) {
            longFired = true;
            return isPwrButton ? EVT_POWER_OFF : EVT_TOGGLE_SPELL_MODE;
        }
    }

    return EVT_NONE;
}
