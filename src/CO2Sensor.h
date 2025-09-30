#ifndef CO2_SENSOR_H
#define CO2_SENSOR_H

#include <Wire.h>
#include <SensirionI2cScd4x.h>

#define NO_VALUE -123456789.0f
#define NO_ERROR 0

class CO2Sensor {
private:
    SensirionI2cScd4x sensor;
    static char errorMessage[64];
    
    void printError(const char *prefix, int16_t err);

public:
    CO2Sensor();
    
    bool initialize(uint32_t samplingIntervalSeconds);
    bool measure(uint16_t &co2, float &temp, float &rh);
    void wakeUp();
    void sleep();
};

#endif