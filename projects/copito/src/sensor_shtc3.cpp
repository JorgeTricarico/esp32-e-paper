#include "sensor_shtc3.h"

static const char *TAG = "SHTC3";

SHTC3Sensor envSensor;

// Global sensor values for display and status
float g_ambientTempC = 0.0f;
float g_ambientHum = 0.0f;
bool  g_ambientSensorValid = false;

SHTC3Sensor::SHTC3Sensor()
    : lastTempC(0.0f), lastHumidity(0.0f), dataValid(false), isDetected(false) {}

uint8_t SHTC3Sensor::checkCrc(const uint8_t *data, size_t len) {
    // Sensirion CRC-8 (polynomial 0x31, init 0xFF)
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 8; bit > 0; --bit) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc = (crc << 1);
        }
    }
    return crc;
}

bool SHTC3Sensor::sendCommand(uint16_t cmd) {
    Wire.beginTransmission(SHTC3_I2C_ADDR);
    Wire.write((uint8_t)(cmd >> 8));
    Wire.write((uint8_t)(cmd & 0xFF));
    return (Wire.endTransmission() == 0);
}

bool SHTC3Sensor::begin() {
    CopitoI2cGuard guard;
    if (!guard.locked) return false;
    // 1. Send Wakeup command (0x3517)
    if (!sendCommand(0x3517)) {
        log_w("SHTC3: no responde en direccion 0x70");
        isDetected = false;
        return false;
    }
    delayMicroseconds(350); // > 240us wake-up time

    // 2. Query Device ID (0xEFC8)
    if (sendCommand(0xEFC8)) {
        if (Wire.requestFrom((uint8_t)SHTC3_I2C_ADDR, (uint8_t)3) == 3) {
            uint8_t idMsb = Wire.read();
            uint8_t idLsb = Wire.read();
            uint8_t idCrc = Wire.read();
            uint8_t idBuf[2] = { idMsb, idLsb };
            if (checkCrc(idBuf, 2) == idCrc) {
                uint16_t id = ((uint16_t)idMsb << 8) | idLsb;
                log_i("SHTC3: Sensor ambiental detectado con exito (ID: 0x%04X)", id);
                isDetected = true;
            }
        }
    }

    if (!isDetected) {
        log_i("SHTC3: Asumiendo sensor activo en 0x70");
        isDetected = true;
    }

    // 3. Initial reading
    float t, h;
    if (readSample(t, h)) {
        log_i("SHTC3: Lectura inicial -> %.1f C, %.1f%% HR", t, h);
    }

    // 4. Put sensor into sleep mode until next read
    sendCommand(0xB098);
    return isDetected;
}

bool SHTC3Sensor::readSample(float &outTempC, float &outHumidity) {
    CopitoI2cGuard guard;
    if (!guard.locked) return false;
    // 1. Wakeup
    if (!sendCommand(0x3517)) {
        return false;
    }
    delayMicroseconds(350);

    // 2. Measure command: Normal mode, Clock Stretching disabled, Read T first (0x7866)
    if (!sendCommand(0x7866)) {
        sendCommand(0xB098); // Sleep
        return false;
    }
    delay(15); // Normal measurement time is max 12.1 ms

    // 3. Read 6 bytes: [T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC]
    if (Wire.requestFrom((uint8_t)SHTC3_I2C_ADDR, (uint8_t)6) != 6) {
        sendCommand(0xB098); // Sleep
        return false;
    }

    uint8_t raw[6];
    for (int i = 0; i < 6; i++) {
        raw[i] = Wire.read();
    }

    // 4. Put back to sleep immediately to eliminate self-heating
    sendCommand(0xB098);

    // 5. Verify CRCs
    if (checkCrc(raw, 2) != raw[2] || checkCrc(raw + 3, 2) != raw[5]) {
        log_w("SHTC3: CRC mismatch en medicion ambiental");
        return false;
    }

    // 6. Convert raw readings to engineering units
    uint16_t rawT = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t rawRH = ((uint16_t)raw[3] << 8) | raw[4];

    lastTempC = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    lastHumidity = 100.0f * ((float)rawRH / 65535.0f);
    if (lastHumidity < 0.0f) lastHumidity = 0.0f;
    if (lastHumidity > 100.0f) lastHumidity = 100.0f;

    dataValid = true;
    outTempC = lastTempC;
    outHumidity = lastHumidity;

    // Update global variables for E-Paper rendering
    g_ambientTempC = lastTempC;
    g_ambientHum = lastHumidity;
    g_ambientSensorValid = true;

    return true;
}
