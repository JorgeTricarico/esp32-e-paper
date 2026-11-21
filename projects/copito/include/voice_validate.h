// =============================================================================
// voice_validate: lógica PURA del pipeline voz/música (sin Arduino, sin WiFi).
// Se compila en el firmware Y se espeja en tests/test_core_logic.py con los
// mismos vectores. Si cambiás una regla acá, actualizá el espejo Python.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Escaneo MP3 (gate antes del DAC) ---
typedef enum {
    VV_MP3_OK = 0,      // ID3 o frame-sync MPEG en ventana
    VV_MP3_TOO_SHORT,   // menos de 4 bytes (o <128 según umbral del llamador)
    VV_MP3_JSON_ERR,    // '{'/'['/ERROR tras BOM/espacios
    VV_MP3_HTML_ERR,    // <html / <!DOC
    VV_MP3_NO_SYNC      // sin sync en la ventana de 256 B
} vv_mp3_kind_t;

typedef struct {
    vv_mp3_kind_t kind;
    size_t data_offset; // offset del ID3 o del primer sync
} vv_mp3_t;

vv_mp3_t vv_mp3_scan(const uint8_t *buf, size_t len);

// --- Parser WAV (camina chunks: soporta fmt extendido, no solo offset 36) ---
enum {
    VV_WAV_OK = 0,
    VV_WAV_TOO_SHORT = 1,
    VV_WAV_NO_RIFF = 2,
    VV_WAV_NO_FMT = 3,
    VV_WAV_BAD_FMT = 4,   // no PCM / canales / bits / rate fuera de rango
    VV_WAV_NO_DATA = 5,
    VV_WAV_EMPTY = 6      // data_size 0 o truncado
};

typedef struct {
    int ok;               // 1 si pasó todas las validaciones (reason == VV_WAV_OK)
    int reason;           // código de arriba
    uint16_t channels;
    uint16_t bits;
    uint32_t rate;
    uint32_t data_size;   // declarado en el chunk 'data'
    size_t data_offset;   // offset real donde empiezan las muestras
    size_t data_avail;    // bytes de muestra realmente presentes en buf
    int32_t peak;         // pico absoluto medido (0 si no se midió)
} vv_wav_t;

vv_wav_t vv_wav_parse(const uint8_t *buf, size_t len, int measure_peak);

// --- Splitter de frases TTS (máx ~150 chars, corta en puntuación) ---
typedef void (*vv_phrase_cb_t)(const char *start, size_t len, void *ctx);
// Devuelve cantidad de frases emitidas. Reglas espejo de streamGoogleFreeTts:
// resto<=150 va entero; si no, corte en . ! ? ; (luego , :) con sig. espacio,
// si no en espacio, si no corte duro. Mínimo de búsqueda: 40 (30 p/espacio).
size_t vv_split_phrases(const char *text, vv_phrase_cb_t emit, void *ctx);

// --- Selección de ruta de música (LAN vs funnel) + parámetros ---
typedef struct {
    int use_lan_first;   // 1 = probar LAN primero, 0 = funnel primero
    int funnel;          // 1 = se espera salir por funnel (parámetros de calle)
    uint32_t rate;       // 22050 LAN / 16000 funnel
    uint32_t pre_sec;    // segundos de pre-buffer: 2 LAN / 4 funnel
    size_t rd_size;      // bytes por read: 4096 LAN / 8192 funnel
    uint32_t stall_ms;   // stall tolerado: 12000 LAN / 25000 funnel
} vv_route_t;

// music_off: comandos apagados (sin host). pub_set: hay fallback funnel.
// lan_failed_recently: el último intento LAN falló hace poco (<90 s).
// funnel_hint: el último transporte bueno fue funnel (o se sabe hotspot).
vv_route_t vv_select_route(int music_off, int pub_set, int lan_failed_recently,
                           int funnel_hint);

size_t vv_prebuffer_bytes(uint32_t rate, int funnel);

// --- Plan de reintento HTTP ---
typedef struct {
    int retry;            // 1 = reintentar
    uint32_t backoff_ms;  // espera antes del reintento (0 si no reintenta)
} vv_retry_t;
// attempt: 0-based. http_code: 0 si fue timeout de red. timed_out: 1 si timeout.
vv_retry_t vv_retry_plan(int attempt, int http_code, int timed_out);

// --- Decisión VAD sobre energías por chunk (para simular casa/calle) ---
typedef struct {
    int detected;      // 1 si hubo habla válida
    size_t cut_idx;    // índice donde cortar (fin de habla + silencio), n si no
} vv_vad_t;
vv_vad_t vv_vad_decide(const uint16_t *energies, size_t n, uint16_t threshold,
                        size_t min_speech, size_t max_silence);

#ifdef __cplusplus
}
#endif
