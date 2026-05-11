/*
  Project: LM1971 volume control from a potentiometer
  Target:  ATtiny412 (megaTinyCore)

  Pin map
  -------
  PA0 -> UPDI header (programming)
  PA1 -> Potentiometer wiper (ADC input)
  PA2 -> LM1971 DATA
  PA3 -> LM1971 CLOCK
  PA6 -> LM1971 LOAD (active-low shift enable / latch)

  Notes
  -----
  - Pot ends are expected at VCC and GND.
  - Sketch starts muted for safe startup.
  - This implementation assumes LM1971 command values:
      0x00 = 0 dB (max volume)
      0x3E = -62 dB (minimum non-mute)
      0x3F and above = mute
*/

// LM1971 control pins
const uint8_t PIN_LM_DATA  = PIN_PA2; // Serial data to LM1971
const uint8_t PIN_LM_CLOCK = PIN_PA3; // Shift clock to LM1971
const uint8_t PIN_LM_LOAD  = PIN_PA6; // Active-low shift enable / latch

// Potentiometer ADC pin
const uint8_t PIN_POT = PIN_PA1;      // Pot wiper input

// LM1971 level constants
const uint8_t LM_ADDRESS_CHANNEL_1 = 0x00; // LM1971 address byte for the single audio channel
const uint8_t LM_LEVEL_MAX_VOLUME  = 0x00; // 0 dB attenuation
const uint8_t LM_LEVEL_MIN_VOLUME  = 0x3E; // -62 dB attenuation
const uint8_t LM_LEVEL_MUTE        = 0x3F; // 0x3F and above select mute

// Small filter to reduce chatter when ADC moves by 1 count
const uint8_t ADC_STEP_THRESHOLD = 2;
const uint8_t ADC_AVERAGE_SAMPLES = 4;       // Small startup/run-time pot smoothing
const uint8_t LM_STARTUP_WRITES = 3;         // Repeat startup frames in case the LM1971 is still settling
const uint16_t LM_STARTUP_SETTLE_MS = 150;   // Allow supply/reference rails to settle after power-up
const uint16_t LM_REFRESH_MS = 1000;         // Periodically resend current level in case a frame was missed

uint16_t lastAdc = 0;
uint8_t lastLevel = LM_LEVEL_MUTE;
uint32_t lastWriteMillis = 0;

// Shift one 8-bit value to LM1971, MSB first.
static void shiftLM1971Byte(uint8_t value) {
  for (int8_t bitIndex = 7; bitIndex >= 0; --bitIndex) {
    digitalWrite(PIN_LM_CLOCK, LOW);
    digitalWrite(PIN_LM_DATA, (value >> bitIndex) & 0x01);
    delayMicroseconds(1);
    digitalWrite(PIN_LM_CLOCK, HIGH);
    delayMicroseconds(1);
  }
}

// Send address byte then attenuation byte as one 16-bit LM1971 transfer.
static void writeLM1971(uint8_t level) {
  digitalWrite(PIN_LM_CLOCK, LOW);
  digitalWrite(PIN_LM_LOAD, LOW);
  delayMicroseconds(2);

  shiftLM1971Byte(LM_ADDRESS_CHANNEL_1);
  shiftLM1971Byte(level);

  // Latch shifted word into LM1971
  digitalWrite(PIN_LM_CLOCK, LOW);
  digitalWrite(PIN_LM_LOAD, HIGH);
  delayMicroseconds(2);
  lastWriteMillis = millis();
}

static void writeLM1971Repeated(uint8_t level, uint8_t repeatCount) {
  for (uint8_t count = 0; count < repeatCount; ++count) {
    writeLM1971(level);
    delay(2);
  }
}

static uint8_t levelFromAdc(uint16_t adcValue) {
  // Pot at minimum -> mute, otherwise 0x3E..0x00 attenuation.
  if (adcValue <= 2) {
    return LM_LEVEL_MUTE;
  }

  return map(adcValue, 3, 1023, LM_LEVEL_MIN_VOLUME, LM_LEVEL_MAX_VOLUME);
}

static uint16_t readPotAverage() {
  uint32_t total = 0;

  // Discard one read after startup/reference changes, then average a few.
  analogRead(PIN_POT);
  for (uint8_t sample = 0; sample < ADC_AVERAGE_SAMPLES; ++sample) {
    total += analogRead(PIN_POT);
    delay(1);
  }

  return total / ADC_AVERAGE_SAMPLES;
}

void setup() {
  pinMode(PIN_LM_DATA, OUTPUT);
  pinMode(PIN_LM_CLOCK, OUTPUT);
  pinMode(PIN_LM_LOAD, OUTPUT);
  pinMode(PIN_POT, INPUT);

  digitalWrite(PIN_LM_DATA, LOW);
  digitalWrite(PIN_LM_CLOCK, LOW);
  digitalWrite(PIN_LM_LOAD, HIGH);

  delay(LM_STARTUP_SETTLE_MS);

  // Conservative startup: force mute before reading pot.
  writeLM1971Repeated(LM_LEVEL_MUTE, LM_STARTUP_WRITES);

  // Initial ADC snapshot for simple change detection.
  lastAdc = readPotAverage();
  lastLevel = levelFromAdc(lastAdc);
  writeLM1971Repeated(lastLevel, LM_STARTUP_WRITES);
}

void loop() {
  const uint16_t adcValue = readPotAverage(); // 0..1023
  const bool adcChanged = abs((int)adcValue - (int)lastAdc) >= ADC_STEP_THRESHOLD;
  const bool refreshDue = (millis() - lastWriteMillis) >= LM_REFRESH_MS;

  // Ignore tiny ADC movement to reduce unnecessary updates.
  if (!adcChanged && !refreshDue) {
    delay(10);
    return;
  }

  if (adcChanged) {
    lastAdc = adcValue;
  }

  const uint8_t targetLevel = adcChanged ? levelFromAdc(adcValue) : lastLevel;
  if (targetLevel != lastLevel) {
    writeLM1971(targetLevel);
    lastLevel = targetLevel;
  } else if (refreshDue) {
    writeLM1971(lastLevel);
  }

  delay(10);
}
