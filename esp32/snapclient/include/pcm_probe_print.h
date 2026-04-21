#pragma once

#include <Arduino.h>

#include "Print.h"

class PcmProbePrint : public Print {
 public:
  explicit PcmProbePrint(Print &target) : target_(&target) {}

  void setTarget(Print &target) { target_ = &target; }

  size_t write(uint8_t value) override { return write(&value, 1); }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (target_ == nullptr) {
      return 0;
    }

    const size_t written = target_->write(buffer, size);
    accumulate(buffer, written);
    maybeLog();
    return written;
  }

 private:
  Print *target_ = nullptr;
  uint32_t windowStartMs_ = millis();
  uint32_t windowBytes_ = 0;
  uint16_t windowPeak_ = 0;

  static uint16_t maxAbsPcm16(const uint8_t *buffer, size_t size) {
    const size_t sampleCount = size / sizeof(int16_t);
    const int16_t *samples = reinterpret_cast<const int16_t *>(buffer);
    uint16_t peak = 0;

    for (size_t i = 0; i < sampleCount; ++i) {
      const int32_t value = samples[i];
      const uint16_t magnitude =
          static_cast<uint16_t>(value < 0 ? -value : value);
      if (magnitude > peak) {
        peak = magnitude;
      }
    }

    return peak;
  }

  void accumulate(const uint8_t *buffer, size_t size) {
    windowBytes_ += static_cast<uint32_t>(size);
    const uint16_t chunkPeak = maxAbsPcm16(buffer, size);
    if (chunkPeak > windowPeak_) {
      windowPeak_ = chunkPeak;
    }
  }

  void maybeLog() {
    const uint32_t nowMs = millis();
    if (nowMs - windowStartMs_ < 1000) {
      return;
    }

    Serial.printf("[snapclient-pcm] bytes=%lu peak16=%u\n",
                  static_cast<unsigned long>(windowBytes_),
                  windowPeak_);
    windowStartMs_ = nowMs;
    windowBytes_ = 0;
    windowPeak_ = 0;
  }
};
