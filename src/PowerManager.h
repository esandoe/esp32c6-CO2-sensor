#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <esp_sleep.h>
#include "Arduino.h"
#include "driver/rtc_io.h"

enum class WakeupReason {
    POWER_ON,
    BUTTON_PRESS,
    MEASURE_TIMER,
    DISPLAY_TIMEOUT,
    OTHER
};

class PowerManager {
private:
    uint8_t batteryPin;
    uint8_t buttonPin;
    float voltageDividerRatio;
    float minVoltage;
    float maxVoltage;
    
    static const uint64_t US_TO_S_FACTOR = 1000000ULL;

public:
    PowerManager(uint8_t batPin);
    PowerManager(uint8_t batPin, uint8_t btnPin);
    
    // Battery management
    uint8_t readBatteryPercentage();
    float readBatteryVoltage();
    
    // Sleep management
    void goToSleep(uint64_t wakeupTimeSeconds);
    void goToSleepUntil(uint64_t nextWakeupMicros);
    void lightSleep(uint64_t sleepTimeSeconds);
    WakeupReason getWakeupReason(bool displayOn);
    
    // Timing utilities
    uint64_t getCurrentTimeMicros();
    uint64_t calculateNextWakeup(uint64_t intervalSeconds, uint64_t lastMeasurementTime);
    
    // Power optimization
    void enableButtonWakeup();
    void disableButtonWakeup();
};

#endif
