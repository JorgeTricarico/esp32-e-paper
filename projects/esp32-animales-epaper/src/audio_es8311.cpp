#include "audio_es8311.h"
#include <Wire.h>
#include <LittleFS.h>
#ifdef ENABLE_SD_FALLBACK
#include <SD.h>
#include <SPI.h>
#endif

AudioEs8311 audio;

static uint8_t calcVolumeReg(uint8_t volume) {
    if (volume == 0) return 0x00;
    if (volume > 100) volume = 100;
    // 100% -> 192 (0 dBFS limpio sin saturar el altavoz 1W/8ohm)
    if (volume <= 50) {
        return (uint8_t)(80 + ((int)volume * 95) / 50);
    }
    int reg = 175 + (((int)volume - 50) * 17) / 50;
    return (uint8_t)(reg > 192 ? 192 : reg);
}

AudioEs8311::AudioEs8311()
    : initialized(false), currentVolume(85), lastSampleRate(AUDIO_DEFAULT_SAMPLE_RATE),
      sessionActive(false), ampActive(false), abortRequested(false) {}

AudioEs8311::~AudioEs8311() {
    powerOff();
}

bool AudioEs8311::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

void AudioEs8311::powerOn() {
    pinMode(PIN_AUDIO_PWR, OUTPUT);
    digitalWrite(PIN_AUDIO_PWR, AUDIO_PWR_ON);
    pinMode(PIN_AUDIO_PA, OUTPUT);
    digitalWrite(PIN_AUDIO_PA, AUDIO_PA_OFF); // PA apagado hasta reproducir
    delay(15);
}

void AudioEs8311::powerOff() {
    enableSpeakerAmp(false);
    mute(true);
    digitalWrite(PIN_AUDIO_PWR, AUDIO_PWR_OFF);
}

void AudioEs8311::enableSpeakerAmp(bool enable) {
    digitalWrite(PIN_AUDIO_PA, enable ? AUDIO_PA_ON : AUDIO_PA_OFF);
    ampActive = enable;
}

void AudioEs8311::beginSession() {
    sessionActive = true;
}

void AudioEs8311::drainDma() {
    int16_t zeros[512];
    memset(zeros, 0, sizeof(zeros));
    size_t bytesWritten = 0;
    for (int f = 0; f < 18; f++) {
        i2s_write(I2S_NUM_0, zeros, sizeof(zeros), &bytesWritten, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void AudioEs8311::endSession() {
    sessionActive = false;
    if (ampActive) {
        drainDma();
        enableSpeakerAmp(false);
        mute(true);
    }
}

void AudioEs8311::requestAbort() {
    abortRequested = true;
}

void AudioEs8311::clearAbort() {
    abortRequested = false;
}

bool AudioEs8311::initCodecRegisters() {
    writeReg(0x44, 0x08);
    writeReg(0x44, 0x08);

    // Soft reset & Power-on
    if (!writeReg(0x00, 0x1F)) return false;
    delay(20);
    writeReg(0x00, 0x00);
    writeReg(0x00, 0x80);

    // Clocks (MCLK/BCLK/DAC)
    writeReg(0x01, 0x3F);
    writeReg(0x02, 0x00);
    writeReg(0x03, 0x10);
    writeReg(0x04, 0x10);
    writeReg(0x05, 0x00);
    writeReg(0x06, 0x03); // BCLK div = 4
    writeReg(0x07, 0x00);
    writeReg(0x08, 0xFF);

    // 16-bit Standard I2S
    writeReg(0x09, 0x0C);
    writeReg(0x0A, 0x0C);

    // System Power & DAC output
    writeReg(0x0B, 0x00);
    writeReg(0x0C, 0x00);
    writeReg(0x0D, 0x01);
    writeReg(0x0E, 0x02);
    writeReg(0x10, 0x1F);
    writeReg(0x11, 0x7F);
    writeReg(0x12, 0x00);
    writeReg(0x13, 0x10);
    writeReg(0x14, 0x1A);

    writeReg(0x37, 0x08);
    writeReg(0x45, 0x00);

    setVolume(currentVolume);
    mute(true); // Silencio hasta reproducir
    return true;
}

bool AudioEs8311::initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = AUDIO_DEFAULT_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
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

    if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr) != ESP_OK) {
        return false;
    }
    if (i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) {
        return false;
    }
    i2s_set_clk(I2S_NUM_0, AUDIO_DEFAULT_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    return true;
}

bool AudioEs8311::begin(uint8_t volume) {
    currentVolume = volume;
    powerOn();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    if (!initCodecRegisters()) {
        log_e("ES8311 no responde en I2C 0x%02X", ES8311_I2C_ADDR);
        return false;
    }
    if (!initI2S()) {
        log_e("Fallo inicializando I2S_NUM_0");
        return false;
    }

    initialized = true;
    log_i("ES8311 listo (Vol=%u%%, PA apagado en reposo)", currentVolume);
    return true;
}

void AudioEs8311::setVolume(uint8_t volume) {
    currentVolume = (volume > 100) ? 100 : volume;
    writeReg(0x32, calcVolumeReg(currentVolume));
}

void AudioEs8311::mute(bool enable) {
    writeReg(0x31, enable ? 0x60 : 0x00);
}

static bool parseWavHeader(File &f, uint32_t &sampleRate, uint16_t &channels, uint32_t &dataBytes) {
    uint8_t hdr[44];
    if (f.read(hdr, sizeof(hdr)) != sizeof(hdr)) return false;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

    channels = (uint16_t)(hdr[22] | (hdr[23] << 8));
    sampleRate = (uint32_t)(hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24));
    uint16_t bits = (uint16_t)(hdr[34] | (hdr[35] << 8));
    if (bits != 16 || (channels != 1 && channels != 2)) return false;
    if (sampleRate < 8000 || sampleRate > 48000) return false;

    // Buscar chunk 'data' (soporta headers extendidos y padding RIFF par)
    f.seek(12);
    uint8_t chunkHdr[8];
    while (f.read(chunkHdr, 8) == 8) {
        uint32_t sz = (uint32_t)(chunkHdr[4] | (chunkHdr[5] << 8) | (chunkHdr[6] << 16) | (chunkHdr[7] << 24));
        if (memcmp(chunkHdr, "data", 4) == 0) {
            dataBytes = sz;
            return true;
        }
        uint32_t skip = sz + (sz & 1U);
        f.seek(f.position() + skip);
    }
    return false;
}

uint32_t AudioEs8311::playWavFile(const char *path) {
    if (!initialized || !path || abortRequested) return 0;

    File f = LittleFS.open(path, "r");
#ifdef ENABLE_SD_FALLBACK
    if (!f) {
        f = SD.open(path, FILE_READ);
    }
#endif
    if (!f) {
        log_w("Audio no encontrado: %s", path);
        return 0;
    }

    uint32_t sampleRate = AUDIO_DEFAULT_SAMPLE_RATE;
    uint16_t channels = 1;
    uint32_t dataBytes = 0;
    if (!parseWavHeader(f, sampleRate, channels, dataBytes)) {
        log_e("Cabecera WAV invalida en %s", path);
        f.close();
        return 0;
    }

    if (sampleRate != lastSampleRate) {
        i2s_set_clk(I2S_NUM_0, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
        lastSampleRate = sampleRate;
    }

    const size_t CHUNK_SAMPLES = 256;
    int16_t monoBuf[CHUNK_SAMPLES];
    int16_t stereoBuf[CHUNK_SAMPLES * 2];
    size_t bytesWritten = 0;

    if (!ampActive) {
        mute(false);
        enableSpeakerAmp(true);
        vTaskDelay(pdMS_TO_TICKS(45)); // Soft-start completo del NS4150B
        memset(stereoBuf, 0, sizeof(stereoBuf));
        for (int w = 0; w < 4; w++) {
            i2s_write(I2S_NUM_0, stereoBuf, sizeof(stereoBuf), &bytesWritten, portMAX_DELAY);
        }
    }

    uint32_t remaining = dataBytes;
    uint32_t totalSamplesPlayed = 0;

    while (remaining > 0 && !abortRequested) {
        if (channels == 1) {
            size_t toRead = min((size_t)remaining, sizeof(monoBuf));
            int nRead = f.read((uint8_t *)monoBuf, toRead);
            if (nRead <= 0) break;
            size_t samples = (size_t)nRead / 2;
            for (size_t i = 0; i < samples; i++) {
                stereoBuf[i * 2] = monoBuf[i];
                stereoBuf[i * 2 + 1] = monoBuf[i];
            }
            i2s_write(I2S_NUM_0, stereoBuf, samples * 4, &bytesWritten, portMAX_DELAY);
            totalSamplesPlayed += samples;
            remaining -= (uint32_t)nRead;
        } else {
            size_t toRead = min((size_t)remaining, sizeof(stereoBuf));
            int nRead = f.read((uint8_t *)stereoBuf, toRead);
            if (nRead <= 0) break;
            i2s_write(I2S_NUM_0, stereoBuf, (size_t)nRead, &bytesWritten, portMAX_DELAY);
            totalSamplesPlayed += (uint32_t)nRead / 4;
            remaining -= (uint32_t)nRead;
        }
    }

    if (!sessionActive || abortRequested) {
        drainDma();
        enableSpeakerAmp(false);
        mute(true);
    }
    f.close();

    return (uint32_t)((totalSamplesPlayed * 1000ULL) / sampleRate);
}
