#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <esp_sleep.h>
#include <Zigbee.h>
#include <U8g2lib.h>
#include "esp_pm.h"
#include "Arduino.h"
#include "driver/rtc_io.h"
#include "rtc.h"
#include "main.h"

#define CO2_SAMPLING_INTERVAL_SECONDS 900
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */

#define CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER 10

ZigbeeCarbonDioxideSensor zbCarbonDioxideSensor(CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER);

U8G2_SSD1315_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
SensirionI2cScd4x sensor;

#define BAT_ADC_PIN A2
#define BAT_VOLTAGE_DIVIDER_RATIO 2.0f // 1:2 motstandskonfig
#define I2C_SDA 20
#define I2C_SCL 19

#define BTN_PIN 0

#define NO_VALUE -123456789.0f

static char errorMessage[64];
static int16_t error;
#define NO_ERROR 0

void printError(const char *prefix, int16_t err)
{
    errorToString(err, errorMessage, sizeof(errorMessage));
    log_e("%s: %d, %s", prefix, err, errorMessage);
}

void displayMeasurement(uint16_t co2, float temp, float rh, String message = "")
{
    u8g2.firstPage();
    do
    {
        // Display CO2 measurement in big font
        u8g2.setFont(u8g2_font_logisoso32_tn);
        String co2Str = (co2 == 0) ? "--" : String(co2);
        u8g2.drawStr(90 - u8g2.getStrWidth(co2Str.c_str()), 56, co2Str.c_str());

        u8g2.setFont(u8g2_font_9x18_tr);
        u8g2.drawStr(94, 56 - 16, "CO2");
        u8g2.drawStr(94, 56, "ppm");

        if (message != "")
        {
            u8g2.setFont(u8g2_font_9x18_tr);
            u8g2.drawStr(0, 12, message.c_str());
        }
        else
        {
            // temp and rh values top
            u8g2.setFont(u8g2_font_9x18_tr); // smaller 14px font

            if (temp != NO_VALUE)
            {
                String tempStr = String(temp, 1);
                u8g2.drawStr(0, 12, tempStr.c_str());
                int tempStrWidth = u8g2.getStrWidth(tempStr.c_str());

                // Draw degree symbol
                u8g2.drawCircle(tempStrWidth + 3, 4, 2);
                u8g2.drawStr(tempStrWidth + 8, 12, "C");
            }
            else
            {
                u8g2.drawStr(0, 12, "--");
            }

            String rhStr = (rh != NO_VALUE) ? String(rh, 1) + "%" : "--";
            u8g2.drawStr(128 - u8g2.getStrWidth(rhStr.c_str()), 12, rhStr.c_str());
        }

    } while (u8g2.nextPage());
}

void reportToZigbee(uint16_t co2, uint8_t batteryPercentage)
{
    zbCarbonDioxideSensor.setCarbonDioxide(co2);
    zbCarbonDioxideSensor.setBatteryPercentage(constrain(batteryPercentage, 0, 100));
    zbCarbonDioxideSensor.reportBatteryPercentage();
    zbCarbonDioxideSensor.report();
}

void startZigbee()
{
    zbCarbonDioxideSensor.setManufacturerAndModel("sando@home", "CO2 Sensor");
    zbCarbonDioxideSensor.setMinMaxValue(1, 10000);
    zbCarbonDioxideSensor.setPowerSource(zb_power_source_t::ZB_POWER_SOURCE_BATTERY);
    Zigbee.addEndpoint(&zbCarbonDioxideSensor);

    esp_zb_cfg_t zigbeeConfig = ZIGBEE_DEFAULT_ED_CONFIG();
    zigbeeConfig.nwk_cfg.zed_cfg.keep_alive = 10000;
    log_i("Starting Zigbee...");
    if (!Zigbee.begin(&zigbeeConfig, false))
    {
        log_e("Zigbee failed to start! Rebooting...");
        ESP.restart();
    }

    log_i("Zigbee started!");
    log_i("Connecting to Zigbee network...");
    while (!Zigbee.connected())
    {
        log_i(".");
        delay(100);
    }
    log_i("Connected to Zigbee network!");
}

bool initializeCO2Sensor()
{
    log_i("Initializing Sensirion SCD41...");

    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(100);

    error = sensor.wakeUp();
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

    // The initial period represents the number of readings after powering up the sensor for the very first time to trigger the
    // first automatic self-calibration. The standard period represents the number of subsequent readings periodically
    // triggering ASC after completion of the initial period. Sensirion recommends adjusting the number of samples
    // comprising initial and standard period to 2 and 7 days at the average intended sampling rate, respectively.

    // The parameter value represents twelve times the number of single shots defining the length of either period.
    // Furthermore, this parameter must be an integer and a multiple of four.

    // Initial period, 2 days at x minute intervals: 2*24 * (60/x) / 12
    // rounded to nearest multiple of 4
    uint16_t initialPeriod = static_cast<uint16_t>(round((2 * 24 * (3600 / CO2_SAMPLING_INTERVAL_SECONDS) / 12) / 4) * 4);
    error = sensor.setAutomaticSelfCalibrationInitialPeriod(initialPeriod);
    if (error != NO_ERROR)
    {
        printError("setAutomaticSelfCalibrationInitialPeriod", error);
        return false;
    }

    // Standard period: 7 days at x minute intervals: 7*24 * (60/x) / 12
    // rounded to nearest multiple of 4
    uint16_t standardPeriod = static_cast<uint16_t>(round((7 * 24 * (3600 / CO2_SAMPLING_INTERVAL_SECONDS) / 12) / 4) * 4);
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
    return true;
}

uint8_t readBatteryPercentage()
{
    pinMode(BAT_ADC_PIN, INPUT);
    uint32_t Vbatt = 0;
    for (int i = 0; i < 16; i++)
    {
        Vbatt += analogReadMilliVolts(BAT_ADC_PIN);
    }
    float batteryVoltage = 2 * Vbatt / 16 / 1000.0; // Adjust for 1:2 divider and convert to volts

    // Anta 0% => 3.2V og 100% => 4.2V
    // Dette gir en prosentverdi mellom 0 og 100
    float batteryPercentage = map(batteryVoltage, 3.2f, 4.2f, 0.0f, 100.0f);
    log_i("Battery voltage: %.2f V, Battery percentage: %.1f %%", batteryVoltage, batteryPercentage);
    return static_cast<uint8_t>(batteryPercentage);
}

bool measureCO2(uint16_t &co2, float &temp, float &rh)
{
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(100);

    auto start = millis();
    error = sensor.measureAndReadSingleShot(co2, temp, rh);
    if (error != NO_ERROR)
    {
        printError("measureAndReadSingleShot", error);
        return false;
    }

    log_i("CO2: %d ppm, Temp: %.2f C, RH: %.2f %%", co2, temp, rh);
    log_i("Measurement took %lu ms", millis() - start);
    return true;
}

void goToSleep(uint64_t next_wakeup)
{
    log_i("Going to sleep...");

    // Enable wakeup on button press
    uint64_t button_pin_mask = 1ULL << BTN_PIN;
    esp_sleep_enable_ext1_wakeup_io(button_pin_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

    // Set automatic wakeup in 15 minutes for periodic measurement
    esp_sleep_enable_timer_wakeup(next_wakeup);
    esp_deep_sleep_start();
}

// Store sensor readings in RTC memory to survive deep sleep
RTC_DATA_ATTR uint16_t co2 = 0;
RTC_DATA_ATTR float temp = NO_VALUE;
RTC_DATA_ATTR float rh = NO_VALUE;
RTC_DATA_ATTR uint8_t batteryPercentage = 0;
RTC_DATA_ATTR uint64_t prev_measurement_time = 0;

RTC_DATA_ATTR bool displayOn = false;

// Flag to make sure we only initialize the sensor on the very first boot after a power cycle
RTC_DATA_ATTR bool first_boot = true;

bool measure()
{
    if (!measureCO2(co2, temp, rh))
    {
        return false;
    }
    batteryPercentage = readBatteryPercentage();

    return true;
}

void zigbeeReport()
{
    reportToZigbee(co2, batteryPercentage);
    delay(500);
}

void setup()
{
    Serial.begin(115200);

    Wire.begin(I2C_SDA, I2C_SCL);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW); // Turn on LED to show we are awake

    if (first_boot && !initializeCO2Sensor())
    {
        log_e("Sensor initialization failed! Rebooting...");
        ESP.restart();
    }
    first_boot = false;

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    // On wake up on button press we turn on the display, go to sleep for 10 seconds
    // and turn off the display on the next wake up.
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1)
    {
        pinMode(BTN_PIN, INPUT);

        u8g2.begin();

        if (!displayOn)
        {
            u8g2.sleepOff();
            displayOn = true;
        }

        displayMeasurement(co2, temp, rh);

        auto start = millis();
        auto duration = 0UL;
        while (digitalRead(BTN_PIN) == HIGH && duration < 3000)
        {
            delay(100); // Wait for button release
            duration = millis() - start;

            if (duration >= 1000 && duration < 3000)
                displayMeasurement(co2, temp, rh, "1: refresh");
            else if (duration >= 3000)
                displayMeasurement(co2, temp, rh, "2: radio ON");
        }

        // If long press, force a new measurement and report
        if (duration >= 1000 && duration < 3000)
        {
            displayMeasurement(co2, temp, rh, "refreshing...");

            if (measure())
            {
                prev_measurement_time = esp_rtc_get_time_us();
                displayMeasurement(co2, temp, rh);

                startZigbee();
                zigbeeReport();
            }

            goToSleep(10 * uS_TO_S_FACTOR);
        }

        // On extra long press, start the radio and stay awake for 5 seconds to allow for communication
        else if (duration >= 3000)
        {
            displayMeasurement(co2, temp, rh, "connecting...");
            startZigbee();
            displayMeasurement(co2, temp, rh, "connected!");

            delay(5000);

            u8g2.sleepOn();
            displayOn = false;
        }

        // otherwise go to sleep immediately
        else
        {
            goToSleep(10 * uS_TO_S_FACTOR);
        }
    }

    // Wake up from timer, and display is on
    else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER && displayOn == true)
    {
        // If the display was on when we woke up, turn it off again
        u8g2.begin();
        u8g2.sleepOn(); // Turn off display
        displayOn = false;
    }

    // Wake up from timer normally to take a new measurement
    else
    {
        if (measure())
        {
            prev_measurement_time = esp_rtc_get_time_us();
            startZigbee();
            zigbeeReport();
        }
    }

    // if we are waking up before the timer, we'll use the RTC counter to calculate when the next wakeup should be
    uint64_t time_since_previous_measure = prev_measurement_time == 0 ? 0 : esp_rtc_get_time_us() - prev_measurement_time;

    uint64_t next_wakeup = std::clamp(CO2_SAMPLING_INTERVAL_SECONDS * uS_TO_S_FACTOR - time_since_previous_measure, 1000000ULL, CO2_SAMPLING_INTERVAL_SECONDS * uS_TO_S_FACTOR);
    log_i("Time since previous measurement: %llu s\n", time_since_previous_measure / uS_TO_S_FACTOR);
    log_i("Next wakeup in: %llu s\n", next_wakeup / uS_TO_S_FACTOR);

    goToSleep(next_wakeup);
}

void loop() {}
