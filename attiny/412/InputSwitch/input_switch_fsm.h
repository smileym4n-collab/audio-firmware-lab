#ifndef INPUT_SWITCH_FSM_H
#define INPUT_SWITCH_FSM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum IsPowerState {
    IS_STARTUP, IS_STANDBY, IS_WARMUP, IS_RUNNING, IS_STOPPING, IS_LOCKOUT
};
enum IsSourceState { IS_SOURCE_IDLE, IS_SOURCE_PRE_MUTE, IS_SOURCE_SETTLING };

typedef struct {
    uint32_t toggle_since, source_since, power_since, blank_since;
    uint32_t audio_since, last_low, last_activity, last_tick;
    uint8_t toggle_candidate, toggle_av, toggle_ready;
    uint8_t source_state, power_state, blanking;
    uint8_t audio_tracking, audio_qualified;
    uint8_t out_source_av, out_mute, out_power;
} InputSwitch;

/* Inputs: toggle_av=1 for AV; audio_low=1 for a LOW AUDIO_PRESENT_N pin. */
void input_switch_init(InputSwitch *s, uint32_t now, uint8_t toggle_av);
void input_switch_update(InputSwitch *s, uint32_t now,
                         uint8_t toggle_av, uint8_t audio_low);

#ifdef __cplusplus
}
#endif
#endif
