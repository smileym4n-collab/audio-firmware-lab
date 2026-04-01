/*
 * DSP module for Bluetooth PCM -> I2S pipeline.
 * Current behavior: pass-through (no filtering).
 *
 * Keep this module as the single place for future audio processing.
 */

#include "dsp.h"

static dsp_peq_settings_t s_left_peq;
static dsp_peq_settings_t s_right_peq;

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
    // Current behavior: bypass all DSP and return unchanged audio.
    // This keeps the path stable while making extension points explicit.
    (void)interleaved_stereo;
    (void)frame_count;

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

    // Pass-through path intentionally does nothing and leaves samples unchanged.
}
