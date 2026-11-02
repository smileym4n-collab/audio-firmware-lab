#include "../input_switch_fsm.h"
#include "../input_switch_config.h"
#include <assert.h>
#include <stdio.h>

static InputSwitch s;
static uint32_t now;
static unsigned scenarios;

static void tick(uint8_t av, uint8_t low) {
    uint8_t old_source = s.out_source_av;
    uint8_t old_power = s.out_power;
    uint8_t old_mute = s.out_mute;
    ++now;
    input_switch_update(&s, now, av, low);
    /* Observable safety properties, checked on every simulated millisecond. */
    assert(s.out_mute || (s.out_power && s.power_state == IS_RUNNING));
    if (s.out_source_av != old_source) assert(old_mute && s.out_mute);
    if (s.out_power != old_power) assert(old_mute && s.out_mute);
    if (s.source_state != IS_SOURCE_IDLE) assert(s.out_mute);
}

static void run(uint32_t ms, uint8_t av, uint8_t low) {
    while (ms--) tick(av, low);
}

static void boot(uint32_t start, uint8_t av) {
    now = start;
    input_switch_init(&s, now, av);
    assert(!s.out_power && s.out_mute && !s.out_source_av);
    run(IS_STARTUP_BLANK_MS + IS_SOURCE_PRE_MUTE_MS + IS_SOURCE_SETTLE_MS +
        IS_SOURCE_BLANK_MS + IS_TOGGLE_DEBOUNCE_MS + 2, av, 0);
    assert(s.power_state == IS_STANDBY && s.out_source_av == av);
}

static void wake(uint8_t av) {
    run(IS_AUDIO_QUALIFY_MS + 1, av, 1);
    assert(s.out_power && s.out_mute && s.power_state == IS_WARMUP);
    run(IS_POWER_WARMUP_MS - 1, av, 1);
    assert(s.out_mute);
    tick(av, 1);
    assert(s.out_power && !s.out_mute && s.power_state == IS_RUNNING);
}

static void shutdown(uint8_t av) {
    const uint32_t already_silent = (uint32_t)(now - s.last_activity);
    assert(already_silent < IS_SILENCE_TIMEOUT_MS);
    run(IS_SILENCE_TIMEOUT_MS - already_silent - 1, av, 0);
    assert(s.out_power && !s.out_mute);
    tick(av, 0);
    assert(s.out_power && s.out_mute && s.power_state == IS_STOPPING);
    run(IS_POWER_OFF_MUTE_MS - 1, av, 0);
    assert(s.out_power && s.out_mute);
    tick(av, 0);
    assert(!s.out_power && s.out_mute && s.power_state == IS_LOCKOUT);
}

static void test_startup_and_polarity(void) {
    for (uint8_t av = 0; av < 2; ++av) {
        now = 0;
        input_switch_init(&s, now, av);
        run(IS_STARTUP_BLANK_MS - 1, av, 1);
        assert(!s.out_power && s.out_mute && s.out_source_av == av);
        tick(av, 1);
        assert(!s.out_power);
        run(IS_AUDIO_QUALIFY_MS, av, 1);
        assert(!s.out_power);
        tick(av, 1);
        assert(s.out_power && s.out_mute);
    }
    ++scenarios;
}

static void test_silence_and_clicks(void) {
    boot(0, 0);
    run(2 * IS_SILENCE_TIMEOUT_MS, 0, 0);
    assert(!s.out_power);
    for (unsigned i = 0; i < 10; ++i) {
        run(120, 0, 1); /* A single click stretched by the hardware envelope. */
        run(400, 0, 0);
    }
    assert(!s.out_power);
    ++scenarios;
}

static void test_audio_gaps(void) {
    boot(0, 0);
    /* Repeated recent activity, with permitted brief gaps. */
    for (unsigned i = 0; i < 6; ++i) {
        run(80, 0, 1);
        run(20, 0, 0);
    }
    assert(s.out_power && s.out_mute);
    boot(0, 0);
    run(IS_AUDIO_QUALIFY_MS - 100, 0, 1);
    run(IS_AUDIO_MAX_GAP_MS + 1, 0, 0);
    run(200, 0, 1);
    assert(!s.out_power); /* Gap discarded the earlier partial qualification. */
    ++scenarios;
}

static void test_debounce_and_source_timing(void) {
    boot(0, 0);
    wake(0);
    for (unsigned i = 0; i < 5; ++i) { run(5, 1, 0); run(5, 0, 0); }
    assert(!s.out_source_av && !s.out_mute);
    tick(1, 0);
    run(IS_TOGGLE_DEBOUNCE_MS - 1, 1, 0);
    assert(!s.out_mute);
    tick(1, 0);
    assert(s.out_mute && !s.out_source_av && s.out_power);
    run(IS_SOURCE_PRE_MUTE_MS - 1, 1, 1);
    assert(!s.out_source_av);
    tick(1, 1);
    assert(s.out_source_av && s.out_mute);
    run(IS_SOURCE_SETTLE_MS - 1, 1, 1);
    assert(s.out_mute);
    tick(1, 1);
    assert(!s.out_mute && s.out_power && s.out_source_av);
    ++scenarios;
}

static void test_rapid_requests(void) {
    boot(0, 0);
    wake(0);
    run(IS_TOGGLE_DEBOUNCE_MS + 1, 1, 0);
    assert(s.out_mute);
    run(IS_TOGGLE_DEBOUNCE_MS + 1, 0, 0); /* Cancel before command changes. */
    run(IS_SOURCE_PRE_MUTE_MS, 0, 0);
    assert(!s.out_source_av && !s.out_mute && s.out_power);
    run(IS_TOGGLE_DEBOUNCE_MS + IS_SOURCE_PRE_MUTE_MS + 1, 1, 0);
    assert(s.out_source_av && s.out_mute);
    run(IS_TOGGLE_DEBOUNCE_MS + 1, 0, 0); /* Reverse during relay settling. */
    assert(s.out_mute);
    run(IS_SOURCE_SETTLE_MS + IS_SOURCE_PRE_MUTE_MS + IS_SOURCE_SETTLE_MS, 0, 0);
    assert(!s.out_source_av && !s.out_mute && s.out_power);
    ++scenarios;
}

static void test_source_blanking_in_standby(void) {
    boot(0, 0);
    run(IS_AUDIO_QUALIFY_MS - 100, 0, 1);
    run(IS_TOGGLE_DEBOUNCE_MS + IS_SOURCE_PRE_MUTE_MS +
        IS_SOURCE_SETTLE_MS + 1, 1, 1);
    assert(s.out_source_av && !s.out_power);
    run(IS_SOURCE_BLANK_MS - 1, 1, 1);
    assert(!s.out_power);
    tick(1, 1); /* First accepted LOW; no carried qualification. */
    run(IS_AUDIO_QUALIFY_MS - 1, 1, 1);
    assert(!s.out_power);
    tick(1, 1);
    assert(s.out_power && s.out_mute);
    ++scenarios;
}

static void test_switch_during_warmup_and_shutdown(void) {
    boot(0, 0);
    run(IS_AUDIO_QUALIFY_MS + 1, 0, 1);
    run(IS_POWER_WARMUP_MS - 40, 0, 0);
    run(IS_TOGGLE_DEBOUNCE_MS + 1, 1, 0);
    assert(s.out_mute && s.out_power);
    run(40, 1, 0); /* Warmup completes during source change: stay muted. */
    assert(s.power_state == IS_RUNNING && s.out_mute);
    run(IS_SOURCE_PRE_MUTE_MS + IS_SOURCE_SETTLE_MS, 1, 0);
    assert(s.out_source_av && s.out_power && !s.out_mute);
    shutdown(1); /* Exactly a fresh silence window from source settling. */
    run(IS_TOGGLE_DEBOUNCE_MS + IS_SOURCE_PRE_MUTE_MS + IS_SOURCE_SETTLE_MS + 1, 0, 1);
    assert(!s.out_power && !s.out_source_av && s.out_mute);
    ++scenarios;
}

static void test_timeout_and_lockout(void) {
    boot(0, 1);
    wake(1);
    shutdown(1);
    assert(s.out_source_av); /* AV/sub-trigger remains selected when off. */
    run(IS_POST_OFF_LOCKOUT_MS - 1, 1, 1);
    assert(!s.out_power && s.out_mute);
    tick(1, 1);
    assert(s.power_state == IS_STANDBY && !s.out_power);
    run(IS_AUDIO_QUALIFY_MS, 1, 1);
    assert(!s.out_power);
    tick(1, 1);
    assert(s.out_power && s.out_mute);
    ++scenarios;
}

static void test_source_change_during_committed_shutdown(void) {
    boot(0, 0);
    wake(0);
    run(IS_SILENCE_TIMEOUT_MS, 0, 0);
    assert(s.power_state == IS_STOPPING && s.out_power && s.out_mute);
    run(IS_TOGGLE_DEBOUNCE_MS + IS_SOURCE_PRE_MUTE_MS + 1, 1, 1);
    assert(s.out_source_av && s.out_power && s.out_mute);
    run(IS_POWER_OFF_MUTE_MS - IS_TOGGLE_DEBOUNCE_MS - IS_SOURCE_PRE_MUTE_MS - 1, 1, 1);
    assert(s.power_state == IS_LOCKOUT && !s.out_power && s.out_mute);
    run(IS_SOURCE_SETTLE_MS, 1, 1);
    assert(s.out_source_av && !s.out_power && s.out_mute);
    ++scenarios;
}

static void test_short_activity_does_not_refresh_timeout(void) {
    boot(0, 0);
    wake(0);
    run(IS_SILENCE_TIMEOUT_MS - 1000, 0, 0);
    run(120, 0, 1);
    run(880, 0, 0);
    assert(s.power_state == IS_STOPPING && s.out_mute && s.out_power);
    /* Even sustained new audio cannot interrupt an already committed stop. */
    run(IS_POWER_OFF_MUTE_MS, 0, 1);
    assert(s.power_state == IS_LOCKOUT && !s.out_power);
    ++scenarios;
}

static void test_valid_audio_refreshes_timeout(void) {
    boot(0, 0);
    wake(0);
    run(IS_SILENCE_TIMEOUT_MS - 2000, 0, 0);
    run(IS_AUDIO_QUALIFY_MS + 1, 0, 1);
    shutdown(0);
    ++scenarios;
}

static void test_new_source_gets_full_timeout(void) {
    boot(0, 0);
    wake(0);
    run(IS_SILENCE_TIMEOUT_MS - 500, 0, 0);
    run(IS_TOGGLE_DEBOUNCE_MS + IS_SOURCE_PRE_MUTE_MS + IS_SOURCE_SETTLE_MS + 1, 1, 0);
    assert(s.out_power && !s.out_mute && s.out_source_av);
    shutdown(1);
    ++scenarios;
}

static void test_delayed_poll_cannot_qualify(void) {
    boot(0, 0);
    tick(0, 1);
    now += IS_AUDIO_QUALIFY_MS + 10;
    input_switch_update(&s, now, 0, 1);
    assert(!s.out_power && !s.audio_qualified);
    ++scenarios;
}

static void test_reset_and_invalid_state(void) {
    boot(0, 1);
    wake(1);
    input_switch_init(&s, now, 1);
    assert(!s.out_power && s.out_mute && !s.out_source_av);
    boot(0, 1);
    wake(1);
    s.power_state = 255;
    input_switch_update(&s, ++now, 1, 1);
    assert(!s.out_power && s.out_mute && !s.out_source_av);
    s.source_state = 255;
    input_switch_update(&s, ++now, 1, 1);
    assert(!s.out_power && s.out_mute);
    ++scenarios;
}

static void test_wrap_and_repeated_cycles(void) {
    boot(UINT32_MAX - IS_STARTUP_BLANK_MS / 2, 1);
    wake(1);
    shutdown(1);
    for (unsigned i = 0; i < 10; ++i) {
        run(IS_POST_OFF_LOCKOUT_MS + 1, 1, 0);
        wake(1);
        shutdown(1);
    }
    /* Warm-up itself straddles wrap, with the power edge already issued. */
    boot(0, 0);
    run(IS_AUDIO_QUALIFY_MS + 1, 0, 1);
    now = UINT32_MAX - 100;
    s.last_tick = s.power_since = now;
    run(IS_POWER_WARMUP_MS - 1, 0, 0);
    assert(s.out_power && s.out_mute);
    tick(0, 0);
    assert(s.out_power && !s.out_mute);
    /* Exercise the source sequence across wrap while already running. */
    now = UINT32_MAX - 100;
    s.last_activity = now;
    s.last_tick = now;
    run(IS_TOGGLE_DEBOUNCE_MS + IS_SOURCE_PRE_MUTE_MS + IS_SOURCE_SETTLE_MS + 1, 1, 0);
    assert(s.out_source_av && !s.out_mute && s.out_power);
    /* Cross wrap during qualification, silence and lockout. */
    boot(0, 0);
    now = UINT32_MAX - 200;
    s.last_tick = now;
    wake(0);
    now = UINT32_MAX - 100;
    s.last_tick = s.last_activity = now;
    shutdown(0);
    now = UINT32_MAX - 100;
    s.last_tick = s.power_since = now;
    run(IS_POST_OFF_LOCKOUT_MS + 1, 0, 0);
    assert(s.power_state == IS_STANDBY && !s.out_power);
    ++scenarios;
}

int main(void) {
    test_startup_and_polarity();
    test_silence_and_clicks();
    test_audio_gaps();
    test_debounce_and_source_timing();
    test_rapid_requests();
    test_source_blanking_in_standby();
    test_switch_during_warmup_and_shutdown();
    test_timeout_and_lockout();
    test_source_change_during_committed_shutdown();
    test_short_activity_does_not_refresh_timeout();
    test_valid_audio_refreshes_timeout();
    test_new_source_gets_full_timeout();
    test_delayed_poll_cannot_qualify();
    test_reset_and_invalid_state();
    test_wrap_and_repeated_cycles();
    printf("PASS: %u scenarios; output invariants checked every simulated tick.\n", scenarios);
    return 0;
}
