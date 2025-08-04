
# ESP32-C6 Zigbee CO₂ Sensor

Low-power CO₂, temperature, humidity, and battery sensor for ESP32-C6 with SCD41 and Zigbee reporting. Does a one-shot measure once every 5 minutes, sleeps between cycles, and optimized for low power battery use. Tested on an Seeed Studio XIAO ESP32C6, with a measured current draw of around 18µA between cycles and between 20mA and 80mA when measuring and reporting.

**Hardware:**
- ESP32-C6
- SCD41 (CO₂ sensor) on I2C (SDA=18, SCL=20)
- Battery voltage divider to ADC (A2): 2x 10MΩ resistor

**PlatformIO/Arduino**
