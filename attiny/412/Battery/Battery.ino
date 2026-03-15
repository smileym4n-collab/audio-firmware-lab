/*
  Battery undervoltage LED warning for ATtiny412 (megaTinyCore)

  Monitors a divided battery voltage on PA6 and blinks an LED on PA3 when
  battery voltage drops below a configurable threshold.

  Assumptions:
  - ATtiny412 running with Vcc = 5.0 V ADC reference (default analogReference)
  - Divider: 270k (top, battery+) and 47k (bottom, GND)
  - LED anode goes to Vcc through a resistor, cathode goes to PA3
    (PA3 sinks current, so LOW = LED on, HIGH = LED off)
*/

// -----------------------------
// Pin map (ATtiny412)
// -----------------------------
// PA3 -> LED cathode (active LOW, PWM capable output)
// PA6 -> battery sense input from divider (270k / 47k)

const uint8_t PIN_LED = PIN_PA3;      // LED output (sinking)
const uint8_t PIN_BAT_SENSE = PIN_PA6; // Divider output to ADC

// Divider values in ohms
const float R_TOP = 270000.0f;    // 270k from battery+ to PA6
const float R_BOTTOM = 47000.0f;  // 47k from PA6 to GND

// ADC assumption for default reference (Vcc)
const float ADC_REF_V = 5.0f;
const uint16_t ADC_MAX = 1023;

// Undervoltage threshold
const float BATTERY_LOW_V = 12.0f;

// Blink settings
const unsigned long BLINK_PERIOD_MS = 500; // Toggle every 500 ms
const uint8_t LED_ON_BRIGHTNESS = 64;      // 0..255 (PWM brightness)

// Simple software debounce/filter for threshold crossing
const uint8_t LOW_BAT_CONFIRM_SAMPLES = 4;
const uint8_t OK_BAT_CONFIRM_SAMPLES = 4;

unsigned long lastBlinkMs = 0;
bool blinkOnPhase = false;
bool lowBatteryActive = false;
uint8_t lowCount = 0;
uint8_t okCount = 0;

uint16_t batteryThresholdAdcCount() {
  // Vadc = Vbat * (R_BOTTOM / (R_TOP + R_BOTTOM))
  const float dividerRatio = (R_BOTTOM / (R_TOP + R_BOTTOM));
  const float thresholdAdc = (BATTERY_LOW_V * dividerRatio / ADC_REF_V) * ADC_MAX;
  return (uint16_t)(thresholdAdc + 0.5f);
}

void setLedBrightness(uint8_t brightness) {
  // LED is active-low, so invert brightness for PWM output:
  // brightness 0   -> fully off (pin HIGH)
  // brightness 255 -> fully on  (pin LOW)
  analogWrite(PIN_LED, 255 - brightness);
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  analogWrite(PIN_LED, 255); // Safe startup state: LED off

  pinMode(PIN_BAT_SENSE, INPUT);
}

void loop() {
  const uint16_t adcValue = analogRead(PIN_BAT_SENSE);
  const uint16_t lowThreshold = batteryThresholdAdcCount();

  // Conservative state update with sample confirmation
  if (adcValue < lowThreshold) {
    if (lowCount < LOW_BAT_CONFIRM_SAMPLES) {
      lowCount++;
    }
    okCount = 0;
    if (lowCount >= LOW_BAT_CONFIRM_SAMPLES) {
      lowBatteryActive = true;
    }
  } else {
    if (okCount < OK_BAT_CONFIRM_SAMPLES) {
      okCount++;
    }
    lowCount = 0;
    if (okCount >= OK_BAT_CONFIRM_SAMPLES) {
      lowBatteryActive = false;
    }
  }

  if (!lowBatteryActive) {
    // Battery is OK -> LED off
    setLedBrightness(0);
    blinkOnPhase = false;
    lastBlinkMs = millis();
    return;
  }

  // Low battery -> PWM blink
  const unsigned long now = millis();
  if (now - lastBlinkMs >= BLINK_PERIOD_MS) {
    lastBlinkMs = now;
    blinkOnPhase = !blinkOnPhase;
  }

  if (blinkOnPhase) {
    setLedBrightness(LED_ON_BRIGHTNESS);
  } else {
    setLedBrightness(0);
  }
}
