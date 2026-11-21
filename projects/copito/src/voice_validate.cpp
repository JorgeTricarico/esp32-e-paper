// =============================================================================
// voice_validate: implementación pura (C99, sin dependencias Arduino).
// Espejo Python en tests/test_core_logic.py — mantener sincronizados.
// =============================================================================
#include "voice_validate.h"
#include <string.h>

vv_mp3_t vv_mp3_scan(const uint8_t *buf, size_t len) {
    vv_mp3_t r;
    r.kind = VV_MP3_TOO_SHORT;
    r.data_offset = 0;
    if (!buf || len < 4) return r;
    // ID3v2 al inicio (Google Translate TTS lo antepone)
    if (len >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        r.kind = VV_MP3_OK;
        r.data_offset = 0;
        return r;
    }
    // JSON / texto de error (salta BOM + espacios), igual que el firmware
    {
        size_t i = 0;
        if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) i = 3;
        while (i < len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) i++;
        if (i < len) {
            if (buf[i] == '{' || buf[i] == '[') { r.kind = VV_MP3_JSON_ERR; r.data_offset = i; return r; }
            if (i + 5 <= len && memcmp(buf + i, "ERROR", 5) == 0) { r.kind = VV_MP3_JSON_ERR; r.data_offset = i; return r; }
            if (i + 5 <= len && memcmp(buf + i, "<html", 5) == 0) { r.kind = VV_MP3_HTML_ERR; r.data_offset = i; return r; }
            if (i + 5 <= len && memcmp(buf + i, "<!DOC", 5) == 0) { r.kind = VV_MP3_HTML_ERR; r.data_offset = i; return r; }
        }
    }
    // Frame sync MPEG en los primeros 256 bytes (permite tags cortos delante)
    {
        size_t scan = (len - 1 < 256) ? len - 1 : 256;
        for (size_t i = 0; i < scan; i++) {
            if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
                r.kind = VV_MP3_OK;
                r.data_offset = i;
                return r;
            }
        }
    }
    r.kind = VV_MP3_NO_SYNC;
    return r;
}

static uint32_t vv_rd32le(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t vv_rd16le(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

vv_wav_t vv_wav_parse(const uint8_t *buf, size_t len, int measure_peak) {
    vv_wav_t r;
    memset(&r, 0, sizeof(r));
    r.reason = VV_WAV_TOO_SHORT;
    if (!buf || len < 12) return r;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        r.reason = VV_WAV_NO_RIFF;
        return r;
    }
    // Caminar chunks desde offset 12 (soporta fmt extendido, fact, JUNK, etc.)
    size_t pos = 12;
    int have_fmt = 0;
    uint16_t audio_fmt = 0;
    while (pos + 8 <= len) {
        uint32_t ck_size = vv_rd32le(buf + pos + 4);
        size_t ck_data = pos + 8;
        if (memcmp(buf + pos, "fmt ", 4) == 0) {
            if (ck_size < 16 || ck_data + 16 > len) break;
            audio_fmt = vv_rd16le(buf + ck_data);
            r.channels = vv_rd16le(buf + ck_data + 2);
            r.rate = vv_rd32le(buf + ck_data + 4);
            r.bits = vv_rd16le(buf + ck_data + 14);
            have_fmt = 1;
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            if (!have_fmt) break;
            // Validar formato ANTES de aceptar el data
            if (audio_fmt != 1) { r.reason = VV_WAV_BAD_FMT; return r; }
            if (r.channels < 1 || r.channels > 2) { r.reason = VV_WAV_BAD_FMT; return r; }
            if (r.bits != 8 && r.bits != 16) { r.reason = VV_WAV_BAD_FMT; return r; }
            if (r.rate < 8000 || r.rate > 48000) { r.reason = VV_WAV_BAD_FMT; return r; }
            r.data_size = ck_size;
            r.data_offset = ck_data;
            size_t avail = (ck_data < len) ? len - ck_data : 0;
            if (avail > ck_size) avail = ck_size;
            r.data_avail = avail;
            if (ck_size == 0 || avail == 0) { r.reason = VV_WAV_EMPTY; return r; }
            // Truncado: se exige al menos el 100% declarado (el firmware aborta a nube)
            if (avail < ck_size) { r.reason = VV_WAV_EMPTY; return r; }
            if (measure_peak && r.bits == 16 && r.channels == 1) {
                int32_t peak = 0;
                const uint8_t *d = buf + ck_data;
                for (size_t i = 0; i + 1 < avail; i += 2) {
                    int16_t s = (int16_t)(d[i] | (d[i + 1] << 8));
                    int32_t a = s < 0 ? -(int32_t)s : (int32_t)s;
                    if (a > peak) peak = a;
                }
                r.peak = peak;
            }
            r.ok = 1;
            r.reason = VV_WAV_OK;
            return r;
        }
        // Avanzar (chunks con tamaño impar llevan 1 byte de pad)
        size_t adv = 8 + ck_size + (ck_size & 1);
        if (adv == 0 || pos + adv <= pos) break;
        pos += adv;
        if (pos >= len) break;
    }
    r.reason = have_fmt ? VV_WAV_NO_DATA : VV_WAV_NO_FMT;
    return r;
}

size_t vv_split_phrases(const char *text, vv_phrase_cb_t emit, void *ctx) {
    if (!text || !emit) return 0;
    size_t text_len = strlen(text);
    size_t start = 0;
    size_t count = 0;
    // Saltar separadores iniciales
    while (start < text_len && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t')) start++;
    while (start < text_len) {
        size_t remaining = text_len - start;
        size_t end;
        if (remaining <= 150) {
            end = text_len;
        } else {
            size_t target = start + 150;
            long best = -1;
            // 1) Puntuación fuerte seguida de espacio/fin
            for (long i = (long)target; i >= (long)start + 40; i--) {
                char c = text[i];
                if ((c == '.' || c == '!' || c == '?' || c == ';') &&
                    (i + 1 >= (long)text_len || text[i + 1] == ' ')) { best = i + 1; break; }
            }
            // 2) Coma o dos puntos
            if (best < 0) {
                for (long i = (long)target; i >= (long)start + 40; i--) {
                    char c = text[i];
                    if ((c == ',' || c == ':') &&
                        (i + 1 >= (long)text_len || text[i + 1] == ' ')) { best = i + 1; break; }
                }
            }
            // 3) Espacio
            if (best < 0) {
                for (long i = (long)target; i >= (long)start + 30; i--) {
                    if (text[i] == ' ') { best = i; break; }
                }
            }
            // 4) Corte duro
            if (best < 0) best = (long)target;
            end = (size_t)best;
        }
        // Trim de la frase (sin modificar el original)
        size_t s = start, e = end;
        while (s < e && (text[s] == ' ' || text[s] == '\n' || text[s] == '\r' || text[s] == '\t')) s++;
        while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\n' || text[e - 1] == '\r' || text[e - 1] == '\t')) e--;
        if (e > s) {
            emit(text + s, e - s, ctx);
            count++;
        }
        start = end;
        while (start < text_len && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t')) start++;
        if (end == start && start < text_len) {
            // Sin progreso (corte duro sobre no-espacios): avanzar 1 para no colgar
            start++;
        }
    }
    return count;
}

vv_route_t vv_select_route(int music_off, int pub_set, int lan_failed_recently,
                           int funnel_hint) {
    vv_route_t r;
    r.use_lan_first = 1;
    r.funnel = 0;
    r.rate = 22050;
    r.pre_sec = 2;
    r.rd_size = 4096;
    r.stall_ms = 12000;
    if (music_off) {
        r.use_lan_first = 0;
        r.rate = 0;
        return r;
    }
    // Calle: LAN falló hace poco, o el último transporte bueno fue funnel.
    // Si hay fallback configurado, el funnel va primero (ahorra ~6-21 s).
    if ((lan_failed_recently || funnel_hint) && pub_set) {
        r.use_lan_first = 0;
        r.funnel = 1;
    } else if (lan_failed_recently && !pub_set) {
        // Sin fallback y LAN caída: igual intentar LAN (única opción), pero
        // con parámetros de calle (más tolerante) por si volvió a medias.
        r.funnel = 1;
    }
    if (r.funnel) {
        r.rate = 16000;
        r.pre_sec = 4;
        r.rd_size = 8192;
        r.stall_ms = 25000;
    }
    return r;
}

size_t vv_prebuffer_bytes(uint32_t rate, int funnel) {
    if (rate < 8000 || rate > 48000) return 0;
    return (size_t)rate * 2 * (funnel ? 4 : 2);
}

vv_retry_t vv_retry_plan(int attempt, int http_code, int timed_out) {
    vv_retry_t r;
    r.retry = 0;
    r.backoff_ms = 0;
    if (attempt < 0) attempt = 0;
    if (timed_out || http_code == 429 || (http_code >= 500 && http_code <= 599)) {
        if (attempt < 2) {
            r.retry = 1;
            r.backoff_ms = (uint32_t)500 << attempt; // 500, 1000 (cap 4000)
            if (r.backoff_ms > 4000) r.backoff_ms = 4000;
        }
        return r;
    }
    // 400/401/403/404 y demás: reintentar no ayuda
    return r;
}

vv_vad_t vv_vad_decide(const uint16_t *energies, size_t n, uint16_t threshold,
                        size_t min_speech, size_t max_silence) {
    vv_vad_t r;
    r.detected = 0;
    r.cut_idx = n;
    if (!energies || n == 0) return r;
    size_t speech_run = 0;
    size_t silence_run = 0;
    int in_speech = 0;
    for (size_t i = 0; i < n; i++) {
        if (energies[i] >= threshold) {
            speech_run++;
            silence_run = 0;
            if (!in_speech && speech_run >= min_speech) in_speech = 1;
        } else {
            silence_run++;
            if (in_speech && silence_run >= max_silence) {
                r.detected = 1;
                r.cut_idx = i + 1;
                return r;
            }
        }
    }
    if (in_speech) {
        r.detected = 1;
        r.cut_idx = n;
    }
    return r;
}
