/*
  Project: LM1971 volume control from a potentiometer
  Target:  ATtiny412 (megaTinyCore)

  Pin map
  -------
  PA0 -> UPDI header (programming)
  PA1 -> Potentiometer wiper (ADC input)
  PA2 -> LM1971 DATA
  PA3 -> LM1971 CLOCK
  PA6 -> LM1971 LOAD (latch)

  Notes
  -----
  - Pot ends are expected at VCC and GND.
  - Sketch starts muted for safe startup.
  - This implementation assumes LM1971 command values:
      0x00 = 0 dB (max volume)
      0x4F = -79 dB (minimum non-mute)
      0x50 = mute
*/

// LM1971 control pins
const uint8_t PIN_LM_DATA  = PIN_PA2; // Serial data to LM1971
const uint8_t PIN_LM_CLOCK = PIN_PA3; // Shift clock to LM1971
const uint8_t PIN_LM_LOAD  = PIN_PA6; // Latch/load strobe

// Potentiometer ADC pin
const uint8_t PIN_POT = PIN_PA1;      // Pot wiper input

// LM1971 level constants
const uint8_t LM_LEVEL_MAX_VOLUME = 0x00; // 0 dB
const uint8_t LM_LEVEL_MIN_VOLUME = 0x4F; // -79 dB
const uint8_t LM_LEVEL_MUTE       = 0x50; // Mute command

// Small filter to reduce chatter when ADC moves by 1 count
const uint8_t ADC_STEP_THRESHOLD = 2;

uint16_t lastAdc = 0;
uint8_t lastLevel = LM_LEVEL_MUTE;

// Send one 8-bit command word to LM1971, MSB first.
static void writeLM1971(uint8_t value) {
  digitalWrite(PIN_LM_LOAD, LOW);

  for (int8_t bitIndex = 7; bitIndex >= 0; --bitIndex) {
    digitalWrite(PIN_LM_CLOCK, LOW);
    digitalWrite(PIN_LM_DATA, (value >> bitIndex) & 0x01);
    digitalWrite(PIN_LM_CLOCK, HIGH);
  }

  // Latch shifted word into LM1971
  digitalWrite(PIN_LM_LOAD, HIGH);
  digitalWrite(PIN_LM_LOAD, LOW);
}

void setup() {
  pinMode(PIN_LM_DATA, OUTPUT);
  pinMode(PIN_LM_CLOCK, OUTPUT);
  pinMode(PIN_LM_LOAD, OUTPUT);

  digitalWrite(PIN_LM_DATA, LOW);
  digitalWrite(PIN_LM_CLOCK, LOW);
  digitalWrite(PIN_LM_LOAD, LOW);

  // Conservative startup: force mute before reading pot.
  writeLM1971(LM_LEVEL_MUTE);

  // Initial ADC snapshot for simple change detection.
  lastAdc = analogRead(PIN_POT);
}

void loop() {
  const uint16_t adcValue = analogRead(PIN_POT); // 0..1023

  // Ignore tiny ADC movement to reduce unnecessary updates.
  if (abs((int)adcValue - (int)lastAdc) < ADC_STEP_THRESHOLD) {
    delay(10);
    return;
  }
  lastAdc = adcValue;

  // Map pot position to LM1971 attenuation range.
  // Pot at minimum -> mute, otherwise 0x4F..0x00
  uint8_t targetLevel;
  if (adcValue <= 2) {
    targetLevel = LM_LEVEL_MUTE;
  } else {
    targetLevel = map(adcValue, 3, 1023, LM_LEVEL_MIN_VOLUME, LM_LEVEL_MAX_VOLUME);
  }

  if (targetLevel != lastLevel) {
    writeLM1971(targetLevel);
    lastLevel = targetLevel;
  }

  delay(10);
}
