#include "CO2Sensor.h"
#include "Arduino.h"
#include <SensirionI2cScd4x.h>

#define SCD41_I2C_ADDR_62 0x62

char CO2Sensor::errorMessage[64];

CO2Sensor::CO2Sensor() {
}

void CO2Sensor::printError(const char *prefix, int16_t err) {
    errorToString(err, errorMessage, sizeof(errorMessage));
    log_e("%s: %d, %s", prefix, err, errorMessage);
}

bool CO2Sensor::initialize(uint32_t samplingIntervalSeconds) {
    log_i("Initializing Sensirion SCD41...");

    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(100);

    int16_t error = sensor.wakeUp();
    if (error != NO_ERROR) {
        printError("wakeUp", error);
        return false;
    }

    error = sensor.stopPeriodicMeasurement();
    if (error != NO_ERROR) {
        printError("stopPeriodicMeasurement", error);
        return false;
    }

    // 424ppm is the current average CO2 level in the atmosphere according to
    // https://www.co2.earth/daily-co2
    error = sensor.setAutomaticSelfCalibrationTarget(424);
    if (error != NO_ERROR) {
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
    if (error != NO_ERROR) {
        printError("setAutomaticSelfCalibrationInitialPeriod", error);
        return false;
    }

    // Standard period: 7 days at x minute intervals: 7*24 * (60/x) / 12
    // rounded to nearest multiple of 4
    uint16_t standardPeriod = static_cast<uint16_t>(round((7 * 24 * (3600 / samplingIntervalSeconds) / 12) / 4) * 4);
    error = sensor.setAutomaticSelfCalibrationStandardPeriod(standardPeriod);
    if (error != NO_ERROR) {
        printError("setAutomaticSelfCalibrationStandardPeriod", error);
        return false;
    }

    // Aktiver autokalibrering
    error = sensor.setAutomaticSelfCalibrationEnabled(true);
    if (error != NO_ERROR) {
        printError("setAutomaticSelfCalibrationEnabled", error);
        return false;
    }

    log_i("Sensiron SCD41 initialized with ASC.");
    return true;
}

bool CO2Sensor::measure(uint16_t &co2, float &temp, float &rh) {
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(100);

    auto start = millis();
    int16_t error = sensor.measureAndReadSingleShot(co2, temp, rh);
    if (error != NO_ERROR) {
        printError("measureAndReadSingleShot", error);
        return false;
    }

    log_i("CO2: %d ppm, Temp: %.2f C, RH: %.2f %%", co2, temp, rh);
    log_i("Measurement took %lu ms", millis() - start);
    return true;
}

void CO2Sensor::wakeUp() {
    sensor.wakeUp();
}

void CO2Sensor::sleep() {
    // Sensor goes to sleep automatically after measurement
}