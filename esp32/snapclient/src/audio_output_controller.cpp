#include "audio_output_controller.h"

#include "board_config.h"
#include "snapclient_config.h"

using namespace audio_tools;

namespace {

int configuredMclkPin() {
  return board_config::I2S_MCLK_ENABLED ? board_config::I2S_MCLK_PIN : -1;
}

}  // namespace

bool AudioOutputController::begin(uint32_t sampleRate) {
  fillConfig(config_,
             sampleRate,
             app_config::AUDIO_CHANNELS,
             app_config::AUDIO_BITS_PER_SAMPLE);

  Serial.printf("[i2s] bclk=%d ws=%d dout=%d\n",
                board_config::I2S_BCLK_PIN,
                board_config::I2S_LRCLK_PIN,
                board_config::I2S_DOUT_PIN);
  if (board_config::I2S_MCLK_ENABLED) {
    Serial.printf("[i2s] mclk=enabled on GPIO%d\n", board_config::I2S_MCLK_PIN);
  } else {
    Serial.println("[i2s] mclk=disabled");
  }
  Serial.printf("[i2s] dma=%u x %u bytes, apll=%s\n",
                app_config::I2S_DMA_BUFFER_COUNT,
                app_config::I2S_DMA_BUFFER_SIZE,
                app_config::I2S_USE_AUDIO_PLL ? "on" : "off");

  return i2sOut_.begin(config_);
}

void AudioOutputController::updateAudioFormat(uint32_t sampleRate,
                                              uint8_t channels,
                                              uint8_t bitsPerSample) {
  if (!i2sOut_.isActive()) {
    return;
  }

  AudioInfo info(sampleRate, channels, bitsPerSample);
  i2sOut_.setAudioInfo(info);

  Serial.printf("[i2s] format update=%lu Hz, %u-bit, %u ch\n",
                static_cast<unsigned long>(sampleRate),
                bitsPerSample,
                channels);
}

size_t AudioOutputController::write(const uint8_t *data, size_t length) {
  return i2sOut_.write(data, length);
}

void AudioOutputController::fillConfig(I2SConfig &cfg,
                                       uint32_t sampleRate,
                                       uint8_t channels,
                                       uint8_t bitsPerSample) {
  cfg = i2sOut_.defaultConfig(TX_MODE);
  cfg.sample_rate = sampleRate;
  cfg.channels = channels;
  cfg.bits_per_sample = bitsPerSample;
  cfg.pin_bck = board_config::I2S_BCLK_PIN;
  cfg.pin_ws = board_config::I2S_LRCLK_PIN;
  cfg.pin_data = board_config::I2S_DOUT_PIN;
  cfg.pin_mck = configuredMclkPin();
  cfg.buffer_count = app_config::I2S_DMA_BUFFER_COUNT;
  cfg.buffer_size = app_config::I2S_DMA_BUFFER_SIZE;
  cfg.use_apll = app_config::I2S_USE_AUDIO_PLL;
  cfg.auto_clear = true;
}
