#pragma once

#include <array>

#include <Arduino.h>

#include "AudioTools.h"

class SnapcastPcmDecoder : public audio_tools::AudioDecoder {
 public:
  static constexpr size_t kHeaderProbeBytes = 44;

  bool begin() override;
  void end() override;
  size_t write(const uint8_t *data, size_t length) override;
  operator bool() override { return true; }

 private:
  void resetState();
  bool applyHeaderAudioInfo();
  bool hasStandardWavPreamble() const;
  uint16_t readLe16(size_t offset) const;
  uint32_t readLe32(size_t offset) const;

  std::array<uint8_t, kHeaderProbeBytes> headerBytes_{};
  size_t headerBytesRead_ = 0;
  bool headerProcessed_ = false;
  bool headerLogged_ = false;
};
