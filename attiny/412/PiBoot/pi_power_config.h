/*
  pi_power_config.h - all tunable constants for the Pi power controller.

  Every timing value is in milliseconds. Keep values well inside uint32_t.
  The host tests include this file too, so the tests exercise these values.
*/
#ifndef PI_POWER_CONFIG_H
#define PI_POWER_CONFIG_H

/* ---------------- Hardware polarity (VERIFY AGAINST THE PCB) -------------- */

/* Power button. 1 = button pulls PA6 to GND (internal pull-up enabled).
   0 = button drives PA6 HIGH (external pull-down required, none assumed). */
#define BUTTON_ACTIVE_LOW        1

/* Status LED. 1 = PA7 HIGH lights the LED (PA7 -> R -> LED -> GND).
   0 = PA7 LOW lights the LED (3V3 -> R -> LED -> PA7). */
#define LED_ACTIVE_HIGH          1

/* ---------------- Button ---------------------------------------------------- */

#define BUTTON_DEBOUNCE_MS       30UL    /* Stable time to accept press/release */
#define EMERGENCY_HOLD_MS        8000UL  /* Continuous hold to force power off  */

/* ---------------- Power on / boot ------------------------------------------ */

/* Minimum time after TPS_EN goes HIGH before SAFE_TO_OFF is even considered.
   This only masks power-up/firmware/kernel glitches on GPIO22. It does NOT
   prove Linux has booted (there is no boot-complete signal). */
#define BOOT_PROTECT_MS          20000UL

/* SAFE_TO_OFF must read stably LOW this long (after BOOT_PROTECT_MS) before
   shutdown detection is armed. */
#define SAFE_LOW_QUALIFY_MS      1000UL

/* ---------------- Shutdown -------------------------------------------------- */

/* PA2 HIGH time. Must exceed the gpio-shutdown overlay debounce (200 ms) with
   margin, but stay well below logind's long-press time (~5 s). */
#define SHUTDOWN_PULSE_MS        500UL

/* SAFE_TO_OFF must read stably HIGH this long to count as shutdown complete.
   Also rejects the ~100 ms HIGH/LOW pre-pulse gpio-poweroff emits first. */
#define SAFE_HIGH_QUALIFY_MS     500UL

/* Extra wait after a qualified HIGH before TPS_EN goes LOW. */
#define POWER_OFF_SETTLE_MS      1000UL

/* No completion seen after this long -> SHUTDOWN_TIMEOUT (power stays ON). */
#define SHUTDOWN_TIMEOUT_MS      120000UL

/* ---------------- Standby --------------------------------------------------- */

/* Minimum off time before the button can power the Pi again. Lets the 5 V
   rail discharge so the Pi sees a clean power cycle. */
#define MIN_OFF_TIME_MS          3000UL

/* ---------------- LED patterns ---------------------------------------------- */

#define LED_SLOW_HALF_MS         500UL   /* Booting: 1 Hz            */
#define LED_FAST_HALF_MS         100UL   /* Shutting down: 5 Hz      */
#define LED_DOUBLE_PERIOD_MS     1500UL  /* Timeout: two blips/period */
#define LED_DOUBLE_ON_MS         120UL
#define LED_DOUBLE_GAP_MS        120UL

/* ---------------- Watchdog -------------------------------------------------- */

/* 1 = enable the WDT (~1 s). A WDT reset restarts in STANDBY, which CUTS Pi
   power (see README "ATtiny reset behaviour"). */
#define ENABLE_WATCHDOG          1

#endif /* PI_POWER_CONFIG_H */
