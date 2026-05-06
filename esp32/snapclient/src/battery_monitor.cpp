#include "battery_monitor.h"

#include "board_config.h"
#include "snapclient_config.h"

namespace {

struct BatteryPoint {
  float voltage;
  int percent;
};

static const BatteryPoint kBatteryCurve4s[] = {
    {16.80f, 100},
    {16.60f, 98},
    {16.40f, 95},
    {16.20f, 90},
    {16.00f, 85},
    {15.80f, 78},
    {15.60f, 70},
    {15.40f, 62},
    {15.20f, 54},
    {15.00f, 46},
    {14.80f, 38},
    {14.60f, 30},
    {14.40f, 22},
    {14.20f, 15},
    {14.00f, 10},
    {13.80f, 6},
    {13.60f, 3},
    {13.20f, 1},
    {12.00f, 0},
};

static constexpr size_t kBatteryCurvePoints =
    sizeof(kBatteryCurve4s) / sizeof(kBatteryCurve4s[0]);

}  // namespace

bool BatteryMonitor::begin() {
  enabled_ = false;
  initialized_ = false;

  if (!board_config::BATTERY_SENSE_ENABLED) {
    return false;
  }

  if (!pinToAdc1Channel(board_config::BATTERY_SENSE_PIN, adcChannel_)) {
    Serial.printf("[battery] disabled: GPIO%d is not an ADC1 pin\n",
                  board_config::BATTERY_SENSE_PIN);
    return false;
  }

  pinMode(board_config::BATTERY_SENSE_PIN, INPUT);
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(adcChannel_, ADC_ATTEN_DB_11);
  const esp_adc_cal_value_t calType = esp_adc_cal_characterize(
      ADC_UNIT_1,
      ADC_ATTEN_DB_11,
      ADC_WIDTH_BIT_12,
      app_config::BATTERY_ADC_DEFAULT_VREF_MV,
      &adcCharacteristics_);
  calibrated_ = (calType != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
  enabled_ = true;
  lastPollMs_ = 0;

  Serial.printf("[battery] pin=GPIO%d divider=%.0f/%.0f calibrated=%s\n",
                board_config::BATTERY_SENSE_PIN,
                app_config::BATTERY_R_TOP_OHMS,
                app_config::BATTERY_R_BOTTOM_OHMS,
                calibrated_ ? "yes" : "no");
  return true;
}

void BatteryMonitor::update(bool force) {
  if (!enabled_) {
    return;
  }

  const uint32_t nowMs = millis();
  if (!force && initialized_ &&
      (nowMs - lastPollMs_) < app_config::BATTERY_POLL_INTERVAL_MS) {
    return;
  }
  lastPollMs_ = nowMs;

  pinVoltage_ = readPinVoltage();
  packVoltage_ = pinToPackVoltage(pinVoltage_);
  const int instantPercent = percentFromVoltage(packVoltage_);

  if (!initialized_) {
    filteredPercent_ = static_cast<float>(instantPercent);
    initialized_ = true;
  } else {
    filteredPercent_ += app_config::BATTERY_PERCENT_SMOOTH_ALPHA *
                        (static_cast<float>(instantPercent) - filteredPercent_);
  }

  percent_ = constrain(static_cast<int>(filteredPercent_ + 0.5f), 0, 100);
}

BatteryStatus BatteryMonitor::status() const {
  BatteryStatus current;
  current.available = enabled_ && initialized_;
  current.voltage = packVoltage_;
  current.percent = percent_;
  return current;
}

bool BatteryMonitor::pinToAdc1Channel(int pin, adc1_channel_t &channel) const {
  switch (pin) {
    case 36:
      channel = ADC1_CHANNEL_0;
      return true;
    case 39:
      channel = ADC1_CHANNEL_3;
      return true;
    case 32:
      channel = ADC1_CHANNEL_4;
      return true;
    case 33:
      channel = ADC1_CHANNEL_5;
      return true;
    case 34:
      channel = ADC1_CHANNEL_6;
      return true;
    case 35:
      channel = ADC1_CHANNEL_7;
      return true;
    default:
      return false;
  }
}

float BatteryMonitor::readPinVoltage() {
  uint32_t adcSum = 0;
  for (uint8_t i = 0; i < app_config::BATTERY_ADC_SAMPLES; ++i) {
    adcSum += static_cast<uint32_t>(adc1_get_raw(adcChannel_));
  }

  rawAdcAverage_ = adcSum / app_config::BATTERY_ADC_SAMPLES;
  if (calibrated_) {
    const uint32_t pinMilliVolts =
        esp_adc_cal_raw_to_voltage(rawAdcAverage_, &adcCharacteristics_);
    return static_cast<float>(pinMilliVolts) / 1000.0f;
  }

  return (static_cast<float>(rawAdcAverage_) /
          app_config::BATTERY_ADC_FULL_SCALE_COUNTS) *
         app_config::BATTERY_ADC_REF_VOLTAGE;
}

float BatteryMonitor::pinToPackVoltage(float pinVoltage) const {
  const float dividerRatio =
      (app_config::BATTERY_R_TOP_OHMS + app_config::BATTERY_R_BOTTOM_OHMS) /
      app_config::BATTERY_R_BOTTOM_OHMS;
  return pinVoltage * dividerRatio;
}

int BatteryMonitor::percentFromVoltage(float packVoltage) const {
  if (packVoltage >= kBatteryCurve4s[0].voltage) {
    return 100;
  }
  if (packVoltage <= kBatteryCurve4s[kBatteryCurvePoints - 1].voltage) {
    return 0;
  }

  for (size_t i = 0; i < (kBatteryCurvePoints - 1); ++i) {
    const BatteryPoint &high = kBatteryCurve4s[i];
    const BatteryPoint &low = kBatteryCurve4s[i + 1];

    if (packVoltage <= high.voltage && packVoltage >= low.voltage) {
      const float spanVoltage = high.voltage - low.voltage;
      if (spanVoltage <= 0.0f) {
        return constrain(high.percent, 0, 100);
      }
      const float t = (packVoltage - low.voltage) / spanVoltage;
      const float interpolated =
          static_cast<float>(low.percent) +
          t * static_cast<float>(high.percent - low.percent);
      return constrain(static_cast<int>(interpolated + 0.5f), 0, 100);
    }
  }

  return 0;
}
