#pragma once

#include "AudioTools.h"

class AudioOutputController {
 public:
  bool begin(uint32_t sampleRate);
  void updateAudioFormat(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample);
  size_t write(const uint8_t *data, size_t length);

  audio_tools::I2SStream &stream() { return i2sOut_; }

 private:
  void fillConfig(audio_tools::I2SConfig &cfg,
                  uint32_t sampleRate,
                  uint8_t channels,
                  uint8_t bitsPerSample);

  audio_tools::I2SStream i2sOut_;
  audio_tools::I2SConfig config_;
};
