#pragma once

#include <vector>

#include <Arduino.h>

#include "AudioTools.h"

class AudioProbeStream : public audio_tools::AudioStream {
 public:
  explicit AudioProbeStream(audio_tools::AudioStream &target) : target_(&target) {}

  void setPcmGain(float gain) { pcmGain_ = gain; }
  void setPeriodicStatsEnabled(bool enabled) { periodicStatsEnabled_ = enabled; }

  bool begin() override {
    // The shared I2S output is started by AudioOutputController before
    // Snapclient begins. Re-opening it here can force a second DMA allocation
    // during codec-header handling and crash the ESP32 driver.
    return target_ != nullptr;
  }

  void end() override {
    if (target_ != nullptr) {
      target_->end();
    }
  }

  void setAudioInfo(audio_tools::AudioInfo newInfo) override {
    info = newInfo;
    firstWriteLogsRemaining_ = 3;
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

    const uint8_t *writeData = data;
    size_t writeLen = len;
    if (shouldApplyGain(len)) {
      prepareGainBuffer(data, len);
      if (!gainBuffer_.empty()) {
        writeData = gainBuffer_.data();
        writeLen = gainBuffer_.size();
      }
    }

    const size_t written = target_->write(writeData, writeLen);
    accumulate(writeData, written);
    maybeLogFirstWrites(writeData, written);
    maybeLog();
    return written;
  }

 private:
  audio_tools::AudioStream *target_ = nullptr;
  std::vector<uint8_t> gainBuffer_;
  uint32_t windowStartMs_ = millis();
  uint32_t windowBytes_ = 0;
  uint16_t windowPeak_ = 0;
  float pcmGain_ = 1.0f;
  bool periodicStatsEnabled_ = true;
  uint8_t firstWriteLogsRemaining_ = 3;

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
    if (!periodicStatsEnabled_) {
      return;
    }

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

  void maybeLogFirstWrites(const uint8_t *buffer, size_t size) {
    if (firstWriteLogsRemaining_ == 0 || size == 0) {
      return;
    }

    Serial.printf("[snapclient-pcm] first-write bytes=%lu peak16=%u\n",
                  static_cast<unsigned long>(size),
                  maxAbsPcm16(buffer, size));
    --firstWriteLogsRemaining_;
  }

  bool shouldApplyGain(size_t size) const {
    return pcmGain_ > 0.0f && pcmGain_ < 0.999f &&
           info.bits_per_sample == 16 && size >= sizeof(int16_t);
  }

  void prepareGainBuffer(const uint8_t *buffer, size_t size) {
    gainBuffer_.resize(size);
    const int16_t *src = reinterpret_cast<const int16_t *>(buffer);
    int16_t *dst = reinterpret_cast<int16_t *>(gainBuffer_.data());
    const size_t sampleCount = size / sizeof(int16_t);

    for (size_t i = 0; i < sampleCount; ++i) {
      const float scaled = static_cast<float>(src[i]) * pcmGain_;
      if (scaled > 32767.0f) {
        dst[i] = 32767;
      } else if (scaled < -32768.0f) {
        dst[i] = -32768;
      } else {
        dst[i] = static_cast<int16_t>(scaled);
      }
    }

    const size_t remainderOffset = sampleCount * sizeof(int16_t);
    for (size_t i = remainderOffset; i < size; ++i) {
      gainBuffer_[i] = buffer[i];
    }
  }
};
