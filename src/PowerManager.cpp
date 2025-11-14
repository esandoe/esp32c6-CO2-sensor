#include "PowerManager.h"
#include "rtc.h"
#include <algorithm>

PowerManager::PowerManager(uint8_t batPin) : PowerManager(batPin, 0)
{
}

PowerManager::PowerManager(uint8_t batPin, uint8_t btnPin)
    : batteryPin(batPin), buttonPin(btnPin), voltageDividerRatio(2.0f),
      minVoltage(3.55f), maxVoltage(3.90f)
{
}

uint8_t PowerManager::readBatteryPercentage()
{
  float voltage = readBatteryVoltage();

  // linear mapping from voltage range to 0-100%
  float percentage = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * 100.0f;

  log_i("Battery voltage: %.4f V, Battery percentage: %.1f %%", voltage, percentage);
  return static_cast<uint8_t>(constrain(percentage, 0.0f, 100.0f));
}

float PowerManager::readBatteryVoltage()
{
  pinMode(batteryPin, INPUT);
  std::vector<uint32_t> voltageReadings;
  voltageReadings.reserve(31);

  // Take multiple samples for better accuracy
  for (int i = 0; i < 31; i++)
  {
    voltageReadings.push_back(analogReadMilliVolts(batteryPin));
  }

  // calculate median
  std::sort(voltageReadings.begin(), voltageReadings.end());
  float medianVoltage = static_cast<float>(voltageReadings[voltageReadings.size() / 2]);

  // Adjust for voltage divider
  medianVoltage = voltageDividerRatio * medianVoltage / 1000.0f;
  return medianVoltage;
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

void PowerManager::lightSleep(uint64_t sleepTimeSeconds)
{
  log_i("Light sleep for %llu seconds...", sleepTimeSeconds);
  esp_sleep_enable_timer_wakeup(sleepTimeSeconds * US_TO_S_FACTOR);
  esp_light_sleep_start();
}

WakeupReason PowerManager::getWakeupReason(bool displayOn)
{
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  switch (cause)
  {
  case ESP_SLEEP_WAKEUP_EXT1:
    return WakeupReason::BUTTON_PRESS;
  case ESP_SLEEP_WAKEUP_TIMER:
    if (displayOn)
      return WakeupReason::DISPLAY_TIMEOUT;
    else
      return WakeupReason::MEASURE_TIMER;
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
#if HEADLESS_MODE
  return; // Do not enable button wakeup in headless mode
#endif
  uint64_t button_pin_mask = 1ULL << buttonPin;
  esp_sleep_enable_ext1_wakeup_io(button_pin_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
}

void PowerManager::disableButtonWakeup()
{
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
}
