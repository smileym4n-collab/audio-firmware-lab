/*
  minipreamp.ino - ATtiny1616 mini preamp controller
  Version: 0.3.1

  Controls:
  - PGA2311 volume IC (stereo, capped at 0 dB max gain)
  - 2 relay input selection through ULN2003A
  - 2 input indicator LEDs through TLC5916
  - 3-digit 7-segment display via AS1115 (I2C)

  Target:
  - ATtiny1616 with megaTinyCore
*/

#include <Arduino.h>

// Pin map (fixed by hardware)
static const uint8_t IN_SEL_ADC_PIN = PIN_PA4;  // Logic input for source selection
static const uint8_t AS1115_SDA_PIN = PIN_PA1;  // AS1115 I2C data
static const uint8_t AS1115_SCL_PIN = PIN_PA2;  // AS1115 I2C clock
static const uint8_t VOL_ADC_PIN     = PIN_PB0;  // Potentiometer wiper for volume
static const uint8_t PGA_MUTE_PIN    = PIN_PB3;  // PGA2311 mute control
static const uint8_t RELAY1_PIN      = PIN_PB4;  // Relay 1 drive (via ULN2003A)
static const uint8_t RELAY2_PIN      = PIN_PB5;  // Relay 2 drive (via ULN2003A)
static const uint8_t CLOCK_PIN       = PIN_PC0;  // Shared shift clock (TLC5916 + PGA2311)
static const uint8_t DATA_PIN        = PIN_PC1;  // Shared shift data  (TLC5916 + PGA2311)
static const uint8_t TLC_LE_PIN      = PIN_PC2;  // TLC5916 latch enable
static const uint8_t PGA_CS_PIN      = PIN_PC3;  // PGA2311 chip select (active low)

// PGA2311 limits
static const uint8_t PGA_CODE_MIN = 0x00;  // -95.5 dB
static const uint8_t PGA_CODE_MAX = 0xCF;  // 0.0 dB (capped, no positive gain)

// TLC5916 LED bits (Q0 = input 1 LED, Q1 = input 2 LED)
static const uint8_t LED_INPUT1_MASK = 0x01;
static const uint8_t LED_INPUT2_MASK = 0x02;

// AS1115 (7-bit I2C address; set to your hardware strap/address selection)
static const uint8_t AS1115_I2C_ADDR = 0x00;

// AS1115 register map (MAX7219 compatible)
static const uint8_t AS1115_REG_DIGIT0      = 0x01;
static const uint8_t AS1115_REG_DIGIT1      = 0x02;
static const uint8_t AS1115_REG_DIGIT2      = 0x03;
static const uint8_t AS1115_REG_DECODE_MODE = 0x09;
static const uint8_t AS1115_REG_INTENSITY   = 0x0A;
static const uint8_t AS1115_REG_SCAN_LIMIT  = 0x0B;
static const uint8_t AS1115_REG_SHUTDOWN    = 0x0C;
static const uint8_t AS1115_REG_DISPLAY_TEST = 0x0F;

// I2C software timing
static const uint8_t SOFT_I2C_DELAY_US = 5;

// Volume-curve tuning
// 0   = fully linear pot-to-volume mapping
// 100 = fully log-like (audio taper) mapping
// Values between 0..100 blend between linear and log-like responses.
static const uint8_t VOLUME_CURVE_BLEND_PERCENT = 60;

// Poll/update timing
static const uint16_t INPUT_POLL_MS  = 10;
static const uint16_t VOLUME_POLL_MS = 10;

static bool g_input2Selected = false;
static uint8_t g_lastPgaCode = PGA_CODE_MIN;
static uint8_t g_lastVolumePercent = 0xFF;
static uint32_t g_lastInputPollMs = 0;
static uint32_t g_lastVolumePollMs = 0;

static inline void i2cSdaHigh()
{
  pinMode(AS1115_SDA_PIN, INPUT_PULLUP);
}

static inline void i2cSdaLow()
{
  pinMode(AS1115_SDA_PIN, OUTPUT);
  digitalWrite(AS1115_SDA_PIN, LOW);
}

static inline void i2cSclHigh()
{
  pinMode(AS1115_SCL_PIN, INPUT_PULLUP);
}

static inline void i2cSclLow()
{
  pinMode(AS1115_SCL_PIN, OUTPUT);
  digitalWrite(AS1115_SCL_PIN, LOW);
}

static inline void i2cDelay()
{
  delayMicroseconds(SOFT_I2C_DELAY_US);
}

static void i2cStart()
{
  i2cSdaHigh();
  i2cSclHigh();
  i2cDelay();
  i2cSdaLow();
  i2cDelay();
  i2cSclLow();
}

static void i2cStop()
{
  i2cSdaLow();
  i2cDelay();
  i2cSclHigh();
  i2cDelay();
  i2cSdaHigh();
  i2cDelay();
}

static bool i2cWriteByte(uint8_t value)
{
  for (uint8_t i = 0; i < 8; ++i) {
    if (value & 0x80u) {
      i2cSdaHigh();
    } else {
      i2cSdaLow();
    }

    i2cDelay();
    i2cSclHigh();
    i2cDelay();
    i2cSclLow();
    value <<= 1;
  }

  i2cSdaHigh();
  i2cDelay();
  i2cSclHigh();
  const bool ack = (digitalRead(AS1115_SDA_PIN) == LOW);
  i2cDelay();
  i2cSclLow();
  return ack;
}

static bool as1115WriteReg(uint8_t reg, uint8_t value)
{
  i2cStart();
  const bool ok = i2cWriteByte(static_cast<uint8_t>(AS1115_I2C_ADDR << 1)) &&
                  i2cWriteByte(reg) &&
                  i2cWriteByte(value);
  i2cStop();
  return ok;
}

static void as1115Init()
{
  as1115WriteReg(AS1115_REG_SHUTDOWN, 0x00);      // Shutdown during setup
  as1115WriteReg(AS1115_REG_DISPLAY_TEST, 0x00);  // Test mode off
  as1115WriteReg(AS1115_REG_SCAN_LIMIT, 0x02);    // Digits 0..2 active
  as1115WriteReg(AS1115_REG_DECODE_MODE, 0x07);   // Code-B decode on digits 0..2
  as1115WriteReg(AS1115_REG_INTENSITY, 0x08);     // Mid brightness
  as1115WriteReg(AS1115_REG_DIGIT0, 0x0F);        // Blank
  as1115WriteReg(AS1115_REG_DIGIT1, 0x0F);        // Blank
  as1115WriteReg(AS1115_REG_DIGIT2, 0x0F);        // Blank
  as1115WriteReg(AS1115_REG_SHUTDOWN, 0x01);      // Normal operation
}

static void as1115ShowPercent(uint8_t percent)
{
  if (percent > 100u) {
    percent = 100u;
  }

  const uint8_t ones = percent % 10u;
  const uint8_t tens = (percent / 10u) % 10u;
  const uint8_t hundreds = percent / 100u;

  // Hardware display order requires swapping outer digits:
  // DIGIT2 = ones (rightmost), DIGIT1 = tens (middle), DIGIT0 = hundreds (leftmost)
  as1115WriteReg(AS1115_REG_DIGIT2, ones);
  as1115WriteReg(AS1115_REG_DIGIT1, (percent >= 10u) ? tens : 0x0F);
  as1115WriteReg(AS1115_REG_DIGIT0, (percent >= 100u) ? hundreds : 0x0F);
}

static inline void pulseClock()
{
  digitalWrite(CLOCK_PIN, HIGH);
  digitalWrite(CLOCK_PIN, LOW);
}

static void shiftOutMsb(uint8_t value)
{
  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t bitMask = 0x80u >> i;
    digitalWrite(DATA_PIN, (value & bitMask) ? HIGH : LOW);
    pulseClock();
  }
}

static void writeTlcLeds(uint8_t ledMask)
{
  digitalWrite(PGA_CS_PIN, HIGH);  // Keep PGA2311 deselected while bus is shared
  digitalWrite(TLC_LE_PIN, LOW);

  shiftOutMsb(ledMask);

  digitalWrite(TLC_LE_PIN, HIGH);  // Latch TLC5916 output register
  digitalWrite(TLC_LE_PIN, LOW);
}

static void writePgaVolume(uint8_t leftCode, uint8_t rightCode)
{
  digitalWrite(TLC_LE_PIN, LOW);   // Keep TLC latch stable while bus is shared
  digitalWrite(PGA_CS_PIN, LOW);   // Start PGA2311 frame

  shiftOutMsb(leftCode);
  shiftOutMsb(rightCode);

  digitalWrite(PGA_CS_PIN, HIGH);  // Latch PGA2311 frame
}

static uint8_t volumeAdcToPgaCode(uint16_t adc)
{
  if (adc > 1023u) {
    adc = 1023u;
  }

  // Linear curve in 10-bit ADC domain.
  const uint16_t linearCurve = adc;

  // Log-like curve using a square law (audio taper style) in 10-bit ADC domain.
  const uint16_t logLikeCurve = static_cast<uint16_t>((static_cast<uint32_t>(adc) * adc) / 1023u);

  const uint8_t blend = (VOLUME_CURVE_BLEND_PERCENT > 100u) ? 100u : VOLUME_CURVE_BLEND_PERCENT;
  const uint16_t blendedCurve = static_cast<uint16_t>(
    (static_cast<uint32_t>(linearCurve) * (100u - blend) + static_cast<uint32_t>(logLikeCurve) * blend) / 100u
  );

  const uint32_t scaled = (static_cast<uint32_t>(blendedCurve) * PGA_CODE_MAX) / 1023u;
  return static_cast<uint8_t>(scaled + PGA_CODE_MIN);
}

static uint8_t pgaCodeToPercent(uint8_t pgaCode)
{
  if (pgaCode > PGA_CODE_MAX) {
    pgaCode = PGA_CODE_MAX;
  }

  return static_cast<uint8_t>((static_cast<uint32_t>(pgaCode) * 100u) / PGA_CODE_MAX);
}

static void applyInputSelection(bool input2)
{
  g_input2Selected = input2;

  if (g_input2Selected) {
    digitalWrite(RELAY1_PIN, LOW);
    digitalWrite(RELAY2_PIN, HIGH);
    writeTlcLeds(LED_INPUT2_MASK);
  } else {
    digitalWrite(RELAY1_PIN, HIGH);
    digitalWrite(RELAY2_PIN, LOW);
    writeTlcLeds(LED_INPUT1_MASK);
  }
}

void setup()
{
  pinMode(IN_SEL_ADC_PIN, INPUT);
  pinMode(AS1115_SDA_PIN, INPUT_PULLUP);
  pinMode(AS1115_SCL_PIN, INPUT_PULLUP);
  pinMode(VOL_ADC_PIN, INPUT);

  pinMode(PGA_MUTE_PIN, OUTPUT);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(TLC_LE_PIN, OUTPUT);
  pinMode(PGA_CS_PIN, OUTPUT);

  // Safe startup defaults
  digitalWrite(PGA_MUTE_PIN, LOW);   // Keep audio muted during setup
  digitalWrite(RELAY1_PIN, LOW);     // Keep both relays inactive until selection is known
  digitalWrite(RELAY2_PIN, LOW);
  digitalWrite(CLOCK_PIN, LOW);
  digitalWrite(DATA_PIN, LOW);
  digitalWrite(TLC_LE_PIN, LOW);
  digitalWrite(PGA_CS_PIN, HIGH);

  // Apply initial state from hardware inputs
  applyInputSelection(digitalRead(IN_SEL_ADC_PIN) == HIGH);

  const uint16_t initialAdc = analogRead(VOL_ADC_PIN);
  g_lastPgaCode = volumeAdcToPgaCode(initialAdc);
  writePgaVolume(g_lastPgaCode, g_lastPgaCode);

  as1115Init();
  g_lastVolumePercent = pgaCodeToPercent(g_lastPgaCode);
  as1115ShowPercent(g_lastVolumePercent);

  digitalWrite(PGA_MUTE_PIN, HIGH);  // Unmute after volume and routing are valid
}

void loop()
{
  const uint32_t nowMs = millis();

  if ((nowMs - g_lastInputPollMs) >= INPUT_POLL_MS) {
    g_lastInputPollMs = nowMs;

    const bool input2Now = (digitalRead(IN_SEL_ADC_PIN) == HIGH);
    if (input2Now != g_input2Selected) {
      applyInputSelection(input2Now);
    }
  }

  if ((nowMs - g_lastVolumePollMs) >= VOLUME_POLL_MS) {
    g_lastVolumePollMs = nowMs;

    const uint16_t volumeAdc = analogRead(VOL_ADC_PIN);
    const uint8_t pgaCode = volumeAdcToPgaCode(volumeAdc);

    if (pgaCode != g_lastPgaCode) {
      g_lastPgaCode = pgaCode;
      writePgaVolume(g_lastPgaCode, g_lastPgaCode);

      const uint8_t percent = pgaCodeToPercent(g_lastPgaCode);
      if (percent != g_lastVolumePercent) {
        g_lastVolumePercent = percent;
        as1115ShowPercent(g_lastVolumePercent);
      }
    }
  }
}
