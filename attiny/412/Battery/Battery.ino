/*
  Battery divider monitor LED indicator for ATtiny412 (megaTinyCore)

  Monitors the divided battery sense voltage on PA6 and drives an LED on PA3:
  - PA6 <= 1.75 V: PWM pulse (2 s ON, 2 s OFF)
  - PA6 <= 1.60 V: LED solid ON

  Assumptions:
  - ATtiny412 running with Vcc = 5.0 V ADC reference (default analogReference)
  - LED anode goes to Vcc through a resistor, cathode goes to PA3
    (PA3 sinks current, so LOW = LED on, HIGH = LED off)
*/

// -----------------------------
// Pin map (ATtiny412)
// -----------------------------
// PA3 -> LED cathode (active LOW, PWM capable output)
// PA6 -> divided battery sense input (ADC)

const uint8_t PIN_LED = PIN_PA3;       // LED output (sinking)
const uint8_t PIN_BAT_SENSE = PIN_PA6; // Divider output to ADC

// ADC assumption for default reference (Vcc)
const float ADC_REF_V = 5.0f;
const uint16_t ADC_MAX = 1023;

// Divider-node voltage thresholds (voltage measured directly at PA6)
const float WARNING_PA6_V = 1.75f; // Enter PWM pulse mode
const float CRITICAL_PA6_V = 1.60f; // Enter solid-on mode

// Small hysteresis to reduce chatter around each threshold
const float WARNING_HYSTERESIS_V = 0.03f;
const float CRITICAL_HYSTERESIS_V = 0.03f;

// LED behavior settings
const unsigned long WARNING_TOGGLE_MS = 2000; // Toggle ON/OFF every 2 seconds
const uint8_t LED_PWM_BRIGHTNESS = 64;         // Warning-mode PWM brightness (0..255)

// Simple software debounce/filter for state changes
const uint8_t STATE_CONFIRM_SAMPLES = 4;

enum LedState : uint8_t {
  LED_STATE_OK = 0,
  LED_STATE_WARNING,
  LED_STATE_CRITICAL
};

LedState ledState = LED_STATE_OK;
uint8_t pendingCount = 0;
unsigned long lastToggleMs = 0;
bool warningOnPhase = false;

uint16_t adcCountFromVoltage(float voltage) {
  const float adc = (voltage / ADC_REF_V) * ADC_MAX;
  return (uint16_t)(adc + 0.5f);
}

void setLedBrightness(uint8_t brightness) {
  // LED is active-low, so invert brightness for PWM output:
  // brightness 0   -> fully off (pin HIGH)
  // brightness 255 -> fully on  (pin LOW)
  analogWrite(PIN_LED, 255 - brightness);
}

LedState classifyTargetState(uint16_t adcValue) {
  const uint16_t warningEnter = adcCountFromVoltage(WARNING_PA6_V);
  const uint16_t warningExit = adcCountFromVoltage(WARNING_PA6_V + WARNING_HYSTERESIS_V);
  const uint16_t criticalEnter = adcCountFromVoltage(CRITICAL_PA6_V);
  const uint16_t criticalExit = adcCountFromVoltage(CRITICAL_PA6_V + CRITICAL_HYSTERESIS_V);

  // Start with the current state and only change when the corresponding
  // enter/exit thresholds are crossed.
  switch (ledState) {
    case LED_STATE_OK:
      if (adcValue <= criticalEnter) {
        return LED_STATE_CRITICAL;
      }
      if (adcValue <= warningEnter) {
        return LED_STATE_WARNING;
      }
      return LED_STATE_OK;

    case LED_STATE_WARNING:
      if (adcValue <= criticalEnter) {
        return LED_STATE_CRITICAL;
      }
      if (adcValue >= warningExit) {
        return LED_STATE_OK;
      }
      return LED_STATE_WARNING;

    case LED_STATE_CRITICAL:
      if (adcValue >= criticalExit) {
        if (adcValue <= warningEnter) {
          return LED_STATE_WARNING;
        }
        return LED_STATE_OK;
      }
      return LED_STATE_CRITICAL;
  }

  return LED_STATE_OK;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  analogWrite(PIN_LED, 255); // Safe startup state: LED off

  pinMode(PIN_BAT_SENSE, INPUT);
}

void loop() {
  const uint16_t adcValue = analogRead(PIN_BAT_SENSE);
  const LedState targetState = classifyTargetState(adcValue);

  // Conservative state update with sample confirmation
  if (targetState != ledState) {
    if (pendingCount < STATE_CONFIRM_SAMPLES) {
      pendingCount++;
    }
    if (pendingCount >= STATE_CONFIRM_SAMPLES) {
      ledState = targetState;
      pendingCount = 0;

      // Reset warning blink phase on entry for deterministic behavior
      if (ledState == LED_STATE_WARNING) {
        warningOnPhase = true;
        lastToggleMs = millis();
      }
    }
  } else {
    pendingCount = 0;
  }

  if (ledState == LED_STATE_OK) {
    setLedBrightness(0);
    warningOnPhase = false;
    lastToggleMs = millis();
    return;
  }

  if (ledState == LED_STATE_CRITICAL) {
    setLedBrightness(255); // Solid ON
    warningOnPhase = false;
    lastToggleMs = millis();
    return;
  }

  // Warning state: PWM pulse every 2 seconds (2 s ON, 2 s OFF)
  const unsigned long now = millis();
  if (now - lastToggleMs >= WARNING_TOGGLE_MS) {
    lastToggleMs = now;
    warningOnPhase = !warningOnPhase;
  }

  if (warningOnPhase) {
    setLedBrightness(LED_PWM_BRIGHTNESS);
  } else {
    setLedBrightness(0);
  }
}
