#include "input_switch_fsm.h"
#include "input_switch_config.h"
#include <string.h>

static uint8_t elapsed(uint32_t now, uint32_t since, uint32_t duration) {
    return (uint32_t)(now - since) >= duration;
}

static void clear_audio(InputSwitch *s) {
    s->audio_tracking = 0;
    s->audio_qualified = 0;
}

static void blank_audio(InputSwitch *s, uint32_t now) {
    clear_audio(s);
    s->blanking = 1;
    s->blank_since = now;
}

void input_switch_init(InputSwitch *s, uint32_t now, uint8_t toggle_av) {
    memset(s, 0, sizeof(*s));
    s->toggle_candidate = !!toggle_av;
    s->toggle_since = s->power_since = s->last_tick = now;
    s->power_state = IS_STARTUP;
    s->out_mute = 1;
}

static void update_source(InputSwitch *s, uint32_t now) {
    switch (s->source_state) {
    case IS_SOURCE_IDLE:
        if (s->toggle_ready && s->toggle_av != s->out_source_av) {
            s->source_state = IS_SOURCE_PRE_MUTE;
            s->source_since = now;
            blank_audio(s, now);
        }
        break;
    case IS_SOURCE_PRE_MUTE:
        if (elapsed(now, s->source_since, IS_SOURCE_PRE_MUTE_MS)) {
            if (s->toggle_av != s->out_source_av) {
                s->out_source_av = s->toggle_av;
                s->source_state = IS_SOURCE_SETTLING;
                s->source_since = now;
            } else {
                /* Request returned before the relay command changed. */
                s->source_state = IS_SOURCE_IDLE;
            }
            blank_audio(s, now);
        }
        break;
    case IS_SOURCE_SETTLING:
        if (elapsed(now, s->source_since, IS_SOURCE_SETTLE_MS)) {
            /* A new debounced request gets its own mute/settle sequence. */
            s->source_state = s->toggle_av == s->out_source_av
                           ? IS_SOURCE_IDLE : IS_SOURCE_PRE_MUTE;
            s->source_since = now;
            blank_audio(s, now);
            if (s->power_state == IS_RUNNING)
                s->last_activity = now; /* New source gets a full window. */
        }
        break;
    default:
        /* Recovery will also reset the power state in update(). */
        break;
    }
}

static uint8_t update_audio(InputSwitch *s, uint32_t now, uint8_t audio_low) {
    if (s->source_state != IS_SOURCE_IDLE || !s->toggle_ready ||
        (s->power_state != IS_STANDBY && s->power_state != IS_RUNNING)) {
        clear_audio(s);
        return 0;
    }
    if (s->blanking) {
        if (!elapsed(now, s->blank_since, IS_SOURCE_BLANK_MS)) {
            clear_audio(s);
            return 0;
        }
        s->blanking = 0;
    }
    if (s->audio_tracking &&
        (uint32_t)(now - s->last_low) > IS_AUDIO_MAX_GAP_MS)
        clear_audio(s);

    if (!audio_low) return 0;
    if (!s->audio_tracking) {
        s->audio_tracking = 1;
        s->audio_since = now;
    }
    s->last_low = now;
    if (elapsed(now, s->audio_since, IS_AUDIO_QUALIFY_MS))
        s->audio_qualified = 1;
    return s->audio_qualified;
}

void input_switch_update(InputSwitch *s, uint32_t now,
                         uint8_t toggle_av, uint8_t audio_low) {
    uint8_t audio_valid;
    if (s->power_state > IS_LOCKOUT || s->source_state > IS_SOURCE_SETTLING) {
        input_switch_init(s, now, toggle_av);
        return;
    }
    if ((uint32_t)(now - s->last_tick) > IS_MAX_SAMPLE_GAP_MS)
        clear_audio(s);
    s->last_tick = now;

    toggle_av = !!toggle_av;
    if (toggle_av != s->toggle_candidate) {
        s->toggle_candidate = toggle_av;
        s->toggle_since = now;
    }
    if (elapsed(now, s->toggle_since, IS_TOGGLE_DEBOUNCE_MS)) {
        s->toggle_av = s->toggle_candidate;
        s->toggle_ready = 1;
    }
    update_source(s, now);
    audio_valid = update_audio(s, now, audio_low);

    switch (s->power_state) {
    case IS_STARTUP:
        if (elapsed(now, s->power_since, IS_STARTUP_BLANK_MS) &&
            s->toggle_ready && s->source_state == IS_SOURCE_IDLE) {
            s->power_state = IS_STANDBY;
            clear_audio(s);
        }
        break;
    case IS_STANDBY:
        if (audio_valid) {
            s->power_state = IS_WARMUP;
            s->power_since = now;
            clear_audio(s);
        }
        break;
    case IS_WARMUP:
        if (elapsed(now, s->power_since, IS_POWER_WARMUP_MS)) {
            s->power_state = IS_RUNNING;
            s->last_activity = now;
            clear_audio(s);
        }
        break;
    case IS_RUNNING:
        if (audio_valid) s->last_activity = now;
        if (s->source_state == IS_SOURCE_IDLE &&
            elapsed(now, s->last_activity, IS_SILENCE_TIMEOUT_MS)) {
            s->power_state = IS_STOPPING;
            s->power_since = now;
            clear_audio(s);
        }
        break;
    case IS_STOPPING:
        /* Finish a shutdown once committed; transients cannot cancel it. */
        if (elapsed(now, s->power_since, IS_POWER_OFF_MUTE_MS)) {
            s->power_state = IS_LOCKOUT;
            s->power_since = now;
            clear_audio(s);
        }
        break;
    case IS_LOCKOUT:
        if (elapsed(now, s->power_since, IS_POST_OFF_LOCKOUT_MS)) {
            s->power_state = IS_STANDBY;
            clear_audio(s);
        }
        break;
    default: break;
    }
    s->out_power = s->power_state == IS_WARMUP ||
                   s->power_state == IS_RUNNING || s->power_state == IS_STOPPING;
    s->out_mute = s->power_state != IS_RUNNING ||
                  s->source_state != IS_SOURCE_IDLE;
}
