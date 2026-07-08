/*
  Project: CoolCube LM1971 volume and TDA7396 amplifier control
  Version: v1.0.0
  Target:  ATtiny1614 (megaTinyCore)

  Pin map from PINOUT.md
  ----------------------
  PA1 -> Volume pot wiper (ADC input)
  PA2 -> LM1971 DATA
  PA3 -> LM1971 CLOCK
  PA4 -> TDA7396 STANDBY control
  PA5 -> TDA7396 MUTE control
  PA6 -> LM1971 LOAD (active-low shift enable / latch)
  PA7 -> Battery sense divider (ADC input)

  Amplifier control logic
  -----------------------
  AMP_STBY_CTRL HIGH = MOSFET on, TDA_STBY_ALL pulled low, standby active / amps off
  AMP_STBY_CTRL LOW  = MOSFET off, TDA_STBY_ALL pulled up to AMP_VBAT, standby released / amps on
  AMP_MUTE_CTRL HIGH = mute active
  AMP_MUTE_CTRL LOW  = mute released

  Notes
  -----
  - LM1971 volume behaviour is copied from the ATtiny412 firmware.
  - Pot ends are expected at VCC and GND.
  - Battery sense uses the default analogRead reference, assumed to be VCC.
*/

// LM1971 control pins
const uint8_t PIN_LM_DATA  = PIN_PA2; // Serial data to LM1971
const uint8_t PIN_LM_CLOCK = PIN_PA3; // Shift clock to LM1971
const uint8_t PIN_LM_LOAD  = PIN_PA6; // Active-low shift enable / latch

// ADC pins
const uint8_t PIN_POT        = PIN_PA1; // Pot wiper input
const uint8_t PIN_VBAT_SENSE = PIN_PA7; // 220k/47k divider from VBAT_IN

// Amplifier control pins. HIGH is the safe/off state for both signals.
const uint8_t PIN_AMP_STBY_CTRL = PIN_PA4; // Inverted: HIGH = standby/off, LOW = released/on
const uint8_t PIN_AMP_MUTE_CTRL = PIN_PA5; // HIGH = mute active, LOW = unmuted

// LM1971 level constants
const uint8_t LM_ADDRESS_CHANNEL_1 = 0x00; // LM1971 address byte for the single audio channel
const uint8_t LM_LEVEL_MAX_VOLUME  = 0x00; // 0 dB attenuation
const uint8_t LM_LEVEL_MIN_VOLUME  = 0x3E; // -62 dB attenuation
const uint8_t LM_LEVEL_MUTE        = 0x3F; // 0x3F and above select mute

// Behaviour copied from the ATtiny412 firmware.
const uint8_t ADC_STEP_THRESHOLD = 2;
const uint8_t ADC_AVERAGE_SAMPLES = 4;          // Small startup/run-time pot smoothing
const uint16_t ADC_MUTE_ENTER_THRESHOLD = 120;  // Bottom pot travel is forced to true LM1971 mute
const uint16_t ADC_MUTE_EXIT_THRESHOLD = 150;   // Hysteresis prevents chatter near minimum
const uint8_t LM_STARTUP_WRITES = 3;            // Repeat startup frames in case the LM1971 is still settling
const uint16_t LM_REFRESH_MS = 1000;            // Periodically resend current level in case a frame was missed

// Startup sequencing
const uint16_t AMP_STARTUP_SETTLE_MS = 5000;     // Rails, DAC, VREF and analogue section settle time
const uint16_t AMP_UNMUTE_DELAY_MS = 2000;      // Delay between standby release and mute release
const uint16_t POST_TDA_UNMUTE_DELAY_MS = 1500; // Keep LM1971 muted after TDA mute release
const uint16_t LM_RAMP_STEP_MS = 20;            // LM1971 ramp step delay
const uint16_t AMP_SHUTDOWN_STANDBY_DELAY_MS = 50; // Tune 0, 20, 50, 100 ms for mute lead time

// Set to 1 to run TDA startup normally but leave the LM1971 at true mute.
#define STARTUP_KEEP_LM1971_MUTED_FOR_TEST 0

// Set to 1 only while probing TDA_STBY_ALL with a meter/scope.
// LOW should let TDA_STBY_ALL rise to AMP_VBAT; HIGH should pull it near 0 V.
#define AMP_STBY_DIAGNOSTIC_MODE 0

// Battery sense constants
const uint16_t ADC_REFERENCE_MV = 5000;      // ATtiny1614 VCC / default analogRead reference
const uint32_t VBAT_TOP_OHMS = 220000UL;     // VBAT_IN to sense node
const uint32_t VBAT_BOTTOM_OHMS = 47000UL;   // Sense node to GND
const uint16_t VBAT_POWER_FAIL_MV = 11500;   // Catch input loss early while control power is still alive
const uint16_t VBAT_RECOVER_MV = 12000;      // Hysteresis if reused for diagnostics/recovery
const uint16_t VBAT_STARTUP_BLANK_MS = 200;  // Let the VBAT sense RC/divider settle at boot
const uint8_t VBAT_FAIL_DEBOUNCE_COUNT = 1;
const uint16_t VBAT_FAST_DROP_ADC_DELTA = 24; // Fast switch-off/unplug detection before absolute threshold
const uint16_t VBAT_FAST_DROP_SAMPLE_MS = 25; // Compare raw VBAT readings over this short interval
const uint16_t VBAT_POWER_FAIL_ADC =
  (uint64_t)VBAT_POWER_FAIL_MV * VBAT_BOTTOM_OHMS * 1023UL /
  ((uint64_t)(VBAT_TOP_OHMS + VBAT_BOTTOM_OHMS) * ADC_REFERENCE_MV);
const uint16_t VBAT_RECOVER_ADC =
  (uint64_t)VBAT_RECOVER_MV * VBAT_BOTTOM_OHMS * 1023UL /
  ((uint64_t)(VBAT_TOP_OHMS + VBAT_BOTTOM_OHMS) * ADC_REFERENCE_MV);

uint16_t lastAdc = 0;
uint8_t lastLevel = LM_LEVEL_MUTE;
uint32_t lastWriteMillis = 0;
uint32_t powerFailCheckEnableMillis = 0;
bool powerFailLatched = false;
bool shutdownSequenceApplied = false;

static bool delayWithPowerFailCheck(uint16_t delayMs);

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

static bool writeLM1971Repeated(uint8_t level, uint8_t repeatCount) {
  for (uint8_t count = 0; count < repeatCount; ++count) {
    writeLM1971(level);
    if (!delayWithPowerFailCheck(2)) {
      return false;
    }
  }

  return true;
}

static uint8_t levelFromAdc(uint16_t adcValue, uint8_t currentLevel) {
  // Pot at minimum -> mute, otherwise 0x3E..0x00 attenuation.
  // A wider mute zone plus hysteresis covers pot end-stop tolerance and wiper noise.
  if (adcValue <= ADC_MUTE_ENTER_THRESHOLD) {
    return LM_LEVEL_MUTE;
  }

  if ((currentLevel == LM_LEVEL_MUTE) && (adcValue <= ADC_MUTE_EXIT_THRESHOLD)) {
    return LM_LEVEL_MUTE;
  }

  return map(adcValue, ADC_MUTE_EXIT_THRESHOLD + 1, 1023,
             LM_LEVEL_MIN_VOLUME, LM_LEVEL_MAX_VOLUME);
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

static uint16_t readBatterySenseRaw() {
  // Power-fail detection intentionally uses a single fresh reading.
  analogRead(PIN_VBAT_SENSE);
  return analogRead(PIN_VBAT_SENSE);
}

static void ampSafeOff() {
  // Assert mute first, then standby. Both HIGH states are safe/off.
  digitalWrite(PIN_AMP_MUTE_CTRL, HIGH);
  digitalWrite(PIN_AMP_STBY_CTRL, HIGH);
}

static void ampShutdownSequence() {
  if (shutdownSequenceApplied) {
    return;
  }

  shutdownSequenceApplied = true;

  // On falling power, mute both audio paths before waiting to assert standby.
  digitalWrite(PIN_AMP_MUTE_CTRL, HIGH);
  writeLM1971(LM_LEVEL_MUTE);
  delay(AMP_SHUTDOWN_STANDBY_DELAY_MS);
  digitalWrite(PIN_AMP_STBY_CTRL, HIGH);
  lastLevel = LM_LEVEL_MUTE;
}

static void releaseAmpStandby() {
  // Inverted by 2N7002 low-side pull-down:
  // LOW turns the MOSFET off, so TDA_STBY_ALL rises and the amps turn on.
  digitalWrite(PIN_AMP_STBY_CTRL, LOW);
}

static void releaseAmpMute() {
  digitalWrite(PIN_AMP_MUTE_CTRL, LOW);
}

static void ampStandbyDiagnosticLoop() {
#if AMP_STBY_DIAGNOSTIC_MODE
  digitalWrite(PIN_AMP_MUTE_CTRL, HIGH);

  while (true) {
    // Standby active/off: TDA_STBY_ALL should be near 0 V.
    digitalWrite(PIN_AMP_STBY_CTRL, HIGH);
    delay(2000);

    // Standby released/on: TDA_STBY_ALL should rise to AMP_VBAT.
    releaseAmpStandby();
    delay(2000);
  }
#endif
}

static bool latchPowerFail() {
  powerFailLatched = true;
  ampShutdownSequence();
  return true;
}

static bool isPowerFailDetected() {
  static uint8_t lowCount = 0;
  static uint16_t previousVbatRaw = 0;
  static uint32_t previousVbatSampleMillis = 0;
  static bool hasPreviousVbatRaw = false;

  if (powerFailLatched) {
    return true;
  }

  // At boot the amps are already held safe. Give VBAT_SENSE time to rise
  // before allowing a low startup reading to latch shutdown forever.
  if ((int32_t)(millis() - powerFailCheckEnableMillis) < 0) {
    hasPreviousVbatRaw = false;
    return false;
  }

  const uint32_t nowMs = millis();
  const uint16_t vbatRaw = readBatterySenseRaw();

  if (vbatRaw <= VBAT_POWER_FAIL_ADC) {
    if (lowCount < VBAT_FAIL_DEBOUNCE_COUNT) {
      ++lowCount;
    }
  } else if (vbatRaw >= VBAT_RECOVER_ADC) {
    lowCount = 0;
  }

  if (lowCount >= VBAT_FAIL_DEBOUNCE_COUNT) {
    return latchPowerFail();
  }

  if (!hasPreviousVbatRaw) {
    previousVbatRaw = vbatRaw;
    previousVbatSampleMillis = nowMs;
    hasPreviousVbatRaw = true;
    return false;
  }

  if ((nowMs - previousVbatSampleMillis) >= VBAT_FAST_DROP_SAMPLE_MS) {
    if ((previousVbatRaw > vbatRaw) &&
        ((previousVbatRaw - vbatRaw) >= VBAT_FAST_DROP_ADC_DELTA)) {
      return latchPowerFail();
    }

    previousVbatRaw = vbatRaw;
    previousVbatSampleMillis = nowMs;
  }

  return false;
}

static bool delayWithPowerFailCheck(uint16_t delayMs) {
  const uint32_t startMs = millis();

  while ((millis() - startMs) < delayMs) {
    if (isPowerFailDetected()) {
      return false;
    }
    delay(1);
  }

  return true;
}

static bool rampLM1971ToLevel(uint8_t targetLevel) {
  if (targetLevel == LM_LEVEL_MUTE) {
    lastLevel = LM_LEVEL_MUTE;
    return !isPowerFailDetected();
  }

  uint8_t rampLevel = LM_LEVEL_MUTE;
  while (rampLevel != targetLevel) {
    if (!delayWithPowerFailCheck(LM_RAMP_STEP_MS)) {
      return false;
    }

    if (rampLevel > targetLevel) {
      --rampLevel;
    } else {
      ++rampLevel;
    }

    writeLM1971(rampLevel);
    lastLevel = rampLevel;

    if (isPowerFailDetected()) {
      return false;
    }
  }

  return true;
}

static bool ampStartupSequence() {
  // Keep the LM1971 muted while the supplies and analogue stages settle.
  lastLevel = LM_LEVEL_MUTE;
  if (!writeLM1971Repeated(LM_LEVEL_MUTE, LM_STARTUP_WRITES)) {
    return false;
  }

  if (!delayWithPowerFailCheck(AMP_STARTUP_SETTLE_MS)) {
    return false;
  }

  lastAdc = readPotAverage();
  if (isPowerFailDetected()) {
    return false;
  }

  const uint8_t startupLevel = levelFromAdc(lastAdc, LM_LEVEL_MUTE);

  // TDA standby and mute are released while the LM1971 is still at true mute.
  releaseAmpStandby();
  if (!delayWithPowerFailCheck(AMP_UNMUTE_DELAY_MS)) {
    return false;
  }

  releaseAmpMute();
  if (!delayWithPowerFailCheck(POST_TDA_UNMUTE_DELAY_MS)) {
    return false;
  }

#if STARTUP_KEEP_LM1971_MUTED_FOR_TEST
  writeLM1971(LM_LEVEL_MUTE);
  lastLevel = LM_LEVEL_MUTE;
  return !isPowerFailDetected();
#else
  return rampLM1971ToLevel(startupLevel);
#endif
}

void setup() {
  // Preload safe output latches, then enable the pins as outputs.
  digitalWrite(PIN_AMP_STBY_CTRL, HIGH);
  digitalWrite(PIN_AMP_MUTE_CTRL, HIGH);
  pinMode(PIN_AMP_STBY_CTRL, OUTPUT);
  pinMode(PIN_AMP_MUTE_CTRL, OUTPUT);
  ampSafeOff();

  pinMode(PIN_LM_DATA, OUTPUT);
  pinMode(PIN_LM_CLOCK, OUTPUT);
  pinMode(PIN_LM_LOAD, OUTPUT);
  pinMode(PIN_POT, INPUT);
  pinMode(PIN_VBAT_SENSE, INPUT);

  digitalWrite(PIN_LM_DATA, LOW);
  digitalWrite(PIN_LM_CLOCK, LOW);
  digitalWrite(PIN_LM_LOAD, HIGH);

  powerFailCheckEnableMillis = millis() + VBAT_STARTUP_BLANK_MS;

  ampStandbyDiagnosticLoop();
  ampStartupSequence();
}

void loop() {
  if (isPowerFailDetected()) {
    ampShutdownSequence();
    return;
  }

#if STARTUP_KEEP_LM1971_MUTED_FOR_TEST
  if ((millis() - lastWriteMillis) >= LM_REFRESH_MS) {
    writeLM1971(LM_LEVEL_MUTE);
    lastLevel = LM_LEVEL_MUTE;
  }
  delayWithPowerFailCheck(10);
  return;
#endif

  const uint16_t adcValue = readPotAverage(); // 0..1023
  if (isPowerFailDetected()) {
    return;
  }

  const bool adcChanged = abs((int)adcValue - (int)lastAdc) >= ADC_STEP_THRESHOLD;
  const bool refreshDue = (millis() - lastWriteMillis) >= LM_REFRESH_MS;
  const uint8_t targetLevel = levelFromAdc(adcValue, lastLevel);

  // Always honor the mute zone, even if the ADC only moved by one count.
  if (!adcChanged && !refreshDue && (targetLevel == lastLevel)) {
    delayWithPowerFailCheck(10);
    return;
  }

  if (adcChanged) {
    lastAdc = adcValue;
  }

  if (targetLevel != lastLevel) {
    writeLM1971(targetLevel);
    lastLevel = targetLevel;
  } else if (refreshDue) {
    writeLM1971(lastLevel);
  }

  delayWithPowerFailCheck(10);
}
