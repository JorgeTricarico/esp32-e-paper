#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>

#define INACTIVITY_TIMEOUT_MS   180000UL  // 3 minutos
#define ANIM_MIN_DURATION_MS    700UL     // Duracion minima Frame B
#define FULL_REFRESH_INTERVAL   6         // 1 Full Refresh cada 6 cambios de animal

enum AppState : uint8_t {
    STATE_IDLE = 0,
    STATE_ANIMATING,
    STATE_SWITCHING,
    STATE_SLEEP
};

enum AppEventType : uint8_t {
    EVT_NONE = 0,
    EVT_NEXT_ANIMAL,
    EVT_PLAY_ANIMAL,
    EVT_TOGGLE_SPELL_MODE,
    EVT_SPELL_LETTER,
    EVT_AUDIO_DONE,
    EVT_ANIM_TIMER_DONE,
    EVT_POWER_OFF,
    EVT_INACTIVITY_TIMEOUT
};

enum DisplayAction : uint8_t {
    DISP_NONE = 0,
    DISP_FULL_ANIMAL,            // Refresco completo del animal actual (Frame A)
    DISP_PARTIAL_ANIMAL,         // Refresco parcial de toda la pantalla (Frame A)
    DISP_PARTIAL_TOGGLE_SPELL,   // Actualiza icono [A] / [A/] en Zona 1
    DISP_PARTIAL_SPRITE_B,       // Refresco parcial solo Zona 2 (Frame B + audio)
    DISP_PARTIAL_SPRITE_A,       // Refresco parcial solo Zona 2 (vuelta a Frame A)
    DISP_SLEEP_SCREEN,           // Pantalla "¡HASTA PRONTO!" + Deep Sleep
    DISP_DEEP_SLEEP_KEEP_SCREEN  // Deep Sleep manteniendo el ultimo animal
};

struct StepResult {
    DisplayAction displayAction;
    bool triggerAudio;
    uint8_t animalIndex;
    bool enterDeepSleep;
};

/**
 * Maquina de estados pura del Libro de Animales (sin dependencias FreeRTOS/Arduino).
 * Facilita pruebas unitarias deterministicas en host.
 */
class AppStateMachine {
public:
    explicit AppStateMachine(uint8_t totalAnimals = 8);

    AppState getState() const { return state; }
    uint8_t getCurrentAnimal() const { return currentAnimal; }
    uint8_t getPartialRefreshCount() const { return partialRefreshCount; }
    bool isSpellModeEnabled() const { return spellMode; }

    StepResult handleEvent(AppEventType evt);

private:
    uint8_t animalCount;
    AppState state;
    uint8_t currentAnimal;
    uint8_t partialRefreshCount;
    bool spellMode;
    bool audioDone;
    bool timerDone;

    StepResult finishAnimationIfReady();
};

#endif // APP_STATE_H
