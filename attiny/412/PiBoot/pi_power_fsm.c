/*
  pi_power_fsm.c - power controller state machine (see README for diagram).

  All timing uses unsigned subtraction (now - start), so millis() wrap at
  ~49.7 days is handled.
*/
#include "pi_power_fsm.h"
#include "pi_power_config.h"

/* ---------------- Debounce helpers ---------------------------------------- */

static void debounce_reset(Debounce *d, uint8_t level, uint32_t now_ms) {
  d->stable = level;
  d->raw = level;
  d->changed_ms = now_ms;
}

/* rise_ms: stable time to accept 0->1, fall_ms: stable time to accept 1->0 */
static void debounce_update(Debounce *d, uint8_t raw, uint32_t now_ms,
                            uint32_t rise_ms, uint32_t fall_ms) {
  if (raw != d->raw) {
    d->raw = raw;
    d->changed_ms = now_ms;
    return;
  }
  if (raw != d->stable) {
    uint32_t need = raw ? rise_ms : fall_ms;
    if ((uint32_t)(now_ms - d->changed_ms) >= need) {
      d->stable = raw;
    }
  }
}

/* ---------------- State helpers ------------------------------------------- */

static void enter_state(PiFsm *f, uint8_t state, uint32_t now_ms) {
  f->state = state;
  f->state_ms = now_ms;
  /* A press that began in a previous state may still count for the
     emergency hold, but its release is not a short-press command here. */
  f->press_short_ok = 0;
}

static void enter_standby(PiFsm *f, uint32_t now_ms) {
  enter_state(f, ST_STANDBY, now_ms);
  f->armed = 0;
}

static void enter_booting(PiFsm *f, uint32_t now_ms) {
  enter_state(f, ST_BOOTING, now_ms);
  f->armed = 0;
  /* SAFE_TO_OFF starts as "not proven LOW"; a fresh LOW must be qualified. */
  debounce_reset(&f->safe, 1, now_ms);
}

static uint32_t state_elapsed(const PiFsm *f, uint32_t now_ms) {
  return (uint32_t)(now_ms - f->state_ms);
}

/* ---------------- LED patterns -------------------------------------------- */

static uint8_t led_for_state(const PiFsm *f, uint32_t now_ms) {
  uint32_t t = state_elapsed(f, now_ms);

  switch (f->state) {
    case ST_STANDBY:
      return 0;
    case ST_BOOTING:
      return ((t / LED_SLOW_HALF_MS) & 1) == 0;
    case ST_RUNNING:
      return 1;
    case ST_SHUTTING_DOWN:
    case ST_POWER_OFF_SETTLE:
      return ((t / LED_FAST_HALF_MS) & 1) == 0;
    case ST_SHUTDOWN_TIMEOUT: {
      uint32_t p = t % LED_DOUBLE_PERIOD_MS;
      if (p < LED_DOUBLE_ON_MS) return 1;
      p -= LED_DOUBLE_ON_MS + LED_DOUBLE_GAP_MS;
      return p < LED_DOUBLE_ON_MS; /* unsigned wrap makes the gap read as 0 */
    }
    default:
      return 0;
  }
}

/* ---------------- Public API ---------------------------------------------- */

void pi_fsm_init(PiFsm *f, uint32_t now_ms) {
  debounce_reset(&f->button, 0, now_ms);
  debounce_reset(&f->safe, 1, now_ms);
  f->press_active = 0;
  f->press_ms = now_ms;
  /* A button held (or stuck) during ATtiny power-up must be released
     before it can do anything. */
  f->ignore_button = 1;
  enter_standby(f, now_ms);
  f->out_tps_en = 0;
  f->out_shutdown_req = 0;
  f->out_led = 0;
}

void pi_fsm_update(PiFsm *f, uint32_t now_ms, uint8_t button_pressed, uint8_t safe_high) {
  uint8_t short_press = 0;
  uint8_t new_press = 0;
  uint8_t emergency = 0;

  /* ---- Button events (debounced edges, hold time from debounced press) ---- */
  uint8_t was_pressed = f->button.stable;
  debounce_update(&f->button, button_pressed ? 1 : 0, now_ms,
                  BUTTON_DEBOUNCE_MS, BUTTON_DEBOUNCE_MS);

  if (!was_pressed && f->button.stable) {
    if (!f->ignore_button) {
      f->press_active = 1;
      f->press_short_ok = 1;
      f->press_ms = now_ms;
      new_press = 1;
    }
  } else if (was_pressed && !f->button.stable) {
    if (f->press_active && f->press_short_ok) {
      short_press = 1;
    }
    f->press_active = 0;
    f->press_short_ok = 0;
  }
  if (!f->button.stable) {
    f->ignore_button = 0;  /* Any debounced release ends the ignore period */
  }

  if (f->press_active && f->state != ST_STANDBY &&
      (uint32_t)(now_ms - f->press_ms) >= EMERGENCY_HOLD_MS) {
    emergency = 1;
  }

  /* ---- Emergency forced off: overrides every powered state ---- */
  if (emergency) {
    f->press_active = 0;
    f->ignore_button = 1;  /* Same hold must not power the Pi back on */
    enter_standby(f, now_ms);
  }

  /* ---- State machine ---- */
  switch (f->state) {
    case ST_STANDBY:
      if (new_press && !emergency) {
        if (state_elapsed(f, now_ms) >= MIN_OFF_TIME_MS) {
          enter_booting(f, now_ms);
          /* The power-on press is consumed; it can't start a hold. */
          f->press_active = 0;
        }
        f->ignore_button = 1;
      }
      break;

    case ST_BOOTING:
      /* Short presses ignored. SAFE_TO_OFF ignored (held in reset) until the
         protection period ends, then a fresh stable LOW must be seen. */
      if (state_elapsed(f, now_ms) < BOOT_PROTECT_MS) {
        debounce_reset(&f->safe, 1, now_ms);
      } else {
        debounce_update(&f->safe, safe_high ? 1 : 0, now_ms,
                        SAFE_HIGH_QUALIFY_MS, SAFE_LOW_QUALIFY_MS);
        if (!f->safe.stable) {
          f->armed = 1;
          enter_state(f, ST_RUNNING, now_ms);
        }
      }
      break;

    case ST_RUNNING:
      debounce_update(&f->safe, safe_high ? 1 : 0, now_ms,
                      SAFE_HIGH_QUALIFY_MS, SAFE_LOW_QUALIFY_MS);
      if (f->armed && f->safe.stable) {
        /* Pi shut itself down (e.g. Volumio menu) and gpio-poweroff fired. */
        enter_state(f, ST_POWER_OFF_SETTLE, now_ms);
      } else if (short_press) {
        enter_state(f, ST_SHUTTING_DOWN, now_ms);
      }
      break;

    case ST_SHUTTING_DOWN:
    case ST_SHUTDOWN_TIMEOUT:
      debounce_update(&f->safe, safe_high ? 1 : 0, now_ms,
                      SAFE_HIGH_QUALIFY_MS, SAFE_LOW_QUALIFY_MS);
      if (f->armed && f->safe.stable) {
        enter_state(f, ST_POWER_OFF_SETTLE, now_ms);
      } else if (f->state == ST_SHUTTING_DOWN) {
        /* Short presses ignored while waiting. */
        if (state_elapsed(f, now_ms) >= SHUTDOWN_TIMEOUT_MS) {
          enter_state(f, ST_SHUTDOWN_TIMEOUT, now_ms);
        }
      } else if (short_press) {
        /* Timeout: a short press re-sends the shutdown request. */
        enter_state(f, ST_SHUTTING_DOWN, now_ms);
      }
      break;

    case ST_POWER_OFF_SETTLE:
      if (state_elapsed(f, now_ms) >= POWER_OFF_SETTLE_MS) {
        enter_standby(f, now_ms);
      }
      break;

    default:
      /* Corrupted state: fail to standby (power off). */
      enter_standby(f, now_ms);
      break;
  }

  /* ---- Outputs from state ---- */
  f->out_tps_en = (f->state != ST_STANDBY);
  f->out_shutdown_req = (f->state == ST_SHUTTING_DOWN &&
                         state_elapsed(f, now_ms) < SHUTDOWN_PULSE_MS);
  f->out_led = led_for_state(f, now_ms);
}
