/*
  PreAmpv2.ino - ATtiny1616 preamp controller (basic firmware)
  Version: 0.3.9

  Scope in this version:
  - 4-way input relay selection from resistor-ladder ADC input
  - PGA2310 stereo volume control from analog potentiometer input
  - 16x2 I2C LCD status output (input + dB)
  - Delayed output relay enable (1 second after startup)

  Added in this version:
  - Apple-protocol IR volume control on PA6 (address 0xAA, commands 0x0B/0x0D)
  - Motorized potentiometer drive on PB5/PB4 for IR volume up/down

  Target:
  - ATtiny1616, megaTinyCore, Arduino IDE
  - Logic rail: 3.3V
*/

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdio.h>
#include <string.h>

/*
  Bring-up notes:
  - LCD library required: "LiquidCrystal_I2C".
  - megaTinyCore board option must use Wire on PA1/PA2:
      Tools -> Wire -> PA1/PA2
  - Confirmed hardware detail: PGA2310 MUTE is active LOW.
  - First hardware checks:
      1) relay polarity (active/inactive states),
      2) LCD operation/addressing,
      3) raw ADC readings (volume pot + input ladder),
      4) input-ladder threshold tuning on real hardware,
      5) subjective volume taper feel.
*/

// Compile-time debug flag for serial bring-up logs.
// 0 = disabled (default), 1 = enabled.
#ifndef PREAMPV2_DEBUG
#define PREAMPV2_DEBUG 0
#endif

#ifndef PREAMPV2_LCD_DEBUG
#define PREAMPV2_LCD_DEBUG 0
#endif

#if PREAMPV2_DEBUG
#define DBG_BEGIN(baud) Serial.begin(baud)
#define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
#define DBG_PRINT(...)    Serial.print(__VA_ARGS__)
#else
#define DBG_BEGIN(baud) do {} while (0)
#define DBG_PRINTLN(...)  do {} while (0)
#define DBG_PRINT(...)    do {} while (0)
#endif

// -----------------------------------------------------------------------------
// Pin map (fixed by hardware)
// -----------------------------------------------------------------------------
static const uint8_t RELAY_DAC_PIN    = PIN_PB0; // Relay 1 (DAC)
static const uint8_t RELAY_AUX1_PIN   = PIN_PC0; // Relay 2 (AUX 1)
static const uint8_t RELAY_AUX2_PIN   = PIN_PC1; // Relay 3 (AUX 2)
static const uint8_t RELAY_PHONO_PIN  = PIN_PC2; // Relay 4 (PHONO)
static const uint8_t RELAY_OUTPUT_PIN = PIN_PC3; // Relay 5 (OUTPUT)

static const uint8_t MOTOR_1_PIN      = PIN_PB5; // Motor drive IN1 (clockwise for volume up)
static const uint8_t MOTOR_2_PIN      = PIN_PB4; // Motor drive IN2 (anti-clockwise for volume down)

static const uint8_t I2C_SDA_PIN      = PIN_PA1; // SDA
static const uint8_t I2C_SCL_PIN      = PIN_PA2; // SCL

static const uint8_t IR_PIN           = PIN_PA6; // TSOP2438 IR demodulated input

static const uint8_t PGA_MUTE_PIN     = PIN_PA4; // PGA2310 MUTE (assumed active LOW)
static const uint8_t PGA_SDI_PIN      = PIN_PA5; // PGA2310 SDI
static const uint8_t PGA_SCLK_PIN     = PIN_PB3; // PGA2310 SCLK
static const uint8_t PGA_CS_PIN       = PIN_PA3; // PGA2310 CS (active LOW)

static const uint8_t VOL_ADC_PIN      = PIN_PB1; // Volume potentiometer wiper (0V..3.3V)
static const uint8_t INPUT_ADC_PIN    = PIN_PA7; // Input selector ladder ADC

// -----------------------------------------------------------------------------
// Configuration constants
// -----------------------------------------------------------------------------
static const uint8_t LCD_COLS = 16;
static const uint8_t LCD_ROWS = 2;

// Output polarity controls (adjust only if hardware driver polarity differs).
static const uint8_t RELAY_ACTIVE_STATE      = HIGH;
static const uint8_t RELAY_INACTIVE_STATE    = LOW;
static const uint8_t PGA_MUTE_ACTIVE_STATE   = LOW;  // Confirmed active LOW.
static const uint8_t PGA_MUTE_INACTIVE_STATE = HIGH; // Confirmed inactive HIGH.

// PGA2310 gain range and project cap.
// PGA2310 uses 0.5 dB steps, code 0 = -95.5 dB, code 255 = +31.5 dB.
// This project limits control to +10.0 dB max.
static const float PGA_MIN_DB = -95.5f;
static const float PGA_MAX_DB = +10.0f;

// Input ladder expected voltages from measured hardware at 3.3V:
// DAC   ~0.58V -> ADC ~180
// AUX1  ~1.21V -> ADC ~375
// AUX2  ~1.98V -> ADC ~614
// PHONO ~2.75V -> ADC ~852
// Midpoint boundaries:
static const uint16_t INPUT_BOUNDARY_1 = 266; // between DAC and AUX1 (based on measured 167/364)
static const uint16_t INPUT_BOUNDARY_2 = 486; // between AUX1 and AUX2 (based on measured 364/608)
static const uint16_t INPUT_BOUNDARY_3 = 724; // between AUX2 and PHONO (based on measured 608/839)

// Debounce requirement for input selection changes.
static const uint8_t INPUT_STABLE_SAMPLES_REQUIRED = 3;

static const uint16_t OUTPUT_RELAY_DELAY_MS = 1000;
static const uint16_t INPUT_SAMPLE_PERIOD_MS = 20;
static const uint16_t VOLUME_SAMPLE_PERIOD_MS = 20;
static const uint16_t DISPLAY_PERIOD_MS = 120;
static const uint16_t DEBUG_PRINT_PERIOD_MS = 250;

// Motorized potentiometer control limits (PB5/PB4 through motor driver).
static const uint16_t MOTOR_STEP_ADC = 8;              // One short IR press target increment.
static const uint16_t MOTOR_DEADBAND_ADC = 3;          // Stop motor when inside this error band.
static const uint16_t MOTOR_MAX_RUN_MS = 2200;         // Safety timeout per continuous movement.
static const uint16_t MOTOR_COMMAND_PULSE_MS = 85;     // Motor run pulse triggered by each IR command.
static const uint8_t ADC_AT_MIN_THRESHOLD = 1;         // Treat as bottom mechanical travel.
static const uint16_t ADC_AT_MAX_THRESHOLD = 1022;     // Treat as top mechanical travel.

// Apple IR command map (confirmed codes).
static const uint8_t IR_APPLE_ADDRESS = 0xAA;
static const uint8_t IR_APPLE_CMD_VOL_UP = 0x0B;
static const uint8_t IR_APPLE_CMD_VOL_DOWN = 0x0D;
static const uint16_t IR_REPEAT_MIN_INTERVAL_MS = 90;  // Controlled repeat speed while holding.

// ADC assumptions (megaTinyCore default analogRead behavior):
// - expected resolution is 10-bit (0..1023)
// - default reference is VDD unless explicitly changed by firmware/board config
// - input-ladder thresholds below are expected to need real-hardware tuning
//   due to resistor tolerance, source impedance, and VDD variation

// -----------------------------------------------------------------------------
// Types/state
// -----------------------------------------------------------------------------
enum InputSource : uint8_t {
  INPUT_DAC = 0,
  INPUT_AUX1,
  INPUT_AUX2,
  INPUT_PHONO,
  INPUT_COUNT
};

static const char* const INPUT_NAMES[INPUT_COUNT] = {
  "DAC", "AUX 1", "AUX 2", "PHONO"
};

static LiquidCrystal_I2C g_lcd(0x27, LCD_COLS, LCD_ROWS);
static bool g_lcdReady = false;

static InputSource g_selectedInput = INPUT_DAC;
static InputSource g_pendingInput = INPUT_DAC;
static uint8_t g_pendingInputStableCount = 0;

static uint8_t g_pgaCode = 0;      // actual code sent to PGA2310
static float g_pgaDb = PGA_MIN_DB; // actual dB represented by code

static bool g_outputRelayEnabled = false;

static uint32_t g_bootMs = 0;
static uint32_t g_lastInputMs = 0;
static uint32_t g_lastVolumeMs = 0;
static uint32_t g_lastDisplayMs = 0;
static uint32_t g_lastDebugMs = 0;

static uint16_t g_lastVolAdc = 0;
static uint16_t g_lastInputAdc = 0;
static InputSource g_lastInputCandidate = INPUT_DAC;

// Motor target state derived from IR commands.
static uint16_t g_motorTargetAdc = 0;
static bool g_motorRunning = false;
static uint32_t g_motorRunStartMs = 0;
static uint32_t g_motorRunUntilMs = 0;

// IR decode state (Apple protocol uses NEC-like framing).
static bool g_irPrevLevelHigh = true;
static uint32_t g_irLastEdgeUs = 0;
static uint16_t g_irLastMarkUs = 0;
static bool g_irFrameActive = false;
static uint8_t g_irBitIndex = 0;
static uint32_t g_irRawData = 0;
static uint8_t g_irLastAppleCommand = 0;
static bool g_irHasLastAppleCommand = false;
static uint32_t g_lastIrApplyMs = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

#if PREAMPV2_LCD_DEBUG
static const char* inputShortName(InputSource input)
{
  switch (input) {
    case INPUT_DAC: return "DAC";
    case INPUT_AUX1: return "A1";
    case INPUT_AUX2: return "A2";
    case INPUT_PHONO: return "PH";
    default: return "?";
  }
}

#endif

static void formatTenthsDb(int16_t dbTenths, char* out, size_t outLen)
{
  if (outLen < 2) {
    return;
  }

  const char sign = (dbTenths < 0) ? '-' : '+';
  int16_t absTenths = dbTenths < 0 ? static_cast<int16_t>(-dbTenths) : dbTenths;
  const uint16_t whole = static_cast<uint16_t>(absTenths / 10);
  const uint8_t frac = static_cast<uint8_t>(absTenths % 10);
  snprintf(out, outLen, "%c%u.%u", sign, whole, frac);
}

#if PREAMPV2_LCD_DEBUG
static void formatAdcVoltage(uint16_t adcValue, char* out, size_t outLen)
{
  if (outLen < 2) {
    return;
  }

  const uint32_t mv = (static_cast<uint32_t>(adcValue) * 3300UL + 511UL) / 1023UL;
  const uint16_t whole = static_cast<uint16_t>(mv / 1000UL);
  const uint16_t frac = static_cast<uint16_t>((mv % 1000UL) / 10UL);
  snprintf(out, outLen, "%u.%02uV", whole, frac);
}

#endif

static uint16_t readAdcAveraged(uint8_t pin, uint8_t samples)
{
  analogRead(pin); // throwaway sample after mux selection
  delayMicroseconds(40);

  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    sum += analogRead(pin);
  }

  return static_cast<uint16_t>(sum / samples);
}

static void motorStop()
{
  digitalWrite(MOTOR_1_PIN, LOW);
  digitalWrite(MOTOR_2_PIN, LOW);
  g_motorRunning = false;
}

static void motorRotateClockwise()
{
  digitalWrite(MOTOR_1_PIN, HIGH);
  digitalWrite(MOTOR_2_PIN, LOW);
  g_motorRunning = true;
}

static void motorRotateAntiClockwise()
{
  digitalWrite(MOTOR_1_PIN, LOW);
  digitalWrite(MOTOR_2_PIN, HIGH);
  g_motorRunning = true;
}

static void configureAnalogInputs()
{
  // Keep ADC pins strictly high-impedance to avoid biasing/pulling analog nodes.
  // PB1 = volume pot input, PA7 = input ladder.
  pinMode(VOL_ADC_PIN, INPUT);
  pinMode(INPUT_ADC_PIN, INPUT);

  PORTB.PIN1CTRL &= ~PORT_PULLUPEN_bm;
  PORTB.PIN1CTRL = (PORTB.PIN1CTRL & ~PORT_ISC_gm) | PORT_ISC_INPUT_DISABLE_gc;

  PORTA.PIN7CTRL &= ~PORT_PULLUPEN_bm;
  PORTA.PIN7CTRL = (PORTA.PIN7CTRL & ~PORT_ISC_gm) | PORT_ISC_INPUT_DISABLE_gc;
}

static void configureI2cForDisplay()
{
  Wire.pins(PIN_PA1, PIN_PA2);
  Wire.begin();
  Wire.setClock(100000);
  delay(50);
}

static uint8_t dbToPgaCode(float db)
{
  if (db < PGA_MIN_DB) {
    db = PGA_MIN_DB;
  }
  if (db > PGA_MAX_DB) {
    db = PGA_MAX_DB;
  }

  const float codeFloat = (db - PGA_MIN_DB) * 2.0f; // 0.5 dB steps
  int16_t codeInt = static_cast<int16_t>(codeFloat + 0.5f);
  if (codeInt < 0) {
    codeInt = 0;
  }
  if (codeInt > 255) {
    codeInt = 255;
  }

  return static_cast<uint8_t>(codeInt);
}

static float pgaCodeToDb(uint8_t code)
{
  return PGA_MIN_DB + (0.5f * static_cast<float>(code));
}

static float volumeAdcToRequestedDb(uint16_t adcValue)
{
  // Pot taper strategy for better listening feel with linear 10k pot:
  // - Segment A (0..70%): expands low-to-mid listening range (-95.5..-20 dB)
  //   using a concave curve to avoid bunching useful control near the top.
  // - Segment B (70..95%): smoother progression through common listening
  //   levels (-20..0 dB).
  // - Segment C (95..100%): reserves top travel for 0..+10 dB headroom.
  // Keep these constants readable/tunable for hardware listening tests.
  const float normalized = static_cast<float>(adcValue) / 1023.0f;

  const float splitA = 0.70f;
  const float splitB = 0.95f;

  if (normalized <= splitA) {
    const float x = normalized / splitA;
    const float shaped = x * x; // expanded low-level control
    return PGA_MIN_DB + shaped * (-20.0f - PGA_MIN_DB);
  }

  if (normalized <= splitB) {
    const float x = (normalized - splitA) / (splitB - splitA);
    return -20.0f + x * (0.0f - (-20.0f));
  }

  const float x = (normalized - splitB) / (1.0f - splitB);
  return 0.0f + x * (PGA_MAX_DB - 0.0f);
}

static void pgaWriteStereo(uint8_t code)
{
  // PGA2310 serial write (datasheet-compatible):
  // - CS must be LOW during write
  // - SDI is MSB-first, latched on SCLK rising edge
  // - 16 clocks total: Right byte then Left byte
  // Hardware SPI is not used because assigned pins (PA5/PB3/PA3) are fixed
  // by this project pin map and may not match the ATtiny1616 SPI hardware pins.
  digitalWrite(PGA_CS_PIN, LOW);
  delayMicroseconds(1);

  // Right channel byte.
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_SCLK_PIN, LOW);
    digitalWrite(PGA_SDI_PIN, (code & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PGA_SCLK_PIN, HIGH);
    delayMicroseconds(1);
  }

  // Left channel byte.
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_SCLK_PIN, LOW);
    digitalWrite(PGA_SDI_PIN, (code & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PGA_SCLK_PIN, HIGH);
    delayMicroseconds(1);
  }

  digitalWrite(PGA_SCLK_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(PGA_CS_PIN, HIGH);
  delayMicroseconds(1);
}

static void setPgaVolumeDb(float requestedDb)
{
  const uint8_t newCode = dbToPgaCode(requestedDb);
  if (newCode == g_pgaCode) {
    return; // avoid unnecessary rewrite on small ADC noise
  }

  pgaWriteStereo(newCode);
  g_pgaCode = newCode;
  g_pgaDb = pgaCodeToDb(newCode); // show exact value sent
}

static bool isWithin(uint16_t value, uint16_t target, uint16_t tolerance)
{
  return (value >= (target > tolerance ? (target - tolerance) : 0)) &&
         (value <= (target + tolerance));
}

static bool decodeAppleFrame(uint32_t rawData, uint8_t* outAddress, uint8_t* outCommand)
{
  // Observed Apple frame layout for this remote:
  // [31:24] address, [23:16] command, [15:0] fixed trailer 0x87EE.
  const uint8_t address = static_cast<uint8_t>((rawData >> 24) & 0xFFu);
  const uint8_t command = static_cast<uint8_t>((rawData >> 16) & 0xFFu);
  const uint16_t trailer = static_cast<uint16_t>(rawData & 0xFFFFu);

  if (trailer != 0x87EEu) {
    return false; // Not an Apple frame for this remote format.
  }

  *outAddress = address;
  *outCommand = command;
  return true;
}

static void applyIrVolumeCommand(bool isVolUp)
{
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastIrApplyMs) < IR_REPEAT_MIN_INTERVAL_MS) {
    return; // Controlled repeat speed while button is held.
  }
  g_lastIrApplyMs = nowMs;

  const uint16_t currentAdc = g_lastVolAdc;

  if (isVolUp) {
    if (currentAdc >= ADC_AT_MAX_THRESHOLD) {
      g_motorTargetAdc = 1023;
      motorStop();
      return;
    }
    g_motorTargetAdc = (g_motorTargetAdc > (1023 - MOTOR_STEP_ADC)) ? 1023 : (g_motorTargetAdc + MOTOR_STEP_ADC);
    motorRotateClockwise();
  } else {
    if (currentAdc <= ADC_AT_MIN_THRESHOLD) {
      g_motorTargetAdc = 0;
      motorStop();
      return;
    }
    g_motorTargetAdc = (g_motorTargetAdc > MOTOR_STEP_ADC) ? (g_motorTargetAdc - MOTOR_STEP_ADC) : 0;
    motorRotateAntiClockwise();
  }

  g_motorRunStartMs = nowMs;
  g_motorRunUntilMs = nowMs + MOTOR_COMMAND_PULSE_MS;
}

static void handleAppleIrFrame(uint32_t rawData, bool isRepeat)
{
  uint8_t address = 0;
  uint8_t command = 0;

  if (isRepeat) {
    if (!g_irHasLastAppleCommand) {
      return;
    }
    command = g_irLastAppleCommand;
    address = IR_APPLE_ADDRESS;
  } else {
    if (!decodeAppleFrame(rawData, &address, &command)) {
      return;
    }
    if (address != IR_APPLE_ADDRESS) {
      return;
    }
    g_irLastAppleCommand = command;
    g_irHasLastAppleCommand = true;
  }

  if (command == IR_APPLE_CMD_VOL_UP) {
    applyIrVolumeCommand(true);
  } else if (command == IR_APPLE_CMD_VOL_DOWN) {
    applyIrVolumeCommand(false);
  }
}

static void processIrEdge(uint32_t nowUs, bool levelHigh)
{
  const uint32_t pulseWidth = nowUs - g_irLastEdgeUs;
  g_irLastEdgeUs = nowUs;

  if (!levelHigh) {
    const uint16_t spaceUs = static_cast<uint16_t>(pulseWidth);

    if (isWithin(g_irLastMarkUs, 9000, 1800) && isWithin(spaceUs, 4500, 900)) {
      g_irFrameActive = true;
      g_irBitIndex = 0;
      g_irRawData = 0;
      return;
    }

    if (isWithin(g_irLastMarkUs, 9000, 1800) && isWithin(spaceUs, 2250, 500)) {
      handleAppleIrFrame(g_irRawData, true);
      g_irFrameActive = false;
      return;
    }

    if (!g_irFrameActive) {
      return;
    }

    if (!isWithin(g_irLastMarkUs, 560, 250)) {
      g_irFrameActive = false;
      return;
    }

    const uint8_t bitValue = (spaceUs > 1000) ? 1u : 0u;
    g_irRawData |= (static_cast<uint32_t>(bitValue) << g_irBitIndex);
    ++g_irBitIndex;

    if (g_irBitIndex >= 32u) {
      handleAppleIrFrame(g_irRawData, false);
      g_irFrameActive = false;
    }
    return;
  }

  g_irLastMarkUs = static_cast<uint16_t>(pulseWidth);
}

static void serviceIrReceiver()
{
  const uint32_t nowUs = micros();
  const bool levelHigh = (digitalRead(IR_PIN) == HIGH);

  if (levelHigh != g_irPrevLevelHigh) {
    processIrEdge(nowUs, levelHigh);
    g_irPrevLevelHigh = levelHigh;
  }

  if ((nowUs - g_irLastEdgeUs) > 20000u) {
    g_irFrameActive = false;
  }
}

static void serviceMotorControl(uint32_t nowMs)
{
  const uint16_t currentAdc = g_lastVolAdc;

  if (currentAdc <= ADC_AT_MIN_THRESHOLD) {
    g_motorTargetAdc = 0;
    motorStop();
    return;
  }

  if (currentAdc >= ADC_AT_MAX_THRESHOLD) {
    g_motorTargetAdc = 1023;
    motorStop();
    return;
  }

  if (!g_motorRunning) {
    return;
  }

  if ((nowMs - g_motorRunStartMs) >= MOTOR_MAX_RUN_MS) {
    motorStop();
    return;
  }

  if (nowMs >= g_motorRunUntilMs) {
    motorStop();
    return;
  }

  if (currentAdc + MOTOR_DEADBAND_ADC >= g_motorTargetAdc &&
      currentAdc <= g_motorTargetAdc + MOTOR_DEADBAND_ADC) {
    motorStop();
  }
}

static InputSource readSelectedInput(uint16_t adcValue)
{
  // Absolute decode from ladder ADC value. This allows direct jumps
  // (for example PHONO -> DAC) in a single decode decision.
  if (adcValue < INPUT_BOUNDARY_1) {
    return INPUT_DAC;
  }
  if (adcValue < INPUT_BOUNDARY_2) {
    return INPUT_AUX1;
  }
  if (adcValue < INPUT_BOUNDARY_3) {
    return INPUT_AUX2;
  }
  return INPUT_PHONO;
}

static void updateInputRelays(InputSource input)
{
  // One-hot relay control: only one input relay active at any time.
  digitalWrite(RELAY_DAC_PIN,   input == INPUT_DAC   ? RELAY_ACTIVE_STATE : RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_AUX1_PIN,  input == INPUT_AUX1  ? RELAY_ACTIVE_STATE : RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_AUX2_PIN,  input == INPUT_AUX2  ? RELAY_ACTIVE_STATE : RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_PHONO_PIN, input == INPUT_PHONO ? RELAY_ACTIVE_STATE : RELAY_INACTIVE_STATE);
}

static void updateDisplay()
{
  if (!g_lcdReady) {
    return;
  }

#if PREAMPV2_LCD_DEBUG
  // LCD diagnostics mode for relay + ADC validation on hardware.
  // Alternates pages every ~1 second:
  //   Page A: input decode state + relay output states
  //   Page B: volume ADC count + estimated voltage + PGA target
  static uint16_t lastInputAdcShown = 65535;
  static uint16_t lastVolAdcShown = 65535;
  static InputSource lastInputShown = INPUT_COUNT;
  static InputSource lastCandidateShown = INPUT_COUNT;
  static uint8_t lastCodeShown = 255;
  static bool lastPageA = false;

  const bool showPageA = ((millis() / 1000UL) % 2UL) == 0UL;

  if ((g_selectedInput == lastInputShown) &&
      (g_lastInputCandidate == lastCandidateShown) &&
      (g_lastInputAdc == lastInputAdcShown) &&
      (g_lastVolAdc == lastVolAdcShown) &&
      (g_pgaCode == lastCodeShown) &&
      (showPageA == lastPageA)) {
    return;
  }

  char line0[LCD_COLS + 1];
  char line1[LCD_COLS + 1];

  if (showPageA) {
    snprintf(line0, sizeof(line0), "S:%-3s C:%-2s A%3u",
             inputShortName(g_selectedInput),
             inputShortName(g_lastInputCandidate),
             g_lastInputAdc);

    const uint8_t relayDac = digitalRead(RELAY_DAC_PIN) == RELAY_ACTIVE_STATE ? 1 : 0;
    const uint8_t relayAux1 = digitalRead(RELAY_AUX1_PIN) == RELAY_ACTIVE_STATE ? 1 : 0;
    const uint8_t relayAux2 = digitalRead(RELAY_AUX2_PIN) == RELAY_ACTIVE_STATE ? 1 : 0;
    const uint8_t relayPhono = digitalRead(RELAY_PHONO_PIN) == RELAY_ACTIVE_STATE ? 1 : 0;

    snprintf(line1, sizeof(line1), "R:%u%u%u%u OUT:%u",
             relayDac, relayAux1, relayAux2, relayPhono,
             digitalRead(RELAY_OUTPUT_PIN) == RELAY_ACTIVE_STATE ? 1 : 0);
  } else {
    char volText[8];
    char dbText[8];
    formatAdcVoltage(g_lastVolAdc, volText, sizeof(volText));
    formatTenthsDb(static_cast<int16_t>(-955 + (static_cast<int16_t>(g_pgaCode) * 5)), dbText, sizeof(dbText));

    snprintf(line0, sizeof(line0), "VOL:%4u %s",
             g_lastVolAdc,
             volText);

    snprintf(line1, sizeof(line1), "DB:%6s C:%3u",
             dbText,
             g_pgaCode);
  }

  g_lcd.setCursor(0, 0);
  g_lcd.print(line0);
  for (uint8_t i = strlen(line0); i < LCD_COLS; ++i) {
    g_lcd.print(' ');
  }

  g_lcd.setCursor(0, 1);
  g_lcd.print(line1);
  for (uint8_t i = strlen(line1); i < LCD_COLS; ++i) {
    g_lcd.print(' ');
  }

  lastInputShown = g_selectedInput;
  lastCandidateShown = g_lastInputCandidate;
  lastInputAdcShown = g_lastInputAdc;
  lastVolAdcShown = g_lastVolAdc;
  lastCodeShown = g_pgaCode;
  lastPageA = showPageA;
#else
  static InputSource lastInputShown = INPUT_COUNT;
  static int16_t lastDbTenthsShown = 32767;

  if (g_selectedInput != lastInputShown) {
    const char* inputText = INPUT_NAMES[g_selectedInput];
    const size_t inputLen = strlen(inputText);
    const uint8_t inputPad = (LCD_COLS > inputLen) ? static_cast<uint8_t>((LCD_COLS - inputLen) / 2) : 0;

    g_lcd.setCursor(0, 0);
    for (uint8_t i = 0; i < LCD_COLS; ++i) {
      g_lcd.print(' ');
    }
    g_lcd.setCursor(inputPad, 0);
    g_lcd.print(inputText);
    lastInputShown = g_selectedInput;
  }

  const int16_t dbTenths = static_cast<int16_t>(-955 + (static_cast<int16_t>(g_pgaCode) * 5));
  if (dbTenths != lastDbTenthsShown) {
    char dbText[8];
    formatTenthsDb(dbTenths, dbText, sizeof(dbText));

    char volumeText[12];
    snprintf(volumeText, sizeof(volumeText), "%s dB", dbText);
    const size_t volumeLen = strlen(volumeText);
    const uint8_t volumePad = (LCD_COLS > volumeLen) ? static_cast<uint8_t>((LCD_COLS - volumeLen) / 2) : 0;

    g_lcd.setCursor(0, 1);
    for (uint8_t i = 0; i < LCD_COLS; ++i) {
      g_lcd.print(' ');
    }
    g_lcd.setCursor(volumePad, 1);
    g_lcd.print(volumeText);
    lastDbTenthsShown = dbTenths;
  }
#endif
}

static void initializeLcd()
{
  g_lcd.init();
  g_lcd.backlight();
  g_lcd.clear();
  g_lcd.home();
  g_lcd.print("PreAmpv2 init...");
  g_lcd.setCursor(0, 1);
  g_lcd.print("Output delay 1s");
  g_lcdReady = true;
}

static void initializeGpioSafe()
{
  pinMode(RELAY_DAC_PIN, OUTPUT);
  pinMode(RELAY_AUX1_PIN, OUTPUT);
  pinMode(RELAY_AUX2_PIN, OUTPUT);
  pinMode(RELAY_PHONO_PIN, OUTPUT);
  pinMode(RELAY_OUTPUT_PIN, OUTPUT);

  pinMode(MOTOR_1_PIN, OUTPUT);
  pinMode(MOTOR_2_PIN, OUTPUT);
  pinMode(IR_PIN, INPUT);

  pinMode(PGA_MUTE_PIN, OUTPUT);
  pinMode(PGA_SDI_PIN, OUTPUT);
  pinMode(PGA_SCLK_PIN, OUTPUT);
  pinMode(PGA_CS_PIN, OUTPUT);

  configureAnalogInputs();

  // Safe startup states.
  digitalWrite(RELAY_DAC_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_AUX1_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_AUX2_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_PHONO_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_OUTPUT_PIN, RELAY_INACTIVE_STATE); // output relay OFF during delay

  digitalWrite(MOTOR_1_PIN, LOW); // motor off at startup
  digitalWrite(MOTOR_2_PIN, LOW); // motor off at startup

  digitalWrite(PGA_CS_PIN, HIGH);
  digitalWrite(PGA_SCLK_PIN, LOW);
  digitalWrite(PGA_SDI_PIN, LOW);
  digitalWrite(PGA_MUTE_PIN, PGA_MUTE_ACTIVE_STATE); // mute asserted during startup (active LOW)
}

void setup()
{
  initializeGpioSafe();
  DBG_BEGIN(115200);
  DBG_PRINTLN(F("PreAmpv2 debug enabled"));

  // PA1/PA2 I2C mapping is required for this hardware.
  (void)I2C_SDA_PIN;
  (void)I2C_SCL_PIN;
  configureI2cForDisplay();
  delay(20);
  initializeLcd();

  // Safe initial input and low volume before unmuting/output enable.
  g_selectedInput = INPUT_DAC;
  updateInputRelays(g_selectedInput);

  g_pgaCode = 255; // force first write to happen
  setPgaVolumeDb(-80.0f);

  g_bootMs = millis();
  g_lastVolAdc = readAdcAveraged(VOL_ADC_PIN, 8);
  g_motorTargetAdc = g_lastVolAdc;
  g_irPrevLevelHigh = (digitalRead(IR_PIN) == HIGH);
  g_irLastEdgeUs = micros();
}

void loop()
{
  const uint32_t nowMs = millis();
  serviceIrReceiver();

  // Delayed output relay turn-on.
  if (!g_outputRelayEnabled && (nowMs - g_bootMs >= OUTPUT_RELAY_DELAY_MS)) {
    digitalWrite(RELAY_OUTPUT_PIN, RELAY_ACTIVE_STATE);
    digitalWrite(PGA_MUTE_PIN, PGA_MUTE_INACTIVE_STATE); // unmute after relay engages
    g_outputRelayEnabled = true;
  }

  // Input ladder sampling + debounce.
  if (nowMs - g_lastInputMs >= INPUT_SAMPLE_PERIOD_MS) {
    g_lastInputMs = nowMs;

    const uint16_t inputAdc = readAdcAveraged(INPUT_ADC_PIN, 8);
    g_lastInputAdc = inputAdc;
    const InputSource candidate = readSelectedInput(inputAdc);
    g_lastInputCandidate = candidate;

    if (candidate == g_pendingInput) {
      if (g_pendingInputStableCount < 255) {
        ++g_pendingInputStableCount;
      }
    } else {
      g_pendingInput = candidate;
      g_pendingInputStableCount = 1;
    }

    if ((g_pendingInput != g_selectedInput) &&
        (g_pendingInputStableCount >= INPUT_STABLE_SAMPLES_REQUIRED)) {
      g_selectedInput = g_pendingInput;
      updateInputRelays(g_selectedInput);
    }
  }

  // Potentiometer -> PGA volume update.
  if (nowMs - g_lastVolumeMs >= VOLUME_SAMPLE_PERIOD_MS) {
    g_lastVolumeMs = nowMs;

    const uint16_t volAdc = readAdcAveraged(VOL_ADC_PIN, 8);
    g_lastVolAdc = volAdc;
    const float requestedDb = volumeAdcToRequestedDb(volAdc);
    setPgaVolumeDb(requestedDb);
  }

  serviceMotorControl(nowMs);

  // LCD update task.
  if (nowMs - g_lastDisplayMs >= DISPLAY_PERIOD_MS) {
    g_lastDisplayMs = nowMs;
    updateDisplay();
  }

  if (PREAMPV2_DEBUG && (nowMs - g_lastDebugMs >= DEBUG_PRINT_PERIOD_MS)) {
    g_lastDebugMs = nowMs;
    DBG_PRINT(F("volAdc="));
    DBG_PRINT(g_lastVolAdc);
    DBG_PRINT(F(" inputAdc="));
    DBG_PRINT(g_lastInputAdc);
    DBG_PRINT(F(" input="));
    DBG_PRINT(INPUT_NAMES[g_selectedInput]);
    DBG_PRINT(F(" pgaDb="));
    DBG_PRINT(g_pgaDb, 1);
    DBG_PRINT(F(" code="));
    DBG_PRINTLN(g_pgaCode);
  }

}
