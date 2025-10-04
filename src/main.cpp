#include <Wire.h>
#include <esp_sleep.h>
#include <Zigbee.h>
#include "Arduino.h"
#include "rtc.h"
#include "CO2Sensor.h"
#include "PowerManager.h"
#include "ZigbeeManager.h"

#ifndef HEADLESS_MODE
#define HEADLESS_MODE 0
#endif

#if !HEADLESS_MODE
#include "Display.h"
Display display;

RTC_DATA_ATTR bool displayOn = false;

#define LONG_PRESS_MS 1000 // 1 second = long press to select
#define DISPLAY_TIMEOUT_SECONDS 10
#define BTN_PIN 0
#endif // !HEADLESS_MODE

#define CO2_SAMPLING_INTERVAL_SECONDS 900
#define CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER 10

#define BAT_ADC_PIN A1
#define I2C_SDA 18
#define I2C_SCL 20

// Store sensor readings in RTC memory to survive deep sleep
#define NO_VALUE -123456789.0f
RTC_DATA_ATTR uint16_t co2 = 0;
RTC_DATA_ATTR float temp = NO_VALUE;
RTC_DATA_ATTR float rh = NO_VALUE;
RTC_DATA_ATTR uint8_t batteryPercentage = 0;
RTC_DATA_ATTR uint64_t prev_measurement_time = 0;

CO2Sensor co2Sensor(CO2_SAMPLING_INTERVAL_SECONDS);
ZigbeeManager zigbeeManager(CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER);
#ifdef BTN_PIN
PowerManager powerManager(BAT_ADC_PIN, BTN_PIN);
#else
PowerManager powerManager(BAT_ADC_PIN);
#endif

void initializeHardware()
{
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW); // Turn on LED to show we are awake
}

bool measure()
{
    if (!co2Sensor.measure(co2, temp, rh))
    {
        return false;
    }
    batteryPercentage = powerManager.readBatteryPercentage();

    return true;
}

bool startAndConnectZigbee()
{
    if (!zigbeeManager.initialize())
    {
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

void zigbeeReport()
{
    zigbeeManager.reportSensorData(co2, batteryPercentage);
    delay(500);
}

#if !HEADLESS_MODE
enum class ButtonPress
{
    NONE,     // No action
    NAVIGATE, // Short press - go to next menu item
    SELECT    // Long press - select current menu item
};

enum class MenuItem
{
    REFRESH = 1,       // Take new measurement and report
    BATTERY = 2,       // Show battery voltage and percentage
    ZIGBEE_TOGGLE = 3, // Toggle Zigbee reporting on/off
    ZIGBEE_ON = 4,     // Start radio and stay awake
    EXIT = 5,          // Exit menu and go to sleep
    MENU_COUNT = 6     // Total number of menu items
};

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

            if (startAndConnectZigbee())
            {
                zigbeeReport();
            }
        }
        return true;

    case MenuItem::BATTERY:
    {
        // Show battery information
        float voltage = powerManager.readBatteryVoltage();
        batteryPercentage = powerManager.readBatteryPercentage();

        char batteryInfo[32];
        snprintf(batteryInfo, sizeof(batteryInfo), "%.2fV %d%%", voltage, batteryPercentage);
        display.showMeasurement(co2, temp, rh, batteryInfo);
        delay(3000);
    }
    break;

    case MenuItem::ZIGBEE_TOGGLE:
        // Toggle Zigbee reporting on/off
        zigbeeManager.toggleReporting();
        display.showMeasurement(co2, temp, rh,
                                zigbeeManager.isReportingEnabled() ? "Zigbee: ON" : "Zigbee: OFF");
        delay(2000);
        break;

    case MenuItem::ZIGBEE_ON:
        // Start Zigbee and stay awake for communication
        if (!zigbeeManager.isReportingEnabled())
        {
            display.showMeasurement(co2, temp, rh, "Zigbee disabled!");
            delay(2000);
            break;
        }

        display.showMeasurement(co2, temp, rh, "Connecting...");
        if (startAndConnectZigbee())
        {
            display.showMeasurement(co2, temp, rh, "Connected!");
            delay(3000);

            display.showMeasurement(co2, temp, rh, "Press to exit");
            while (digitalRead(BTN_PIN) == LOW)
            {
                delay(10);
            }
        }
        else
        {
            display.showMeasurement(co2, temp, rh, "Connection failed!");
            delay(2000);
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

        case MenuItem::BATTERY:
            display.showMeasurement(co2, temp, rh, "2. Battery");
            break;

        case MenuItem::ZIGBEE_TOGGLE:
        {
            String zigbeeStatus = zigbeeManager.isReportingEnabled() ? "3. Zigbee: ON" : "3. Zigbee: OFF";
            display.showMeasurement(co2, temp, rh, zigbeeStatus);
            break;
        }

        case MenuItem::ZIGBEE_ON:
            display.showMeasurement(co2, temp, rh, "4. Stay awake");
            break;

        case MenuItem::EXIT:
            display.showMeasurement(co2, temp, rh, "5. Exit");
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
        }
        else if (btnPress == ButtonPress::NAVIGATE)
        {
            item = static_cast<MenuItem>(++currentMenuItem);

            if (item >= MenuItem::MENU_COUNT)
            {
                item = MenuItem::REFRESH;
                currentMenuItem = 1;
            }
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
#endif // !HEADLESS_MODE

void setup()
{
    initializeHardware();
    
#if !HEADLESS_MODE
    WakeupReason wakeup_reason = powerManager.getWakeupReason(displayOn);
    if (wakeup_reason == WakeupReason::BUTTON_PRESS)
        handleButtonWakeup();

    // If display was on, turn it off to save power
    if (wakeup_reason == WakeupReason::DISPLAY_TIMEOUT)
    {
        display.begin();
        display.turnOff();
        displayOn = false;
    }
#else // HEADLESS_MODE
    WakeupReason wakeup_reason = powerManager.getWakeupReason(false);
#endif // !HEADLESS_MODE

    // Normal measurement on power on or timer wakeup
    if (wakeup_reason == WakeupReason::POWER_ON || wakeup_reason == WakeupReason::MEASURE_TIMER)
    {
        if (measure())
        {
            prev_measurement_time = powerManager.getCurrentTimeMicros();
            if (startAndConnectZigbee())
            {
                zigbeeReport();
            }
        }
    }

    // Calculate next wakeup and go to sleep
    uint64_t next_wakeup = powerManager.calculateNextWakeup(CO2_SAMPLING_INTERVAL_SECONDS, prev_measurement_time);
    powerManager.goToSleepUntil(next_wakeup);
}

void loop() {}
