#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>
#include "config.h"

struct BatteryStatus {
    uint32_t milliVolts;
    int percentage;
    bool isCharging;
    bool isLow;
};

class BatteryMonitor {
public:
    static void begin();
    static uint32_t readRawMilliVolts();
    static uint32_t readFilteredMilliVolts();
    static int calculatePercentage(uint32_t milliVolts);
    static BatteryStatus getStatus();
    static void getStatusText(char *outBuf, size_t bufSize);

private:
    static const int MOVING_AVG_SIZE = 8;
    static uint32_t s_history[MOVING_AVG_SIZE];
    static int s_histIdx;
    static int s_histCount;
    static uint32_t s_lastFilteredMv;
};

extern BatteryMonitor batteryMonitor;

#endif // BATTERY_MONITOR_H
