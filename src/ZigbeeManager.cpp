#include "ZigbeeManager.h"

ZigbeeManager::ZigbeeManager(uint8_t endpoint, const String& mfg, const String& mdl, 
                            uint16_t minValue, uint16_t maxValue, uint32_t keepAlive)
    : endpointNumber(endpoint), manufacturer(mfg), model(mdl),
      minCO2Value(minValue), maxCO2Value(maxValue), keepAliveTime(keepAlive),
      isInitialized(false), isConnected(false) {
    
    carbonDioxideSensor = new ZigbeeCarbonDioxideSensor(endpointNumber);
}

ZigbeeManager::~ZigbeeManager() {
    delete carbonDioxideSensor;
}

bool ZigbeeManager::initialize() {
    if (!isReportingEnabled()) {
        log_i("Zigbee reporting is disabled, skipping initialization");
        return false;
    }
    
    if (isInitialized) {
        log_i("Zigbee already initialized");
        return true;
    }
    
    // Configure the sensor
    carbonDioxideSensor->setManufacturerAndModel(manufacturer.c_str(), model.c_str());
    carbonDioxideSensor->setMinMaxValue(minCO2Value, maxCO2Value);
    carbonDioxideSensor->setPowerSource(zb_power_source_t::ZB_POWER_SOURCE_BATTERY);
    
    // Add endpoint to Zigbee
    Zigbee.addEndpoint(carbonDioxideSensor);
    
    // Configure Zigbee
    esp_zb_cfg_t zigbeeConfig = ZIGBEE_DEFAULT_ED_CONFIG();
    zigbeeConfig.nwk_cfg.zed_cfg.keep_alive = keepAliveTime;
    
    log_i("Starting Zigbee...");
    if (!Zigbee.begin(&zigbeeConfig, false)) {
        log_e("Zigbee failed to start!");
        return false;
    }
    
    log_i("Zigbee started!");
    isInitialized = true;
    return true;
}

bool ZigbeeManager::connect() {
    if (!isInitialized) {
        log_e("Zigbee not initialized. Call initialize() first.");
        return false;
    }
    
    if (isConnected) {
        log_i("Already connected to Zigbee network");
        return true;
    }
    
    log_i("Connecting to Zigbee network...");
    
    // Wait for connection with timeout
    uint32_t startTime = millis();
    const uint32_t TIMEOUT_MS = 10000; // 10 second timeout
    
    while (!Zigbee.connected() && (millis() - startTime) < TIMEOUT_MS) {
        log_i(".");
        delay(100);
    }
    
    if (Zigbee.connected()) {
        log_i("Connected to Zigbee network!");
        isConnected = true;
        return true;
    } else {
        log_e("Failed to connect to Zigbee network within timeout");
        return false;
    }
}

bool ZigbeeManager::isZigbeeConnected() const {
    return isConnected && Zigbee.connected();
}

void ZigbeeManager::reportCO2(uint16_t co2Value) {
    if (!isZigbeeConnected()) {
        log_w("Cannot report CO2: Not connected to Zigbee network");
        return;
    }
    
    carbonDioxideSensor->setCarbonDioxide(co2Value);
    carbonDioxideSensor->report();
    log_i("Reported CO2: %d ppm", co2Value);
}

void ZigbeeManager::reportBattery(uint8_t batteryPercentage) {
    if (!isZigbeeConnected()) {
        log_w("Cannot report battery: Not connected to Zigbee network");
        return;
    }
    
    uint8_t clampedPercentage = constrain(batteryPercentage, 0, 100);
    carbonDioxideSensor->setBatteryPercentage(clampedPercentage);
    carbonDioxideSensor->reportBatteryPercentage();
    log_i("Reported battery: %d%%", clampedPercentage);
}

void ZigbeeManager::reportSensorData(uint16_t co2, uint8_t batteryPercentage) {
    if (!isZigbeeConnected()) {
        log_w("Cannot report sensor data: Not connected to Zigbee network");
        return;
    }
    
    // Set both values
    carbonDioxideSensor->setCarbonDioxide(co2);
    carbonDioxideSensor->setBatteryPercentage(constrain(batteryPercentage, 0, 100));
    
    // Report both
    carbonDioxideSensor->reportBatteryPercentage();
    carbonDioxideSensor->report();
    
    log_i("Reported sensor data - CO2: %d ppm, Battery: %d%%", co2, batteryPercentage);
}

void ZigbeeManager::setManufacturerAndModel(const String& mfg, const String& mdl) {
    manufacturer = mfg;
    model = mdl;
    
    if (isInitialized) {
        carbonDioxideSensor->setManufacturerAndModel(manufacturer.c_str(), model.c_str());
    }
}

void ZigbeeManager::setCO2Range(uint16_t minValue, uint16_t maxValue) {
    minCO2Value = minValue;
    maxCO2Value = maxValue;
    
    if (isInitialized) {
        carbonDioxideSensor->setMinMaxValue(minCO2Value, maxCO2Value);
    }
}

void ZigbeeManager::setKeepAlive(uint32_t keepAliveMs) {
    keepAliveTime = keepAliveMs;
}

bool ZigbeeManager::isReportingEnabled() {
#if HEADLESS_MODE
    return true; // Always enabled in headless mode
#endif
    preferences.begin("zigbee", true); // true = read-only mode
    bool enabled = preferences.getBool("enabled", true); // default to true
    preferences.end();
    return enabled;
}

void ZigbeeManager::setReportingEnabled(bool enabled) {
    preferences.begin("zigbee", false); // false = read/write mode
    preferences.putBool("enabled", enabled);
    preferences.end();
    log_i("Saved Zigbee reporting setting: %s", enabled ? "enabled" : "disabled");
}

void ZigbeeManager::toggleReporting() {
    setReportingEnabled(!isReportingEnabled());
}

void ZigbeeManager::restart() {
    log_e("Restarting ESP due to Zigbee issue...");
    ESP.restart();
}
