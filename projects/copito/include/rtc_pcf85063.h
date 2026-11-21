#ifndef RTC_PCF85063_H
#define RTC_PCF85063_H

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include "config.h"

// Registro base del PCF85063 (I2C @ 0x51)
#define PCF85063_REG_CTRL1      0x00
#define PCF85063_REG_CTRL2      0x01
#define PCF85063_REG_OFFSET     0x02
#define PCF85063_REG_RAM        0x03
#define PCF85063_REG_SEC        0x04
#define PCF85063_REG_MIN        0x05
#define PCF85063_REG_HOUR       0x06
#define PCF85063_REG_DAY        0x07
#define PCF85063_REG_WDAY       0x08
#define PCF85063_REG_MONTH      0x09
#define PCF85063_REG_YEAR       0x0A

class PCF85063Driver {
public:
    PCF85063Driver();
    bool begin(TwoWire &wire = Wire);
    bool isPresent();
    
    // Lectura y sincronización del RTC hacia la estructura tm
    bool readTime(struct tm &outTm);
    
    // Escritura de la hora desde tm hacia el RTC
    bool setTime(const struct tm &inTm);
    
    // Sincroniza la hora del RTC hacia el reloj interno del sistema ESP32 (settimeofday)
    bool syncSystemTimeFromRtc();

    // Sincroniza la hora actual del sistema ESP32 hacia el RTC de hardware
    bool syncRtcFromSystemTime();

    static inline uint8_t bcdToDec(uint8_t val) {
        return ((val >> 4) * 10) + (val & 0x0F);
    }
    static inline uint8_t decToBcd(uint8_t val) {
        return ((val / 10) << 4) | (val % 10);
    }

private:
    TwoWire *wirePtr;
    bool initialized;
};

extern PCF85063Driver rtcDriver;

#endif // RTC_PCF85063_H
