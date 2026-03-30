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
  // Linear mapping from pot ADC (0..1023) into project range (-95.5..+10.0 dB).
  const float normalized = static_cast<float>(adcValue) / 1023.0f;
  return PGA_MIN_DB + normalized * (PGA_MAX_DB - PGA_MIN_DB);
}

static void pgaWriteStereo(uint8_t code)
{
  digitalWrite(PGA_CS_PIN, LOW);

  // Left channel byte.
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_SCLK_PIN, LOW);
    digitalWrite(PGA_SDI_PIN, (code & (1u << bit)) ? HIGH : LOW);
    digitalWrite(PGA_SCLK_PIN, HIGH);
  }

  // Right channel byte.
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_SCLK_PIN, LOW);
    digitalWrite(PGA_SDI_PIN, (code & (1u << bit)) ? HIGH : LOW);
    digitalWrite(PGA_SCLK_PIN, HIGH);
  }

  digitalWrite(PGA_SCLK_PIN, LOW);
  digitalWrite(PGA_CS_PIN, HIGH);
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
  digitalWrite(RELAY_DAC_PIN,   input == INPUT_DAC   ? HIGH : LOW);
  digitalWrite(RELAY_AUX1_PIN,  input == INPUT_AUX1  ? HIGH : LOW);
  digitalWrite(RELAY_AUX2_PIN,  input == INPUT_AUX2  ? HIGH : LOW);
  digitalWrite(RELAY_PHONO_PIN, input == INPUT_PHONO ? HIGH : LOW);
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
  digitalWrite(RELAY_DAC_PIN, LOW);
  digitalWrite(RELAY_AUX1_PIN, LOW);
  digitalWrite(RELAY_AUX2_PIN, LOW);
  digitalWrite(RELAY_PHONO_PIN, LOW);
  digitalWrite(RELAY_OUTPUT_PIN, LOW); // output relay OFF during delay

  digitalWrite(MOTOR_1_PIN, LOW); // placeholder-safe
  digitalWrite(MOTOR_2_PIN, LOW); // placeholder-safe

  digitalWrite(PGA_CS_PIN, HIGH);
  digitalWrite(PGA_SCLK_PIN, LOW);
  digitalWrite(PGA_SDI_PIN, LOW);
  digitalWrite(PGA_MUTE_PIN, LOW); // mute asserted during startup (assumed active LOW)
}

void setup()
{
  initializeGpioSafe();

  // PB2/PB3 I2C mapping is selected in megaTinyCore board options
  // (Tools -> Wire -> PB2/PB3) for this hardware.
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
    digitalWrite(RELAY_OUTPUT_PIN, HIGH);
    digitalWrite(PGA_MUTE_PIN, HIGH); // unmute after relay engages
    g_outputRelayEnabled = true;
  }

  // Input ladder sampling + debounce.
  if (nowMs - g_lastInputMs >= INPUT_SAMPLE_PERIOD_MS) {
    g_lastInputMs = nowMs;

    const uint16_t inputAdc = readAdcAveraged(INPUT_ADC_PIN, 8);
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
    const float requestedDb = volumeAdcToRequestedDb(volAdc);
    setPgaVolumeDb(requestedDb);
  }

  // LCD update task.
  if (nowMs - g_lastDisplayMs >= DISPLAY_PERIOD_MS) {
    g_lastDisplayMs = nowMs;
    updateDisplay();
  }

  // Placeholder for future IR task (PA1).
  // Placeholder for future motorized potentiometer task (PB5/PB4).
}
