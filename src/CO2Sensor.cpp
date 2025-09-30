#include "CO2Sensor.h"
#include "Arduino.h"
#include <SensirionI2cScd4x.h>

#define SCD41_I2C_ADDR_62 0x62

char CO2Sensor::errorMessage[64];

CO2Sensor::CO2Sensor(uint32_t samplingIntervalSeconds)
    : samplingIntervalSeconds(samplingIntervalSeconds)
{
}

void CO2Sensor::printError(const char *prefix, int16_t err)
{
    errorToString(err, errorMessage, sizeof(errorMessage));
    log_e("%s: %d, %s", prefix, err, errorMessage);
}

bool CO2Sensor::initialize()
{
    log_i("Initializing Sensirion SCD41...");

    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(100);

    if (CO2SensorInitialized)
    {
        return true;
    }

    if (checkConfiguration())
    {
        CO2SensorInitialized = true;
        return CO2SensorInitialized;
    }

    int16_t error = sensor.wakeUp();
    if (error != NO_ERROR)
    {
        printError("wakeUp", error);
        return false;
    }

    error = sensor.stopPeriodicMeasurement();
    if (error != NO_ERROR)
    {
        printError("stopPeriodicMeasurement", error);
        return false;
    }

    // 424ppm is the current average CO2 level in the atmosphere according to
    // https://www.co2.earth/daily-co2
    error = sensor.setAutomaticSelfCalibrationTarget(424);
    if (error != NO_ERROR)
    {
        printError("setAutomaticSelfCalibrationTarget", error);
        return false;
    }

    // The initial period represents the number of readings after powering up the sensor for the very first time to trigger the
    // first automatic self-calibration. The standard period represents the number of subsequent readings periodically
    // triggering ASC after completion of the initial period. Sensirion recommends adjusting the number of samples
    // comprising initial and standard period to 2 and 7 days at the average intended sampling rate, respectively.

    // The parameter value represents twelve times the number of single shots defining the length of either period.
    // Furthermore, this parameter must be an integer and a multiple of four.

    // Initial period, 2 days at x minute intervals: 2*24 * (60/x) / 12
    // rounded to nearest multiple of 4
    uint16_t initialPeriod = static_cast<uint16_t>(round((2 * 24 * (3600 / samplingIntervalSeconds) / 12) / 4) * 4);
    error = sensor.setAutomaticSelfCalibrationInitialPeriod(initialPeriod);
    if (error != NO_ERROR)
    {
        printError("setAutomaticSelfCalibrationInitialPeriod", error);
        return false;
    }

    // Standard period: 7 days at x minute intervals: 7*24 * (60/x) / 12
    // rounded to nearest multiple of 4
    uint16_t standardPeriod = static_cast<uint16_t>(round((7 * 24 * (3600 / samplingIntervalSeconds) / 12) / 4) * 4);
    error = sensor.setAutomaticSelfCalibrationStandardPeriod(standardPeriod);
    if (error != NO_ERROR)
    {
        printError("setAutomaticSelfCalibrationStandardPeriod", error);
        return false;
    }

    // Aktiver autokalibrering
    error = sensor.setAutomaticSelfCalibrationEnabled(true);
    if (error != NO_ERROR)
    {
        printError("setAutomaticSelfCalibrationEnabled", error);
        return false;
    }

    log_i("Sensiron SCD41 initialized with ASC.");

    CO2SensorInitialized = true;
    return CO2SensorInitialized;
}

bool CO2Sensor::checkConfiguration()
{
    // Try to wake up the sensor and check if it responds
    int16_t error = sensor.wakeUp();
    if (error != NO_ERROR)
    {
        log_d("Sensor not responding to wakeUp: error %d", error);
        return false;
    }

    // Check if automatic self-calibration is enabled
    uint16_t ascEnabled;
    error = sensor.getAutomaticSelfCalibrationEnabled(ascEnabled);
    if (error != NO_ERROR)
    {
        log_d("Failed to read ASC status: error %d", error);
        return false;
    }

    if (ascEnabled == 0)
    {
        log_d("Automatic self-calibration is not enabled");
        return false;
    }

    // Check if the calibration target is set correctly (424 ppm)
    uint16_t target;
    error = sensor.getAutomaticSelfCalibrationTarget(target);
    if (error != NO_ERROR)
    {
        log_d("Failed to read ASC target: error %d", error);
        return false;
    }

    if (target != 424)
    {
        log_d("ASC target mismatch: current=%d, expected=424", target);
        return false;
    }

    // Calculate expected periods based on sampling interval
    uint16_t expectedInitialPeriod = static_cast<uint16_t>(round((2 * 24 * (3600 / samplingIntervalSeconds) / 12) / 4) * 4);
    uint16_t expectedStandardPeriod = static_cast<uint16_t>(round((7 * 24 * (3600 / samplingIntervalSeconds) / 12) / 4) * 4);

    // Check if initial period is set correctly
    uint16_t currentInitialPeriod;
    error = sensor.getAutomaticSelfCalibrationInitialPeriod(currentInitialPeriod);
    if (error != NO_ERROR)
    {
        log_d("Failed to read ASC initial period: error %d", error);
        return false;
    }

    if (currentInitialPeriod != expectedInitialPeriod)
    {
        log_d("ASC initial period mismatch: current=%d, expected=%d", currentInitialPeriod, expectedInitialPeriod);
        return false;
    }

    // Check if standard period is set correctly
    uint16_t currentStandardPeriod;
    error = sensor.getAutomaticSelfCalibrationStandardPeriod(currentStandardPeriod);
    if (error != NO_ERROR)
    {
        log_d("Failed to read ASC standard period: error %d", error);
        return false;
    }

    if (currentStandardPeriod != expectedStandardPeriod)
    {
        log_d("ASC standard period mismatch: current=%d, expected=%d", currentStandardPeriod, expectedStandardPeriod);
        return false;
    }

    log_d("Sensor is properly configured (target=%d, initial=%d, standard=%d)",
          target, currentInitialPeriod, currentStandardPeriod);
    return true;
}

bool CO2Sensor::measure(uint16_t &co2, float &temp, float &rh)
{
    if (!initialize())
    {
        return false;
    }

    auto start = millis();
    int16_t error = sensor.measureAndReadSingleShot(co2, temp, rh);
    if (error != NO_ERROR)
    {
        printError("measureAndReadSingleShot", error);
        return false;
    }

    log_i("CO2: %d ppm, Temp: %.2f C, RH: %.2f %%", co2, temp, rh);
    log_i("Measurement took %lu ms", millis() - start);
    return true;
}
