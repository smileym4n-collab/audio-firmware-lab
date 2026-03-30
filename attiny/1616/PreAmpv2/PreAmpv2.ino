/*
  PreAmpv2.ino - ATtiny1616 preamp controller (basic firmware)
  Version: 0.1.0

  Scope in this version:
  - 4-way input relay selection from resistor-ladder ADC input
  - PGA2310 stereo volume control from analog potentiometer input
  - 16x2 I2C LCD status output (input + dB)
  - Delayed output relay enable (1 second after startup)

  Placeholders only (not implemented yet):
  - IR control on PA1
  - Motorized potentiometer drive on PB5/PB4

  Target:
  - ATtiny1616, megaTinyCore, Arduino IDE
  - Logic rail: 3.3V
*/

#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <stdio.h>
#include <string.h>

/*
  Bring-up notes:
  - LCD library required: "hd44780" by Bill Perry (uses hd44780_I2Cexp).
  - megaTinyCore board option must use Wire on PB2/PB3:
      Tools -> Wire -> PB2/PB3
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

static const uint8_t MOTOR_1_PIN      = PIN_PB5; // Motor 1 placeholder (not used yet)
static const uint8_t MOTOR_2_PIN      = PIN_PB4; // Motor 2 placeholder (not used yet)

static const uint8_t I2C_SDA_PIN      = PIN_PB2; // SDA
static const uint8_t I2C_SCL_PIN      = PIN_PB3; // SCL

static const uint8_t IR_PIN           = PIN_PA1; // IR placeholder (not used yet)

static const uint8_t PGA_MUTE_PIN     = PIN_PA4; // PGA2310 MUTE (assumed active LOW)
static const uint8_t PGA_SDI_PIN      = PIN_PA5; // PGA2310 SDI
static const uint8_t PGA_SCLK_PIN     = PIN_PA2; // PGA2310 SCLK
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

// Input ladder expected voltages at 3.3V:
// DAC   ~0.80V -> ADC ~248
// AUX1  ~1.17V -> ADC ~363
// AUX2  ~1.94V -> ADC ~601
// PHONO ~2.70V -> ADC ~837
// Midpoint boundaries:
static const uint16_t INPUT_BOUNDARY_1 = 306; // between DAC and AUX1
static const uint16_t INPUT_BOUNDARY_2 = 482; // between AUX1 and AUX2
static const uint16_t INPUT_BOUNDARY_3 = 719; // between AUX2 and PHONO

// Schmitt hysteresis around each boundary to reduce chatter.
static const uint8_t INPUT_HYST_ADC = 12;

// Debounce requirement for input selection changes.
static const uint8_t INPUT_STABLE_SAMPLES_REQUIRED = 3;

static const uint16_t OUTPUT_RELAY_DELAY_MS = 1000;
static const uint16_t INPUT_SAMPLE_PERIOD_MS = 20;
static const uint16_t VOLUME_SAMPLE_PERIOD_MS = 20;
static const uint16_t DISPLAY_PERIOD_MS = 120;
static const uint16_t DEBUG_PRINT_PERIOD_MS = 250;

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

static hd44780_I2Cexp g_lcd;
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

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
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
  // Hardware SPI is not used because assigned pins (PA5/PA2/PA3) are fixed
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

static InputSource readSelectedInput(uint16_t adcValue)
{
  // Schmitt-style decode: transition boundaries depend on current selection.
  // This avoids relay chatter near threshold voltages.
  switch (g_selectedInput) {
    case INPUT_DAC:
      if (adcValue > (INPUT_BOUNDARY_1 + INPUT_HYST_ADC)) return INPUT_AUX1;
      return INPUT_DAC;

    case INPUT_AUX1:
      if (adcValue < (INPUT_BOUNDARY_1 - INPUT_HYST_ADC)) return INPUT_DAC;
      if (adcValue > (INPUT_BOUNDARY_2 + INPUT_HYST_ADC)) return INPUT_AUX2;
      return INPUT_AUX1;

    case INPUT_AUX2:
      if (adcValue < (INPUT_BOUNDARY_2 - INPUT_HYST_ADC)) return INPUT_AUX1;
      if (adcValue > (INPUT_BOUNDARY_3 + INPUT_HYST_ADC)) return INPUT_PHONO;
      return INPUT_AUX2;

    case INPUT_PHONO:
    default:
      if (adcValue < (INPUT_BOUNDARY_3 - INPUT_HYST_ADC)) return INPUT_AUX2;
      return INPUT_PHONO;
  }
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

  static InputSource lastInputShown = INPUT_COUNT;
  static int16_t lastDbTenthsShown = 32767;

  if (g_selectedInput != lastInputShown) {
    char line0[LCD_COLS + 1];
    snprintf(line0, sizeof(line0), "Input: %-7s", INPUT_NAMES[g_selectedInput]);
    g_lcd.setCursor(0, 0);
    g_lcd.print(line0);
    for (uint8_t i = strlen(line0); i < LCD_COLS; ++i) {
      g_lcd.print(' ');
    }
    lastInputShown = g_selectedInput;
  }

  const int16_t dbTenths = static_cast<int16_t>(g_pgaDb * 10.0f);
  if (dbTenths != lastDbTenthsShown) {
    char line1[LCD_COLS + 1];
    snprintf(line1, sizeof(line1), "Vol: %6.1f dB", g_pgaDb);
    g_lcd.setCursor(0, 1);
    g_lcd.print(line1);
    for (uint8_t i = strlen(line1); i < LCD_COLS; ++i) {
      g_lcd.print(' ');
    }
    lastDbTenthsShown = dbTenths;
  }
}

static void initializeLcd()
{
  // Uses Bill Perry's hd44780 library auto-probing I2Cexp driver.
  // This is generally more reliable on megaavr than many LiquidCrystal_I2C forks.
  const int status = g_lcd.begin(LCD_COLS, LCD_ROWS);
  g_lcdReady = (status == 0);

  if (!g_lcdReady) {
    return;
  }

  g_lcd.clear();
  g_lcd.setCursor(0, 0);
  g_lcd.print("PreAmpv2 init...");
  g_lcd.setCursor(0, 1);
  g_lcd.print("Output delay 1s");
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

  pinMode(VOL_ADC_PIN, INPUT);
  pinMode(INPUT_ADC_PIN, INPUT);

  // Safe startup states.
  digitalWrite(RELAY_DAC_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_AUX1_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_AUX2_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_PHONO_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(RELAY_OUTPUT_PIN, RELAY_INACTIVE_STATE); // output relay OFF during delay

  digitalWrite(MOTOR_1_PIN, LOW); // placeholder-safe
  digitalWrite(MOTOR_2_PIN, LOW); // placeholder-safe

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

  // PB2/PB3 I2C mapping is selected in megaTinyCore board options
  // (Tools -> Wire -> PB2/PB3) for this hardware.
  (void)I2C_SDA_PIN;
  (void)I2C_SCL_PIN;
  Wire.begin();
  initializeLcd();

  // Safe initial input and low volume before unmuting/output enable.
  g_selectedInput = INPUT_DAC;
  updateInputRelays(g_selectedInput);

  g_pgaCode = 255; // force first write to happen
  setPgaVolumeDb(-80.0f);

  g_bootMs = millis();
}

void loop()
{
  const uint32_t nowMs = millis();

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

  // Placeholder for future IR task (PA1).
  // Placeholder for future motorized potentiometer task (PB5/PB4).
}
