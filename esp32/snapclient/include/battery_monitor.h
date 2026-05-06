#pragma once

#include <Arduino.h>

#include <driver/adc.h>
#include <esp_adc_cal.h>

struct BatteryStatus {
  bool available = false;
  float voltage = 0.0f;
  int percent = 0;
};

class BatteryMonitor {
 public:
  bool begin();
  void update(bool force = false);
  BatteryStatus status() const;

 private:
  bool pinToAdc1Channel(int pin, adc1_channel_t &channel) const;
  float readPinVoltage();
  float pinToPackVoltage(float pinVoltage) const;
  int percentFromVoltage(float packVoltage) const;

  bool enabled_ = false;
  bool initialized_ = false;
  bool calibrated_ = false;
  adc1_channel_t adcChannel_ = ADC1_CHANNEL_0;
  esp_adc_cal_characteristics_t adcCharacteristics_{};
  uint32_t lastPollMs_ = 0;
  uint32_t rawAdcAverage_ = 0;
  float pinVoltage_ = 0.0f;
  float packVoltage_ = 0.0f;
  float filteredPercent_ = 0.0f;
  int percent_ = 0;
};
