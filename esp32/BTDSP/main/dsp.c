/*
 * DSP module for Bluetooth PCM -> I2S pipeline.
 * Current behavior: pass-through (no filtering).
 *
 * Keep this module as the single place for future audio processing.
 */

#include "dsp.h"

#include <limits.h>

static dsp_peq_settings_t s_left_peq;
static dsp_peq_settings_t s_right_peq;
static uint8_t s_output_cap_percent = 100;

static int16_t clamp_int16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

void dsp_init(void)
{
    // Default PEQ placeholder (disabled).
    // FUTURE "LESS BOXY" STARTING POINT:
    //   center_frequency_hz = 380.0f
    //   gain_db             = -3.0f
    //   q                   = 1.0f
    s_left_peq = (dsp_peq_settings_t){
        .enabled = false,
        .center_frequency_hz = 380.0f,
        .gain_db = -3.0f,
        .q = 1.0f,
    };
    s_right_peq = s_left_peq;
}

void dsp_set_peq(const dsp_peq_settings_t *left, const dsp_peq_settings_t *right)
{
    if (left != NULL) {
        s_left_peq = *left;
    }
    if (right != NULL) {
        s_right_peq = *right;
    }
}

void dsp_set_output_cap_percent(uint8_t cap_percent)
{
    if (cap_percent > 100U) {
        cap_percent = 100U;
    }
    s_output_cap_percent = cap_percent;
}

uint8_t dsp_get_output_cap_percent(void)
{
    return s_output_cap_percent;
}

void dsp_process_stereo_int16(int16_t *interleaved_stereo, uint32_t frame_count)
{
    // Audio buffer format:
    //   interleaved_stereo[0] = Left  sample for frame 0
    //   interleaved_stereo[1] = Right sample for frame 0
    //   interleaved_stereo[2] = Left  sample for frame 1
    //   interleaved_stereo[3] = Right sample for frame 1
    //   ...
    //
    // `frame_count` is the number of stereo frames.
    //
    // Current behavior:
    // - apply output volume cap (0..100%)
    // - no PEQ/shelves/sub filtering yet
    //
    // This keeps the signal path simple while adding clipping protection.
    uint32_t sample_count = frame_count * 2U;
    for (uint32_t i = 0; i < sample_count; ++i) {
        int32_t scaled = ((int32_t)interleaved_stereo[i] * (int32_t)s_output_cap_percent) / 100;
        interleaved_stereo[i] = clamp_int16(scaled);
    }

    // ================= FUTURE DSP IMPLEMENTATION AREA =================
    // 1) PEQ biquad (per channel):
    //    - If s_left_peq.enabled, process left samples.
    //    - If s_right_peq.enabled, process right samples.
    //    - "Less boxy" first experiment:
    //         center_frequency_hz: 350-400
    //         gain_db:             about -3.0
    //         q:                   about 1.0
    //
    // 2) Bass / treble shelves:
    //    - Add optional low-shelf and high-shelf stages after PEQ.
    //
    // 3) Subwoofer low-pass:
    //    - Optional mono sum (L+R)/2, then low-pass for sub out path.
    // ==================================================================

    // After the cap, audio is still effectively pass-through until new filters are added.
}
