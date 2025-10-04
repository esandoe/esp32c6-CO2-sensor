#ifndef CO2_SENSOR_H
#define CO2_SENSOR_H

#include <Wire.h>
#include <SensirionI2cScd4x.h>

#define NO_VALUE -123456789.0f
#define NO_ERROR 0

RTC_DATA_ATTR static bool CO2SensorInitialized = false;

class CO2Sensor
{
private:
    SensirionI2cScd4x sensor;
    uint32_t samplingIntervalSeconds;
    float temperatureOffset = 0.0f;
    static char errorMessage[64];

    bool initialize();
    bool checkConfiguration();
    void printError(const char *prefix, int16_t err);

public:
    CO2Sensor(uint32_t samplingIntervalSeconds, float temperatureOffset = 0.0f);

    bool measure(uint16_t &co2, float &temp, float &rh);

    /**
     * @brief Start a single shot measurement.
     *
     * This function only triggers a measurement, which takes 5 seconds to complete.
     * It is simply a copy of the measureSingleShot() function from the Sensirion library,
     * except that it does not wait for the measurement to complete, which allows us to
     * save power by putting the CPU to sleep while waiting.
     *
     * @return error_code 0 on success, an error code otherwise.
     */
    bool startMeasurement();
    bool readMeasurement(uint16_t &co2, float &temp, float &rh);
    bool isMeasurementReady();
};

#endif
