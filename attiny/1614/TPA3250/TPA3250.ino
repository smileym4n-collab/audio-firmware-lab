/*
  TPA3250.ino - ATtiny1614 stereo amplifier controller

  Target:
  - ATtiny1614, megaTinyCore, Arduino IDE
  - 3.3 V logic rail, matching the reference project and TPA3250 logic I/O
  - TPA3250 power amplifier and PGA2310/PGA2311 stereo volume control

  The PGA serial transfer, gain coding, pot mapping, ADC settling and channel
  order are adapted from attiny/1616/PreAmpv2/PreAmpv2.ino.
*/

#include <Arduino.h>

#if !defined(__AVR_ATtiny1614__)
#error "Select ATtiny1614 in megaTinyCore before compiling this sketch."
#endif

// -----------------------------------------------------------------------------
// Pin map (fixed by the amplifier PCB)
// -----------------------------------------------------------------------------
static const uint8_t TPA_FAULT_PIN    = PIN_PB0; // Physical 9; active-low
static const uint8_t TPA_CLIP_OTW_PIN = PIN_PB1; // Active-low, TPA internal pull-up
static const uint8_t TPA_RESET_PIN    = PIN_PB3; // Physical 6; active-low, 47k pull-down

static const uint8_t VOLUME_ADC_PIN   = PIN_PA1; // Pot wiper, 0 V to MCU VDD
static const uint8_t PVDD_ADC_PIN     = PIN_PA2; // Physical 12; 100k/10k divider
static const uint8_t PGA_MUTE_PIN     = PIN_PA3; // Physical 13; active-low
static const uint8_t PGA_CLOCK_PIN    = PIN_PA4;
static const uint8_t PGA_DATA_PIN     = PIN_PA5;
static const uint8_t PGA_CS_PIN       = PIN_PA6; // Active-low

static_assert(TPA_RESET_PIN == PIN_PB3 && TPA_FAULT_PIN == PIN_PB0 &&
                  PVDD_ADC_PIN == PIN_PA2 && PGA_MUTE_PIN == PIN_PA3,
              "Confirmed amplifier control pin map has changed");

// -----------------------------------------------------------------------------
// User-adjustable hardware calibration and operating thresholds
// -----------------------------------------------------------------------------

// The volume pot is measured against VDD, so its result is ratiometric and is
// insensitive to normal 3.3 V rail tolerance. PVDD uses the fixed internal 2.5 V
// reference so a collapsing MCU supply cannot hide a collapsing PVDD rail.
static const uint16_t ADC_MAX_COUNT = 1023;       // megaTinyCore 10-bit analogRead
static const uint32_t PVDD_ADC_REFERENCE_MV = 2500UL; // Calibrate if required
static const uint16_t PVDD_ADC_CALIBRATION_PERMILLE = 1000; // 1000 = no correction
static const uint32_t PVDD_DIVIDER_TOP_OHMS = 100000UL;
static const uint32_t PVDD_DIVIDER_BOTTOM_OHMS = 10000UL;

// Initial policy for a nominal 19 V to 24 V supply. These are board-level tuning
// values: verify them while observing real switch-off ripple and rail hold-up.
static const uint16_t PVDD_STARTUP_MV = 18000;
static const uint16_t PVDD_SHUTDOWN_MV = 17000;       // 1 V restart hysteresis
static const uint16_t PVDD_FAST_SHUTDOWN_MV = 16000;  // Two raw samples required
static const uint16_t PVDD_EMERGENCY_RESET_MV = 12000;
static const uint16_t PVDD_POWER_CYCLED_MV = 5000;

// PGA2310/PGA2311 gain coding: code 1 = -95.5 dB, code 192 = 0 dB,
// and code 255 = +31.5 dB. Positive gain is deliberately disabled by default.
static constexpr float PGA_MIN_DB = -95.5f;
static constexpr float PGA_MAX_DB = 0.0f; // Configurable upper limit; keep <= +31.5 dB

// -----------------------------------------------------------------------------
// Timing and filtering constants
// -----------------------------------------------------------------------------
static const uint16_t PVDD_SAMPLE_PERIOD_MS = 10;
static const uint8_t PVDD_ADC_SAMPLES = 4;
static const uint8_t PVDD_FILTER_DIVISOR = 4; // IIR: 1/4 new sample, 3/4 history
static const uint8_t PVDD_FAST_LOW_SAMPLES = 2;
static const uint16_t PVDD_SHUTDOWN_DEBOUNCE_MS = 40;
static const uint16_t PVDD_STARTUP_STABLE_MS = 500; // Also exceeds TI's 400 ms RESET hold
static const uint16_t POWER_CYCLE_STABLE_MS = 250;

static const uint16_t VOLUME_SAMPLE_PERIOD_MS = 20;
static const uint8_t VOLUME_ADC_SAMPLES = 8;
static const uint8_t VOLUME_ONE_STEP_STABLE_SAMPLES = 2;

static const uint16_t PGA_MUTE_SETTLE_MS = 20; // PGA zero-crossing mute can take ~16 ms
static const uint16_t STARTUP_PVDD_DIP_GRACE_MS = 250;
static const uint16_t TPA_STARTUP_FAULT_GUARD_US = 1000;
static const uint16_t TPA_STARTUP_FAULT_TIMEOUT_MS = 50;
static const uint16_t TPA_PRECHARGE_COMPLETE_MS = 300; // From RESET rising edge

// TI requires RESET rising at least 4 ms after a genuine FAULT falling edge.
// The one-second retry hold provides ample margin and prevents rapid cycling;
// the separate minimum is also checked explicitly before every fault retry.
static const uint16_t TPA_FAULT_TO_RESET_RELEASE_MIN_MS = 4;
static const uint16_t FAULT_RETRY_DELAY_MS = 1000;
static const uint8_t FAULT_MAX_RETRIES = 2;
static const uint32_t FAULT_RETRY_WINDOW_MS = 30000UL;

// A sustained low CLIP_OTW can indicate OTW. It is recorded only; normal clip
// pulses and this indication never alter gain, MUTE or RESET.
static const uint16_t CLIP_OTW_PROLONGED_MS = 1000;
static const uint32_t UINT32_MAX_VALUE = 0xFFFFFFFFUL;

static_assert(PGA_MAX_DB <= 31.5f, "PGA gain cannot exceed +31.5 dB");
static_assert(PVDD_STARTUP_MV > PVDD_SHUTDOWN_MV,
              "PVDD startup must exceed shutdown threshold");
static_assert(PVDD_SHUTDOWN_MV > PVDD_FAST_SHUTDOWN_MV,
              "Fast shutdown threshold must be below normal shutdown threshold");
static_assert(PVDD_STARTUP_STABLE_MS >= 400,
              "TPA RESET must remain low for at least 400 ms of valid PVDD");
static_assert(TPA_PRECHARGE_COMPLETE_MS >= 220,
              "TPA precharge interval is shorter than the documented startup");
static_assert(TPA_STARTUP_FAULT_GUARD_US <
                  (TPA_STARTUP_FAULT_TIMEOUT_MS * 1000UL),
              "Startup FAULT observation guard exceeds its timeout");
static_assert(FAULT_RETRY_DELAY_MS >= TPA_FAULT_TO_RESET_RELEASE_MIN_MS,
              "Fault retry can release RESET too soon after FAULT");
static_assert(PVDD_ADC_REFERENCE_MV *
                  (PVDD_DIVIDER_TOP_OHMS + PVDD_DIVIDER_BOTTOM_OHMS) /
                  PVDD_DIVIDER_BOTTOM_OHMS >= 24000UL,
              "PVDD ADC range must include the 24 V operating rail");

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------
enum AmplifierState : uint8_t {
  BOOT_SAFE = 0,
  WAIT_FOR_POWER,
  STARTING,
  PRECHARGE,
  RUNNING,
  SHUTTING_DOWN,
  FAULT_HOLD,
  FAULT_LOCKOUT
};

static AmplifierState g_state = BOOT_SAFE;
static uint32_t g_stateEnteredMs = 0;

static uint16_t g_pvddRawMv = 0;
static uint16_t g_pvddFilteredMv = 0;
static uint32_t g_lastPvddSampleMs = 0;
static bool g_pvddStartupTiming = false;
static uint32_t g_pvddStartupSinceMs = 0;
static bool g_pvddShutdownTiming = false;
static uint32_t g_pvddShutdownSinceMs = 0;
static bool g_powerCycleTiming = false;
static uint32_t g_powerCycleSinceMs = 0;
static uint8_t g_pvddFastLowCount = 0;

static uint16_t g_lastVolumeAdc = 0;
static uint32_t g_lastVolumeSampleMs = 0;
static uint8_t g_pgaCode = 0;
static bool g_pgaProgrammed = false;
static uint8_t g_pendingPgaCode = 0;
static uint8_t g_pendingPgaStableCount = 0;

static uint8_t g_adcReference = 0xFF;

static volatile bool g_faultInterruptPending = false;
static volatile bool g_faultProtectionArmed = false;
static volatile bool g_startupFaultWindow = false;
static volatile bool g_startupFaultEdgeSeen = false;
static uint8_t g_faultRetryCount = 0;
static uint32_t g_faultWindowStartMs = 0;
static uint32_t g_resetReleasedMs = 0;
static uint32_t g_resetReleasedUs = 0;
static uint32_t g_lastGenuineFaultMs = 0;

enum FaultReason : uint8_t {
  FAULT_REASON_NONE = 0,
  FAULT_REASON_STARTUP_TIMEOUT,
  FAULT_REASON_PRECHARGE,
  FAULT_REASON_RUNNING,
  FAULT_REASON_UNEXPECTED_STATE
};

static FaultReason g_faultReason = FAULT_REASON_NONE;

static bool g_clipOtwLow = false;
static volatile bool g_clipOtwProlonged = false; // Visible to a debugger
static uint32_t g_clipOtwLowSinceMs = 0;
static volatile uint32_t g_clipOtwPulseCount = 0; // Visible to a debugger

// No diagnostic transport existed in this sketch, and every documented UART
// route overlaps a board function. These event/value variables are always
// debugger-visible without changing any pin. Text logging can be enabled only
// after the UART has been routed externally without disturbing the fixed pins.
#ifndef TPA_ENABLE_SERIAL_DIAGNOSTICS
#define TPA_ENABLE_SERIAL_DIAGNOSTICS 0
#endif

enum TpaDiagnosticEvent : uint8_t {
  TPA_DIAG_NONE = 0,
  TPA_DIAG_RESET_ASSERTED,
  TPA_DIAG_PVDD_WAIT,
  TPA_DIAG_RESET_RELEASED,
  TPA_DIAG_STARTUP_FAULT_LOW,
  TPA_DIAG_FAULT_HIGH,
  TPA_DIAG_PRECHARGE,
  TPA_DIAG_RUNNING,
  TPA_DIAG_FAULT,
  TPA_DIAG_RETRY,
  TPA_DIAG_LOCKOUT
};

static volatile uint8_t g_lastTpaDiagnosticEvent = TPA_DIAG_NONE;
static volatile uint32_t g_lastTpaDiagnosticValue = 0;
static volatile uint32_t g_tpaDiagnosticEventCount = 0;
static bool g_pvddWaitReported = false;
static bool g_startupFaultLowReported = false;

#if TPA_ENABLE_SERIAL_DIAGNOSTICS
static const __FlashStringHelper* faultReasonText(FaultReason reason)
{
  switch (reason) {
    case FAULT_REASON_STARTUP_TIMEOUT: return F("startup FAULT timeout");
    case FAULT_REASON_PRECHARGE:       return F("precharge FAULT");
    case FAULT_REASON_RUNNING:         return F("runtime FAULT");
    default:                           return F("unexpected state");
  }
}

static void initializeDiagnostics()
{
  // Enabling this requires the UART to have been safely routed off every fixed
  // board pin before setup(). It is deliberately disabled in the shipped build.
  Serial.begin(115200);
}
#else
static void initializeDiagnostics() {}
#endif

static void logTpaDiagnostic(TpaDiagnosticEvent event, uint32_t value = 0,
                             FaultReason reason = FAULT_REASON_NONE)
{
  g_lastTpaDiagnosticEvent = event;
  g_lastTpaDiagnosticValue = value;
  if (g_tpaDiagnosticEventCount < UINT32_MAX_VALUE) {
    ++g_tpaDiagnosticEventCount;
  }

#if TPA_ENABLE_SERIAL_DIAGNOSTICS
  switch (event) {
    case TPA_DIAG_RESET_ASSERTED:
      Serial.println(F("TPA: RESET asserted"));
      break;
    case TPA_DIAG_PVDD_WAIT:
      Serial.print(F("TPA: PVDD valid, waiting "));
      Serial.print(value);
      Serial.println(F(" ms"));
      break;
    case TPA_DIAG_RESET_RELEASED:
      Serial.print(F("TPA: releasing RESET; PVDD "));
      Serial.print(value);
      Serial.println(F(" mV"));
      break;
    case TPA_DIAG_STARTUP_FAULT_LOW:
      Serial.println(F("TPA: startup FAULT low (expected)"));
      break;
    case TPA_DIAG_FAULT_HIGH:
      Serial.print(F("TPA: FAULT high after "));
      Serial.print(value);
      Serial.println(F(" us"));
      break;
    case TPA_DIAG_PRECHARGE:
      Serial.println(F("TPA: precharge"));
      break;
    case TPA_DIAG_RUNNING:
      Serial.println(F("TPA: running"));
      break;
    case TPA_DIAG_FAULT:
      Serial.print(F("TPA: "));
      Serial.println(faultReasonText(reason));
      break;
    case TPA_DIAG_RETRY:
      Serial.print(F("TPA: retry "));
      Serial.print(value);
      Serial.print('/');
      Serial.print(FAULT_MAX_RETRIES);
      Serial.print(F("; reason: "));
      Serial.println(faultReasonText(reason));
      break;
    case TPA_DIAG_LOCKOUT:
      Serial.print(F("TPA: locked out; reason: "));
      Serial.println(faultReasonText(reason));
      break;
    default:
      break;
  }
#else
  (void)reason;
#endif
}

// -----------------------------------------------------------------------------
// Active-low output helpers
// -----------------------------------------------------------------------------
static inline void tpaResetAssert()
{
  PORTB.OUTCLR = PIN3_bm; // PB3 LOW = TPA3250 held in RESET
}

static inline void tpaResetRelease()
{
  PORTB.OUTSET = PIN3_bm; // PB3 HIGH = TPA3250 enabled
}

static inline bool tpaResetIsReleased()
{
  return (PORTB.OUT & PIN3_bm) != 0;
}

static inline void pgaMute()
{
  PORTA.OUTCLR = PIN3_bm; // PA3 LOW = PGA hardware mute asserted
}

static inline void pgaUnmute()
{
  PORTA.OUTSET = PIN3_bm; // PA3 HIGH = PGA hardware mute released
}

static void forceSafeOutputs()
{
  g_faultProtectionArmed = false;
  g_startupFaultWindow = false;
  pgaMute();
  tpaResetAssert();
}

// This is intentionally the first setup action. Output latches are loaded before
// their direction bits, so enabling the pins cannot produce a RESET-high or
// MUTE-high glitch. The external PB3 pull-down remains helpful until DIR is set.
static void initializeSafetyOutputsFirst()
{
  PORTB.OUTCLR = PIN3_bm; // TPA RESET LOW

  PORTA.OUTCLR = PIN3_bm | PIN4_bm | PIN5_bm; // MUTE, CLOCK and DATA LOW
  PORTA.OUTSET = PIN6_bm;                     // PGA CS HIGH (inactive)

  PORTB.DIRSET = PIN3_bm;
  PORTA.DIRSET = PIN3_bm | PIN4_bm | PIN5_bm | PIN6_bm;
}

// -----------------------------------------------------------------------------
// ADC and PVDD monitoring
// -----------------------------------------------------------------------------
static void selectAdcReference(uint8_t reference)
{
  if (reference == g_adcReference) {
    return;
  }

  analogReference(reference);
  g_adcReference = reference;
  delayMicroseconds(100); // Internal-reference startup / reference change settling
}

static uint16_t readAdcAveraged(uint8_t pin, uint8_t samples, uint8_t reference)
{
  selectAdcReference(reference);
  analogRead(pin); // Throw away the first conversion after reference/mux selection
  delayMicroseconds(40); // Retained from the known-working PGA pot implementation

  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    sum += analogRead(pin);
  }

  return static_cast<uint16_t>(sum / samples);
}

static uint16_t readVolumePotAveraged()
{
  // VDD reference makes the pot reading independent of 3.3 V rail tolerance.
  return readAdcAveraged(VOLUME_ADC_PIN, VOLUME_ADC_SAMPLES, VDD);
}

static uint16_t pvddAdcCountsToMillivolts(uint16_t adcCounts)
{
  // ADC pin voltage = PVDD * Rbottom / (Rtop + Rbottom).
  // Staged 32-bit math avoids expensive 64-bit arithmetic while retaining more
  // precision than the ADC itself. With 100k/10k, PVDD is ADC voltage * 11.
  uint32_t adcPinMv =
      (static_cast<uint32_t>(adcCounts) * PVDD_ADC_REFERENCE_MV +
       (ADC_MAX_COUNT / 2)) /
      ADC_MAX_COUNT;

  uint32_t pvddMv =
      (adcPinMv * (PVDD_DIVIDER_TOP_OHMS + PVDD_DIVIDER_BOTTOM_OHMS) +
       (PVDD_DIVIDER_BOTTOM_OHMS / 2)) /
      PVDD_DIVIDER_BOTTOM_OHMS;

  pvddMv =
      (pvddMv * PVDD_ADC_CALIBRATION_PERMILLE + 500UL) / 1000UL;

  return static_cast<uint16_t>(pvddMv > 65535UL ? 65535UL : pvddMv);
}

// Returns the estimated external PVDD rail in millivolts.
static uint16_t readPvddMillivolts()
{
  const uint16_t adcCounts =
      readAdcAveraged(PVDD_ADC_PIN, PVDD_ADC_SAMPLES, INTERNAL2V5);
  return pvddAdcCountsToMillivolts(adcCounts);
}

static void updateStableTimer(bool condition, bool* timing,
                              uint32_t* sinceMs, uint32_t nowMs)
{
  if (condition) {
    if (!*timing) {
      *timing = true;
      *sinceMs = nowMs;
    }
  } else {
    *timing = false;
  }
}

static bool stableFor(bool timing, uint32_t sinceMs,
                      uint32_t intervalMs, uint32_t nowMs)
{
  return timing && ((nowMs - sinceMs) >= intervalMs);
}

static void samplePvdd(uint32_t nowMs)
{
  if ((nowMs - g_lastPvddSampleMs) < PVDD_SAMPLE_PERIOD_MS) {
    return;
  }
  g_lastPvddSampleMs = nowMs;

  g_pvddRawMv = readPvddMillivolts();
  if (g_pvddFilteredMv == 0) {
    g_pvddFilteredMv = g_pvddRawMv;
  } else {
    g_pvddFilteredMv = static_cast<uint16_t>(
        ((static_cast<uint32_t>(g_pvddFilteredMv) *
          (PVDD_FILTER_DIVISOR - 1)) + g_pvddRawMv +
         (PVDD_FILTER_DIVISOR / 2)) /
        PVDD_FILTER_DIVISOR);
  }

  updateStableTimer(g_pvddFilteredMv >= PVDD_STARTUP_MV,
                    &g_pvddStartupTiming, &g_pvddStartupSinceMs, nowMs);
  updateStableTimer(g_pvddFilteredMv <= PVDD_SHUTDOWN_MV,
                    &g_pvddShutdownTiming, &g_pvddShutdownSinceMs, nowMs);
  updateStableTimer(g_pvddFilteredMv <= PVDD_POWER_CYCLED_MV,
                    &g_powerCycleTiming, &g_powerCycleSinceMs, nowMs);

  if (g_pvddRawMv <= PVDD_FAST_SHUTDOWN_MV) {
    if (g_pvddFastLowCount < PVDD_FAST_LOW_SAMPLES) {
      ++g_pvddFastLowCount;
    }
  } else {
    g_pvddFastLowCount = 0;
  }
}

static bool pvddReadyForStartup(uint32_t nowMs)
{
  return stableFor(g_pvddStartupTiming, g_pvddStartupSinceMs,
                   PVDD_STARTUP_STABLE_MS, nowMs);
}

static bool pvddCollapseDetected(uint32_t nowMs)
{
  const bool filteredLow =
      stableFor(g_pvddShutdownTiming, g_pvddShutdownSinceMs,
                PVDD_SHUTDOWN_DEBOUNCE_MS, nowMs);
  const bool fastLow = g_pvddFastLowCount >= PVDD_FAST_LOW_SAMPLES;
  return filteredLow || fastLow;
}

static bool pvddValidForAudio(uint32_t nowMs)
{
  return (g_pvddRawMv > PVDD_FAST_SHUTDOWN_MV) &&
         (g_pvddFilteredMv > PVDD_SHUTDOWN_MV) &&
         !pvddCollapseDetected(nowMs);
}

static void restartPvddStartupQualification()
{
  // A fast raw-rail dip can be real before the IIR filter has fallen. Explicitly
  // discard the old qualification so RESET cannot be re-released immediately.
  g_pvddStartupTiming = false;
  g_pvddWaitReported = false;
}

static bool resetReleaseAllowedAfterFault(uint32_t nowMs)
{
  return (g_faultReason == FAULT_REASON_NONE) ||
         ((nowMs - g_lastGenuineFaultMs) >=
          TPA_FAULT_TO_RESET_RELEASE_MIN_MS);
}

// -----------------------------------------------------------------------------
// PGA2310/PGA2311 control (adapted without protocol changes from PreAmpv2)
// -----------------------------------------------------------------------------
static uint8_t dbToPgaCode(float db)
{
  if (db < PGA_MIN_DB) {
    db = PGA_MIN_DB;
  }
  if (db > PGA_MAX_DB) {
    db = PGA_MAX_DB;
  }

  const float codeFloat = (db + 96.0f) * 2.0f; // Code 192 = 0 dB
  int16_t codeInt = static_cast<int16_t>(codeFloat + 0.5f);
  if (codeInt < 0) {
    codeInt = 0;
  }
  if (codeInt > 255) {
    codeInt = 255;
  }
  return static_cast<uint8_t>(codeInt);
}

static float volumeAdcToRequestedDb(uint16_t adcValue)
{
  // Linear travel in dB matches the reference preamp's perceived-volume taper.
  const float normalized = static_cast<float>(adcValue) / ADC_MAX_COUNT;
  return PGA_MIN_DB + normalized * (PGA_MAX_DB - PGA_MIN_DB);
}

static void pgaWriteStereo(uint8_t code)
{
  // Known-working PGA2310/PGA2311 transfer from PreAmpv2:
  // CS LOW for all 16 clocks, MSB first, Right byte then Left byte. Data is
  // sampled on each SCLK rising edge; SCLK returns LOW before CS rises.
  digitalWrite(PGA_CS_PIN, LOW);
  delayMicroseconds(1);

  // Right channel byte.
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_CLOCK_PIN, LOW);
    digitalWrite(PGA_DATA_PIN, (code & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PGA_CLOCK_PIN, HIGH);
    delayMicroseconds(1);
  }

  // Left channel byte.
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PGA_CLOCK_PIN, LOW);
    digitalWrite(PGA_DATA_PIN, (code & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PGA_CLOCK_PIN, HIGH);
    delayMicroseconds(1);
  }

  digitalWrite(PGA_CLOCK_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(PGA_CS_PIN, HIGH);
  delayMicroseconds(1);
}

static void writePgaCode(uint8_t newCode)
{
  if (g_pgaProgrammed && (newCode == g_pgaCode)) {
    return;
  }

  pgaWriteStereo(newCode);
  g_pgaCode = newCode;
  g_pgaProgrammed = true;
  g_pendingPgaCode = newCode;
  g_pendingPgaStableCount = 0;
}

static void applyVolumePotReading(uint16_t adcValue, bool immediate)
{
  const uint8_t newCode = dbToPgaCode(volumeAdcToRequestedDb(adcValue));

  if (immediate || !g_pgaProgrammed) {
    writePgaCode(newCode);
    return;
  }

  if (newCode == g_pgaCode) {
    g_pendingPgaCode = newCode;
    g_pendingPgaStableCount = 0;
    return;
  }

  const uint8_t difference = (newCode > g_pgaCode)
                                 ? (newCode - g_pgaCode)
                                 : (g_pgaCode - newCode);

  // Large pot movements remain responsive. A one-code (0.5 dB) move must be
  // seen twice, preventing ADC noise at a code boundary from chattering.
  if (difference > 1) {
    writePgaCode(newCode);
    return;
  }

  if (newCode == g_pendingPgaCode) {
    if (g_pendingPgaStableCount < VOLUME_ONE_STEP_STABLE_SAMPLES) {
      ++g_pendingPgaStableCount;
    }
  } else {
    g_pendingPgaCode = newCode;
    g_pendingPgaStableCount = 1;
  }

  if (g_pendingPgaStableCount >= VOLUME_ONE_STEP_STABLE_SAMPLES) {
    writePgaCode(newCode);
  }
}

static void serviceVolume(uint32_t nowMs)
{
  if ((nowMs - g_lastVolumeSampleMs) < VOLUME_SAMPLE_PERIOD_MS) {
    return;
  }
  g_lastVolumeSampleMs = nowMs;
  g_lastVolumeAdc = readVolumePotAveraged();
  applyVolumePotReading(g_lastVolumeAdc, false);
}

// -----------------------------------------------------------------------------
// FAULT and CLIP_OTW monitoring
// -----------------------------------------------------------------------------
static void onTpaFaultFalling()
{
  if (!g_faultProtectionArmed) {
    if (g_startupFaultWindow) {
      g_startupFaultEdgeSeen = true;
    }
    return;
  }

  // Protection is armed only after the expected startup FAULT-low period has
  // cleared. A later PRECHARGE/RUNNING fault is an emergency: mute and reset
  // immediately rather than waiting for the normal zero-crossing mute interval.
  PORTA.OUTCLR = PIN3_bm;
  PORTB.OUTCLR = PIN3_bm;
  g_faultInterruptPending = true;
}

static void serviceClipOtw(uint32_t nowMs)
{
  const bool lowNow = digitalRead(TPA_CLIP_OTW_PIN) == LOW;
  if (lowNow && !g_clipOtwLow) {
    g_clipOtwLow = true;
    g_clipOtwLowSinceMs = nowMs;
    if (g_clipOtwPulseCount < UINT32_MAX_VALUE) {
      ++g_clipOtwPulseCount;
    }
  } else if (!lowNow) {
    g_clipOtwLow = false;
    g_clipOtwProlonged = false;
  }

  if (g_clipOtwLow &&
      ((nowMs - g_clipOtwLowSinceMs) >= CLIP_OTW_PROLONGED_MS)) {
    g_clipOtwProlonged = true; // Reserved for future thermal policy/diagnostics
  }
}

// -----------------------------------------------------------------------------
// Amplifier state machine
// -----------------------------------------------------------------------------
static void enterState(AmplifierState state, uint32_t nowMs)
{
  g_state = state;
  g_stateEnteredMs = nowMs;
}

static void enterFaultState(uint32_t nowMs, FaultReason reason);

static void beginStarting(uint32_t nowMs)
{
  pgaMute();
  g_faultProtectionArmed = false;
  g_faultInterruptPending = false;
  g_startupFaultEdgeSeen = false;
  g_startupFaultWindow = true;
  g_startupFaultLowReported = false;
  enterState(STARTING, nowMs); // State changes before expected FAULT falling edge
  tpaResetRelease();
  g_resetReleasedMs = nowMs;
  g_resetReleasedUs = micros();
  logTpaDiagnostic(TPA_DIAG_RESET_RELEASED, g_pvddFilteredMv);
}

static void beginPrecharge(uint32_t nowMs)
{
  const uint32_t faultHighElapsedUs = micros() - g_resetReleasedUs;

  // From this point onward, any new FAULT-low edge is genuine. Change state
  // before arming the ISR, then make a final level check to close the race.
  g_startupFaultWindow = false;
  g_faultInterruptPending = false;
  enterState(PRECHARGE, nowMs);
  g_faultProtectionArmed = true;

  if (digitalRead(TPA_FAULT_PIN) == LOW) {
    forceSafeOutputs();
    g_faultInterruptPending = true;
  }

  // Log only after protection has been armed; a slow diagnostic sink must not
  // extend the unprotected interval after startup FAULT has cleared.
  logTpaDiagnostic(TPA_DIAG_FAULT_HIGH, faultHighElapsedUs);
  logTpaDiagnostic(TPA_DIAG_PRECHARGE);
}

static void beginRunning(uint32_t nowMs)
{
  // The ISR is already armed from PRECHARGE. Never unmute if a fault arrived,
  // RESET is not actually high, or the monitored supply is no longer valid.
  noInterrupts();
  const bool faultPresent =
      g_faultInterruptPending || (digitalRead(TPA_FAULT_PIN) == LOW);
  const bool startupStillValid =
      tpaResetIsReleased() && pvddValidForAudio(nowMs);

  if (faultPresent) {
    interrupts();
    enterFaultState(nowMs, FAULT_REASON_PRECHARGE);
    return;
  }
  if (!startupStillValid) {
    interrupts();
    forceSafeOutputs();
    restartPvddStartupQualification();
    enterState(WAIT_FOR_POWER, nowMs);
    return;
  }

  enterState(RUNNING, nowMs);
  pgaUnmute();
  interrupts();
  logTpaDiagnostic(TPA_DIAG_RUNNING);
}

static void beginNormalShutdown(uint32_t nowMs)
{
  // Normal power loss: let the PGA's zero-crossing mute complete before RESET.
  pgaMute();
  g_faultProtectionArmed = false;
  restartPvddStartupQualification();
  enterState(SHUTTING_DOWN, nowMs);
}

static void enterFaultState(uint32_t nowMs, FaultReason reason)
{
  forceSafeOutputs();
  g_faultInterruptPending = false;
  g_lastGenuineFaultMs = nowMs;
  g_faultReason = reason;
  logTpaDiagnostic(TPA_DIAG_FAULT, 0, reason);

  if ((g_faultWindowStartMs == 0) ||
      ((nowMs - g_faultWindowStartMs) > FAULT_RETRY_WINDOW_MS)) {
    g_faultWindowStartMs = nowMs;
    g_faultRetryCount = 0;
  }

  if (g_faultRetryCount >= FAULT_MAX_RETRIES) {
    enterState(FAULT_LOCKOUT, nowMs);
    logTpaDiagnostic(TPA_DIAG_LOCKOUT, 0, reason);
  } else {
    enterState(FAULT_HOLD, nowMs);
  }
}

static void serviceStateMachine(uint32_t nowMs)
{
  switch (g_state) {
    case BOOT_SAFE:
      forceSafeOutputs();
      logTpaDiagnostic(TPA_DIAG_RESET_ASSERTED);
      enterState(WAIT_FOR_POWER, nowMs);
      break;

    case WAIT_FOR_POWER:
      forceSafeOutputs();
      if (g_pvddStartupTiming && !g_pvddWaitReported) {
        logTpaDiagnostic(TPA_DIAG_PVDD_WAIT, PVDD_STARTUP_STABLE_MS);
        g_pvddWaitReported = true;
      } else if (!g_pvddStartupTiming) {
        g_pvddWaitReported = false;
      }
      if (pvddReadyForStartup(nowMs) &&
          resetReleaseAllowedAfterFault(nowMs)) {
        beginStarting(nowMs);
      }
      break;

    case STARTING:
      // The output stage can briefly pull a fast bench supply into current limit
      // when RESET is released. PGA MUTE remains asserted during this grace
      // period. A severe collapse still resets immediately; after the grace
      // period the normal filtered/fast PVDD shutdown rules apply.
      if ((g_pvddRawMv <= PVDD_EMERGENCY_RESET_MV) ||
          (((nowMs - g_stateEnteredMs) >= STARTUP_PVDD_DIP_GRACE_MS) &&
           pvddCollapseDetected(nowMs))) {
        forceSafeOutputs(); // Already muted, so no mute-completion delay is needed
        restartPvddStartupQualification();
        enterState(WAIT_FOR_POWER, nowMs);
        break;
      }

      if (g_startupFaultEdgeSeen && !g_startupFaultLowReported) {
        logTpaDiagnostic(TPA_DIAG_STARTUP_FAULT_LOW);
        g_startupFaultLowReported = true;
      }

      if (digitalRead(TPA_FAULT_PIN) == HIGH) {
        // Do not accept the pre-pulse HIGH immediately after RESET. Normally the
        // expected low edge is observed; the 1 ms guard also supports hardware
        // whose FAULT remains high throughout a successful startup.
        if (g_startupFaultLowReported || g_startupFaultEdgeSeen ||
            ((micros() - g_resetReleasedUs) >=
             TPA_STARTUP_FAULT_GUARD_US)) {
          beginPrecharge(nowMs);
        }
      } else {
        // This first FAULT-low is documented TPA startup behaviour (including
        // AVDD ramp/PPSC), so the interrupt remains disarmed in STARTING.
        if (!g_startupFaultLowReported) {
          logTpaDiagnostic(TPA_DIAG_STARTUP_FAULT_LOW);
          g_startupFaultLowReported = true;
        }
        if ((nowMs - g_resetReleasedMs) >= TPA_STARTUP_FAULT_TIMEOUT_MS) {
          enterFaultState(nowMs, FAULT_REASON_STARTUP_TIMEOUT);
        }
      }
      break;

    case PRECHARGE:
      // FAULT has already returned high, so a new low is a genuine fault. PGA
      // remains muted until 300 ms total has elapsed from RESET release.
      if (g_faultInterruptPending || (digitalRead(TPA_FAULT_PIN) == LOW)) {
        enterFaultState(nowMs, FAULT_REASON_PRECHARGE);
      } else if ((g_pvddRawMv <= PVDD_EMERGENCY_RESET_MV) ||
                 (((nowMs - g_resetReleasedMs) >= STARTUP_PVDD_DIP_GRACE_MS) &&
                  pvddCollapseDetected(nowMs))) {
        forceSafeOutputs();
        restartPvddStartupQualification();
        enterState(WAIT_FOR_POWER, nowMs);
      } else if ((nowMs - g_resetReleasedMs) >= TPA_PRECHARGE_COMPLETE_MS) {
        beginRunning(nowMs);
      }
      break;

    case RUNNING:
      if (g_faultInterruptPending || (digitalRead(TPA_FAULT_PIN) == LOW)) {
        enterFaultState(nowMs, FAULT_REASON_RUNNING);
      } else if (pvddCollapseDetected(nowMs)) {
        beginNormalShutdown(nowMs);
      } else if ((g_faultWindowStartMs != 0) &&
                 ((nowMs - g_faultWindowStartMs) > FAULT_RETRY_WINDOW_MS)) {
        // A long fault-free run restores the retry allowance.
        g_faultWindowStartMs = 0;
        g_faultRetryCount = 0;
      }
      break;

    case SHUTTING_DOWN:
      if (digitalRead(TPA_FAULT_PIN) == LOW ||
          g_pvddRawMv <= PVDD_EMERGENCY_RESET_MV) {
        // A fault or exceptionally fast rail collapse overrides the normal mute
        // delay because keeping the power stage enabled is now the greater risk.
        tpaResetAssert();
        enterState(WAIT_FOR_POWER, nowMs);
      } else if ((nowMs - g_stateEnteredMs) >= PGA_MUTE_SETTLE_MS) {
        tpaResetAssert();
        enterState(WAIT_FOR_POWER, nowMs);
      }
      break;

    case FAULT_HOLD:
      forceSafeOutputs();
      if (stableFor(g_powerCycleTiming, g_powerCycleSinceMs,
                    POWER_CYCLE_STABLE_MS, nowMs)) {
        // A genuine power cycle clears the retry history. Merely sagging below
        // the shutdown threshold must not bypass retry counting.
        g_faultRetryCount = 0;
        g_faultWindowStartMs = 0;
        enterState(WAIT_FOR_POWER, nowMs);
      } else if (((nowMs - g_stateEnteredMs) >= FAULT_RETRY_DELAY_MS) &&
                 resetReleaseAllowedAfterFault(nowMs) &&
                 pvddReadyForStartup(nowMs)) {
        ++g_faultRetryCount;
        logTpaDiagnostic(TPA_DIAG_RETRY, g_faultRetryCount, g_faultReason);
        beginStarting(nowMs);
      }
      break;

    case FAULT_LOCKOUT:
      forceSafeOutputs();
      if (stableFor(g_powerCycleTiming, g_powerCycleSinceMs,
                    POWER_CYCLE_STABLE_MS, nowMs)) {
        g_faultRetryCount = 0;
        g_faultWindowStartMs = 0;
        enterState(WAIT_FOR_POWER, nowMs);
      }
      break;

    default:
      // Corrupt/unexpected state always fails toward muted and reset.
      forceSafeOutputs();
      g_faultReason = FAULT_REASON_UNEXPECTED_STATE;
      logTpaDiagnostic(TPA_DIAG_FAULT, 0, g_faultReason);
      enterState(FAULT_LOCKOUT, nowMs);
      logTpaDiagnostic(TPA_DIAG_LOCKOUT, 0, g_faultReason);
      break;
  }
}

static void configureInputsAndAdc()
{
  pinMode(TPA_FAULT_PIN, INPUT);
  pinMode(TPA_CLIP_OTW_PIN, INPUT);
  pinMode(VOLUME_ADC_PIN, INPUT);
  pinMode(PVDD_ADC_PIN, INPUT);

  // TPA3250 provides internal pull-ups on FAULT and CLIP_OTW. Do not add MCU
  // pull-ups or bias either analogue input.
  PORTB.PIN0CTRL &= ~PORT_PULLUPEN_bm;
  PORTB.PIN1CTRL &= ~PORT_PULLUPEN_bm;
  PORTA.PIN1CTRL =
      (PORTA.PIN1CTRL & ~(PORT_PULLUPEN_bm | PORT_ISC_gm)) |
      PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN2CTRL =
      (PORTA.PIN2CTRL & ~(PORT_PULLUPEN_bm | PORT_ISC_gm)) |
      PORT_ISC_INPUT_DISABLE_gc;

  analogReadResolution(10);
}

void setup()
{
  initializeSafetyOutputsFirst();
  initializeDiagnostics();
  configureInputsAndAdc();

  g_state = BOOT_SAFE;
  g_stateEnteredMs = millis();

  // Establish both PGA channel gains while hardware MUTE and TPA RESET remain
  // asserted. The first write is forced even if the calculated code is zero.
  g_lastVolumeAdc = readVolumePotAveraged();
  applyVolumePotReading(g_lastVolumeAdc, true);

  g_pvddRawMv = readPvddMillivolts();
  g_pvddFilteredMv = g_pvddRawMv;
  g_lastPvddSampleMs = millis();

  attachInterrupt(digitalPinToInterrupt(TPA_FAULT_PIN),
                  onTpaFaultFalling, FALLING);
}

void loop()
{
  const uint32_t nowMs = millis();

  samplePvdd(nowMs);
  serviceVolume(nowMs);
  serviceClipOtw(nowMs);
  serviceStateMachine(nowMs);
}
