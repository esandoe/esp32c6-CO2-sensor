#include "PowerManager.h"
#include "rtc.h"
#include <algorithm>

PowerManager::PowerManager(uint8_t batPin, uint8_t btnPin, float dividerRatio,
                           float minVolt, float maxVolt)
    : batteryPin(batPin), buttonPin(btnPin), voltageDividerRatio(dividerRatio),
      minVoltage(minVolt), maxVoltage(maxVolt)
{
}

uint8_t PowerManager::readBatteryPercentage()
{
  float voltage = readBatteryVoltage();
  float percentage = map(voltage, minVoltage, maxVoltage, 0.0f, 100.0f);

  log_i("Battery voltage: %.2f V, Battery percentage: %.1f %%", voltage, percentage);
  return static_cast<uint8_t>(constrain(percentage, 0.0f, 100.0f));
}

float PowerManager::readBatteryVoltage()
{
  pinMode(batteryPin, INPUT);
  uint32_t totalVoltage = 0;

  // Take multiple samples for better accuracy
  for (int i = 0; i < 16; i++)
  {
    totalVoltage += analogReadMilliVolts(batteryPin);
  }

  // Calculate average and adjust for voltage divider
  float averageVoltage = voltageDividerRatio * totalVoltage / 16 / 1000.0f;
  return averageVoltage;
}

void PowerManager::goToSleep(uint64_t wakeupTimeSeconds)
{
  goToSleepUntil(wakeupTimeSeconds * US_TO_S_FACTOR);
}

void PowerManager::goToSleepUntil(uint64_t nextWakeupMicros)
{
  log_i("Going to sleep for %llu seconds...", nextWakeupMicros / US_TO_S_FACTOR);

  enableButtonWakeup();

  esp_sleep_enable_timer_wakeup(nextWakeupMicros);

  esp_deep_sleep_start();
}

WakeupReason PowerManager::getWakeupReason()
{
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  switch (cause)
  {
  case ESP_SLEEP_WAKEUP_EXT1:
    return WakeupReason::BUTTON_PRESS;
  case ESP_SLEEP_WAKEUP_TIMER:
    return WakeupReason::TIMER;
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return WakeupReason::POWER_ON;
  default:
    return WakeupReason::OTHER;
  }
}

uint64_t PowerManager::getCurrentTimeMicros()
{
  return esp_rtc_get_time_us();
}

uint64_t PowerManager::calculateNextWakeup(uint64_t intervalSeconds, uint64_t lastMeasurementTime)
{
  uint64_t currentTime = getCurrentTimeMicros();
  uint64_t timeSinceLastMeasurement = (lastMeasurementTime == 0) ? 0 : (currentTime - lastMeasurementTime);

  uint64_t intervalMicros = intervalSeconds * US_TO_S_FACTOR;
  uint64_t nextWakeup = std::clamp(intervalMicros - timeSinceLastMeasurement,
                                   1000000ULL, intervalMicros);

  log_i("Time since previous measurement: %llu s", timeSinceLastMeasurement / US_TO_S_FACTOR);
  log_i("Next wakeup in: %llu s", nextWakeup / US_TO_S_FACTOR);

  return nextWakeup;
}

void PowerManager::enableButtonWakeup()
{
  uint64_t button_pin_mask = 1ULL << buttonPin;
  esp_sleep_enable_ext1_wakeup_io(button_pin_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
}

void PowerManager::disableButtonWakeup()
{
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
}