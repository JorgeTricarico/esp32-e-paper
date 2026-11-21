#include "app_state.h"

AppStateMachine::AppStateMachine(uint8_t totalAnimals)
    : animalCount(totalAnimals == 0 ? 1 : totalAnimals),
      state(STATE_IDLE),
      currentAnimal(0),
      partialRefreshCount(0),
      spellMode(true),
      audioDone(false),
      timerDone(false) {}

StepResult AppStateMachine::finishAnimationIfReady() {
    StepResult res = {DISP_NONE, false, currentAnimal, false};
    if (state == STATE_ANIMATING && audioDone && timerDone) {
        state = STATE_IDLE;
        res.displayAction = DISP_PARTIAL_SPRITE_A;
    }
    return res;
}

StepResult AppStateMachine::handleEvent(AppEventType evt) {
    StepResult res = {DISP_NONE, false, currentAnimal, false};

    if (state == STATE_SLEEP) {
        return res;
    }

    switch (evt) {
        case EVT_NEXT_ANIMAL:
            // Si estaba animando/sonando, cancelar y pasar directo al siguiente animal
            if (state == STATE_SWITCHING) {
                return res;
            }
            audioDone = false;
            timerDone = false;
            state = STATE_SWITCHING;
            currentAnimal = (uint8_t)((currentAnimal + 1) % animalCount);
            partialRefreshCount++;
            res.animalIndex = currentAnimal;
            if (partialRefreshCount >= FULL_REFRESH_INTERVAL) {
                partialRefreshCount = 0;
                res.displayAction = DISP_FULL_ANIMAL;
            } else {
                res.displayAction = DISP_PARTIAL_ANIMAL;
            }
            // En nuestra arquitectura el refresco de cambio es sincrono en task_display
            state = STATE_IDLE;
            return res;

        case EVT_PLAY_ANIMAL:
            // Ignorar entradas repetidas de BOOT mientras anima/suena
            if (state != STATE_IDLE) {
                return res;
            }
            state = STATE_ANIMATING;
            audioDone = false;
            timerDone = false;
            res.displayAction = DISP_PARTIAL_SPRITE_B;
            res.triggerAudio = true;
            res.animalIndex = currentAnimal;
            return res;

        case EVT_TOGGLE_SPELL_MODE:
            if (state != STATE_IDLE) {
                return res;
            }
            spellMode = !spellMode;
            res.displayAction = DISP_PARTIAL_TOGGLE_SPELL;
            res.animalIndex = currentAnimal;
            return res;

        case EVT_AUDIO_DONE:
            if (state == STATE_ANIMATING) {
                audioDone = true;
                return finishAnimationIfReady();
            }
            return res;

        case EVT_ANIM_TIMER_DONE:
            if (state == STATE_ANIMATING) {
                timerDone = true;
                return finishAnimationIfReady();
            }
            return res;

        case EVT_POWER_OFF:
            state = STATE_SLEEP;
            res.displayAction = DISP_SLEEP_SCREEN;
            res.enterDeepSleep = true;
            return res;

        case EVT_INACTIVITY_TIMEOUT:
            if (state == STATE_IDLE) {
                state = STATE_SLEEP;
                res.displayAction = DISP_DEEP_SLEEP_KEEP_SCREEN;
                res.enterDeepSleep = true;
            }
            return res;

        case EVT_NONE:
        default:
            return res;
    }
}
