#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <esp_sleep.h>
#include <Zigbee.h>
#include "esp_pm.h"
#include "Arduino.h"

#define CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER 10

ZigbeeCarbonDioxideSensor zbCarbonDioxideSensor(CARBON_DIOXIDE_SENSOR_ENDPOINT_NUMBER);
SensirionI2cScd4x sensor;

#define BAT_ADC_PIN A2
#define BAT_VOLTAGE_DIVIDER_RATIO 2.0f // 1:2 motstandskonfig
#define I2C_SDA 18
#define I2C_SCL 20

static char errorMessage[64];
static int16_t error;
#define NO_ERROR 0

void printError(const char *prefix, int16_t err)
{
    errorToString(err, errorMessage, sizeof(errorMessage));
    Serial.print(prefix);
    Serial.print(": ");
    Serial.print(err);
    Serial.print(", ");
    Serial.println(errorMessage);
}

void setup()
{
    Serial.begin(115200);
    pinMode(BAT_ADC_PIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Slå av LED for å spare strøm ved oppstart
}

void blink()
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(200);
    digitalWrite(LED_BUILTIN, HIGH);
}

void reportToZigbee(uint16_t co2, uint8_t batteryPercentage)
{
    zbCarbonDioxideSensor.setManufacturerAndModel("sando@home", "CO2 Sensor");
    zbCarbonDioxideSensor.setMinMaxValue(1, 10000);
    zbCarbonDioxideSensor.setPowerSource(zb_power_source_t::ZB_POWER_SOURCE_BATTERY);
    Zigbee.addEndpoint(&zbCarbonDioxideSensor);

    esp_zb_cfg_t zigbeeConfig = ZIGBEE_DEFAULT_ED_CONFIG();
    zigbeeConfig.nwk_cfg.zed_cfg.keep_alive = 10000;

    Serial.println("Starter Zigbee...");
    if (!Zigbee.begin(&zigbeeConfig, false))
    {
        Serial.println("Zigbee feilet å starte! Rebooter...");
        ESP.restart();
    }

    Serial.println("Zigbee startet!");
    Serial.println("Kobler til Zigbee-nettverk...");
    while (!Zigbee.connected())
    {
        Serial.print(".");
        delay(100);
    }
    Serial.println();

    zbCarbonDioxideSensor.setCarbonDioxide(co2);
    zbCarbonDioxideSensor.setBatteryPercentage(batteryPercentage);
    zbCarbonDioxideSensor.reportBatteryPercentage();
    zbCarbonDioxideSensor.report();

    blink(); // Blink LED for å indikere at data er sendt

    // Vent litt for at Zigbee skal få tid til å sende data, og slik at det er mulig å kommunisere med enheten på zigbee-nettverket
    delay(1000);
}

uint8_t readBatteryPercentage()
{
    uint32_t Vbatt = 0;
    for (int i = 0; i < 16; i++)
    {
        Vbatt += analogReadMilliVolts(BAT_ADC_PIN); // Read and accumulate ADC voltage
    }
    float batteryVoltage = 2 * Vbatt / 16 / 1000.0; // Adjust for 1:2 divider and convert to volts

    // Anta 0% => 3.2V og 100% => 4.2V
    // Dette gir en prosentverdi mellom 0 og 100
    float batteryPercentage = ((batteryVoltage - 3.2f) / (4.2f - 3.2f)) * 100.0f;
    Serial.print("Batterispenning: ");
    Serial.print(batteryVoltage);
    Serial.println(" V");
    Serial.print("Batteriprosent: ");
    Serial.print(batteryPercentage);
    Serial.println(" %");
    return static_cast<uint8_t>(batteryPercentage);
}

bool measureCO2(uint16_t &co2, float &temp, float &rh)
{
    Wire.begin(I2C_SDA, I2C_SCL);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(100);

    // Wake up sensor, ta måling, les ut data og power down
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
    sensor.reinit();

    error = sensor.measureSingleShot();
    if (error != NO_ERROR)
    {
        printError("measureSingleShot", error);
        return false;
    }
    error = sensor.measureAndReadSingleShot(co2, temp, rh);
    if (error == NO_ERROR)
    {
        Serial.printf("CO2: %d ppm, Temp: %.2f C, RH: %.2f %%\n", co2, temp, rh);
    }
    else
    {
        printError("measureAndReadSingleShot", error);
    }
    error = sensor.powerDown();
    if (error != NO_ERROR)
    {
        printError("powerDown", error);
        return false;
    }
    Wire.end();

    return true;
}

void loop()
{
    uint16_t co2 = 0;
    float temp = 0.0, rh = 0.0;

    bool res = measureCO2(co2, temp, rh);
    if (!res)
        return;

    uint8_t batteryPercentage = readBatteryPercentage();

    // Zigbee: rapporter kun når vi har fersk data
    reportToZigbee(co2, batteryPercentage);

    Serial.println("Going to sleep...");
    // Dyp dvale i 5 minutter
    esp_sleep_enable_timer_wakeup(5ULL * 60 * 1000000ULL);
    esp_deep_sleep_start();
}
