/*
  Host tests for pi_power_fsm.c. No Raspberry Pi or ATtiny needed.

  The simulator steps the FSM once per simulated millisecond, like loop().
  Build and run:  make -C tests
*/
#include <stdio.h>
#include <stdint.h>
#include "../pi_power_fsm.h"
#include "../pi_power_config.h"

static PiFsm f;
static uint32_t now;
static uint8_t btn;   /* simulated button, 1 = pressed */
static uint8_t safe;  /* simulated GPIO22, 1 = HIGH    */
static uint32_t pulse_ms;     /* total ms PI_SHUTDOWN_REQ was active */
static uint32_t pulse_count;  /* rising edges on PI_SHUTDOWN_REQ     */
static uint32_t tps_off_edges;
static int failures;
static const char *current;

#define CHECK(cond) do { if (!(cond)) { failures++; \
  printf("  FAIL %s:%d [%s] %s (t=%lu state=%u)\n", __FILE__, __LINE__, \
         current, #cond, (unsigned long)now, f.state); } } while (0)

static void step(void) {
  uint8_t prev_req = f.out_shutdown_req;
  uint8_t prev_tps = f.out_tps_en;
  now++;
  pi_fsm_update(&f, now, btn, safe);
  if (f.out_shutdown_req) pulse_ms++;
  if (f.out_shutdown_req && !prev_req) pulse_count++;
  if (prev_tps && !f.out_tps_en) tps_off_edges++;
}

static void run(uint32_t ms) { while (ms--) step(); }

/* Run while asserting TPS_EN never drops. */
static void run_powered(uint32_t ms) {
  while (ms--) { step(); CHECK(f.out_tps_en); if (!f.out_tps_en) return; }
}

static void reset_sim(uint32_t start) {
  now = start; btn = 0; safe = 0;
  pulse_ms = pulse_count = tps_off_edges = 0;
  pi_fsm_init(&f, now);
}

static void press(uint32_t ms) { btn = 1; run(ms); btn = 0; run(100); }

/* Standby -> past min-off -> press -> booting -> armed -> running. */
static void boot_to_running(void) {
  run(MIN_OFF_TIME_MS);
  press(100);
  CHECK(f.state == ST_BOOTING);
  run_powered(BOOT_PROTECT_MS + SAFE_LOW_QUALIFY_MS + 10);
  CHECK(f.state == ST_RUNNING);
}

/* gpio-poweroff: HIGH 100 ms, LOW 100 ms, then HIGH and stays. */
static void pi_poweroff_signal(void) {
  safe = 1; run(100);
  safe = 0; run(100);
  safe = 1;
}

/* ------------------------------------------------------------------------ */

static void test_init_safe(void) {
  current = "init outputs safe";
  reset_sim(0);
  CHECK(f.state == ST_STANDBY);
  CHECK(!f.out_tps_en && !f.out_shutdown_req && !f.out_led);
  run(60000);
  CHECK(f.state == ST_STANDBY && !f.out_tps_en && !f.out_led);
}

static void test_button_held_at_startup(void) {
  current = "button held at ATtiny power-up";
  reset_sim(0);
  btn = 1; run(MIN_OFF_TIME_MS + 10000);
  CHECK(f.state == ST_STANDBY && !f.out_tps_en);
  btn = 0; run(100);
  press(100);
  CHECK(f.state == ST_BOOTING && f.out_tps_en);
}

static void test_normal_power_on(void) {
  current = "normal power on";
  reset_sim(0);
  run(MIN_OFF_TIME_MS);
  btn = 1; run(BUTTON_DEBOUNCE_MS - 1);
  CHECK(!f.out_tps_en);            /* not before debounce */
  run(2);
  CHECK(f.out_tps_en && f.state == ST_BOOTING);
  btn = 0;
  /* LED slow blink: both levels seen within a second */
  { int on = 0, off = 0, i;
    for (i = 0; i < 1000; i++) { step(); if (f.out_led) on++; else off++; }
    CHECK(on > 400 && off > 400); }
  CHECK(!f.out_shutdown_req);
}

static void test_min_off_lockout(void) {
  current = "min off time lockout";
  reset_sim(0);
  run(1000);
  press(100);
  CHECK(f.state == ST_STANDBY);    /* too soon */
  run(MIN_OFF_TIME_MS);
  press(100);
  CHECK(f.state == ST_BOOTING);
}

static void test_button_bounce(void) {
  current = "button bounce";
  reset_sim(0);
  run(MIN_OFF_TIME_MS);
  /* Glitches shorter than debounce do nothing */
  { int i; for (i = 0; i < 20; i++) { btn = 1; run(5); btn = 0; run(5); } }
  run(100);
  CHECK(f.state == ST_STANDBY);
  /* Bouncy press then held: exactly one power-on, no oscillation */
  { int i; for (i = 0; i < 6; i++) { btn = 1; run(3); btn = 0; run(2); } }
  btn = 1; run(200);
  /* Bouncy release */
  { int i; for (i = 0; i < 6; i++) { btn = 0; run(3); btn = 1; run(2); } }
  btn = 0; run(200);
  CHECK(f.state == ST_BOOTING);
  CHECK(tps_off_edges == 0);
}

static void test_boot_glitches(void) {
  current = "boot-time GPIO22 glitches";
  reset_sim(0);
  run(MIN_OFF_TIME_MS);
  press(100);
  /* GPIO22 HIGH and noisy for most of the protection window */
  safe = 1; run_powered(BOOT_PROTECT_MS - 3000);
  { int i; for (i = 0; i < 20; i++) { safe = i & 1; run_powered(50); } }
  safe = 1; run_powered(2000);
  CHECK(f.state == ST_BOOTING);
  /* Still HIGH after protection: never armed, never off */
  run_powered(60000);
  CHECK(f.state == ST_BOOTING && !f.armed);
  /* Now LOW, but shorter than the qualify time, then HIGH again */
  safe = 0; run_powered(SAFE_LOW_QUALIFY_MS - 100);
  safe = 1; run_powered(5000);
  CHECK(f.state == ST_BOOTING && !f.armed);
  /* Properly qualified LOW arms */
  safe = 0; run_powered(SAFE_LOW_QUALIFY_MS + 5);
  CHECK(f.state == ST_RUNNING && f.armed && f.out_led);
}

static void test_arm_timing(void) {
  current = "arming waits for protection + qualified LOW";
  reset_sim(0);
  run(MIN_OFF_TIME_MS);
  press(100);                      /* GPIO22 LOW the whole time */
  { uint32_t boot_ms = f.state_ms;
    run_powered(BOOT_PROTECT_MS + SAFE_LOW_QUALIFY_MS - 10 - (now - boot_ms));
    CHECK(f.state == ST_BOOTING);  /* LOW before protection end not counted */
    run_powered(20);
    CHECK(f.state == ST_RUNNING); }
}

static void test_short_presses_during_boot(void) {
  current = "short presses during boot ignored";
  reset_sim(0);
  run(MIN_OFF_TIME_MS);
  press(100);
  { int i; for (i = 0; i < 10; i++) { btn = 1; run_powered(200); btn = 0; run_powered(300); } }
  CHECK(f.state == ST_BOOTING && pulse_count == 0);
}

static void test_power_on_hold_not_emergency(void) {
  current = "long power-on press does not force off";
  reset_sim(0);
  run(MIN_OFF_TIME_MS);
  btn = 1;
  run(BUTTON_DEBOUNCE_MS + 1);
  CHECK(f.out_tps_en);
  run_powered(EMERGENCY_HOLD_MS + 5000);
  btn = 0; run_powered(100);
  CHECK(f.state == ST_BOOTING);
}

static void test_normal_shutdown(void) {
  current = "normal shutdown";
  reset_sim(0);
  boot_to_running();
  press(150);
  CHECK(f.state == ST_SHUTTING_DOWN);
  run_powered(SHUTDOWN_PULSE_MS + 100);
  CHECK(pulse_count == 1);
  CHECK(pulse_ms >= SHUTDOWN_PULSE_MS - 1 && pulse_ms <= SHUTDOWN_PULSE_MS);
  CHECK(!f.out_shutdown_req);
  /* Extra presses while waiting do not re-send or cut power */
  press(100); press(100);
  CHECK(pulse_count == 1 && f.state == ST_SHUTTING_DOWN);
  /* Pi takes 20 s to shut down */
  run_powered(20000);
  pi_poweroff_signal();
  run_powered(SAFE_HIGH_QUALIFY_MS + POWER_OFF_SETTLE_MS - 10);
  run(20);
  CHECK(f.state == ST_STANDBY && !f.out_tps_en && !f.out_led);
  CHECK(tps_off_edges == 1);
}

static void test_short_high_rejected(void) {
  current = "short GPIO22 HIGH while running rejected";
  reset_sim(0);
  boot_to_running();
  safe = 1; run_powered(SAFE_HIGH_QUALIFY_MS - 10);
  safe = 0; run_powered(5000);
  CHECK(f.state == ST_RUNNING);
  /* Repeated 100 ms pulses never qualify */
  { int i; for (i = 0; i < 50; i++) { safe = 1; run_powered(100); safe = 0; run_powered(100); } }
  CHECK(f.state == ST_RUNNING);
}

static void test_external_shutdown(void) {
  current = "shutdown initiated by Volumio";
  reset_sim(0);
  boot_to_running();
  run_powered(60000);
  pi_poweroff_signal();
  run_powered(SAFE_HIGH_QUALIFY_MS + POWER_OFF_SETTLE_MS - 10);
  run(20);
  CHECK(f.state == ST_STANDBY && !f.out_tps_en);
  CHECK(pulse_count == 0);  /* ATtiny never asked */
}

static void test_shutdown_timeout(void) {
  current = "shutdown signal never received";
  reset_sim(0);
  boot_to_running();
  press(100);
  run_powered(SHUTDOWN_TIMEOUT_MS + 10);
  CHECK(f.state == ST_SHUTDOWN_TIMEOUT);
  /* Double blink: count rising LED edges over one period */
  { int i, edges = 0; uint8_t prev = f.out_led;
    for (i = 0; i < (int)LED_DOUBLE_PERIOD_MS; i++) {
      step(); if (f.out_led && !prev) edges++; prev = f.out_led; }
    CHECK(edges == 2); }
  /* Power stays on indefinitely */
  run_powered(30UL * 60UL * 1000UL);
  CHECK(f.state == ST_SHUTDOWN_TIMEOUT);
  /* Short press retries the request */
  press(100);
  CHECK(f.state == ST_SHUTTING_DOWN && pulse_count == 2);
  /* Late completion is still honoured */
  run_powered(5000);
  safe = 1; run_powered(SAFE_HIGH_QUALIFY_MS + POWER_OFF_SETTLE_MS - 10);
  run(20);
  CHECK(f.state == ST_STANDBY && !f.out_tps_en);
}

static void test_emergency(void) {
  current = "emergency forced off";
  reset_sim(0);
  boot_to_running();
  btn = 1;
  run_powered(BUTTON_DEBOUNCE_MS + EMERGENCY_HOLD_MS - 5);
  CHECK(f.state == ST_RUNNING);   /* no graceful request during the hold */
  run(10);
  CHECK(f.state == ST_STANDBY && !f.out_tps_en);
  CHECK(pulse_count == 0);
  /* Keep holding: must not power back on */
  run(20000);
  CHECK(!f.out_tps_en);
  btn = 0; run(MIN_OFF_TIME_MS);
  press(100);
  CHECK(f.state == ST_BOOTING);

  current = "emergency from shutdown timeout";
  reset_sim(0);
  boot_to_running();
  press(100);
  run_powered(SHUTDOWN_TIMEOUT_MS + 10);
  btn = 1; run(BUTTON_DEBOUNCE_MS + EMERGENCY_HOLD_MS + 5);
  CHECK(!f.out_tps_en);

  current = "7 s hold = graceful, not forced";
  reset_sim(0);
  boot_to_running();
  btn = 1; run_powered(7000); btn = 0; run_powered(100);
  CHECK(f.state == ST_SHUTTING_DOWN && pulse_count == 1);
}

static void test_reset_behaviour(void) {
  current = "ATtiny reset while Pi powered";
  reset_sim(0);
  boot_to_running();
  /* A reset re-runs setup(): models WDT/BOR/UPDI reset */
  pi_fsm_init(&f, now);
  step();
  CHECK(!f.out_tps_en && f.state == ST_STANDBY);
  /* Corrupted state value fails safe */
  boot_to_running();
  f.state = 0x55;
  step();
  CHECK(!f.out_tps_en && f.state == ST_STANDBY);
}

static void test_repeated_cycles(void) {
  int i;
  current = "repeated power cycles";
  reset_sim(0);
  for (i = 0; i < 50; i++) {
    boot_to_running();
    if (i & 1) { press(100); run_powered(3000); }
    pi_poweroff_signal();
    run(SAFE_HIGH_QUALIFY_MS + POWER_OFF_SETTLE_MS + 10);
    CHECK(f.state == ST_STANDBY);
    safe = 0;
  }
  CHECK(tps_off_edges == 50);
}

static void test_millis_wrap(void) {
  current = "millis() wrap";
  reset_sim(0xFFFFFFFFUL - 10000UL);
  boot_to_running();             /* spans the wrap */
  CHECK(now < 0x80000000UL);
  press(100);
  run_powered(1000);
  safe = 1; run_powered(SAFE_HIGH_QUALIFY_MS + 10);
  run(POWER_OFF_SETTLE_MS + 10);
  CHECK(f.state == ST_STANDBY);
}

int main(void) {
  test_init_safe();
  test_button_held_at_startup();
  test_normal_power_on();
  test_min_off_lockout();
  test_button_bounce();
  test_boot_glitches();
  test_arm_timing();
  test_short_presses_during_boot();
  test_power_on_hold_not_emergency();
  test_normal_shutdown();
  test_short_high_rejected();
  test_external_shutdown();
  test_shutdown_timeout();
  test_emergency();
  test_reset_behaviour();
  test_repeated_cycles();
  test_millis_wrap();

  if (failures) { printf("%d check(s) FAILED\n", failures); return 1; }
  printf("All tests passed.\n");
  return 0;
}
