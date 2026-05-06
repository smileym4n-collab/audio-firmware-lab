#pragma once

#include "AudioTools.h"
#include "channel_mode.h"

class AudioOutputController {
 public:
  bool begin(uint32_t sampleRate);
  bool begin(uint32_t sampleRate, uint8_t dmaBufferCount, uint16_t dmaBufferSize);
  void updateAudioFormat(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample);
  void setChannelMode(app_config::ChannelMode mode);
  app_config::ChannelMode channelMode() const { return channelMode_; }
  size_t write(const uint8_t *data, size_t length);
  void rampToMute(uint32_t durationMs);
  void rampToFullScale(uint32_t durationMs);
  void muteForRestart(uint32_t durationMs);

  audio_tools::I2SStream &stream() { return i2sOut_; }

 private:
  void fillConfig(audio_tools::I2SConfig &cfg,
                  uint32_t sampleRate,
                  uint8_t channels,
                  uint8_t bitsPerSample,
                  uint8_t dmaBufferCount,
                  uint16_t dmaBufferSize);
  size_t writeRaw(const uint8_t *data, size_t length);
  void beginGainRamp(uint16_t targetGainQ15, uint32_t durationMs);
  uint16_t currentGainQ15();
  size_t writeGainAdjusted(const uint8_t *data, size_t length, uint16_t gainQ15);

  audio_tools::I2SStream i2sOut_;
  audio_tools::I2SConfig config_;
  app_config::ChannelMode channelMode_ = app_config::ChannelMode::Stereo;
  uint16_t gainStartQ15_ = 0;
  uint16_t gainCurrentQ15_ = 0;
  uint16_t gainTargetQ15_ = 32767;
  uint32_t gainRampStartMs_ = 0;
  uint32_t gainRampDurationMs_ = 0;
};
