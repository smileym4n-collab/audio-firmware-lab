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
    // Disabled by default. Example "boxy" cut placeholder:
    // center_frequency_hz = 380.0f, gain_db = -3.0f, q = 1.0f
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
    (void)interleaved_stereo;
    (void)frame_count;

    // Pass-through for now.
    // Future work area: implement per-channel PEQ biquad here.
    // 1) Split interleaved samples into L/R (or process in-place by index).
    // 2) Apply optional PEQ when s_left_peq.enabled / s_right_peq.enabled.
    // 3) Add optional bass/treble shelves.
    // 4) Add optional mono-summed subwoofer low-pass path.
    // 5) Re-pack to interleaved stereo before returning.

    // Pass-through path intentionally does nothing and leaves samples unchanged.
}
