#include "voice_bank.h"
#include <LittleFS.h>

// OJO: MINIMP3_IMPLEMENTATION vive en ai_engine.cpp; acá solo declaración.
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

static const char *TAG = "BANK";

bool bankHas(const char *name) {
    if (!name || !name[0]) return false;
    char path[48];
    snprintf(path, sizeof(path), "/bank/%s.mp3", name);
    return LittleFS.exists(path);
}

bool playBankPhrase(const char *name, AudioStreamCallback cb) {
    if (!bankHas(name)) return false;
    char path[48];
    snprintf(path, sizeof(path), "/bank/%s.mp3", name);
    File f = LittleFS.open(path, "r");
    if (!f || f.size() == 0 || f.size() > 300000) {
        if (f) f.close();
        return false;
    }

    uint8_t *in = (uint8_t *)malloc(4096);
    short *pcm = (short *)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(short));
    mp3dec_t *mp3d = (mp3dec_t *)malloc(sizeof(mp3dec_t));
    if (!in || !pcm || !mp3d) {
        free(in); free(pcm); free(mp3d);
        f.close();
        return false;
    }

    mp3dec_init(mp3d);
    size_t left = 0; // bytes válidos en 'in' aún no consumidos
    bool eof = false, started = false;
    int frames = 0;

    while (true) {
        // Rellenar buffer si hay lugar y queda archivo (lo no consumido queda al inicio)
        if (!eof && left < 2048) {
            size_t room = 4096 - left;
            size_t n = f.read(in + left, room);
            if (n == 0) {
                eof = true;
            } else {
                left += n;
            }
        }
        if (left == 0) break; // EOF y nada pendiente

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(mp3d, in, left, pcm, &info);
        if (info.frame_bytes > 0 && (size_t)info.frame_bytes <= left) {
            memmove(in, in + info.frame_bytes, left - info.frame_bytes);
            left -= info.frame_bytes;
        } else if (eof) {
            break; // cola inválida al final: cortar sin trabarse
        } else {
            // frame incompleto: si el buffer está lleno y no avanza, saltear 1 byte
            if (left >= 4096) {
                memmove(in, in + 1, left - 1);
                left--;
            }
            continue;
        }

        if (samples > 0) {
            if (!started) {
                uint32_t hz = (info.hz > 0) ? (uint32_t)info.hz : 24000;
                audio.startPlayback(hz);
                started = true;
                if (cb) cb(true);
            }
            if (info.channels == 2) {
                for (int i = 0; i < samples; i++) {
                    pcm[i] = (int16_t)(((int32_t)pcm[i * 2] + (int32_t)pcm[i * 2 + 1]) / 2);
                }
            }
            audio.writePcmChunk((const uint8_t *)pcm, (size_t)samples * sizeof(int16_t));
            if (cb && (++frames % 4 == 0)) cb(frames % 8 < 4);
        }
    }

    free(in); free(pcm); free(mp3d);
    f.close();
    if (started) {
        audio.stopPlayback();
        return true;
    }
    return false;
}
