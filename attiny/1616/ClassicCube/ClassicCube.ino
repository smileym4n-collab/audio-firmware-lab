/*
  ClassicCube.ino - ATtiny1616 stereo preamp control for ClassicCube
  Version: 0.1.0

  Scope in this version:
  - PGA2311 stereo volume control from a 0V..3.3V potentiometer
  - PGA2311 hardware mute control with safe startup muting
  - Single relay input select driven through a ULN2003A stage
  - Toggle-switch input selection with reversible logic in code

  Reference:
  - PGA serial write timing and mute sequencing are based on the
    proven approach used in attiny/1616/PreAmpv2/PreAmpv2.ino.

  Target:
  - ATtiny1616
  - megaTinyCore
  - Arduino IDE
  - Logic rail: 3.3V
*/

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Pin map (edit these first to match your hardware)
// -----------------------------------------------------------------------------
static const uint8_t INPUT_RELAY_PIN = PIN_PA1;  // ULN2003A input that energizes the source-select relay
static const uint8_t INPUT_SWITCH_PIN = PIN_PA2; // Toggle switch logic input

static const uint8_t PGA_MUTE_PIN = PIN_PA4; // PGA2311 MUTE
static const uint8_t PGA_SDI_PIN  = PIN_PA5; // PGA2311 SDI
static const uint8_t PGA_SCLK_PIN = PIN_PB3; // PGA2311 SCLK
static const uint8_t PGA_CS_PIN   = PIN_PA3; // PGA2311 CS

static const uint8_t VOL_ADC_PIN  = PIN_PB1; // Volume potentiometer wiper (0V..3.3V)

// -----------------------------------------------------------------------------
// Hardware behavior configuration
// -----------------------------------------------------------------------------
static const uint8_t RELAY_ACTIVE_STATE      = HIGH; // Typical ULN2003A drive polarity
static const uint8_t RELAY_INACTIVE_STATE    = LOW;
static const uint8_t PGA_MUTE_ACTIVE_STATE   = LOW;  // Same assumption as PreAmpv2
static const uint8_t PGA_MUTE_INACTIVE_STATE = HIGH;

static const bool INPUT_SWITCH_HIGH_SELECTS_RELAY_ON = true;
static const bool INPUT_SWITCH_USE_INTERNAL_PULLUP   = false;

// PGA2311 gain limits.
// Device range is -95.5 dB .. +31.5 dB in 0.5 dB steps.
// The default cap here is conservative; raise it if your build needs gain.
static const float PGA_MIN_DB = -95.5f;
static const float PGA_MAX_DB = 0.0f;

static const uint16_t INPUT_SAMPLE_PERIOD_MS = 5;
static const uint8_t INPUT_STABLE_SAMPLES_REQUIRED = 4;
static const uint16_t VOLUME_SAMPLE_PERIOD_MS = 20;
static const uint16_t INPUT_RELAY_SETTLE_MS = 8;
static const uint8_t VOLUME_ADC_SAMPLES = 8;

// Compile-time debug flag for serial bring-up logs.
// 0 = disabled (default), 1 = enabled.
#ifndef CLASSICCUBE_DEBUG
#define CLASSICCUBE_DEBUG 0
#endif

#if CLASSICCUBE_DEBUG
#define DBG_BEGIN(baud)    Serial.begin(baud)
#define DBG_PRINT(...)     Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...)   Serial.println(__VA_ARGS__)
#else
#define DBG_BEGIN(baud)    do {} while (0)
#define DBG_PRINT(...)     do {} while (0)
#define DBG_PRINTLN(...)   do {} while (0)
#endif

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------
static bool g_muted = true;
static bool g_inputRelayOn = false;
static bool g_pendingRelayState = false;
static uint8_t g_pendingRelayStableCount = 0;

static uint8_t g_pgaCode = 255; // force first write during setup
static float g_pgaDb = PGA_MIN_DB;
static uint16_t g_lastVolAdc = 0;

static uint32_t g_lastInputSampleMs = 0;
static uint32_t g_lastVolumeSampleMs = 0;

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

static void configureAnalogInputs()
{
  pinMode(VOL_ADC_PIN, INPUT);
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
  // Same 3-segment taper strategy as PreAmpv2 so a linear pot feels more usable:
  // - lower travel expands quiet listening levels
  // - middle travel spans common listening range
  // - top travel reserves the configured headroom
  const float normalized = static_cast<float>(adcValue) / 1023.0f;

  const float splitA = 0.70f;
  const float splitB = 0.95f;

  if (normalized <= splitA) {
    const float x = normalized / splitA;
    const float shaped = x * x;
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
  // Directly follows the known-good bit-banged write sequence from PreAmpv2:
  // CS low, right byte first, then left byte, MSB first on SDI.
  digitalWrite(PGA_CS_PIN, LOW);
  delayMicroseconds(1);

  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_SCLK_PIN, LOW);
    digitalWrite(PGA_SDI_PIN, (code & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PGA_SCLK_PIN, HIGH);
    delayMicroseconds(1);
  }

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

static void setMute(bool muteEnabled)
{
  digitalWrite(PGA_MUTE_PIN, muteEnabled ? PGA_MUTE_ACTIVE_STATE : PGA_MUTE_INACTIVE_STATE);
  g_muted = muteEnabled;
}

static void setPgaVolumeDb(float requestedDb)
{
  const uint8_t newCode = dbToPgaCode(requestedDb);
  if (newCode == g_pgaCode) {
    return;
  }

  pgaWriteStereo(newCode);
  g_pgaCode = newCode;
  g_pgaDb = pgaCodeToDb(newCode);
}

static bool readSwitchRequestsRelayOn()
{
  const bool rawHigh = (digitalRead(INPUT_SWITCH_PIN) == HIGH);
  return INPUT_SWITCH_HIGH_SELECTS_RELAY_ON ? rawHigh : !rawHigh;
}

static void applyInputRelayState(bool relayOn)
{
  if (relayOn == g_inputRelayOn) {
    return;
  }

  const bool restoreAudioAfterSwitch = !g_muted;
  if (restoreAudioAfterSwitch) {
    setMute(true);
    delay(INPUT_RELAY_SETTLE_MS);
  }

  digitalWrite(INPUT_RELAY_PIN, relayOn ? RELAY_ACTIVE_STATE : RELAY_INACTIVE_STATE);
  g_inputRelayOn = relayOn;

  delay(INPUT_RELAY_SETTLE_MS);

  if (restoreAudioAfterSwitch) {
    setMute(false);
  }
}

static void initializeGpioSafe()
{
  pinMode(INPUT_RELAY_PIN, OUTPUT);
  pinMode(INPUT_SWITCH_PIN, INPUT_SWITCH_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);

  pinMode(PGA_MUTE_PIN, OUTPUT);
  pinMode(PGA_SDI_PIN, OUTPUT);
  pinMode(PGA_SCLK_PIN, OUTPUT);
  pinMode(PGA_CS_PIN, OUTPUT);

  configureAnalogInputs();

  digitalWrite(INPUT_RELAY_PIN, RELAY_INACTIVE_STATE);
  digitalWrite(PGA_CS_PIN, HIGH);
  digitalWrite(PGA_SCLK_PIN, LOW);
  digitalWrite(PGA_SDI_PIN, LOW);
  digitalWrite(PGA_MUTE_PIN, PGA_MUTE_ACTIVE_STATE);
}

static void serviceInputSelection(uint32_t nowMs)
{
  if ((nowMs - g_lastInputSampleMs) < INPUT_SAMPLE_PERIOD_MS) {
    return;
  }
  g_lastInputSampleMs = nowMs;

  const bool requestedRelayState = readSwitchRequestsRelayOn();
  if (requestedRelayState != g_pendingRelayState) {
    g_pendingRelayState = requestedRelayState;
    g_pendingRelayStableCount = 1;
    return;
  }

  if (g_pendingRelayStableCount < INPUT_STABLE_SAMPLES_REQUIRED) {
    ++g_pendingRelayStableCount;
  }

  if (g_pendingRelayStableCount >= INPUT_STABLE_SAMPLES_REQUIRED) {
    applyInputRelayState(g_pendingRelayState);
  }
}

static void serviceVolume(uint32_t nowMs)
{
  if ((nowMs - g_lastVolumeSampleMs) < VOLUME_SAMPLE_PERIOD_MS) {
    return;
  }
  g_lastVolumeSampleMs = nowMs;

  g_lastVolAdc = readAdcAveraged(VOL_ADC_PIN, VOLUME_ADC_SAMPLES);
  setPgaVolumeDb(volumeAdcToRequestedDb(g_lastVolAdc));

  DBG_PRINT(F("volAdc="));
  DBG_PRINT(g_lastVolAdc);
  DBG_PRINT(F(" db="));
  DBG_PRINT(g_pgaDb, 1);
  DBG_PRINT(F(" code="));
  DBG_PRINT(g_pgaCode);
  DBG_PRINT(F(" relay="));
  DBG_PRINTLN(g_inputRelayOn ? F("ON") : F("OFF"));
}

void setup()
{
  initializeGpioSafe();
  DBG_BEGIN(115200);

  g_pendingRelayState = readSwitchRequestsRelayOn();
  applyInputRelayState(g_pendingRelayState);

  g_lastVolAdc = readAdcAveraged(VOL_ADC_PIN, VOLUME_ADC_SAMPLES);
  setPgaVolumeDb(volumeAdcToRequestedDb(g_lastVolAdc));

  delay(20);
  setMute(false);
}

void loop()
{
  const uint32_t nowMs = millis();

  serviceInputSelection(nowMs);
  serviceVolume(nowMs);
}
