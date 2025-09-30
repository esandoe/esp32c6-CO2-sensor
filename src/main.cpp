#include <Wire.h>
#include <esp_sleep.h>
#include <Zigbee.h>
#include "esp_pm.h"
#include "Arduino.h"
#include "driver/rtc_io.h"
#include "rtc.h"
#include "CO2Sensor.h"
#include "Display.h"
#include "PowerManager.h"
#include "ZigbeeManager.h"

#define CO2_SAMPLING_INTERVAL_SECONDS 900
#define CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER 10

#define LONG_PRESS_MS 1000 // 1 second = long press to select
#define DISPLAY_TIMEOUT_SECONDS 10

#define BAT_ADC_PIN A2
#define I2C_SDA 20
#define I2C_SCL 19
#define BTN_PIN 0

CO2Sensor co2Sensor;
Display display;
PowerManager powerManager(BAT_ADC_PIN, BTN_PIN);
ZigbeeManager zigbeeManager(CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER);

bool startAndConnectZigbee()
{
    if (!zigbeeManager.initialize())
    {
        log_e("Zigbee initialization failed! Rebooting...");
        ESP.restart();
        return false;
    }

    if (!zigbeeManager.connect())
    {
        log_e("Zigbee connection failed! Rebooting...");
        ESP.restart();
        return false;
    }

    return true;
}

// Store sensor readings in RTC memory to survive deep sleep
RTC_DATA_ATTR uint16_t co2 = 0;

#define NO_VALUE -123456789.0f
RTC_DATA_ATTR float temp = NO_VALUE;
RTC_DATA_ATTR float rh = NO_VALUE;
RTC_DATA_ATTR uint8_t batteryPercentage = 0;
RTC_DATA_ATTR uint64_t prev_measurement_time = 0;

RTC_DATA_ATTR bool displayOn = false;

// Flag to make sure we only initialize the sensor on the very first boot after a power cycle
RTC_DATA_ATTR bool first_boot = true;

enum class ButtonPress
{
    NONE,     // No action
    NAVIGATE, // Short press - go to next menu item
    SELECT    // Long press - select current menu item
};

enum class MenuItem
{
    REFRESH = 1,   // Take new measurement and report
    ZIGBEE_ON = 2, // Start radio and stay awake
    EXIT = 3,      // Exit menu and go to sleep
    MENU_COUNT = 4 // Total number of menu items
};

bool measure()
{
    if (!co2Sensor.measure(co2, temp, rh))
    {
        return false;
    }
    batteryPercentage = powerManager.readBatteryPercentage();

    return true;
}

void zigbeeReport()
{
    zigbeeManager.reportSensorData(co2, batteryPercentage);
    delay(500);
}

void initializeHardware()
{
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW); // Turn on LED to show we are awake

    // Initialize CO2 sensor only on first boot
    if (first_boot && !co2Sensor.initialize(CO2_SAMPLING_INTERVAL_SECONDS))
    {
        log_e("Sensor initialization failed! Rebooting...");
        ESP.restart();
    }
    first_boot = false;
}

ButtonPress detectButtonPress()
{
    pinMode(BTN_PIN, INPUT);

    uint32_t start = millis();
    while (digitalRead(BTN_PIN) == LOW)
    {
        if (millis() - start >= DISPLAY_TIMEOUT_SECONDS * 1000)
        {
            return ButtonPress::NONE;
        }
    }

    start = millis();
    while (digitalRead(BTN_PIN) == HIGH)
    {
        delay(10);

        if (millis() - start >= LONG_PRESS_MS)
        {
            return ButtonPress::SELECT;
        }
    }

    return ButtonPress::NAVIGATE;
}

bool executeMenuItem(MenuItem item)
{
    switch (item)
    {
    case MenuItem::REFRESH:
        // Take new measurement and report
        display.showMeasurement(co2, temp, rh, "...");

        if (measure())
        {
            prev_measurement_time = powerManager.getCurrentTimeMicros();
            display.showMeasurement(co2, temp, rh);

            startAndConnectZigbee();
            zigbeeReport();
        }
        break;

    case MenuItem::ZIGBEE_ON:
        // Start Zigbee and stay awake for communication
        display.showMeasurement(co2, temp, rh, "Connecting...");
        startAndConnectZigbee();
        display.showMeasurement(co2, temp, rh, "Connected!");
        delay(3000);

        display.showMeasurement(co2, temp, rh, "Press to exit");
        while (digitalRead(BTN_PIN) == LOW)
        {
            delay(10);
        }
        break;

    case MenuItem::EXIT:
        display.showMeasurement(co2, temp, rh, "Exiting...");
        delay(1000);
        while (digitalRead(BTN_PIN) == HIGH)
        {
            delay(10);
        }

    default:
        return true;
    }

    return false;
}

void openMenu()
{
    uint8_t currentMenuItem = 1;
    MenuItem item = static_cast<MenuItem>(currentMenuItem);
    while (true)
    {
        switch (item)
        {
        case MenuItem::REFRESH:
            display.showMeasurement(co2, temp, rh, "1. Refresh");
            break;

        case MenuItem::ZIGBEE_ON:
            display.showMeasurement(co2, temp, rh, "2. Zigbee");
            break;

        case MenuItem::EXIT:
            display.showMeasurement(co2, temp, rh, "3. Exit");
            break;

        default:
            break;
        }

        ButtonPress btnPress = detectButtonPress();
        if (btnPress == ButtonPress::SELECT)
        {
            log_i("Selected menu item %d", currentMenuItem);
            bool exit = executeMenuItem(item);
            if (exit)
                return;
            currentMenuItem = 0;
        }
        else if (btnPress == ButtonPress::NAVIGATE)
        {
            currentMenuItem++;

            if (currentMenuItem >= static_cast<uint8_t>(MenuItem::MENU_COUNT))
            {
                currentMenuItem = 1;
            }

            item = static_cast<MenuItem>(currentMenuItem);
        }
        else
        {
            return;
        }
    }
}

void handleButtonWakeup()
{
    display.begin();
    display.turnOn();

    if (displayOn)
    {
        openMenu();
    }
    else
    {
        displayOn = true;
    }
    
    display.showMeasurement(co2, temp, rh);
    powerManager.goToSleep(DISPLAY_TIMEOUT_SECONDS);
}

void handleTimerWakeup()
{
    if (displayOn)
    {
        // Turn off display if it was on
        display.begin();
        display.turnOff();
        displayOn = false;
        return;
    }

    // Normal measurement cycle
    if (measure())
    {
        prev_measurement_time = powerManager.getCurrentTimeMicros();
        startAndConnectZigbee();
        zigbeeReport();
    }
}

void setup()
{
    initializeHardware();

    WakeupReason wakeup_reason = powerManager.getWakeupReason();

    switch (wakeup_reason)
    {
    case WakeupReason::BUTTON_PRESS:
        handleButtonWakeup();
        break;

    case WakeupReason::TIMER:
        handleTimerWakeup();
        break;

    case WakeupReason::POWER_ON:
    case WakeupReason::OTHER:
    default:
        // Normal measurement on power on or unknown wakeup
        if (measure())
        {
            prev_measurement_time = powerManager.getCurrentTimeMicros();
            startAndConnectZigbee();
            zigbeeReport();
        }
        break;
    }

    // Calculate next wakeup and go to sleep
    uint64_t next_wakeup = powerManager.calculateNextWakeup(CO2_SAMPLING_INTERVAL_SECONDS, prev_measurement_time);
    powerManager.goToSleepUntil(next_wakeup);
}

void loop() {}
