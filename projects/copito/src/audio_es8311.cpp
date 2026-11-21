#include "audio_es8311.h"
#include "dsp_audio.h"
#include "copito_log.h"
#include <Wire.h>

// Sonda digital: acumuladores del último playback (ver AudioProbe en el .h).
// Escritor = tareas de audio; lector = /api/audio_probe. Volátiles y lectura
// aproximada: es diagnóstico, no metrología.
static volatile uint32_t s_prSamples = 0;
static volatile uint32_t s_prLimited = 0;
static volatile uint32_t s_prClipped = 0;
static volatile int32_t s_prPeakPre = 0;
static volatile int32_t s_prPeakPost = 0;
static volatile int64_t s_prSumSq = 0;
static volatile int64_t s_prSum = 0;

static const char *TAG = "AUDIO";

AudioEngine audio;

AudioEngine::AudioEngine() : isInitialized(false), currentVolume(100), currentPlaybackSpeed(100) {}

AudioEngine::~AudioEngine() {
    stopPlayback();
}

bool AudioEngine::writeReg(uint8_t reg, uint8_t val) {
    CopitoI2cGuard guard;
    if (!guard.locked) return false;
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

uint8_t AudioEngine::readReg(uint8_t reg) {
    CopitoI2cGuard guard;
    if (!guard.locked) return 0xFF;
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0xFF;
    Wire.requestFrom((uint8_t)ES8311_I2C_ADDR, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;
}

void AudioEngine::powerOn() {
    // Power gate for ES8311 codec (GPIO 42, active LOW) and NS4150B PA (GPIO 46, active HIGH)
    pinMode(PIN_AUDIO_PWR, OUTPUT);
    digitalWrite(PIN_AUDIO_PWR, AUDIO_PWR_ON);
    pinMode(PIN_AUDIO_PA, OUTPUT);
    digitalWrite(PIN_AUDIO_PA, AUDIO_PA_ON);
    delay(10);
}

void AudioEngine::powerOff() {
    digitalWrite(PIN_AUDIO_PA, AUDIO_PA_OFF);
    digitalWrite(PIN_AUDIO_PWR, AUDIO_PWR_OFF);
}

bool AudioEngine::initCodecRegisters() {
    // 1. Noise immunity enhancement
    writeReg(0x44, 0x08);
    writeReg(0x44, 0x08); // Repeated write as per ES8311 datasheet recommendation

    // 2. Soft Reset & Power-on (Matches Espressif/Waveshare official ES8311 driver)
    writeReg(0x00, 0x1F);
    delay(20);
    writeReg(0x00, 0x00);
    writeReg(0x00, 0x80); // Power-on command

    // 3. Clock Manager Configuration (Enable all clocks: 0x3F)
    writeReg(0x01, 0x3F); // Enable all internal clocks (ADC, DAC, MCLK, BCLK)
    writeReg(0x02, 0x00); // Pre-divider = 1, mult = 1
    writeReg(0x03, 0x10); // ADC clock (single speed, OSR = 0x10)
    writeReg(0x04, 0x10); // DAC clock (OSR = 0x10)
    writeReg(0x05, 0x00); // ADC & DAC clock dividers = 1
    writeReg(0x06, 0x03); // BCLK divider = 4 (64 * fs): reg = (bclk_div - 1) = 3
    writeReg(0x07, 0x00); // LRCK divider High
    writeReg(0x08, 0xFF); // LRCK divider Low

    // 4. SDP Serial Audio Format (16-bit Standard I2S format, unmuted)
    writeReg(0x09, 0x0C); // DAC SDPIN: 16-bit standard I2S (bit 6 = 0)
    writeReg(0x0A, 0x0C); // ADC SDPOUT: 16-bit standard I2S (bit 6 = 0)

    // 5. System Power Setup
    writeReg(0x0B, 0x00);
    writeReg(0x0C, 0x00);
    writeReg(0x0D, 0x01); // Power up analog circuitry
    writeReg(0x0E, 0x02); // Enable analog PGA and ADC modulator
    writeReg(0x10, 0x1F);
    writeReg(0x11, 0x7F);
    writeReg(0x12, 0x00); // Power up DAC
    writeReg(0x13, 0x10); // Enable output to HP drive
    writeReg(0x14, 0x1A); // Analog differential mic input, max PGA gain

    // 6. ADC & Microphone Configuration (Analog Mic input, +42dB Max Gain, +6dB Digital Boost)
    writeReg(0x15, 0x40); // ADC PGA enable
    writeReg(0x16, 0x07); // Mic Gain Scale: +42 dB (ES8311_MIC_GAIN_42DB = 7)
    writeReg(0x17, 0xC8); // ADC digital volume (0xC8 = 200, matching official Waveshare driver)
    writeReg(0x1B, 0x0A);
    writeReg(0x1C, 0x6A); // ADC Equalizer bypass, cancel DC offset

    // 7. DAC & Output Volume
    writeReg(0x31, 0x00); // Unmute DAC
    setVolume(currentVolume);

    // 8. Output Power Stage & Internal reference
    writeReg(0x37, 0x08); // Bypass DAC equalizer
    writeReg(0x45, 0x00);

    CLOGI(TAG, "ES8311 registers initialized successfully (Mic Gain: +42dB, Digital Vol: 200, BCLK Div: 4)");
    return true;
}

bool AudioEngine::initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // ES8311 standard Stereo Duplex
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false, // ESP32-S3 uses PLL_D2 fractional divider for robust audio clock
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = PIN_I2S_MCLK,
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = PIN_I2S_DIN
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        CLOGE(TAG, "Failed to install I2S driver: %d", err);
        return false;
    }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
        CLOGE(TAG, "Failed to set I2S pins: %d", err);
        return false;
    }

    // Enable MCLK output clock on GPIO 14 at 16kHz Stereo (64 BCLKs per sample frame)
    i2s_set_clk(I2S_NUM_0, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    return true;
}

bool AudioEngine::begin(uint8_t volume) {
    currentVolume = volume;

    // Power gate setup
    pinMode(PIN_AUDIO_PWR, OUTPUT);
    powerOn();

    // Setup I2C if not already active
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    // Initialize ES8311 Codec and I2S
    if (!initCodecRegisters()) {
        CLOGE(TAG, "ES8311 I2C initialization failed");
        return false;
    }

    if (!initI2S()) {
        CLOGE(TAG, "ESP32-S3 I2S initialization failed");
        return false;
    }

    isInitialized = true;
    CLOGI(TAG, "AudioEngine ready (Vol: %d%%)", currentVolume);
    return true;
}

uint8_t calc_es8311_volume_reg(uint8_t volume) {
    if (volume == 0) return 0x00;
    if (volume > 100) volume = 100;
    // Calibracion acustica para el altavoz 1W/8ohm del Waveshare e-Paper:
    // 0 dBFS = registro 192. NUNCA boost por encima: con masters calientes
    // (picos 22000-26000) el +3.5 dB anterior saturaba el NS4150B y sonaba
    // "todo mal, saturado". 100% = 192 (0 dBFS, máximo limpio).
    //  - 100% -> 192 (0 dBFS)
    //  -  85% -> 186 (volumen estandar presente y claro)
    //  -  75% -> 183
    //  -  50% -> 175 (-8.0 dB)
    //  -  20% -> 120 (-35.5 dB descanso)
    //  -   1% -> 80  (-55.5 dB intimo)
    //  -   0% -> 0   (Mute total)
    if (volume <= 50) {
        int v = (int)volume;
        int reg = 80 + (v * 95) / 50;
        return (uint8_t)reg;
    } else {
        int v = (int)volume - 50;
        int reg = 175 + (v * 17) / 50; // 50%->175 ... 100%->192
        if (reg > 192) reg = 192;
        return (uint8_t)reg;
    }
}

void AudioEngine::setVolume(uint8_t volume) {
    currentVolume = constrain(volume, 0, 100);
    uint8_t regVal = calc_es8311_volume_reg(currentVolume);
    writeReg(0x32, regVal);
}

void AudioEngine::mute(bool enable) {
    if (enable) {
        writeReg(0x31, 0x60); // Mute DAC
    } else {
        writeReg(0x31, 0x00); // Unmute DAC
    }
}

void AudioEngine::playChimeSequence(const ChimeNote *notes, size_t numNotes, uint16_t totalMs, int16_t maxAmp) {
    if (!isInitialized || numNotes == 0) return;

    powerOn();
    mute(false);
    setVolume(currentVolume);
    i2s_set_clk(I2S_NUM_0, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

    size_t totalSamples = ((size_t)AUDIO_SAMPLE_RATE * totalMs) / 1000;
    const size_t CHUNK_SIZE = 256;
    int16_t stereoBuffer[CHUNK_SIZE * 2];

    float phases1[16] = {0};
    float phases2[16] = {0};
    size_t activeCount = min(numNotes, (size_t)16);

    float pInc1[16];
    float pInc2[16];
    size_t startSamples[16];
    size_t durSamples[16];
    size_t attackSamples = (AUDIO_SAMPLE_RATE * 16) / 1000; // 16ms velvety soft attack

    for (size_t n = 0; n < activeCount; n++) {
        pInc1[n] = 2.0f * 3.14159265f * notes[n].freqHz / (float)AUDIO_SAMPLE_RATE;
        pInc2[n] = 2.0f * 3.14159265f * (notes[n].freqHz * 2.0f) / (float)AUDIO_SAMPLE_RATE;
        startSamples[n] = ((size_t)AUDIO_SAMPLE_RATE * notes[n].startMs) / 1000;
        durSamples[n] = ((size_t)AUDIO_SAMPLE_RATE * notes[n].durationMs) / 1000;
    }

    size_t currentSample = 0;
    while (currentSample < totalSamples) {
        size_t toGenerate = min(CHUNK_SIZE, totalSamples - currentSample);

        for (size_t i = 0; i < toGenerate; i++) {
            size_t s = currentSample + i;
            float sampleSum = 0.0f;

            for (size_t n = 0; n < activeCount; n++) {
                if (s >= startSamples[n] && s < (startSamples[n] + durSamples[n])) {
                    size_t relS = s - startSamples[n];
                    float env = 0.0f;
                    if (relS < attackSamples) {
                        env = (float)relS / (float)attackSamples;
                    } else {
                        float t = (float)(relS - attackSamples) / (float)AUDIO_SAMPLE_RATE;
                        env = expf(-notes[n].decayRate * t);
                    }

                    // Warm, velvety acoustic synthesis: 86% fundamental + 14% octave (zero harshness)
                    float tone = (0.86f * sinf(phases1[n]) + 0.14f * sinf(phases2[n])) * env * notes[n].volume;
                    sampleSum += tone;

                    phases1[n] += pInc1[n];
                    if (phases1[n] >= 6.2831853f) phases1[n] -= 6.2831853f;
                    phases2[n] += pInc2[n];
                    if (phases2[n] >= 6.2831853f) phases2[n] -= 6.2831853f;
                }
            }

            int32_t val = (int32_t)(sampleSum * maxAmp);
            // Soft-limiter armonico suave: lineal hasta 14000
            if (val > 14000) val = 14000 + (val - 14000) / 4;
            else if (val < -14000) val = -14000 + (val + 14000) / 4;
            if (val > 24000) val = 24000;
            else if (val < -24000) val = -24000;
            stereoBuffer[2 * i] = (int16_t)val;
            stereoBuffer[2 * i + 1] = (int16_t)val;
        }

        size_t bytesWritten = 0;
        i2s_write(I2S_NUM_0, stereoBuffer, toGenerate * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        currentSample += toGenerate;
    }

    delay(20);
}

void AudioEngine::playTone(uint16_t freqHz, uint16_t durationMs) {
    ChimeNote note = { (float)freqHz, 0, durationMs, 5.0f, 0.85f };
    playChimeSequence(&note, 1, durationMs, 11000);
}

void AudioEngine::playStartupSound() {
    // Warm, soothing chord progression in low-mid register (C4 -> E4 -> G4 -> C5)
    ChimeNote startupNotes[] = {
        { 261.63f,   0, 600, 4.0f, 0.75f }, // C4 (warm fundamental)
        { 329.63f, 120, 550, 4.5f, 0.80f }, // E4
        { 392.00f, 240, 500, 5.0f, 0.85f }, // G4
        { 523.25f, 360, 600, 3.5f, 0.90f }  // C5 (peaceful finish)
    };
    playChimeSequence(startupNotes, 4, 950, 12000);
}

void AudioEngine::playWakeupSound() {
    // Soft, sweet twin chime on wakeup (C4 -> G4)
    ChimeNote wakeNotes[] = {
        { 261.63f,   0, 300, 6.0f, 0.75f }, // C4
        { 392.00f, 120, 450, 4.5f, 0.85f }  // G4
    };
    playChimeSequence(wakeNotes, 2, 550, 12000);
}

void AudioEngine::playSleepSound() {
    // Calming bedtime lullaby descent (G4 -> E4 -> C4) with long warm decay
    ChimeNote sleepNotes[] = {
        { 392.00f,   0, 500, 4.5f, 0.80f }, // G4
        { 329.63f, 180, 600, 4.0f, 0.85f }, // E4
        { 261.63f, 380, 800, 3.0f, 0.90f }  // C4 (deep relaxation)
    };
    playChimeSequence(sleepNotes, 3, 1200, 11000);
}

void AudioEngine::playListeningSound() {
    // Subtle, low droplet chime (D4 -> G4, non-jarring, intimate volume)
    ChimeNote listNotes[] = {
        { 293.66f,  0, 220, 9.0f, 0.70f }, // D4
        { 392.00f, 80, 350, 6.5f, 0.80f }  // G4
    };
    playChimeSequence(listNotes, 2, 450, 9000);
}

void AudioEngine::playThinkingSound() {
    // Extremely subtle water droplet tick (C5 at very low volume)
    ChimeNote thinkNotes[] = {
        { 523.25f, 0, 120, 16.0f, 0.30f }
    };
    playChimeSequence(thinkNotes, 1, 150, 7000);
}

void AudioEngine::playSuccessSound() {
    // Warm marimba twin chime (C4 -> F4)
    ChimeNote okNotes[] = {
        { 261.63f,  0, 250, 7.0f, 0.75f }, // C4
        { 349.23f, 90, 400, 5.0f, 0.85f }  // F4
    };
    playChimeSequence(okNotes, 2, 500, 12000);
}

void AudioEngine::playErrorSound() {
    // Gentle soft apologetic sigh (E4 -> C4)
    ChimeNote errNotes[] = {
        { 329.63f,   0, 250, 6.5f, 0.65f }, // E4
        { 261.63f, 130, 400, 4.5f, 0.70f }  // C4
    };
    playChimeSequence(errNotes, 2, 550, 10000);
}

void AudioEngine::playCancelSound() {
    // Soft subtle muted drop when no speech detected (G4 -> E4)
    ChimeNote cancelNotes[] = {
        { 392.00f,  0, 200, 10.0f, 0.60f },
        { 329.63f, 70, 250,  8.0f, 0.65f }
    };
    playChimeSequence(cancelNotes, 2, 350, 9000);
}

void AudioEngine::playChimeUp() {
    playStartupSound();
}

void AudioEngine::playChimeDown() {
    playSleepSound();
}

void AudioEngine::buildWavHeader(uint8_t *header, uint32_t pcmDataSize, uint32_t sampleRate) {
    uint32_t totalFileSize = pcmDataSize + 36;
    uint32_t byteRate = sampleRate * 1 * sizeof(int16_t); // 16-bit mono

    memcpy(header, "RIFF", 4);
    header[4] = (uint8_t)(totalFileSize & 0xFF);
    header[5] = (uint8_t)((totalFileSize >> 8) & 0xFF);
    header[6] = (uint8_t)((totalFileSize >> 16) & 0xFF);
    header[7] = (uint8_t)((totalFileSize >> 24) & 0xFF);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0; // Subchunk1Size
    header[20] = 1;  header[21] = 0; // AudioFormat (1 = PCM)
    header[22] = 1;  header[23] = 0; // NumChannels (1 = Mono)
    header[24] = (uint8_t)(sampleRate & 0xFF);
    header[25] = (uint8_t)((sampleRate >> 8) & 0xFF);
    header[26] = (uint8_t)((sampleRate >> 16) & 0xFF);
    header[27] = (uint8_t)((sampleRate >> 24) & 0xFF);
    header[28] = (uint8_t)(byteRate & 0xFF);
    header[29] = (uint8_t)((byteRate >> 8) & 0xFF);
    header[30] = (uint8_t)((byteRate >> 16) & 0xFF);
    header[31] = (uint8_t)((byteRate >> 24) & 0xFF);
    header[32] = 2;  header[33] = 0; // BlockAlign (NumChannels * BitsPerSample/8)
    header[34] = 16; header[35] = 0; // BitsPerSample
    memcpy(header + 36, "data", 4);
    header[40] = (uint8_t)(pcmDataSize & 0xFF);
    header[41] = (uint8_t)((pcmDataSize >> 8) & 0xFF);
    header[42] = (uint8_t)((pcmDataSize >> 16) & 0xFF);
    header[43] = (uint8_t)((pcmDataSize >> 24) & 0xFF);
}

bool AudioEngine::recordWav(uint8_t **outWavBuffer, size_t *outWavSize, uint32_t maxDurationMs, StopCheckCallback stopCb, uint32_t silenceStartMs, uint32_t silenceEndMs) {
    if (!isInitialized) return false;

    // Allocate memory in PSRAM or heap for Mono WAV output
    size_t pcmCapacity = ((size_t)AUDIO_SAMPLE_RATE * maxDurationMs / 1000) * sizeof(int16_t);
    size_t totalAlloc = pcmCapacity + 44; // 44 bytes for standard WAV header

    uint8_t *buffer = nullptr;
    if (psramFound()) {
        buffer = (uint8_t *)ps_malloc(totalAlloc);
    }
    if (!buffer) {
        buffer = (uint8_t *)malloc(totalAlloc);
    }
    if (!buffer) {
        CLOGE(TAG, "Insufficient memory to record audio!");
        return false;
    }

    // Configure I2S Clock to 16kHz Stereo Duplex
    i2s_set_clk(I2S_NUM_0, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    i2s_zero_dma_buffer(I2S_NUM_0);

    // Ensure ES8311 ADC and Microphone path are active & powered
    writeReg(0x0E, 0x02); // Enable analog PGA and ADC modulator
    writeReg(0x14, 0x1A); // Analog differential mic, max PGA gain
    writeReg(0x16, 0x07); // Mic Gain Scale: +42 dB (Max sensitivity)
    writeReg(0x17, 0xC8); // ADC digital volume (0xC8 = 200)
    writeReg(0x0A, 0x0C); // Unmute ADC SDP

    int16_t *monoPcm = (int16_t *)(buffer + 44);
    size_t monoSamplesTotal = 0;
    size_t maxMonoSamples = pcmCapacity / sizeof(int16_t);

    unsigned long startTime = millis();
    unsigned long lastSpeechTime = millis();
    unsigned long speechStartTime = 0;
    bool speechDetected = false;

    // Intelligent VAD parameters:
    // VOICE_ENERGY_THRESHOLD lowered to 300 so quiet speech, whispers and word endings aren't mistaken for silence.
    // END_OF_SPEECH_SILENCE_MS set to at least 2200ms so pauses between words don't cut the user off prematurely!
    const int32_t VOICE_ENERGY_THRESHOLD = 300;
    const uint32_t INITIAL_SILENCE_TIMEOUT_MS = (silenceStartMs < 1500) ? 3000 : silenceStartMs;
    const uint32_t END_OF_SPEECH_SILENCE_MS   = (silenceEndMs < 1500) ? 2200 : silenceEndMs;
    const uint32_t MIN_SPEECH_DURATION_MS     = 400;

    CLOGI(TAG, "Starting intelligent audio recording (VAD + 2nd-click check, up to %u ms)...", maxDurationMs);
    pinMode(PIN_USER_LED, OUTPUT);
    digitalWrite(PIN_USER_LED, USER_LED_ON); // Turn ON LED: Mic is ACTIVE

    // Read buffer for stereo frames from I2S DMA
    // 256 frames = 256 Left + 256 Right = 512 int16_t = 1024 bytes (16ms per chunk)
    const size_t STEREO_CHUNK_FRAMES = 256;
    int16_t stereoChunk[STEREO_CHUNK_FRAMES * 2];

    int64_t totalEnergyLeft = 0;
    int64_t totalEnergyRight = 0;
    int chunkCount = 0;
    int activeChannel = -1; // -1: auto-detecting, 0: Left, 1: Right, 2: Both

    while (monoSamplesTotal < maxMonoSamples && (millis() - startTime) < maxDurationMs) {
        // 1. Check for manual cut via 2nd button click
        if (stopCb && stopCb()) {
            CLOGI(TAG, "Recording stopped manually by user 2nd button click!");
            break;
        }

        // 2. Read stereo chunk from I2S
        size_t bytesRead = 0;
        esp_err_t res = i2s_read(I2S_NUM_0, stereoChunk, sizeof(stereoChunk), &bytesRead, pdMS_TO_TICKS(80));

        if (res == ESP_OK && bytesRead > 0) {
            size_t framesRead = bytesRead / (2 * sizeof(int16_t));
            if (framesRead == 0) continue;

            // Channel detection for the first 8 chunks (~128ms)
            if (chunkCount < 8) {
                for (size_t i = 0; i < framesRead; i++) {
                    totalEnergyLeft += abs((int32_t)stereoChunk[2 * i]);
                    totalEnergyRight += abs((int32_t)stereoChunk[2 * i + 1]);
                }
                chunkCount++;
                if (chunkCount == 8) {
                    if (totalEnergyRight > totalEnergyLeft * 2) {
                        activeChannel = 1; // RIGHT channel is microphone
                    } else if (totalEnergyLeft > totalEnergyRight * 2) {
                        activeChannel = 0; // LEFT channel is microphone
                    } else {
                        activeChannel = 2; // Signal present on both
                    }
                    CLOGI(TAG, "Mic Channel Auto-Detected: %s (L_energy: %lld, R_energy: %lld)",
                          (activeChannel == 1 ? "RIGHT" : (activeChannel == 0 ? "LEFT" : "BOTH")),
                          totalEnergyLeft, totalEnergyRight);
                }
            }

            // 3. Extract active mono samples and compute frame energy
            int32_t chunkEnergySum = 0;
            size_t samplesToAdd = min(framesRead, maxMonoSamples - monoSamplesTotal);

            for (size_t i = 0; i < samplesToAdd; i++) {
                int16_t sLeft = stereoChunk[2 * i];
                int16_t sRight = stereoChunk[2 * i + 1];
                int16_t monoSample;

                if (activeChannel == 1) {
                    monoSample = sRight;
                } else if (activeChannel == 0) {
                    monoSample = sLeft;
                } else {
                    // Fallback / before detection / or both: pick the one with greater magnitude
                    monoSample = (abs((int32_t)sRight) > abs((int32_t)sLeft)) ? sRight : sLeft;
                }

                monoPcm[monoSamplesTotal + i] = monoSample;
                chunkEnergySum += abs((int32_t)monoSample);
            }

            monoSamplesTotal += samplesToAdd;
            int32_t avgEnergy = (samplesToAdd > 0) ? (chunkEnergySum / samplesToAdd) : 0;

            // 4. Voice Activity Detection logic
            if (avgEnergy >= VOICE_ENERGY_THRESHOLD) {
                if (!speechDetected) {
                    speechDetected = true;
                    speechStartTime = millis();
                    CLOGI(TAG, "Voice activity detected! Energy: %d (at %lu ms)", avgEnergy, millis() - startTime);
                }
                lastSpeechTime = millis();
            }

            // 5. Initial Silence Cutoff: No speech began within 2.4s -> cancel cleanly
            if (!speechDetected && (millis() - startTime > INITIAL_SILENCE_TIMEOUT_MS)) {
                CLOGI(TAG, "No speech detected within %u ms. Canceling early to save time.", INITIAL_SILENCE_TIMEOUT_MS);
                digitalWrite(PIN_USER_LED, USER_LED_OFF);
                freeWavBuffer(buffer);
                *outWavBuffer = nullptr;
                *outWavSize = 0;
                return false;
            }

            // 6. End of Speech Detection: User spoke and paused for 1.2s -> finish phrase
            if (speechDetected && (millis() - speechStartTime >= MIN_SPEECH_DURATION_MS)) {
                if (millis() - lastSpeechTime > END_OF_SPEECH_SILENCE_MS) {
                    CLOGI(TAG, "End-of-speech pause detected (%lu ms of silence). Completing recording!", millis() - lastSpeechTime);
                    break;
                }
            }
        }
    }

    // Recording loop finished (VAD, 2nd click, or max duration): Turn OFF LED immediately!
    digitalWrite(PIN_USER_LED, USER_LED_OFF);

    size_t bytesRecorded = monoSamplesTotal * sizeof(int16_t);
    if (bytesRecorded < 1600) { // Under 100ms of audio
        CLOGW(TAG, "Recorded audio too short (%u bytes). Discarding.", bytesRecorded);
        freeWavBuffer(buffer);
        *outWavBuffer = nullptr;
        *outWavSize = 0;
        return false;
    }

    CLOGI(TAG, "Recording completed successfully: %u bytes mono PCM (%lu ms)",
          bytesRecorded, (bytesRecorded * 1000) / (AUDIO_SAMPLE_RATE * sizeof(int16_t)));

    // Build standard WAV header at the start of buffer
    buildWavHeader(buffer, bytesRecorded, AUDIO_SAMPLE_RATE);

    *outWavBuffer = buffer;
    *outWavSize = bytesRecorded + 44;
    return true;
}

void AudioEngine::freeWavBuffer(uint8_t *wavBuffer) {
    if (wavBuffer) {
        free(wavBuffer);
    }
}

void AudioEngine::setPlaybackSpeed(uint8_t speedPercent) {
    // El "speed" se implementa cambiando el clock del I2S: mueve pitch+tempo
    // a la vez (efecto chipmunk). Rango útil para voz: 80-125%. La música
    // fuerza 100 explícito en los streamers. Más allá de 125% la voz se
    // vuelve aguda/inentendible, por eso se recorta acá y no solo en config.
    if (speedPercent < 80) speedPercent = 80;
    if (speedPercent > 125) speedPercent = 125;
    currentPlaybackSpeed = speedPercent;
    CLOGI(TAG, "Velocidad de reproducción configurada: %d%% (%.2fx)", currentPlaybackSpeed, currentPlaybackSpeed / 100.0f);
}

void AudioEngine::startPlayback(uint32_t sampleRate) {
    powerOn();
    mute(true); // arrancar muteado: el "ploc" sale del amplificador
    // prendido con basura vieja en el bus/DMA
    setVolume(currentVolume);
    uint32_t effectiveRate = sampleRate;
    if (currentPlaybackSpeed >= 80 && currentPlaybackSpeed <= 125 && currentPlaybackSpeed != 100) {
        effectiveRate = (sampleRate * (uint32_t)currentPlaybackSpeed) / 100;
    }
    i2s_set_clk(I2S_NUM_0, effectiveRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    i2s_zero_dma_buffer(I2S_NUM_0);
    probeReset(); // la sonda mide ESTE playback (ver /api/audio_probe?mode=last)
    delay(30); // estabiliza fuente + bus antes de abrir el parlante
    mute(false);
}

size_t AudioEngine::writePcmChunk(const uint8_t *data, size_t len, int gain256) {
    // Anti-saturación: nunca reproducir basura o buffers degenerados a máximo volumen.
    // - Puntero nulo o menos de 1 muestra: no hay nada válido que enviar al DAC.
    // - Longitud impar: recorta el byte huérfano (un int16_t a medias es un "click" a full-scale).
    // - DAC no inicializado: evita blast de ruido si el codec aún no está listo.
    if (!isInitialized || data == nullptr || len < sizeof(int16_t)) return 0;
    len &= ~(size_t)1; // trunca byte impar residual de bordes de chunk HTTP/base64
    // Matemática en dsp_audio.h (fuente única de verdad, testeable en PC).
    // If incoming data is mono 16-bit PCM, expand to stereo so both DAC channels are driven
    const int16_t *monoSamples = (const int16_t *)data;
    size_t numSamples = len / sizeof(int16_t);
    const size_t BATCH = 256;
    int16_t stereoBuf[BATCH * 2];

    size_t processed = 0;
    while (processed < numSamples) {
        size_t batchCount = min(BATCH, numSamples - processed);

        for (size_t i = 0; i < batchCount; i++) {
            int16_t s = monoSamples[processed + i];
            bool lim = false, clip = false;
            int32_t v = dsp_apply_sample_ex(s, gain256, &lim, &clip);
            // Sonda (diagnóstico remoto sin oído)
            int32_t pre = s < 0 ? -(int32_t)s : (int32_t)s;
            int32_t post = v < 0 ? -v : v;
            if (pre > s_prPeakPre) s_prPeakPre = pre;
            if (post > s_prPeakPost) s_prPeakPost = post;
            if (lim) s_prLimited++;
            if (clip) s_prClipped++;
            s_prSamples++;
            s_prSumSq += (int64_t)v * (int64_t)v;
            s_prSum += (int64_t)v;

            int16_t o = (int16_t)v;
            stereoBuf[2 * i] = o;
            stereoBuf[2 * i + 1] = o;
        }
        size_t bytesWritten = 0;
        esp_err_t err = i2s_write(I2S_NUM_0, stereoBuf, batchCount * 2 * sizeof(int16_t), &bytesWritten, pdMS_TO_TICKS(500));
        if (err != ESP_OK) {
            CLOGW(TAG, "i2s_write error / timeout: %d", err);
            break;
        }
        processed += batchCount;
    }
    return processed * sizeof(int16_t);
}

void AudioEngine::stopPlayback() {
    delay(60);
    mute(true); // cerrar muteado: evita el click de cola al drenar el DMA
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_set_clk(I2S_NUM_0, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

void AudioEngine::probeReset() {
    s_prSamples = 0;
    s_prLimited = 0;
    s_prClipped = 0;
    s_prPeakPre = 0;
    s_prPeakPost = 0;
    s_prSumSq = 0;
    s_prSum = 0;
}

void AudioEngine::probeSnapshot(AudioProbe &out) {
    out.samples = s_prSamples;
    out.limited = s_prLimited;
    out.clipped = s_prClipped;
    out.peakPre = s_prPeakPre;
    out.peakPost = s_prPeakPost;
    out.sumSq = s_prSumSq;
    out.sum = s_prSum;
}

void AudioEngine::playPcmData(const uint8_t *pcmData, size_t len, uint32_t sampleRate, AudioStreamCallback cb) {
    startPlayback(sampleRate);
    size_t offset = 0;
    size_t chunkSize = 1024;
    int frameCounter = 0;

    while (offset < len) {
        size_t toSend = min(chunkSize, len - offset);
        writePcmChunk(pcmData + offset, toSend);
        offset += toSend;

        // Callback for lip-sync animation
        if (cb && (++frameCounter % 4 == 0)) {
            cb(frameCounter % 8 < 4); // Alternate mouth open/close
        }
    }
    stopPlayback();
}
