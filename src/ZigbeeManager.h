#ifndef ZIGBEE_MANAGER_H
#define ZIGBEE_MANAGER_H

#include <Zigbee.h>
#include <Preferences.h>
#include "Arduino.h"

class ZigbeeManager {
private:
    ZigbeeCarbonDioxideSensor* carbonDioxideSensor;
    uint8_t endpointNumber;
    String manufacturer;
    String model;
    uint16_t minCO2Value;
    uint16_t maxCO2Value;
    uint32_t keepAliveTime;
    
    bool isInitialized;
    bool isConnected;
    
    Preferences preferences;

public:
    ZigbeeManager(uint8_t endpoint = 10, 
                  const String& mfg = "sando@home", 
                  const String& mdl = "CO2 Sensor",
                  uint16_t minValue = 1,
                  uint16_t maxValue = 10000,
                  uint32_t keepAlive = 10000);
    
    ~ZigbeeManager();
    
    // Initialization and connection
    bool initialize();
    bool connect();
    bool isZigbeeConnected() const;
    
    // Data reporting
    void reportCO2(uint16_t co2Value);
    void reportBattery(uint8_t batteryPercentage);
    void reportSensorData(uint16_t co2, uint8_t batteryPercentage);
    
    // Configuration
    void setManufacturerAndModel(const String& mfg, const String& mdl);
    void setCO2Range(uint16_t minValue, uint16_t maxValue);
    void setKeepAlive(uint32_t keepAliveMs);
    
    // Settings management
    bool isReportingEnabled();
    void setReportingEnabled(bool enabled);
    void toggleReporting();
    
    // Utility
    void restart();
};

#endif