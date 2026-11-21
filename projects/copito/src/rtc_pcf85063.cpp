#include "rtc_pcf85063.h"
#include <sys/time.h>

static const char *TAG = "PCF85063";

PCF85063Driver rtcDriver;

PCF85063Driver::PCF85063Driver() : wirePtr(&Wire), initialized(false) {}

bool PCF85063Driver::begin(TwoWire &wire) {
    CopitoI2cGuard guard;
    if (!guard.locked) return false;
    wirePtr = &wire;
    wirePtr->beginTransmission(RTC_PCF85063_ADDR);
    if (wirePtr->endTransmission() != 0) {
        log_w("PCF85063 RTC no detectado en bus I2C @ 0x%02X", RTC_PCF85063_ADDR);
        initialized = false;
        return false;
    }

    // Inicializar registros de control (arranque de oscilador, deshabilitar stop)
    wirePtr->beginTransmission(RTC_PCF85063_ADDR);
    wirePtr->write(PCF85063_REG_CTRL1);
    wirePtr->write(0x00); // Normal mode, 24h format, RTC run
    wirePtr->write(0x00); // Control 2: alarm disabled, timer disabled
    wirePtr->endTransmission();

    initialized = true;
    log_i("PCF85063 RTC inicializado exitosamente @ 0x%02X", RTC_PCF85063_ADDR);
    return true;
}

bool PCF85063Driver::isPresent() {
    if (!wirePtr) return false;
    CopitoI2cGuard guard;
    if (!guard.locked) return false;
    wirePtr->beginTransmission(RTC_PCF85063_ADDR);
    return (wirePtr->endTransmission() == 0);
}

bool PCF85063Driver::readTime(struct tm &outTm) {
    CopitoI2cGuard guard;
    if (!guard.locked) return false;
    if (!initialized && !begin(*wirePtr)) return false;

    wirePtr->beginTransmission(RTC_PCF85063_ADDR);
    wirePtr->write(PCF85063_REG_SEC);
    if (wirePtr->endTransmission() != 0) return false;

    if (wirePtr->requestFrom((uint8_t)RTC_PCF85063_ADDR, (uint8_t)7) != 7) {
        return false;
    }

    uint8_t rawSec  = wirePtr->read();
    uint8_t rawMin  = wirePtr->read();
    uint8_t rawHour = wirePtr->read();
    uint8_t rawDay  = wirePtr->read();
    uint8_t rawWday = wirePtr->read();
    uint8_t rawMon  = wirePtr->read();
    uint8_t rawYear = wirePtr->read();

    // Bit 7 de segundos es OS (Oscillator Stop flag): si está en 1, el reloj perdió energía
    bool oscStopped = (rawSec & 0x80) != 0;
    outTm.tm_sec  = bcdToDec(rawSec & 0x7F);
    outTm.tm_min  = bcdToDec(rawMin & 0x7F);
    outTm.tm_hour = bcdToDec(rawHour & 0x3F);
    outTm.tm_mday = bcdToDec(rawDay & 0x3F);
    outTm.tm_wday = rawWday & 0x07;
    outTm.tm_mon  = bcdToDec(rawMon & 0x1F) - 1; // 0..11
    outTm.tm_year = bcdToDec(rawYear) + 100;     // Años desde 1900 (2000..2099 -> 100..199)
    outTm.tm_isdst = 0;

    if (oscStopped || outTm.tm_year < 120 ||
        outTm.tm_sec < 0 || outTm.tm_sec > 59 ||
        outTm.tm_min < 0 || outTm.tm_min > 59 ||
        outTm.tm_hour < 0 || outTm.tm_hour > 23 ||
        outTm.tm_mday < 1 || outTm.tm_mday > 31 ||
        outTm.tm_wday < 0 || outTm.tm_wday > 6 ||
        outTm.tm_mon < 0 || outTm.tm_mon > 11) {
        log_w("PCF85063 lectura invalida o oscilador reiniciado (ano=%d, mes=%d, dia=%d)",
              outTm.tm_year + 1900, outTm.tm_mon + 1, outTm.tm_mday);
        return false;
    }

    return true;
}

bool PCF85063Driver::setTime(const struct tm &inTm) {
    CopitoI2cGuard guard;
    if (!guard.locked) return false;
    if (!initialized && !begin(*wirePtr)) return false;

    wirePtr->beginTransmission(RTC_PCF85063_ADDR);
    wirePtr->write(PCF85063_REG_SEC);
    wirePtr->write(decToBcd(inTm.tm_sec) & 0x7F);        // limpia OS bit para arrancar
    wirePtr->write(decToBcd(inTm.tm_min) & 0x7F);
    wirePtr->write(decToBcd(inTm.tm_hour) & 0x3F);
    wirePtr->write(decToBcd(inTm.tm_mday) & 0x3F);
    wirePtr->write(inTm.tm_wday & 0x07);
    wirePtr->write(decToBcd(inTm.tm_mon + 1) & 0x1F);   // 1..12
    int y2k = (inTm.tm_year >= 100) ? (inTm.tm_year - 100) : inTm.tm_year;
    wirePtr->write(decToBcd(y2k));
    if (wirePtr->endTransmission() != 0) {
        log_e("Error al escribir fecha/hora en PCF85063");
        return false;
    }

    log_i("PCF85063 sincronizado: %02d:%02d:%02d %02d/%02d/%04d",
          inTm.tm_hour, inTm.tm_min, inTm.tm_sec,
          inTm.tm_mday, inTm.tm_mon + 1, inTm.tm_year + 1900);
    return true;
}

bool PCF85063Driver::syncSystemTimeFromRtc() {
    struct tm rtcTm;
    if (!readTime(rtcTm)) return false;

    time_t t = mktime(&rtcTm);
    if (t > 1577836800) { // Mayor a 2020-01-01
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        log_i("Reloj del sistema ESP32 sincronizado desde RTC PCF85063: %02d:%02d",
              rtcTm.tm_hour, rtcTm.tm_min);
        return true;
    }
    return false;
}

bool PCF85063Driver::syncRtcFromSystemTime() {
    struct tm sysTm;
    if (!getLocalTime(&sysTm, 100)) return false;
    if (sysTm.tm_year < 120) return false; // No sincronizar si es año 1970
    return setTime(sysTm);
}
