#ifndef INPUT_SWITCH_CONFIG_H
#define INPUT_SWITCH_CONFIG_H

/* All durations are milliseconds. Defaults require bench confirmation. */
#define IS_TOGGLE_DEBOUNCE_MS       20UL
#define IS_STARTUP_BLANK_MS       2000UL
#define IS_SOURCE_PRE_MUTE_MS       75UL
#define IS_SOURCE_SETTLE_MS        150UL
#define IS_SOURCE_BLANK_MS        1000UL
#define IS_AUDIO_QUALIFY_MS        500UL
/* The hardware already stretches audio pulses with a nominal 103 ms RC.
   Permit brief HIGH gaps, but only qualify/refresh activity on LOW samples. */
#define IS_AUDIO_MAX_GAP_MS         50UL
/* A delayed main loop must not turn an old LOW sample into sustained audio. */
#define IS_MAX_SAMPLE_GAP_MS        20UL
#define IS_POWER_WARMUP_MS        2000UL
#define IS_SILENCE_TIMEOUT_MS   600000UL
#define IS_POWER_OFF_MUTE_MS       200UL
#define IS_POST_OFF_LOCKOUT_MS   10000UL

#if IS_TOGGLE_DEBOUNCE_MS < 20 || IS_SOURCE_PRE_MUTE_MS < 1 || IS_SOURCE_SETTLE_MS < 1
#error "Keep toggle debounce >=20 ms and relay sequencing delays nonzero."
#endif
#if IS_AUDIO_QUALIFY_MS <= IS_AUDIO_MAX_GAP_MS || IS_MAX_SAMPLE_GAP_MS < 1
#error "Invalid audio qualification timing."
#endif
#if IS_STARTUP_BLANK_MS >= 0x80000000UL || IS_SOURCE_BLANK_MS >= 0x80000000UL || \
    IS_AUDIO_QUALIFY_MS >= 0x80000000UL || IS_POWER_WARMUP_MS >= 0x80000000UL || \
    IS_SILENCE_TIMEOUT_MS >= 0x80000000UL || IS_POWER_OFF_MUTE_MS >= 0x80000000UL || \
    IS_POST_OFF_LOCKOUT_MS >= 0x80000000UL
#error "Durations must be below half the uint32_t millis range."
#endif

#endif
