// DSP de audio PURO (sin Arduino, sin I2S, sin logs): compila en ESP32 y en PC.
// Es la UNICA fuente de verdad de ganancia + soft-limiter. El firmware lo llama
// desde writePcmChunk; los tests Python lo espejan con vectores fijos; el modo
// synth de /api/audio_probe lo ejercita en el equipo sin hacer ruido.
#pragma once

#include <stdint.h>
#include <stddef.h>

#define DSP_TRIM_NUM 256
#define DSP_TRIM_DEN 256   // trim unity: la causa era la velocidad (tts_speed 110),
                           // no el nivel. Volumen se maneja con knob + ganancias por fuente.
#define DSP_GAIN_MIN 32    // piso -18dB (nunca silencio accidental)
#define DSP_GAIN_MAX 256   // techo unity (el boost lo decide cada fuente)
#define DSP_KNEE 26000     // rodilla del limiter (~-2dBFS): compresion 4:1 arriba
#define DSP_CEIL 28000     // techo estricto (protege NS4150B de flat-top clipping)
#define DSP_NORM_SILENCE 200 // pico menor = silencio, no amplificar

static inline int32_t dsp_apply_sample_ex(int16_t sample, int gain256,
                                          bool *limited, bool *clipped) {
    if (gain256 < DSP_GAIN_MIN) gain256 = DSP_GAIN_MIN;
    if (gain256 > DSP_GAIN_MAX) gain256 = DSP_GAIN_MAX;
    gain256 = (gain256 * DSP_TRIM_NUM) / DSP_TRIM_DEN;
    int32_t v = ((int32_t)sample * gain256) / 256;
    bool lim = false, clip = false;
    if (v > DSP_KNEE) {
        v = DSP_KNEE + (v - DSP_KNEE) / 4;
        lim = true;
    } else if (v < -DSP_KNEE) {
        v = -DSP_KNEE + (v + DSP_KNEE) / 4;
        lim = true;
    }
    if (v > DSP_CEIL) {
        v = DSP_CEIL;
        clip = true;
    } else if (v < -DSP_CEIL) {
        v = -DSP_CEIL;
        clip = true;
    }
    if (limited) *limited = lim;
    if (clipped) *clipped = clip;
    return v;
}

static inline int32_t dsp_apply_sample(int16_t sample, int gain256) {
    return dsp_apply_sample_ex(sample, gain256, NULL, NULL);
}

// Ganancia de normalizacion por pico: lleva `peak` a `target` sin boost.
// Devuelve 0 si es silencio (no amplificar ruido). Siempre en [128,256].
static inline int32_t dsp_norm_gain256(int32_t peak, int32_t target) {
    if (peak < DSP_NORM_SILENCE) return 0;
    int32_t g = (target * 256) / peak;
    if (g > 256) g = 256; // nunca boost (crujia picos)
    if (g < 128) g = 128;
    return g;
}
