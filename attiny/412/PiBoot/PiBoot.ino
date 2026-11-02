/*
  Project: Raspberry Pi 4B / Volumio power controller
  Target:  ATtiny412 (megaTinyCore), 3.3 V permanent supply
  Version: 0.1.0

  Pin map
  -------
  PA0 -> UPDI (programming only, never reconfigured)
  PA1 -> TPS_EN          output, HIGH = TPS22975 ON = Pi 5 V present
  PA2 -> PI_SHUTDOWN_REQ output, HIGH = 2N7002 on = Pi GPIO23 pulled LOW
                         (R3 100k gate pull-down holds it off during reset)
  PA3 <- SAFE_TO_OFF_IN  input, Pi GPIO22 (gpio-poweroff), R2 100k pull-down
                         HIGH = Pi reports shutdown complete
  PA6 <- POWER_BUTTON    input, polarity in pi_power_config.h (VERIFY)
  PA7 -> STATUS_LED      output, polarity in pi_power_config.h (VERIFY)

  Clock / fuse assumptions
  ------------------------
  - Select 10 MHz (or lower) internal clock: 16/20 MHz are out of spec at 3.3 V.
  - BOD: recommend "2.6 V", enabled/sampled (set when burning bootloader).
  - Reset pin option must stay "UPDI". Do not change it.
  - millis() is used for all timing; no other timers are touched.

  Serial debug
  ------------
  None. Serial TX/RX on the 412 are PA6/PA7 (default) or PA1/PA2 (alt), all
  used here, so the UART cannot be used without moving hardware.

  All logic lives in pi_power_fsm.c; this file only does pin I/O.
*/

#include <avr/io.h>
#include <avr/xmega.h>
#include "pi_power_config.h"
#include "pi_power_fsm.h"

#if F_CPU > 10000000UL
#error "Select 10 MHz or lower: ATtiny412 at 3.3 V is not rated above 10 MHz."
#endif

// Pins
const uint8_t PIN_TPS_EN       = PIN_PA1; // TPS22975 ON
const uint8_t PIN_SHUTDOWN_REQ = PIN_PA2; // 2N7002 gate
const uint8_t PIN_SAFE_TO_OFF  = PIN_PA3; // Pi GPIO22
const uint8_t PIN_BUTTON       = PIN_PA6; // Front-panel button
const uint8_t PIN_LED          = PIN_PA7; // Front-panel LED

static PiFsm fsm;

static void writeLed(uint8_t on) {
#if LED_ACTIVE_HIGH
  digitalWrite(PIN_LED, on ? HIGH : LOW);
#else
  digitalWrite(PIN_LED, on ? LOW : HIGH);
#endif
}

static uint8_t readButtonPressed(void) {
#if BUTTON_ACTIVE_LOW
  return digitalRead(PIN_BUTTON) == LOW;
#else
  return digitalRead(PIN_BUTTON) == HIGH;
#endif
}

static void watchdogStart(void) {
#if ENABLE_WATCHDOG
  _PROTECTED_WRITE(WDT.CTRLA, WDT_PERIOD_1KCLK_gc); // ~1.0 s
#endif
}

static inline void watchdogKick(void) {
#if ENABLE_WATCHDOG
  __asm__ __volatile__("wdr");
#endif
}

void setup() {
  // Safe states first: set output latch LOW before enabling the driver.
  digitalWrite(PIN_TPS_EN, LOW);
  pinMode(PIN_TPS_EN, OUTPUT);
  digitalWrite(PIN_SHUTDOWN_REQ, LOW);
  pinMode(PIN_SHUTDOWN_REQ, OUTPUT);
  writeLed(0);
  pinMode(PIN_LED, OUTPUT);

  pinMode(PIN_SAFE_TO_OFF, INPUT); // R2 external pull-down, no internal pull
#if BUTTON_ACTIVE_LOW
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#else
  pinMode(PIN_BUTTON, INPUT); // needs external pull-down
#endif

  pi_fsm_init(&fsm, millis());
  watchdogStart();
}

void loop() {
  watchdogKick();

  pi_fsm_update(&fsm, millis(), readButtonPressed(),
                digitalRead(PIN_SAFE_TO_OFF) == HIGH);

  digitalWrite(PIN_TPS_EN, fsm.out_tps_en ? HIGH : LOW);
  digitalWrite(PIN_SHUTDOWN_REQ, fsm.out_shutdown_req ? HIGH : LOW);
  writeLed(fsm.out_led);
}
