#ifndef SENSOR_SHTC3_H
#define SENSOR_SHTC3_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

class SHTC3Sensor {
public:
    SHTC3Sensor();
    bool begin();
    bool readSample(float &outTempC, float &outHumidity);

    float getTemperature() const { return lastTempC; }
    float getHumidity() const { return lastHumidity; }
    bool  isValid() const { return dataValid; }
    bool  isConnected() const { return isDetected; }

private:
    float lastTempC;
    float lastHumidity;
    bool  dataValid;
    bool  isDetected;

    bool sendCommand(uint16_t cmd);
    uint8_t checkCrc(const uint8_t *data, size_t len);
};

extern SHTC3Sensor envSensor;

// Global sensor values accessible anywhere (e.g. for E-Paper TopBar)
extern float g_ambientTempC;
extern float g_ambientHum;
extern bool  g_ambientSensorValid;

#endif // SENSOR_SHTC3_H
