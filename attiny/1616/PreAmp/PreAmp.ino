/*
  PreAmp.ino - ATtiny1616 preamp controller
  Version: 0.1.5

  Features:
  - PGA2310 volume control, capped at +10.0 dB maximum
  - 4-input relay selection via ULN2003 (DAC, AUX 1, AUX 2, PHONO)
  - Output relay control via ULN2003 with 1 second power-on delay
  - 16x2 I2C LCD: centered input on row 1, centered volume dB on row 2
  - Adjustable volume curve blending: linear to log-like
  - Motorized potentiometer drive via DRV8210
  - IR remote volume up/down/repeat handling (volume control only)

  Target:
  - ATtiny1616 using megaTinyCore (Arduino framework)
*/

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Pin map (fixed by hardware)
// -----------------------------------------------------------------------------
static const uint8_t RELAY_DAC_PIN     = PIN_PB0;  // Relay 1 (DAC) via ULN2003 input
static const uint8_t RELAY_AUX1_PIN    = PIN_PC0;  // Relay 2 (AUX 1) via ULN2003 input
static const uint8_t RELAY_AUX2_PIN    = PIN_PC1;  // Relay 3 (AUX 2) via ULN2003 input
static const uint8_t RELAY_PHONO_PIN   = PIN_PC2;  // Relay 4 (PHONO) via ULN2003 input
static const uint8_t OUTPUT_RELAY_PIN  = PIN_PC3;  // Output relay via ULN2003 input

static const uint8_t MOTOR_PIN_1       = PIN_PB5;  // DRV8210 input 1
static const uint8_t MOTOR_PIN_2       = PIN_PB4;  // DRV8210 input 2

static const uint8_t I2C_SDA_PIN       = PIN_PA1;  // LCD SDA
static const uint8_t I2C_SCL_PIN       = PIN_PA2;  // LCD SCL

static const uint8_t IR_PIN            = PIN_PA6;  // IR demodulated input

static const uint8_t PGA_MUTE_PIN      = PIN_PA4;  // PGA2310 mute control
static const uint8_t PGA_SDI_PIN       = PIN_PA5;  // PGA2310 serial data in
static const uint8_t PGA_SCLK_PIN      = PIN_PB3;  // PGA2310 serial clock
static const uint8_t PGA_CS_PIN        = PIN_PA3;  // PGA2310 chip select (active low)

static const uint8_t VOL_ADC_PIN       = PIN_PB1;  // Motorized potentiometer wiper
static const uint8_t INPUT_ADC_PIN     = PIN_PA7;  // Input selector resistor ladder

// -----------------------------------------------------------------------------
// User-tunable constants
// -----------------------------------------------------------------------------

// LCD settings
static const uint8_t LCD_COLS = 16;
static const uint8_t LCD_ROWS = 2;
static const uint8_t LCD_ADDR_PRIMARY = 0x27;   // Most common PCF8574 address
static const uint8_t LCD_ADDR_ALT = 0x3F;       // Common alternate address
LiquidCrystal_I2C g_lcdPrimary(LCD_ADDR_PRIMARY, LCD_COLS, LCD_ROWS);
LiquidCrystal_I2C g_lcdAlt(LCD_ADDR_ALT, LCD_COLS, LCD_ROWS);
static LiquidCrystal_I2C* g_lcd = &g_lcdPrimary;

// PGA2310 transfer and volume limits
static const float PGA_MIN_DB = -95.5f;    // PGA2310 minimum in dB
static const float PGA_MAX_DB = +10.0f;    // Requested cap for this project

// 0   = fully linear mapping
// 100 = fully log-like mapping
static const uint8_t VOLUME_CURVE_BLEND_PERCENT = 60;

// ADC / control timing
static const uint16_t INPUT_POLL_MS  = 20;
static const uint16_t VOLUME_POLL_MS = 15;
static const uint16_t DISPLAY_POLL_MS = 120;

// Motor control behaviour
static const uint16_t MOTOR_STEP_ADC = 8;          // IR step size in ADC counts
static const uint16_t MOTOR_DEADBAND_ADC = 3;      // Stop motor inside this error band
static const uint16_t MOTOR_MAX_RUN_MS = 2200;     // Safety timeout per continuous move

// IR command map (NEC protocol defaults; adjust for your remote)
static const uint8_t IR_CMD_VOL_UP = 0x18;
static const uint8_t IR_CMD_VOL_DOWN = 0x52;

// Input ladder thresholds from measured selector voltages at 3.3V VDD:
// Relay 1 (DAC):   0.541V  (~168 ADC counts)
// Relay 2 (AUX 1): 1.170V  (~363 ADC counts)
// Relay 3 (AUX 2): 1.940V  (~601 ADC counts)
// Relay 4 (PHONO): 2.700V  (~837 ADC counts)
// Midpoint thresholds: 266, 482, 719.
static const uint16_t THRESHOLD_R1_TO_R2 = 266;
static const uint16_t THRESHOLD_R2_TO_R3 = 482;
static const uint16_t THRESHOLD_R3_TO_R4 = 719;

// -----------------------------------------------------------------------------
// State
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

static InputSource g_activeInput = INPUT_DAC;
static float g_currentDb = PGA_MIN_DB;
static uint8_t g_currentPgaCode = 0;

static uint16_t g_currentAdc = 0;
static uint16_t g_targetAdc = 0;

static uint32_t g_lastInputPollMs = 0;
static uint32_t g_lastVolumePollMs = 0;
static uint32_t g_lastDisplayPollMs = 0;
static uint32_t g_bootMs = 0;

static bool g_outputRelayEnabled = false;

// Motor runtime guard
static bool g_motorRunning = false;
static uint32_t g_motorRunStartMs = 0;

// IR decode state (simple NEC decoder, no external IR library)
static bool g_irPrevLevelHigh = true;
static uint32_t g_irLastEdgeUs = 0;
static uint16_t g_irLastMarkUs = 0;
static bool g_irFrameActive = false;
static uint8_t g_irBitIndex = 0;
static uint32_t g_irRawData = 0;
static uint8_t g_irLastCommand = 0;
static bool g_irHasLastCommand = false;
static bool g_lcdAvailable = false;

// -----------------------------------------------------------------------------
// Utility helpers
// -----------------------------------------------------------------------------

static void configureVolumeAdcPin()
{
  pinMode(VOL_ADC_PIN, INPUT);

  // Explicitly disable pull-up and digital input buffer on PB1.
  // This keeps the pin as a high-impedance analog input and avoids
  // digital buffer interaction while sampling a buffered pot wiper.
  PORTB.PIN1CTRL &= ~PORT_PULLUPEN_bm;
  PORTB.PIN1CTRL = (PORTB.PIN1CTRL & ~PORT_ISC_gm) | PORT_ISC_INPUT_DISABLE_gc;
}

static uint16_t readAdcAveraged(uint8_t pin, uint8_t samples)
{
  analogRead(pin);  // Throw away first conversion after channel selection
  delayMicroseconds(40);

  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    sum += analogRead(pin);
  }
  return static_cast<uint16_t>(sum / samples);
}

static bool i2cDevicePresent(uint8_t address)
{
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

static void centerPrint(uint8_t row, const char* text)
{
  if (!g_lcdAvailable) {
    return;
  }

  size_t len = strlen(text);
  if (len > LCD_COLS) {
    len = LCD_COLS;
  }

  const int8_t padding = static_cast<int8_t>((LCD_COLS - len) / 2);
  g_lcd->setCursor(0, row);
  for (uint8_t i = 0; i < LCD_COLS; ++i) {
    g_lcd->print(' ');
  }
  g_lcd->setCursor((padding > 0) ? padding : 0, row);
  for (size_t i = 0; i < len; ++i) {
    g_lcd->print(text[i]);
  }
}

static uint8_t dbToPgaCode(float db)
{
  if (db < PGA_MIN_DB) {
    db = PGA_MIN_DB;
  }
  if (db > PGA_MAX_DB) {
    db = PGA_MAX_DB;
  }

  const float rawCode = (db - PGA_MIN_DB) * 2.0f;
  int16_t code = static_cast<int16_t>(rawCode + 0.5f);
  if (code < 0) {
    code = 0;
  }
  if (code > 255) {
    code = 255;
  }
  return static_cast<uint8_t>(code);
}

static float adcToDb(uint16_t adcValue)
{
  const float normalized = static_cast<float>(adcValue) / 1023.0f;
  const float curveBlend = static_cast<float>(VOLUME_CURVE_BLEND_PERCENT) / 100.0f;

  const float linear = normalized;
  const float audioLike = normalized * normalized;
  const float shaped = (linear * (1.0f - curveBlend)) + (audioLike * curveBlend);

  const float dbSpan = PGA_MAX_DB - PGA_MIN_DB;
  return PGA_MIN_DB + (shaped * dbSpan);
}

static void pgaWriteStereo(uint8_t code)
{
  digitalWrite(PGA_CS_PIN, LOW);

  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_SCLK_PIN, LOW);
    digitalWrite(PGA_SDI_PIN, (code & (1u << bit)) ? HIGH : LOW);
    digitalWrite(PGA_SCLK_PIN, HIGH);
  }

  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_SCLK_PIN, LOW);
    digitalWrite(PGA_SDI_PIN, (code & (1u << bit)) ? HIGH : LOW);
    digitalWrite(PGA_SCLK_PIN, HIGH);
  }

  digitalWrite(PGA_SCLK_PIN, LOW);
  digitalWrite(PGA_CS_PIN, HIGH);
}

static void applyPgaFromAdc(uint16_t adcValue)
{
  const float newDb = adcToDb(adcValue);
  const uint8_t newCode = dbToPgaCode(newDb);

  if (newCode != g_currentPgaCode) {
    pgaWriteStereo(newCode);
    g_currentPgaCode = newCode;
  }

  g_currentDb = newDb;
}

static InputSource decodeInputFromAdc(uint16_t adcValue)
{
  if (adcValue < THRESHOLD_R1_TO_R2) {
    return INPUT_DAC;
  }
  if (adcValue < THRESHOLD_R2_TO_R3) {
    return INPUT_AUX1;
  }
  if (adcValue < THRESHOLD_R3_TO_R4) {
    return INPUT_AUX2;
  }
  return INPUT_PHONO;
}

static void driveInputRelays(InputSource src)
{
  digitalWrite(RELAY_DAC_PIN, LOW);
  digitalWrite(RELAY_AUX1_PIN, LOW);
  digitalWrite(RELAY_AUX2_PIN, LOW);
  digitalWrite(RELAY_PHONO_PIN, LOW);

  switch (src) {
    case INPUT_DAC:
      digitalWrite(RELAY_DAC_PIN, HIGH);
      break;
    case INPUT_AUX1:
      digitalWrite(RELAY_AUX1_PIN, HIGH);
      break;
    case INPUT_AUX2:
      digitalWrite(RELAY_AUX2_PIN, HIGH);
      break;
    case INPUT_PHONO:
      digitalWrite(RELAY_PHONO_PIN, HIGH);
      break;
    default:
      break;
  }
}

static void motorStop()
{
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
  g_motorRunning = false;
}

// Direction assumption:
// MOTOR_PIN_1=HIGH, MOTOR_PIN_2=LOW increases ADC reading (volume up).
// Swap these two branches if your wiring direction is opposite.
static void motorDriveTowardTarget()
{
  const int16_t error = static_cast<int16_t>(g_targetAdc) - static_cast<int16_t>(g_currentAdc);

  if (abs(error) <= static_cast<int16_t>(MOTOR_DEADBAND_ADC)) {
    motorStop();
    return;
  }

  if (!g_motorRunning) {
    g_motorRunning = true;
    g_motorRunStartMs = millis();
  } else if ((millis() - g_motorRunStartMs) > MOTOR_MAX_RUN_MS) {
    motorStop();
    g_targetAdc = g_currentAdc;
    return;
  }

  if (error > 0) {
    digitalWrite(MOTOR_PIN_1, HIGH);
    digitalWrite(MOTOR_PIN_2, LOW);
  } else {
    digitalWrite(MOTOR_PIN_1, LOW);
    digitalWrite(MOTOR_PIN_2, HIGH);
  }
}

static void updateDisplay()
{
  centerPrint(0, INPUT_NAMES[g_activeInput]);

  char line[17];
  dtostrf(g_currentDb, 5, 1, line);
  strncat(line, " DB", sizeof(line) - strlen(line) - 1);
  centerPrint(1, line);
}

// -----------------------------------------------------------------------------
// IR decoding (NEC, volume commands only)
// -----------------------------------------------------------------------------

static bool isWithin(uint16_t value, uint16_t target, uint16_t tolerance)
{
  return (value + tolerance >= target) && (value <= target + tolerance);
}

static void handleIrVolumeCommand(uint8_t command, bool isRepeat)
{
  const bool isVolUp = (command == IR_CMD_VOL_UP);
  const bool isVolDown = (command == IR_CMD_VOL_DOWN);

  if (!isVolUp && !isVolDown) {
    return;
  }

  (void)isRepeat;

  if (isVolUp) {
    const uint16_t upper = 1023;
    g_targetAdc = (g_targetAdc + MOTOR_STEP_ADC > upper) ? upper : (g_targetAdc + MOTOR_STEP_ADC);
  } else {
    g_targetAdc = (g_targetAdc > MOTOR_STEP_ADC) ? (g_targetAdc - MOTOR_STEP_ADC) : 0;
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
      if (g_irHasLastCommand) {
        handleIrVolumeCommand(g_irLastCommand, true);
      }
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
      const uint8_t addr = static_cast<uint8_t>(g_irRawData & 0xFFu);
      const uint8_t addrInv = static_cast<uint8_t>((g_irRawData >> 8) & 0xFFu);
      const uint8_t cmd = static_cast<uint8_t>((g_irRawData >> 16) & 0xFFu);
      const uint8_t cmdInv = static_cast<uint8_t>((g_irRawData >> 24) & 0xFFu);

      if ((uint8_t)(addr ^ addrInv) == 0xFFu && (uint8_t)(cmd ^ cmdInv) == 0xFFu) {
        g_irLastCommand = cmd;
        g_irHasLastCommand = true;
        handleIrVolumeCommand(cmd, false);
      }
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

// -----------------------------------------------------------------------------
// Setup / loop
// -----------------------------------------------------------------------------

void setup()
{
  pinMode(RELAY_DAC_PIN, OUTPUT);
  pinMode(RELAY_AUX1_PIN, OUTPUT);
  pinMode(RELAY_AUX2_PIN, OUTPUT);
  pinMode(RELAY_PHONO_PIN, OUTPUT);
  pinMode(OUTPUT_RELAY_PIN, OUTPUT);

  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);

  pinMode(PGA_MUTE_PIN, OUTPUT);
  pinMode(PGA_SDI_PIN, OUTPUT);
  pinMode(PGA_SCLK_PIN, OUTPUT);
  pinMode(PGA_CS_PIN, OUTPUT);

  configureVolumeAdcPin();
  pinMode(INPUT_ADC_PIN, INPUT);
  pinMode(IR_PIN, INPUT);

  // Safe startup states
  digitalWrite(RELAY_DAC_PIN, LOW);
  digitalWrite(RELAY_AUX1_PIN, LOW);
  digitalWrite(RELAY_AUX2_PIN, LOW);
  digitalWrite(RELAY_PHONO_PIN, LOW);
  digitalWrite(OUTPUT_RELAY_PIN, LOW);
  motorStop();

  digitalWrite(PGA_CS_PIN, HIGH);
  digitalWrite(PGA_SCLK_PIN, LOW);
  digitalWrite(PGA_SDI_PIN, LOW);
  digitalWrite(PGA_MUTE_PIN, LOW);  // mute during initialization

  Wire.begin();
  Wire.setClock(100000UL);

  if (i2cDevicePresent(LCD_ADDR_PRIMARY)) {
    g_lcd = &g_lcdPrimary;
    g_lcdAvailable = true;
  } else if (i2cDevicePresent(LCD_ADDR_ALT)) {
    g_lcd = &g_lcdAlt;
    g_lcdAvailable = true;
  }

  if (g_lcdAvailable) {
    g_lcd->init();
    g_lcd->backlight();
  }
  centerPrint(0, "PREAMP");
  centerPrint(1, "STARTING");

  g_irPrevLevelHigh = (digitalRead(IR_PIN) == HIGH);
  g_irLastEdgeUs = micros();

  g_currentAdc = readAdcAveraged(VOL_ADC_PIN, 4);
  g_targetAdc = g_currentAdc;
  applyPgaFromAdc(g_currentAdc);

  g_activeInput = decodeInputFromAdc(readAdcAveraged(INPUT_ADC_PIN, 4));
  driveInputRelays(g_activeInput);

  updateDisplay();

  g_bootMs = millis();
}

void loop()
{
  const uint32_t now = millis();
  serviceIrReceiver();

  if ((now - g_lastInputPollMs) >= INPUT_POLL_MS) {
    g_lastInputPollMs = now;
    const InputSource newInput = decodeInputFromAdc(readAdcAveraged(INPUT_ADC_PIN, 4));
    if (newInput != g_activeInput) {
      g_activeInput = newInput;
      driveInputRelays(g_activeInput);
    }
  }

  if ((now - g_lastVolumePollMs) >= VOLUME_POLL_MS) {
    g_lastVolumePollMs = now;
    g_currentAdc = readAdcAveraged(VOL_ADC_PIN, 4);
    applyPgaFromAdc(g_currentAdc);
    motorDriveTowardTarget();
  }

  if (!g_outputRelayEnabled && (now - g_bootMs >= 1000u)) {
    digitalWrite(OUTPUT_RELAY_PIN, HIGH);
    g_outputRelayEnabled = true;
    digitalWrite(PGA_MUTE_PIN, HIGH);  // unmute after output relay closes
  }

  if ((now - g_lastDisplayPollMs) >= DISPLAY_POLL_MS) {
    g_lastDisplayPollMs = now;
    updateDisplay();
  }
}
