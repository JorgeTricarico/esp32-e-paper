#ifndef AUDIO_ES8311_H
#define AUDIO_ES8311_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

// Callback type for animating mouth during audio playback
typedef void (*AudioStreamCallback)(bool mouthState);

uint8_t calc_es8311_volume_reg(uint8_t volume);

// Sonda digital (loopback sin micrófono): lo que realmente fue al DAC.
// La llena writePcmChunk en cada playback; la lee GET /api/audio_probe.
// peakPre = fuente antes de ganancia (diagnostica masters calientes);
// peakPost = tras ganancia+limiter (lo que oyó el parlante);
// limited = muestras que tocaron la rodilla; clipped = techo (siempre 0 salvo bug).
struct AudioProbe {
    uint32_t samples = 0;
    uint32_t limited = 0;
    uint32_t clipped = 0;
    int32_t peakPre = 0;
    int32_t peakPost = 0;
    int64_t sumSq = 0;
    int64_t sum = 0;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool begin(uint8_t volume = 100);
    void powerOn();
    void powerOff();
    void setVolume(uint8_t volume); // 0 to 100
    void mute(bool enable);

struct ChimeNote {
    float freqHz;
    uint16_t startMs;
    uint16_t durationMs;
    float decayRate;
    float volume;
};

typedef bool (*StopCheckCallback)();

    // Modern Soft Acoustic Chimes & Robot Soundscapes
    void playChimeSequence(const ChimeNote *notes, size_t numNotes, uint16_t totalMs, int16_t maxAmp = 12000);
    void playTone(uint16_t freqHz, uint16_t durationMs);
    void playStartupSound();
    void playWakeupSound();
    void playSleepSound();
    void playListeningSound();
    void playThinkingSound();
    void playSuccessSound();
    void playErrorSound();
    void playCancelSound();
    void playChimeUp();
    void playChimeDown();

    // Voice Recording with Intelligent VAD & Manual 2nd-click Cut
    bool recordWav(uint8_t **outWavBuffer, size_t *outWavSize,
                   uint32_t maxDurationMs   = 8500,
                   StopCheckCallback stopCb = nullptr,
                   uint32_t silenceStartMs  = 2400,   // Cancel if no speech starts in this ms
                   uint32_t silenceEndMs    = 1200);  // Cut after this ms of silence post-speech
    void freeWavBuffer(uint8_t *wavBuffer);

    // Audio Playback (Streaming PCM chunks directly to speaker)
    void startPlayback(uint32_t sampleRate = AUDIO_SAMPLE_RATE);
    // gain256: ganancia digital previa al limiter (256 = unity). Musica densa
    // (masters hot de YouTube) usa 128 (-6dB) para no saturar el amplificador.
    size_t writePcmChunk(const uint8_t *data, size_t len, int gain256 = 256);
    void stopPlayback();
    // Sonda: resetea (lo llama startPlayback) y foto para /api/audio_probe.
    void probeReset();
    void probeSnapshot(AudioProbe &out);
    void setPlaybackSpeed(uint8_t speedPercent);
    uint8_t getPlaybackSpeed() const { return currentPlaybackSpeed; }

    // Direct playback helper with optional mouth animation callback
    void playPcmData(const uint8_t *pcmData, size_t len, uint32_t sampleRate = AUDIO_SAMPLE_RATE, AudioStreamCallback cb = nullptr);

private:
    bool isInitialized;
    uint8_t currentVolume;
    uint8_t currentPlaybackSpeed;

    // ES8311 I2C Register Operations
    bool writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);
    bool initCodecRegisters();
    bool initI2S();

    // Standard 44-byte WAV header generator
    void buildWavHeader(uint8_t *header, uint32_t pcmDataSize, uint32_t sampleRate = 16000);
};

extern AudioEngine audio;

#endif // AUDIO_ES8311_H
