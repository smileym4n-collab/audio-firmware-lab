#pragma once

#include <Arduino.h>

#include "AudioTools.h"

class AudioProbeStream : public audio_tools::AudioStream {
 public:
  explicit AudioProbeStream(audio_tools::AudioStream &target) : target_(&target) {}

  bool begin() override { return target_ != nullptr ? target_->begin() : false; }

  void end() override {
    if (target_ != nullptr) {
      target_->end();
    }
  }

  void setAudioInfo(audio_tools::AudioInfo newInfo) override {
    info = newInfo;
    Serial.printf("[snapclient-pcm] format=%ld Hz, %d-bit, %d ch\n",
                  static_cast<long>(newInfo.sample_rate),
                  newInfo.bits_per_sample,
                  newInfo.channels);
    if (target_ != nullptr) {
      target_->setAudioInfo(newInfo);
      const audio_tools::AudioInfo applied = target_->audioInfo();
      Serial.printf("[i2s] format update=%ld Hz, %d-bit, %d ch\n",
                    static_cast<long>(applied.sample_rate),
                    applied.bits_per_sample,
                    applied.channels);
      if (applied.sample_rate != newInfo.sample_rate ||
          applied.bits_per_sample != newInfo.bits_per_sample ||
          applied.channels != newInfo.channels) {
        Serial.printf(
            "[i2s] format mismatch requested=%ld/%d/%d applied=%ld/%d/%d\n",
            static_cast<long>(newInfo.sample_rate),
            newInfo.bits_per_sample,
            newInfo.channels,
            static_cast<long>(applied.sample_rate),
            applied.bits_per_sample,
            applied.channels);
      }
    }
  }

  audio_tools::AudioInfo audioInfo() override {
    return target_ != nullptr ? target_->audioInfo() : info;
  }

  int available() override { return target_ != nullptr ? target_->available() : 0; }

  int availableForWrite() override {
    return target_ != nullptr ? target_->availableForWrite() : 0;
  }

  size_t readBytes(uint8_t *data, size_t len) override {
    return target_ != nullptr ? target_->readBytes(data, len) : 0;
  }

  size_t write(const uint8_t *data, size_t len) override {
    if (target_ == nullptr) {
      return 0;
    }

    const size_t written = target_->write(data, len);
    accumulate(data, written);
    maybeLog();
    return written;
  }

 private:
  audio_tools::AudioStream *target_ = nullptr;
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
