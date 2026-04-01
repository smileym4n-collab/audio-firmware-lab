/*
 * BTDSP: ESP32 A2DP sink with I2S output and DSP staging.
 * Target: ESP32-WROOM-32UE-N4CT (ESP-IDF)
 *
 * Pin map (I2S to external DAC, no MCLK):
 * - BCK  (bit clock): GPIO26
 * - LRCK (word select): GPIO25
 * - DATA (serial data out): GPIO13
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dsp.h"

#include "driver/i2s.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#define TAG "BTDSP"

#define I2S_PORT I2S_NUM_0
#define I2S_BCK_GPIO GPIO_NUM_26
#define I2S_LRCK_GPIO GPIO_NUM_25
#define I2S_DOUT_GPIO GPIO_NUM_13

#define DEVICE_NAME "ESP32-BTDSP"

static uint32_t s_a2dp_sample_rate = 44100;
static bool s_logged_dsp_mode = false;
static uint8_t s_pcm_work_buffer[4096];

static void i2s_reconfigure_for_rate(uint32_t sample_rate_hz)
{
    i2s_set_clk(I2S_PORT, sample_rate_hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    ESP_LOGI(TAG, "I2S started/reconfigured: %lu Hz, 16-bit, stereo", (unsigned long)sample_rate_hz);
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    if (len > sizeof(s_pcm_work_buffer)) {
        ESP_LOGW(TAG, "PCM frame too large (%lu), truncating to %u bytes",
                 (unsigned long)len, (unsigned int)sizeof(s_pcm_work_buffer));
        len = sizeof(s_pcm_work_buffer);
    }

    memcpy(s_pcm_work_buffer, data, len);

    // A2DP sink PCM from ESP-IDF is interleaved stereo int16.
    int16_t *samples = (int16_t *)s_pcm_work_buffer;
    uint32_t total_samples = len / sizeof(int16_t);
    uint32_t frame_count = total_samples / 2U;

    dsp_process_stereo_int16(samples, frame_count);

    if (!s_logged_dsp_mode) {
        ESP_LOGI(TAG, "DSP stage active (current mode: pass-through)");
        s_logged_dsp_mode = true;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_write(I2S_PORT, s_pcm_work_buffer, len, &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_write failed: %s", esp_err_to_name(err));
    }
}

static uint32_t a2dp_sample_rate_from_cfg(uint8_t octet0)
{
    // SBC sample-rate flags live in octet0 for ESP_A2D_MCT_SBC.
    if (octet0 & (1 << 6)) {
        return 32000;
    }
    if (octet0 & (1 << 5)) {
        return 44100;
    }
    if (octet0 & (1 << 4)) {
        return 48000;
    }
    if (octet0 & (1 << 7)) {
        return 16000;
    }
    return 44100;
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG,
                 "Bluetooth connection state: %d, peer: %02x:%02x:%02x:%02x:%02x:%02x",
                 param->conn_stat.state,
                 param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                 param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                 param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
        break;

    case ESP_A2D_AUDIO_CFG_EVT:
        if (param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            uint8_t octet0 = param->audio_cfg.mcc.cie.sbc[0];
            s_a2dp_sample_rate = a2dp_sample_rate_from_cfg(octet0);
            ESP_LOGI(TAG, "A2DP audio config (SBC): sample_rate=%lu Hz", (unsigned long)s_a2dp_sample_rate);
            i2s_reconfigure_for_rate(s_a2dp_sample_rate);
        } else {
            ESP_LOGI(TAG, "A2DP audio config: codec type=%d", param->audio_cfg.mcc.type);
        }
        break;

    default:
        break;
    }
}

static void init_i2s(void)
{
    const i2s_config_t i2s_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = s_a2dp_sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    const i2s_pin_config_t pin_cfg = {
        .bck_io_num = I2S_BCK_GPIO,
        .ws_io_num = I2S_LRCK_GPIO,
        .data_out_num = I2S_DOUT_GPIO,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &i2s_cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pin_cfg));
    i2s_reconfigure_for_rate(s_a2dp_sample_rate);
}

static void init_bluetooth(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_dev_set_device_name(DEVICE_NAME));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));

    ESP_ERROR_CHECK(esp_avrc_ct_init());
    ESP_ERROR_CHECK(esp_avrc_tg_init());

    ESP_ERROR_CHECK(esp_a2d_register_callback(bt_app_a2d_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_init());

    ESP_LOGI(TAG, "Bluetooth A2DP sink initialized, name: %s", DEVICE_NAME);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Starting BTDSP (Bluetooth PCM -> DSP -> I2S)");

    dsp_init();
    init_i2s();
    init_bluetooth();

    // Future runtime DSP update example:
    // dsp_peq_settings_t peq = { .enabled = false, .center_frequency_hz = 380.0f, .gain_db = -3.0f, .q = 1.0f };
    // dsp_set_peq(&peq, &peq);
}
