/*
  SolarLights.ino

  ATtiny1616 / megaTinyCore four-channel solar garden-light controller.
  The PCB's LM2596 ON/OFF inputs are active LOW: LOW = enabled, HIGH = off.

  Build assumptions:
    - megaTinyCore 2.6.11 or later
    - Board: ATtiny3226/3216/1626/1616/... (atxy6), chip ATtiny1616
    - 20 MHz internal clock
    - millis()/micros() timer: TCD0 (the ATtiny1616 default)
    - PWM pins: PB0-2, PA3-5 (default routing)
    - UPDI remains enabled

  PB0, PB1 and PB2 are TCA0 WO0..WO2 hardware-PWM outputs. PA3 is kept
  as ordinary GPIO because Channel 4 must always be either LOW or solid HIGH.
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <string.h>

// The fixed PCB needs the default TCA route (WO0..WO2 on PB0..PB2).
#if defined(TCA_PORTMUX) && (TCA_PORTMUX != 0)
#error "Select the default TCA route: PB0-2, PA3-5"
#endif

// ---------------------------------------------------------------------------
// Fixed PCB pinout
// ---------------------------------------------------------------------------

constexpr uint8_t DUSK_SENSE_PIN = PIN_PA1;
constexpr uint8_t BAT_ADC_PIN = PIN_PA2;

constexpr uint8_t CH4_PWM_PIN = PIN_PA3;
constexpr uint8_t CH1_ENABLE_PIN = PIN_PA4;
constexpr uint8_t CH2_ENABLE_PIN = PIN_PA5;
constexpr uint8_t CH3_ENABLE_PIN = PIN_PA6;
constexpr uint8_t CH4_ENABLE_PIN = PIN_PA7;

constexpr uint8_t CH1_PWM_PIN = PIN_PB0;
constexpr uint8_t CH2_PWM_PIN = PIN_PB1;
constexpr uint8_t CH3_PWM_PIN = PIN_PB2;

constexpr uint8_t TEST_BUTTON_PIN = PIN_PB4;
constexpr uint8_t LEARN_BUTTON_PIN = PIN_PB5;
constexpr uint8_t RF_DATA_PIN = PIN_PC0;

// ---------------------------------------------------------------------------
// User-adjustable timing, light and ADC constants
// ---------------------------------------------------------------------------

constexpr uint32_t DUSK_CONFIRM_MS = 3UL * 60UL * 1000UL;
constexpr uint32_t DAWN_CONFIRM_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t FULL_BRIGHTNESS_MS = 3UL * 60UL * 60UL * 1000UL;
constexpr uint32_t LONG_FADE_MS = 3UL * 60UL * 60UL * 1000UL;
constexpr uint8_t FINAL_BRIGHTNESS_PERCENT = 20;
constexpr uint32_t FAST_FADE_MS = 2000UL;
constexpr uint32_t LM2596_SETTLE_MS = 50UL;
constexpr uint32_t SCHEDULE_UPDATE_INTERVAL_MS = 5000UL;

constexpr uint32_t ADC_SAMPLE_INTERVAL_MS = 250UL;
constexpr uint8_t ADC_AVERAGE_SAMPLES = 8;
constexpr uint16_t ADC_FULL_SCALE = 1023;
// VDD is the ADC reference. Change this to a measured board VDD for calibration.
constexpr float ADC_REFERENCE_VOLTS = 5.000f;
constexpr float DUSK_DARK_THRESHOLD_VOLTS = 2.00f;
constexpr float DUSK_LIGHT_THRESHOLD_VOLTS = 1.00f;
constexpr uint16_t DUSK_DARK_THRESHOLD_ADC =
    static_cast<uint16_t>((DUSK_DARK_THRESHOLD_VOLTS / ADC_REFERENCE_VOLTS) * ADC_FULL_SCALE + 0.5f);
constexpr uint16_t DUSK_LIGHT_THRESHOLD_ADC =
    static_cast<uint16_t>((DUSK_LIGHT_THRESHOLD_VOLTS / ADC_REFERENCE_VOLTS) * ADC_FULL_SCALE + 0.5f);

constexpr float BATTERY_DIVIDER_UPPER_OHMS = 100000.0f;
constexpr float BATTERY_DIVIDER_LOWER_OHMS = 33000.0f;
// Adjust after comparing getBatteryVoltage() with a trusted meter.
constexpr float BATTERY_CALIBRATION = 1.000f;

constexpr uint32_t BUTTON_DEBOUNCE_MS = 30UL;
constexpr uint32_t LEARN_HOLD_MS = 2000UL;
constexpr uint32_t ERASE_HOLD_MS = 8000UL;
constexpr uint32_t LEARN_TIMEOUT_MS = 60000UL;
constexpr uint32_t INDICATOR_ON_MS = 700UL;
constexpr uint32_t INDICATOR_OFF_MS = 700UL;

// Generic fixed-code OOK receiver timing limits.
constexpr uint16_t RF_MIN_PULSE_US = 100;
constexpr uint16_t RF_MAX_DATA_PULSE_US = 4000;
constexpr uint16_t RF_FRAME_GAP_US = 4500;
constexpr uint8_t RF_MIN_FRAME_PULSES = 24;
constexpr uint8_t RF_MAX_FRAME_PULSES = 96;
constexpr uint8_t RF_CONFIRM_REPEATS = 3;
constexpr uint32_t RF_REPEAT_WINDOW_MS = 350UL;
constexpr uint32_t RF_RELEASE_GUARD_MS = 650UL;
constexpr uint8_t RF_REPEAT_MAX_BIT_ERRORS = 2;
constexpr uint8_t RF_STORED_MAX_BIT_ERRORS = 2;
constexpr uint8_t RF_TIMING_TOLERANCE_PERCENT = 35;
constexpr uint16_t RF_MIN_CLUSTER_RATIO_PERCENT = 160;

constexpr uint8_t CHANNEL_COUNT = 4;
constexpr uint8_t PWM_CHANNEL_COUNT = 3;
constexpr uint8_t FULL_VISUAL_LEVEL = 255;
constexpr uint8_t FINAL_VISUAL_LEVEL =
    (FULL_VISUAL_LEVEL * FINAL_BRIGHTNESS_PERCENT + 50) / 100;
constexpr uint32_t NIGHT_SCHEDULE_MS = FULL_BRIGHTNESS_MS + LONG_FADE_MS;

// Gamma 2.2 converts a linear visual-brightness request to electrical PWM duty.
const uint8_t GAMMA_22[256] PROGMEM = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
    3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
    6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,
   12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
   20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
   30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
   42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
   56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
   73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
   91,  93,  94,  95,  97,  98,  99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
  113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
  137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
  163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
  192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
  223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255,
};

// ---------------------------------------------------------------------------
// Light channel state machine
// ---------------------------------------------------------------------------

class LightChannel {
 public:
  LightChannel(uint8_t pwmPin, uint8_t enablePin, bool supportsPWM)
      : pwmPin_(pwmPin), enablePin_(enablePin), supportsPWM_(supportsPWM) {}

  void beginSafe() {
    targetVisual_ = 0;
    currentVisual_ = 0;
    lastDuty_ = 0;
    regulatorOn_ = false;
    settling_ = false;
    fading_ = false;
    immediateAfterSettle_ = false;
  }

  void request(uint8_t visualLevel, uint32_t now) {
    immediateAfterSettle_ = false;
    if (!supportsPWM_) {
      visualLevel = visualLevel ? FULL_VISUAL_LEVEL : 0;
    }
    if (visualLevel == targetVisual_) {
      return;
    }
    targetVisual_ = visualLevel;

    if (!supportsPWM_) {
      if (targetVisual_) {
        if (!regulatorOn_) {
          digitalWrite(pwmPin_, LOW);
          digitalWrite(enablePin_, LOW);
          regulatorOn_ = true;
          settling_ = true;
          settleStartedMs_ = now;
        } else if (!settling_) {
          digitalWrite(pwmPin_, HIGH);
          currentVisual_ = FULL_VISUAL_LEVEL;
        }
      } else {
        // Remove the load before disabling the regulator.
        digitalWrite(pwmPin_, LOW);
        digitalWrite(enablePin_, HIGH);
        currentVisual_ = 0;
        regulatorOn_ = false;
        settling_ = false;
      }
      return;
    }

    if (targetVisual_ && !regulatorOn_) {
      writeVisual(0);
      digitalWrite(enablePin_, LOW);
      regulatorOn_ = true;
      settling_ = true;
      fading_ = false;
      currentVisual_ = 0;
      settleStartedMs_ = now;
    } else if (!targetVisual_ && settling_) {
      writeVisual(0);
      digitalWrite(enablePin_, HIGH);
      regulatorOn_ = false;
      settling_ = false;
    } else if (regulatorOn_ && !settling_) {
      startFade(targetVisual_, now);
    }
  }

  // Used only for learn/confirmation indications. digitalWrite() disconnects
  // TCA PWM on PB0..PB2, producing a true steady LOW or HIGH output.
  void requestImmediate(bool on, uint32_t now) {
    targetVisual_ = on ? FULL_VISUAL_LEVEL : 0;
    fading_ = false;
    immediateAfterSettle_ = on;

    if (!on) {
      digitalWrite(pwmPin_, LOW);
      lastDuty_ = 0;
      currentVisual_ = 0;
      digitalWrite(enablePin_, HIGH);
      regulatorOn_ = false;
      settling_ = false;
      return;
    }

    if (!regulatorOn_) {
      digitalWrite(pwmPin_, LOW);
      lastDuty_ = 0;
      currentVisual_ = 0;
      digitalWrite(enablePin_, LOW);
      regulatorOn_ = true;
      settling_ = true;
      settleStartedMs_ = now;
    } else if (!settling_) {
      digitalWrite(pwmPin_, HIGH);
      lastDuty_ = FULL_VISUAL_LEVEL;
      currentVisual_ = FULL_VISUAL_LEVEL;
    }
  }

  void update(uint32_t now) {
    if (settling_ && static_cast<uint32_t>(now - settleStartedMs_) >= LM2596_SETTLE_MS) {
      settling_ = false;
      if (!targetVisual_) {
        digitalWrite(enablePin_, HIGH);
        regulatorOn_ = false;
      } else if (immediateAfterSettle_) {
        digitalWrite(pwmPin_, HIGH);
        lastDuty_ = FULL_VISUAL_LEVEL;
        currentVisual_ = FULL_VISUAL_LEVEL;
      } else if (supportsPWM_) {
        startFade(targetVisual_, now);
      } else {
        digitalWrite(pwmPin_, HIGH);  // Channel 4 is never PWM-dimmed.
        currentVisual_ = FULL_VISUAL_LEVEL;
      }
    }

    if (!supportsPWM_ || !fading_) {
      return;
    }

    const uint32_t elapsed = now - fadeStartedMs_;
    if (elapsed >= FAST_FADE_MS) {
      currentVisual_ = fadeTo_;
      fading_ = false;
    } else {
      const int16_t change = static_cast<int16_t>(fadeTo_) - fadeFrom_;
      const int32_t position =
          static_cast<int32_t>(change) * static_cast<int32_t>(elapsed) /
          static_cast<int32_t>(FAST_FADE_MS);
      currentVisual_ = static_cast<uint8_t>(static_cast<int16_t>(fadeFrom_) + position);
    }
    writeVisual(currentVisual_);

    if (!fading_ && currentVisual_ == 0 && targetVisual_ == 0) {
      digitalWrite(enablePin_, HIGH);
      regulatorOn_ = false;
    }
  }

 private:
  void startFade(uint8_t destination, uint32_t now) {
    fadeFrom_ = currentVisual_;
    fadeTo_ = destination;
    fadeStartedMs_ = now;
    fading_ = (fadeFrom_ != fadeTo_);
    if (!fading_) {
      writeVisual(currentVisual_);
      if (currentVisual_ == 0) {
        digitalWrite(enablePin_, HIGH);
        regulatorOn_ = false;
      }
    }
  }

  void writeVisual(uint8_t visualLevel) {
    const uint8_t duty = pgm_read_byte(&GAMMA_22[visualLevel]);
    if (duty != lastDuty_) {
      analogWrite(pwmPin_, duty);  // TCA0 hardware PWM on PB0..PB2.
      lastDuty_ = duty;
    }
  }

  const uint8_t pwmPin_;
  const uint8_t enablePin_;
  const bool supportsPWM_;
  uint8_t targetVisual_ = 0;
  uint8_t currentVisual_ = 0;
  uint8_t lastDuty_ = 0;
  uint8_t fadeFrom_ = 0;
  uint8_t fadeTo_ = 0;
  bool regulatorOn_ = false;
  bool settling_ = false;
  bool fading_ = false;
  bool immediateAfterSettle_ = false;
  uint32_t settleStartedMs_ = 0;
  uint32_t fadeStartedMs_ = 0;
};

LightChannel channels[CHANNEL_COUNT] = {
    LightChannel(CH1_PWM_PIN, CH1_ENABLE_PIN, true),
    LightChannel(CH2_PWM_PIN, CH2_ENABLE_PIN, true),
    LightChannel(CH3_PWM_PIN, CH3_ENABLE_PIN, true),
    LightChannel(CH4_PWM_PIN, CH4_ENABLE_PIN, false),
};

// Remote overrides are deliberately RAM-only. With no override, every channel
// follows the dusk sensor and night schedule automatically.
bool manualOverrideActive[CHANNEL_COUNT] = {false, false, false, false};
bool manualChannelOn[CHANNEL_COUNT] = {false, false, false, false};

void clearManualOverrides() {
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    manualOverrideActive[i] = false;
    manualChannelOn[i] = false;
  }
}

// ---------------------------------------------------------------------------
// Dusk/dawn and battery ADC state
// ---------------------------------------------------------------------------

bool nightMode = false;
bool duskCandidateActive = false;
bool dawnCandidateActive = false;
uint32_t duskCandidateStartedMs = 0;
uint32_t dawnCandidateStartedMs = 0;
uint32_t nightStartedMs = 0;
uint32_t lastAdcSampleMs = 0;
uint16_t filteredDuskAdc = 0;
uint16_t filteredBatteryAdc = 0;
bool adcFilterInitialized = false;

uint16_t readAveragedAdc(uint8_t pin) {
  // The throwaway conversion and maximum 1-series sample duration give the
  // ADC sample capacitor ample acquisition time from the 24.8 kOhm divider.
  (void)analogRead(pin);
  uint32_t total = 0;
  for (uint8_t i = 0; i < ADC_AVERAGE_SAMPLES; ++i) {
    total += analogRead(pin);
  }
  return static_cast<uint16_t>(total / ADC_AVERAGE_SAMPLES);
}

float getBatteryVoltage() {
  const float adcPinVolts =
      (static_cast<float>(filteredBatteryAdc) * ADC_REFERENCE_VOLTS) / ADC_FULL_SCALE;
  const float dividerRatio =
      BATTERY_DIVIDER_LOWER_OHMS /
      (BATTERY_DIVIDER_UPPER_OHMS + BATTERY_DIVIDER_LOWER_OHMS);
  return (adcPinVolts / dividerRatio) * BATTERY_CALIBRATION;
}

void updateAdcAndDayNight(uint32_t now) {
  if (static_cast<uint32_t>(now - lastAdcSampleMs) < ADC_SAMPLE_INTERVAL_MS) {
    return;
  }
  lastAdcSampleMs = now;

  const uint16_t dusk = readAveragedAdc(DUSK_SENSE_PIN);
  const uint16_t battery = readAveragedAdc(BAT_ADC_PIN);
  if (!adcFilterInitialized) {
    filteredDuskAdc = dusk;
    filteredBatteryAdc = battery;
    adcFilterInitialized = true;
  } else {
    filteredDuskAdc = static_cast<uint16_t>((3UL * filteredDuskAdc + dusk + 2) / 4);
    filteredBatteryAdc = static_cast<uint16_t>((3UL * filteredBatteryAdc + battery + 2) / 4);
  }

  if (!nightMode) {
    dawnCandidateActive = false;
    if (filteredDuskAdc >= DUSK_DARK_THRESHOLD_ADC) {
      if (!duskCandidateActive) {
        duskCandidateActive = true;
        duskCandidateStartedMs = now;
      } else if (static_cast<uint32_t>(now - duskCandidateStartedMs) >= DUSK_CONFIRM_MS) {
        nightMode = true;
        nightStartedMs = now;
        duskCandidateActive = false;
        clearManualOverrides();
      }
    } else {
      duskCandidateActive = false;
    }
  } else {
    duskCandidateActive = false;
    if (filteredDuskAdc <= DUSK_LIGHT_THRESHOLD_ADC) {
      if (!dawnCandidateActive) {
        dawnCandidateActive = true;
        dawnCandidateStartedMs = now;
      } else if (static_cast<uint32_t>(now - dawnCandidateStartedMs) >= DAWN_CONFIRM_MS) {
        nightMode = false;
        dawnCandidateActive = false;
        clearManualOverrides();
      }
    } else {
      dawnCandidateActive = false;
    }
  }
}

uint8_t scheduledVisualLevel(uint32_t now) {
  if (!nightMode) {
    return 0;
  }
  const uint32_t elapsed = now - nightStartedMs;
  if (elapsed < FULL_BRIGHTNESS_MS) {
    return FULL_VISUAL_LEVEL;
  }
  if (elapsed >= NIGHT_SCHEDULE_MS) {
    return 0;
  }
  const uint32_t fadeElapsed = elapsed - FULL_BRIGHTNESS_MS;
  const uint32_t reduction =
      static_cast<uint32_t>(FULL_VISUAL_LEVEL - FINAL_VISUAL_LEVEL) * fadeElapsed /
      LONG_FADE_MS;
  return static_cast<uint8_t>(FULL_VISUAL_LEVEL - reduction);
}

// ---------------------------------------------------------------------------
// Debounced buttons and non-blocking indicator flashes
// ---------------------------------------------------------------------------

class DebouncedButton {
 public:
  void begin(uint8_t pin) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
    rawPressed_ = stablePressed_ = (digitalRead(pin_) == LOW);
    rawChangedMs_ = millis();
  }

  void update(uint32_t now) {
    const bool pressedNow = (digitalRead(pin_) == LOW);
    if (pressedNow != rawPressed_) {
      rawPressed_ = pressedNow;
      rawChangedMs_ = now;
    }
    if (rawPressed_ != stablePressed_ &&
        static_cast<uint32_t>(now - rawChangedMs_) >= BUTTON_DEBOUNCE_MS) {
      stablePressed_ = rawPressed_;
      changed_ = true;
    }
  }

  bool pressed() const { return stablePressed_; }
  bool takeChanged() {
    const bool result = changed_;
    changed_ = false;
    return result;
  }

 private:
  uint8_t pin_ = 0;
  bool rawPressed_ = false;
  bool stablePressed_ = false;
  bool changed_ = false;
  uint32_t rawChangedMs_ = 0;
};

DebouncedButton testButton;
DebouncedButton learnButton;

struct IndicatorPattern {
  bool active = false;
  bool phaseOn = false;
  uint8_t channelMask = 0;
  uint8_t pulsesRemaining = 0;
  uint16_t onMs = INDICATOR_ON_MS;
  uint16_t offMs = INDICATOR_OFF_MS;
  uint32_t nextTransitionMs = 0;
} indicator;

void startIndicator(uint8_t channelMask, uint8_t pulseCount, uint32_t now,
                    uint16_t onMs = INDICATOR_ON_MS,
                    uint16_t offMs = INDICATOR_OFF_MS) {
  indicator.active = true;
  indicator.phaseOn = true;
  indicator.channelMask = channelMask;
  indicator.pulsesRemaining = pulseCount;
  indicator.onMs = onMs;
  indicator.offMs = offMs;
  indicator.nextTransitionMs = now + onMs;
}

void updateIndicator(uint32_t now) {
  if (!indicator.active || static_cast<int32_t>(now - indicator.nextTransitionMs) < 0) {
    return;
  }
  if (indicator.phaseOn) {
    indicator.phaseOn = false;
    --indicator.pulsesRemaining;
    indicator.nextTransitionMs = now + indicator.offMs;
  } else {
    if (indicator.pulsesRemaining == 0) {
      indicator.active = false;
    } else {
      indicator.phaseOn = true;
      indicator.nextTransitionMs = now + indicator.onMs;
    }
  }
}

// ---------------------------------------------------------------------------
// Protocol-independent fixed-code OOK capture, validation and EEPROM storage
// ---------------------------------------------------------------------------

constexpr uint8_t RF_RING_SIZE = 128;  // Must stay a power of two.
constexpr uint8_t RF_SIGNATURE_BYTES = (RF_MAX_FRAME_PULSES + 7) / 8;
volatile uint16_t rfEdgeRing[RF_RING_SIZE];
volatile uint8_t rfRingHead = 0;
volatile uint8_t rfRingTail = 0;
volatile bool rfRingOverflow = false;
volatile uint32_t rfLastEdgeUs = 0;

// Bit 15 stores the level of the pulse that just ended; bits 0..14 are us.
void rfEdgeIsr() {
  const uint32_t nowUs = micros();
  uint32_t duration = nowUs - rfLastEdgeUs;
  rfLastEdgeUs = nowUs;
  if (duration > 0x7FFFUL) {
    duration = 0x7FFFUL;
  }
  const bool endedHigh = (VPORTC.IN & PIN0_bm) == 0;
  const uint16_t packed = static_cast<uint16_t>(duration) |
                          (endedHigh ? 0x8000U : 0U);
  const uint8_t next = (rfRingHead + 1) & (RF_RING_SIZE - 1);
  if (next == rfRingTail) {
    rfRingOverflow = true;
  } else {
    rfEdgeRing[rfRingHead] = packed;
    rfRingHead = next;
  }
}

struct __attribute__((packed)) RfSignature {
  uint8_t pulseCount;
  uint8_t firstPulseHigh;
  uint16_t shortUs;
  uint16_t longUs;
  uint8_t longPulseBits[RF_SIGNATURE_BYTES];
};

constexpr uint32_t EEPROM_MAGIC = 0x534C5246UL;  // "SLRF"
constexpr uint8_t EEPROM_FORMAT_VERSION = 1;

struct __attribute__((packed)) StoredRfSettings {
  uint32_t magic;
  uint8_t version;
  uint8_t learnedMask;
  RfSignature codes[CHANNEL_COUNT];
  uint16_t crc;
};

// Explicit declarations prevent Arduino's sketch preprocessor from placing
// generated prototypes before the two RF structure definitions above.
uint16_t settingsCrc(const StoredRfSettings &settings);
bool signatureShapeValid(const RfSignature &signature);
uint8_t signatureBitErrors(const RfSignature &a, const RfSignature &b);
bool signaturesMatch(const RfSignature &a, const RfSignature &b,
                     uint8_t maxBitErrors, uint8_t timingTolerancePercent);
bool makeSignature(RfSignature &result);
bool learnedCodeIsDuplicate(const RfSignature &signature, uint8_t beforeChannel);
int8_t matchLearnedChannel(const RfSignature &signature);
void acceptConfirmedRf(const RfSignature &signature, uint32_t now);

StoredRfSettings rfSettings;
uint16_t rfFrameDurations[RF_MAX_FRAME_PULSES];
uint8_t rfFramePulseCount = 0;
bool rfFrameFirstHigh = false;
bool rfFrameSynchronized = false;
bool rfFrameInvalid = false;
RfSignature rfRepeatCandidate = {};
uint8_t rfRepeatCount = 0;
uint32_t rfRepeatLastMs = 0;
bool rfPressLatched = false;
uint32_t rfLastMatchedFrameMs = 0;

uint16_t crc16Ccitt(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  while (length--) {
    crc ^= static_cast<uint16_t>(*data++) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

uint16_t settingsCrc(const StoredRfSettings &settings) {
  return crc16Ccitt(reinterpret_cast<const uint8_t *>(&settings),
                    sizeof(StoredRfSettings) - sizeof(settings.crc));
}

bool signatureShapeValid(const RfSignature &signature) {
  return signature.pulseCount >= RF_MIN_FRAME_PULSES &&
         signature.pulseCount <= RF_MAX_FRAME_PULSES &&
         signature.shortUs >= RF_MIN_PULSE_US &&
         signature.longUs <= RF_MAX_DATA_PULSE_US &&
         static_cast<uint32_t>(signature.longUs) * 100UL >=
             static_cast<uint32_t>(signature.shortUs) * RF_MIN_CLUSTER_RATIO_PERCENT;
}

void loadRfSettings() {
  EEPROM.get(0, rfSettings);
  bool valid = rfSettings.magic == EEPROM_MAGIC &&
               rfSettings.version == EEPROM_FORMAT_VERSION &&
               rfSettings.crc == settingsCrc(rfSettings) &&
               (rfSettings.learnedMask & 0xF0) == 0;
  if (valid) {
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      if ((rfSettings.learnedMask & (1U << i)) &&
          !signatureShapeValid(rfSettings.codes[i])) {
        valid = false;
      }
    }
  }
  if (!valid) {
    memset(&rfSettings, 0, sizeof(rfSettings));
    rfSettings.magic = EEPROM_MAGIC;
    rfSettings.version = EEPROM_FORMAT_VERSION;
  }
}

void saveRfSettings() {
  rfSettings.magic = EEPROM_MAGIC;
  rfSettings.version = EEPROM_FORMAT_VERSION;
  rfSettings.crc = settingsCrc(rfSettings);
  EEPROM.put(0, rfSettings);  // EEPROM.put uses update semantics in megaTinyCore.
}

void eraseRfSettings() {
  memset(&rfSettings, 0, sizeof(rfSettings));
  rfSettings.magic = EEPROM_MAGIC;
  rfSettings.version = EEPROM_FORMAT_VERSION;
  saveRfSettings();
}

bool timingWithinPercent(uint16_t a, uint16_t b, uint8_t tolerancePercent) {
  const uint16_t larger = (a > b) ? a : b;
  const uint16_t smaller = (a > b) ? b : a;
  return static_cast<uint32_t>(larger - smaller) * 100UL <=
         static_cast<uint32_t>(larger) * tolerancePercent;
}

uint8_t signatureBitErrors(const RfSignature &a, const RfSignature &b) {
  if (a.pulseCount != b.pulseCount || a.firstPulseHigh != b.firstPulseHigh) {
    return 255;
  }
  uint8_t errors = 0;
  for (uint8_t pulse = 0; pulse < a.pulseCount; ++pulse) {
    const uint8_t mask = 1U << (pulse & 7);
    if ((a.longPulseBits[pulse >> 3] & mask) !=
        (b.longPulseBits[pulse >> 3] & mask)) {
      ++errors;
    }
  }
  return errors;
}

bool signaturesMatch(const RfSignature &a, const RfSignature &b,
                     uint8_t maxBitErrors, uint8_t timingTolerancePercent) {
  return timingWithinPercent(a.shortUs, b.shortUs, timingTolerancePercent) &&
         timingWithinPercent(a.longUs, b.longUs, timingTolerancePercent) &&
         signatureBitErrors(a, b) <= maxBitErrors;
}

bool makeSignature(RfSignature &result) {
  if (rfFrameInvalid || rfFramePulseCount < RF_MIN_FRAME_PULSES ||
      rfFramePulseCount > RF_MAX_FRAME_PULSES) {
    return false;
  }

  uint16_t shortCenter = 0xFFFF;
  uint16_t longCenter = 0;
  for (uint8_t i = 0; i < rfFramePulseCount; ++i) {
    const uint16_t value = rfFrameDurations[i];
    if (value < shortCenter) shortCenter = value;
    if (value > longCenter) longCenter = value;
  }

  uint8_t shortCount = 0;
  uint8_t longCount = 0;
  for (uint8_t iteration = 0; iteration < 6; ++iteration) {
    uint32_t shortTotal = 0;
    uint32_t longTotal = 0;
    shortCount = 0;
    longCount = 0;
    for (uint8_t i = 0; i < rfFramePulseCount; ++i) {
      const uint16_t value = rfFrameDurations[i];
      const uint16_t shortDistance =
          (value > shortCenter) ? value - shortCenter : shortCenter - value;
      const uint16_t longDistance =
          (value > longCenter) ? value - longCenter : longCenter - value;
      if (shortDistance <= longDistance) {
        shortTotal += value;
        ++shortCount;
      } else {
        longTotal += value;
        ++longCount;
      }
    }
    if (!shortCount || !longCount) {
      return false;
    }
    shortCenter = static_cast<uint16_t>(shortTotal / shortCount);
    longCenter = static_cast<uint16_t>(longTotal / longCount);
  }

  if (shortCenter > longCenter) {
    const uint16_t swap = shortCenter;
    shortCenter = longCenter;
    longCenter = swap;
    const uint8_t countSwap = shortCount;
    shortCount = longCount;
    longCount = countSwap;
  }
  if (shortCount < rfFramePulseCount / 8 || longCount < rfFramePulseCount / 8 ||
      static_cast<uint32_t>(longCenter) * 100UL <
          static_cast<uint32_t>(shortCenter) * RF_MIN_CLUSTER_RATIO_PERCENT) {
    return false;
  }

  memset(&result, 0, sizeof(result));
  result.pulseCount = rfFramePulseCount;
  result.firstPulseHigh = rfFrameFirstHigh;
  result.shortUs = shortCenter;
  result.longUs = longCenter;
  const uint16_t boundary = (shortCenter + longCenter) / 2;
  for (uint8_t pulse = 0; pulse < rfFramePulseCount; ++pulse) {
    if (rfFrameDurations[pulse] > boundary) {
      result.longPulseBits[pulse >> 3] |= 1U << (pulse & 7);
    }
  }
  return true;
}

enum class LearnState : uint8_t {
  Off,
  PromptFlashes,
  WaitingForCode,
  ConfirmFlashes,
  CompleteFlashes,
  EraseFlashes,
  TimeoutFlashes,
};

LearnState learnState = LearnState::Off;
uint8_t learnChannel = 0;
uint32_t learnDeadlineMs = 0;

void resetRfRepeatCandidate() {
  memset(&rfRepeatCandidate, 0, sizeof(rfRepeatCandidate));
  rfRepeatCount = 0;
  rfRepeatLastMs = 0;
}

bool learnedCodeIsDuplicate(const RfSignature &signature, uint8_t beforeChannel) {
  for (uint8_t i = 0; i < beforeChannel; ++i) {
    if ((rfSettings.learnedMask & (1U << i)) &&
        signaturesMatch(signature, rfSettings.codes[i],
                        RF_STORED_MAX_BIT_ERRORS, RF_TIMING_TOLERANCE_PERCENT)) {
      return true;
    }
  }
  return false;
}

int8_t matchLearnedChannel(const RfSignature &signature) {
  int8_t bestChannel = -1;
  uint8_t bestErrors = 255;
  uint8_t secondBestErrors = 255;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    if (!(rfSettings.learnedMask & (1U << channel))) {
      continue;
    }
    const RfSignature &stored = rfSettings.codes[channel];
    if (!timingWithinPercent(signature.shortUs, stored.shortUs,
                             RF_TIMING_TOLERANCE_PERCENT) ||
        !timingWithinPercent(signature.longUs, stored.longUs,
                             RF_TIMING_TOLERANCE_PERCENT)) {
      continue;
    }
    const uint8_t errors = signatureBitErrors(signature, stored);
    if (errors < bestErrors) {
      secondBestErrors = bestErrors;
      bestErrors = errors;
      bestChannel = channel;
    } else if (errors < secondBestErrors) {
      secondBestErrors = errors;
    }
  }
  if (bestErrors > RF_STORED_MAX_BIT_ERRORS) {
    return -1;
  }
  // Reject an ambiguous damaged frame when two stored buttons are equally close.
  if (secondBestErrors != 255 && bestErrors == secondBestErrors) {
    return -1;
  }
  return bestChannel;
}

void acceptConfirmedRf(const RfSignature &signature, uint32_t now) {
  if (learnState == LearnState::WaitingForCode) {
    if (learnedCodeIsDuplicate(signature, learnChannel)) {
      resetRfRepeatCandidate();
      return;
    }
    rfSettings.codes[learnChannel] = signature;
    rfSettings.learnedMask |= 1U << learnChannel;
    saveRfSettings();
    startIndicator(1U << learnChannel, 3, now);
    learnState = LearnState::ConfirmFlashes;
    resetRfRepeatCandidate();
    return;
  }

  if (learnState != LearnState::Off) {
    return;
  }
  const int8_t channel = matchLearnedChannel(signature);
  if (channel < 0) {
    return;
  }
  rfLastMatchedFrameMs = now;
  if (!rfPressLatched) {
    rfPressLatched = true;
    // A remote held during TEST cannot silently alter the saved RAM state.
    if (!testButton.pressed()) {
      if (!manualOverrideActive[channel]) {
        // The first press always reverses what AUTO currently requests.
        manualChannelOn[channel] = (scheduledVisualLevel(now) == 0);
        manualOverrideActive[channel] = true;
      } else {
        manualChannelOn[channel] = !manualChannelOn[channel];
      }
    }
  }
}

void handleCompletedRfFrame(uint32_t now) {
  RfSignature signature;
  if (!makeSignature(signature)) {
    return;
  }
  if (rfRepeatCount &&
      static_cast<uint32_t>(now - rfRepeatLastMs) <= RF_REPEAT_WINDOW_MS &&
      signaturesMatch(signature, rfRepeatCandidate,
                      RF_REPEAT_MAX_BIT_ERRORS, RF_TIMING_TOLERANCE_PERCENT)) {
    if (rfRepeatCount < 255) ++rfRepeatCount;
  } else {
    rfRepeatCandidate = signature;
    rfRepeatCount = 1;
  }
  rfRepeatLastMs = now;
  if (rfRepeatCount >= RF_CONFIRM_REPEATS) {
    acceptConfirmedRf(signature, now);
  }
}

void resetRfFrameParser() {
  rfFramePulseCount = 0;
  rfFrameInvalid = false;
}

void serviceRf(uint32_t now) {
  if (rfRingOverflow) {
    noInterrupts();
    rfRingTail = rfRingHead;
    rfRingOverflow = false;
    interrupts();
    rfFrameSynchronized = false;
    resetRfFrameParser();
  }

  while (true) {
    noInterrupts();
    if (rfRingTail == rfRingHead) {
      interrupts();
      break;
    }
    const uint16_t packed = rfEdgeRing[rfRingTail];
    rfRingTail = (rfRingTail + 1) & (RF_RING_SIZE - 1);
    interrupts();

    const uint16_t duration = packed & 0x7FFFU;
    const bool endedHigh = packed & 0x8000U;
    if (duration >= RF_FRAME_GAP_US) {
      if (rfFrameSynchronized && rfFramePulseCount) {
        handleCompletedRfFrame(now);
      }
      rfFrameSynchronized = true;
      resetRfFrameParser();
      continue;
    }
    if (!rfFrameSynchronized) {
      continue;
    }
    if (duration < RF_MIN_PULSE_US || duration > RF_MAX_DATA_PULSE_US) {
      rfFrameInvalid = true;
      continue;
    }
    if (rfFramePulseCount >= RF_MAX_FRAME_PULSES) {
      rfFrameInvalid = true;
      continue;
    }
    if (rfFramePulseCount == 0) {
      rfFrameFirstHigh = endedHigh;
    }
    rfFrameDurations[rfFramePulseCount++] = duration;
  }

  if (rfPressLatched &&
      static_cast<uint32_t>(now - rfLastMatchedFrameMs) >= RF_RELEASE_GUARD_MS) {
    rfPressLatched = false;
    resetRfRepeatCandidate();
  }
}

// ---------------------------------------------------------------------------
// Learn-mode and output coordination
// ---------------------------------------------------------------------------

void enterLearnMode(uint32_t now) {
  learnChannel = 0;
  resetRfRepeatCandidate();
  startIndicator(1U << learnChannel, 2, now);
  learnState = LearnState::PromptFlashes;
}

void beginEraseConfirmation(uint32_t now) {
  eraseRfSettings();
  resetRfRepeatCandidate();
  startIndicator(0x0F, 5, now, 550, 450);
  learnState = LearnState::EraseFlashes;
}

void updateLearnState(uint32_t now) {
  switch (learnState) {
    case LearnState::Off:
      break;
    case LearnState::PromptFlashes:
      if (!indicator.active) {
        resetRfRepeatCandidate();
        learnDeadlineMs = now + LEARN_TIMEOUT_MS;
        learnState = LearnState::WaitingForCode;
      }
      break;
    case LearnState::WaitingForCode:
      if (static_cast<int32_t>(now - learnDeadlineMs) >= 0) {
        startIndicator(0x0F, 4, now, 450, 350);
        learnState = LearnState::TimeoutFlashes;
        resetRfRepeatCandidate();
      }
      break;
    case LearnState::ConfirmFlashes:
      if (!indicator.active) {
        ++learnChannel;
        if (learnChannel >= CHANNEL_COUNT) {
          startIndicator(0x0F, 2, now);
          learnState = LearnState::CompleteFlashes;
        } else {
          startIndicator(1U << learnChannel, 2, now);
          learnState = LearnState::PromptFlashes;
        }
      }
      break;
    case LearnState::CompleteFlashes:
    case LearnState::EraseFlashes:
    case LearnState::TimeoutFlashes:
      if (!indicator.active) {
        learnState = LearnState::Off;
      }
      break;
  }
}

void updateButtons(uint32_t now) {
  testButton.update(now);
  learnButton.update(now);

  static uint32_t learnPressedMs = 0;
  static bool learnEntryHandled = false;
  static bool eraseHandled = false;
  if (learnButton.takeChanged()) {
    if (learnButton.pressed()) {
      learnPressedMs = now;
      learnEntryHandled = false;
      eraseHandled = false;
    } else {
      learnEntryHandled = false;
      eraseHandled = false;
    }
  }
  if (!learnButton.pressed()) {
    return;
  }
  const uint32_t heldMs = now - learnPressedMs;
  if (!learnEntryHandled && heldMs >= LEARN_HOLD_MS) {
    learnEntryHandled = true;
    enterLearnMode(now);
  }
  if (!eraseHandled && heldMs >= ERASE_HOLD_MS) {
    eraseHandled = true;
    beginEraseConfirmation(now);
  }
}

void updateOutputRequests(uint32_t now) {
  uint8_t requested[CHANNEL_COUNT] = {0, 0, 0, 0};

  if (testButton.pressed()) {
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      requested[i] = FULL_VISUAL_LEVEL;
    }
  } else if (indicator.active) {
    if (indicator.phaseOn) {
      for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
        if (indicator.channelMask & (1U << i)) {
          requested[i] = FULL_VISUAL_LEVEL;
        }
      }
    }
  } else if (learnState == LearnState::Off) {
    static uint8_t scheduleLevel = 0;
    static uint32_t lastScheduleUpdateMs = 0;
    static bool scheduleNightMode = false;
    if (scheduleNightMode != nightMode ||
        static_cast<uint32_t>(now - lastScheduleUpdateMs) >=
        SCHEDULE_UPDATE_INTERVAL_MS) {
      scheduleNightMode = nightMode;
      lastScheduleUpdateMs = now;
      scheduleLevel = scheduledVisualLevel(now);
    }
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      if (manualOverrideActive[i]) {
        if (manualChannelOn[i]) {
          requested[i] = FULL_VISUAL_LEVEL;
        }
      } else if (scheduleLevel) {
        requested[i] = (i < PWM_CHANNEL_COUNT) ? scheduleLevel : FULL_VISUAL_LEVEL;
      }
    }
  }

  const bool immediateLearnOutput = (learnState != LearnState::Off) || indicator.active;
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    if (immediateLearnOutput) {
      channels[i].requestImmediate(requested[i] != 0, now);
    } else {
      channels[i].request(requested[i], now);
    }
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void establishSafeOutputsImmediately() {
  // Set output latches before changing direction to prevent active-low glitches.
  PORTA.OUTSET = PIN4_bm | PIN5_bm | PIN6_bm | PIN7_bm;  // Regulators OFF.
  PORTA.OUTCLR = PIN3_bm;                                  // CH4 load OFF.
  PORTB.OUTCLR = PIN0_bm | PIN1_bm | PIN2_bm;             // CH1..3 PWM OFF.
  PORTA.DIRSET = PIN3_bm | PIN4_bm | PIN5_bm | PIN6_bm | PIN7_bm;
  PORTB.DIRSET = PIN0_bm | PIN1_bm | PIN2_bm;
}

void setup() {
  establishSafeOutputsImmediately();
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    channels[i].beginSafe();
  }

  testButton.begin(TEST_BUTTON_PIN);
  learnButton.begin(LEARN_BUTTON_PIN);
  pinMode(RF_DATA_PIN, INPUT);
  pinMode(DUSK_SENSE_PIN, INPUT);
  pinMode(BAT_ADC_PIN, INPUT);

  analogReference(VDD);
  analogReadResolution(10);
  analogSampleDuration(31);  // Maximum sample time on the ATtiny1616 ADC.

  loadRfSettings();
  rfLastEdgeUs = micros();
  attachInterrupt(digitalPinToInterrupt(RF_DATA_PIN), rfEdgeIsr, CHANGE);
}

void loop() {
  const uint32_t now = millis();

  serviceRf(now);
  updateButtons(now);
  updateAdcAndDayNight(now);
  updateIndicator(now);
  updateLearnState(now);

  static uint32_t lastOutputUpdateMs = 0;
  if (static_cast<uint32_t>(now - lastOutputUpdateMs) >= 5) {
    lastOutputUpdateMs = now;
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      channels[i].update(now);
    }
    updateOutputRequests(now);
  }
}
