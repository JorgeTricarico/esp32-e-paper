#include "battery_monitor.h"

struct LiPoPoint {
    uint16_t mv;
    uint8_t  pct;
};

static const LiPoPoint LIPO_CURVE[] = {
    { 4200, 100 },
    { 4100,  90 },
    { 4000,  80 },
    { 3900,  70 },
    { 3820,  60 },
    { 3760,  50 },
    { 3720,  40 },
    { 3680,  30 },
    { 3630,  20 },
    { 3550,  10 },
    { 3400,   5 },
    { 3200,   0 }
};
static const size_t LIPO_POINTS_COUNT = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

uint32_t BatteryMonitor::s_history[BatteryMonitor::MOVING_AVG_SIZE] = {0};
int BatteryMonitor::s_histIdx = 0;
int BatteryMonitor::s_histCount = 0;
uint32_t BatteryMonitor::s_lastFilteredMv = 3800;

BatteryMonitor batteryMonitor;

void BatteryMonitor::begin() {
    pinMode(PIN_VBAT_ADC, INPUT);
    s_histIdx = 0;
    s_histCount = 0;
    
    // Warm up the filter with initial readings
    uint32_t initial = readRawMilliVolts();
    if (initial < 2500 || initial > 5000) {
        initial = 3800; // Fallback to safe nominal LiPo voltage if sensor unplugged
    }
    for (int i = 0; i < MOVING_AVG_SIZE; i++) {
        s_history[i] = initial;
    }
    s_histCount = MOVING_AVG_SIZE;
    s_lastFilteredMv = initial;
}

uint32_t BatteryMonitor::readRawMilliVolts() {
    // 8 samples with trimmed mean (drop min and max)
    const int SAMPLES = 8;
    uint32_t readings[SAMPLES];
    uint32_t minVal = 0xFFFFFFFF;
    uint32_t maxVal = 0;
    uint32_t sum = 0;

    for (int i = 0; i < SAMPLES; i++) {
        // analogReadMilliVolts takes attenuation and calibration into account
        uint32_t v = (uint32_t)analogReadMilliVolts(PIN_VBAT_ADC) * 2;
        readings[i] = v;
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
        sum += v;
        if (i < SAMPLES - 1) delayMicroseconds(250);
    }

    // Discard min and max, average remaining 6
    sum -= (minVal + maxVal);
    return sum / (SAMPLES - 2);
}

uint32_t BatteryMonitor::readFilteredMilliVolts() {
    uint32_t raw = readRawMilliVolts();
    
    // Plausibility check: ignore crazy readings if ADC is glitching
    if (raw < 2000 || raw > 5500) {
        return s_lastFilteredMv;
    }

    s_history[s_histIdx] = raw;
    s_histIdx = (s_histIdx + 1) % MOVING_AVG_SIZE;
    if (s_histCount < MOVING_AVG_SIZE) {
        s_histCount++;
    }

    uint32_t sum = 0;
    for (int i = 0; i < s_histCount; i++) {
        sum += s_history[i];
    }
    s_lastFilteredMv = sum / s_histCount;
    return s_lastFilteredMv;
}

int BatteryMonitor::calculatePercentage(uint32_t milliVolts) {
    if (milliVolts >= LIPO_CURVE[0].mv) {
        return 100;
    }
    if (milliVolts <= LIPO_CURVE[LIPO_POINTS_COUNT - 1].mv) {
        return 0;
    }

    for (size_t i = 0; i < LIPO_POINTS_COUNT - 1; i++) {
        uint16_t vHigh = LIPO_CURVE[i].mv;
        uint16_t vLow  = LIPO_CURVE[i + 1].mv;
        if (milliVolts <= vHigh && milliVolts >= vLow) {
            uint8_t pHigh = LIPO_CURVE[i].pct;
            uint8_t pLow  = LIPO_CURVE[i + 1].pct;
            // Linear interpolation
            uint32_t diffV = vHigh - vLow;
            if (diffV == 0) return pLow;
            uint32_t span = (milliVolts - vLow) * (pHigh - pLow);
            return pLow + (int)(span / diffV);
        }
    }
    return 0;
}

BatteryStatus BatteryMonitor::getStatus() {
    BatteryStatus st;
    st.milliVolts = readFilteredMilliVolts();
    st.percentage = calculatePercentage(st.milliVolts);
    // When connected to USB charging, voltage is elevated above normal float (typically > 4250 mV)
    st.isCharging = (st.milliVolts >= 4260);
    st.isLow = (st.percentage <= 15);
    return st;
}

void BatteryMonitor::getStatusText(char *outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return;
    BatteryStatus st = getStatus();
    if (st.isCharging) {
        snprintf(outBuf, bufSize, "cargando, %d por ciento", st.percentage);
    } else if (st.isLow) {
        snprintf(outBuf, bufSize, "bateria baja, %d por ciento", st.percentage);
    } else {
        snprintf(outBuf, bufSize, "%d por ciento", st.percentage);
    }
}
