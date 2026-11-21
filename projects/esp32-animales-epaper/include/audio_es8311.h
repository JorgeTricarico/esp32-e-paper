#ifndef AUDIO_ES8311_H
#define AUDIO_ES8311_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "board_pins.h"

#define AUDIO_DEFAULT_SAMPLE_RATE 16000

/**
 * Driver I2C + I2S para codec ES8311 y amplificador NS4150B.
 * Habilita PA_EN (GPIO 46) unicamente durante la reproduccion de WAVs para
 * evitar ruido blanco (hiss) en reposo y ahorrar bateria.
 */
class AudioEs8311 {
public:
    AudioEs8311();
    ~AudioEs8311();

    bool begin(uint8_t volume = 85);
    void powerOn();
    void powerOff();
    void setVolume(uint8_t volume); // 0..100
    void mute(bool enable);

    // Reproduce un archivo WAV PCM desde LittleFS (con fallback opcional a SD)
    // Retorna la duracion reproducida en ms (0 si fallo).
    uint32_t playWavFile(const char *path);

    // Mantiene el amplificador activo entre reproducciones consecutivas (deletreo)
    void beginSession();
    void endSession();
    void requestAbort();
    void clearAbort();

private:
    bool initialized;
    uint8_t currentVolume;
    uint32_t lastSampleRate;
    bool sessionActive;
    bool ampActive;
    volatile bool abortRequested;

    bool writeReg(uint8_t reg, uint8_t val);
    bool initCodecRegisters();
    bool initI2S();
    void enableSpeakerAmp(bool enable);
    void drainDma();
};

extern AudioEs8311 audio;

#endif // AUDIO_ES8311_H
