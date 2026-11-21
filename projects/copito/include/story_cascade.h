#ifndef STORY_CASCADE_H
#define STORY_CASCADE_H

#include <Arduino.h>

// Cuento ilustrado en cascada: Gemini parte el cuento en 3-6 partes
// (texto + prompt visual), cada parte se ilustra (Nano Banana -> Pollinations)
// y se narra con voz de narrador. La parte 1 se muestra en cuanto esta lista;
// las demas se precargan en fondo mientras se cuenta.
//   BOOT (o PWR corto) = siguiente parte (corta la voz y avanza).
//   PWR mantenido 1.5s = salir del cuento.
// Si la cascada falla (JSON roto, sin red) devuelve false y el llamador sigue
// con el cuento clasico largo sin ilustrar (plan B).
bool playStoryCascade(const String &topic);
bool storyCascadeActive();
void storySetActive(bool on);
bool storyWaitBoot(uint32_t gen, uint32_t timeoutMs);
void storyRequestAdvance();void storyRequestExit();
// 1er POWER narrando: frena la voz y queda pausado en la imagen (BOOT avanza,
// 2do POWER sale). storyPaused() lo mira la rama PWR de botones.
void storyRequestPause();
bool storyPaused();

// Mínimo de letras para aceptar un cuento clásico (2026-09-09: con la cuota
// agotada los proveedores devuelven stubs como "Aquí va." y eso no se narra).
static const size_t STORY_MIN_CHARS = 200;
// Texto honesto cuando no hay cuento (cuota caída o stubs).
#define STORY_FALLBACK_TEXT "No pude inventar el cuento ahora, proba en un rato."

// Flag de cancelacion para los fetch de imagen en curso (ai_engine lo mira
// entre modelos y en sus loops de red para abortar rapido al salir).
extern volatile bool g_storyCancel;

#endif // STORY_CASCADE_H
