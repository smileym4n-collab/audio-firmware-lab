#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    float center_frequency_hz;
    float gain_db;
    float q;
} dsp_peq_settings_t;

void dsp_init(void);
void dsp_set_peq(const dsp_peq_settings_t *left, const dsp_peq_settings_t *right);
void dsp_set_output_cap_percent(uint8_t cap_percent);
uint8_t dsp_get_output_cap_percent(void);
void dsp_process_stereo_int16(int16_t *interleaved_stereo, uint32_t frame_count);

#ifdef __cplusplus
}
#endif
