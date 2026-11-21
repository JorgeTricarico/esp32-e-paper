#include <cassert>
#include <iostream>

#include "app_state.h"
#include "input_manager.h"
#include "ota_manager.h"

static void test_circular_navigation_and_full_refresh_every_6() {
    AppStateMachine fsm(8);
    assert(fsm.getCurrentAnimal() == 0);

    // Primeros 5 cambios -> refresco parcial
    for (int i = 1; i <= 5; i++) {
        StepResult r = fsm.handleEvent(EVT_NEXT_ANIMAL);
        assert(r.displayAction == DISP_PARTIAL_ANIMAL);
        assert(r.animalIndex == (uint8_t)i);
        assert(fsm.getPartialRefreshCount() == (uint8_t)i);

        // Verificar que al reproducir sonido en ese animal, coincide el indice
        StepResult play = fsm.handleEvent(EVT_PLAY_ANIMAL);
        assert(play.triggerAudio == true);
        assert(play.animalIndex == (uint8_t)i);
        assert(play.displayAction == DISP_PARTIAL_SPRITE_B);

        // Finalizar animacion
        fsm.handleEvent(EVT_ANIM_TIMER_DONE);
        StepResult done = fsm.handleEvent(EVT_AUDIO_DONE);
        assert(done.displayAction == DISP_PARTIAL_SPRITE_A);
        assert(done.animalIndex == (uint8_t)i);
    }

    // 6to cambio -> refresco completo automatico y reset del contador
    StepResult r6 = fsm.handleEvent(EVT_NEXT_ANIMAL);
    assert(r6.displayAction == DISP_FULL_ANIMAL);
    assert(r6.animalIndex == 6);
    assert(fsm.getPartialRefreshCount() == 0);

    // Ciclo circular (7 -> 0)
    fsm.handleEvent(EVT_NEXT_ANIMAL);
    StepResult rWrap = fsm.handleEvent(EVT_NEXT_ANIMAL);
    assert(rWrap.animalIndex == 0);
}

static void test_animation_ignores_rapid_presses() {
    AppStateMachine fsm(8);

    // Por defecto el deletreo esta desactivado; toggle por pulsacion larga de BOOT
    assert(fsm.isSpellModeEnabled() == false);
    StepResult togOn = fsm.handleEvent(EVT_TOGGLE_SPELL_MODE);
    assert(togOn.displayAction == DISP_PARTIAL_TOGGLE_SPELL);
    assert(fsm.isSpellModeEnabled() == true);
    StepResult togOff = fsm.handleEvent(EVT_TOGGLE_SPELL_MODE);
    assert(togOff.displayAction == DISP_PARTIAL_TOGGLE_SPELL);
    assert(fsm.isSpellModeEnabled() == false);

    // Pulsar BOOT inicia animacion + audio
    StepResult start = fsm.handleEvent(EVT_PLAY_ANIMAL);
    assert(fsm.getState() == STATE_ANIMATING);
    assert(start.displayAction == DISP_PARTIAL_SPRITE_B);
    assert(start.triggerAudio == true);

    // Pulsaciones repetidas de BOOT o PWR durante animacion deben ignorarse
    StepResult spamBoot = fsm.handleEvent(EVT_PLAY_ANIMAL);
    assert(spamBoot.displayAction == DISP_NONE);
    assert(spamBoot.triggerAudio == false);

    StepResult spamPwr = fsm.handleEvent(EVT_NEXT_ANIMAL);
    assert(spamPwr.displayAction == DISP_NONE);

    // Solo vuelve a Frame A cuando terminan tanto el timer como el audio
    StepResult r1 = fsm.handleEvent(EVT_ANIM_TIMER_DONE);
    assert(r1.displayAction == DISP_NONE);
    assert(fsm.getState() == STATE_ANIMATING);

    StepResult r2 = fsm.handleEvent(EVT_AUDIO_DONE);
    assert(r2.displayAction == DISP_PARTIAL_SPRITE_A);
    assert(fsm.getState() == STATE_IDLE);
}

static void test_power_off_and_inactivity_sleep() {
    AppStateMachine fsm1(8);
    StepResult off = fsm1.handleEvent(EVT_POWER_OFF);
    assert(fsm1.getState() == STATE_SLEEP);
    assert(off.displayAction == DISP_SLEEP_SCREEN);
    assert(off.enterDeepSleep == true);

    AppStateMachine fsm2(8);
    StepResult idleTimeout = fsm2.handleEvent(EVT_INACTIVITY_TIMEOUT);
    assert(fsm2.getState() == STATE_SLEEP);
    assert(idleTimeout.displayAction == DISP_DEEP_SLEEP_KEEP_SCREEN);
    assert(idleTimeout.enterDeepSleep == true);
}

static void test_button_debouncer_short_vs_long() {
    ButtonDebouncer pwr(true);

    // Rebote menor a 50 ms no dispara nada
    assert(pwr.update(true, 10, true) == EVT_NONE);
    assert(pwr.update(true, 40, true) == EVT_NONE);

    // Presion estable a los 60 ms
    assert(pwr.update(true, 60, true) == EVT_NONE);
    // Suelta a los 400 ms (< 1500 ms) -> pulsacion corta
    assert(pwr.update(false, 400, true) == EVT_NONE); // debounce de soltado
    assert(pwr.update(false, 460, true) == EVT_NEXT_ANIMAL);

    // Pulsacion larga >= 3000 ms
    assert(pwr.update(true, 1000, true) == EVT_NONE);
    assert(pwr.update(true, 1060, true) == EVT_NONE);
    assert(pwr.update(true, 4059, true) == EVT_NONE);
    assert(pwr.update(true, 4060, true) == EVT_POWER_OFF);
    // Soltar despues de long press no debe disparar EVT_NEXT_ANIMAL
    assert(pwr.update(false, 4200, true) == EVT_NONE);
    assert(pwr.update(false, 4260, true) == EVT_NONE);
}

static void test_ota_checkin_schedule() {
    // Primera hora: 6 ciclos de 10 minutos
    for (uint16_t q = 0; q < 6; q++) {
        assert(calcNextCheckinMinutes(q) == 10);
    }
    // Luego backoff exponencial con tope 720 min (12 h)
    assert(calcNextCheckinMinutes(6) == 20);
    assert(calcNextCheckinMinutes(7) == 40);
    assert(calcNextCheckinMinutes(8) == 80);
    assert(calcNextCheckinMinutes(9) == 160);
    assert(calcNextCheckinMinutes(10) == 320);
    assert(calcNextCheckinMinutes(11) == 640);
    assert(calcNextCheckinMinutes(12) == 720);
    assert(calcNextCheckinMinutes(13) == 720);
    assert(calcNextCheckinMinutes(14) == 0);
    assert(calcNextCheckinMinutes(20) == 0);
}

int main() {
    test_circular_navigation_and_full_refresh_every_6();
    test_animation_ignores_rapid_presses();
    test_power_off_and_inactivity_sleep();
    test_button_debouncer_short_vs_long();
    test_ota_checkin_schedule();
    std::cout << "[OK] Todos los tests C++ de maquina de estados pasaron." << std::endl;
    return 0;
}
