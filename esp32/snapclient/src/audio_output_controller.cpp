#include "audio_output_controller.h"

#include <stdint.h>
#include <string.h>

#include "board_config.h"
#include "snapclient_config.h"

using namespace audio_tools;

namespace {

constexpr uint16_t kFullScaleGainQ15 = 32767;
constexpr size_t kGainBufferBytes = 256;

int configuredMclkPin() {
  return board_config::I2S_MCLK_ENABLED ? board_config::I2S_MCLK_PIN : -1;
}

uint16_t sanitizedDmaBufferSize(uint16_t requestedSize) {
  constexpr uint16_t kMinI2sDmaBufferSize = 8;
  constexpr uint16_t kMaxI2sDmaBufferSize = 1024;

  if (requestedSize < kMinI2sDmaBufferSize) {
    Serial.printf("[i2s] requested dma buffer size %u is too small, clamping to %u\n",
                  requestedSize,
                  kMinI2sDmaBufferSize);
    return kMinI2sDmaBufferSize;
  }

  if (requestedSize > kMaxI2sDmaBufferSize) {
    Serial.printf("[i2s] requested dma buffer size %u exceeds ESP32 limit, clamping to %u\n",
                  requestedSize,
                  kMaxI2sDmaBufferSize);
    return kMaxI2sDmaBufferSize;
  }

  return requestedSize;
}

uint16_t maxAbsPcm16(const uint8_t *data, size_t length) {
  const size_t sampleCount = length / sizeof(int16_t);
  const int16_t *samples = reinterpret_cast<const int16_t *>(data);
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

}  // namespace

bool AudioOutputController::begin(uint32_t sampleRate) {
  return begin(sampleRate,
               app_config::I2S_DMA_BUFFER_COUNT,
               app_config::I2S_DMA_BUFFER_SIZE);
}

bool AudioOutputController::begin(uint32_t sampleRate,
                                  uint8_t dmaBufferCount,
                                  uint16_t dmaBufferSize) {
  gainCurrentQ15_ = 0;
  gainStartQ15_ = 0;
  gainTargetQ15_ = 0;
  gainRampStartMs_ = millis();
  gainRampDurationMs_ = 0;

  fillConfig(config_,
             sampleRate,
             app_config::AUDIO_CHANNELS,
             app_config::AUDIO_BITS_PER_SAMPLE,
             dmaBufferCount,
             dmaBufferSize);

  Serial.printf("[i2s] begin format=%lu Hz, %u-bit, %u ch\n",
                static_cast<unsigned long>(sampleRate),
                app_config::AUDIO_BITS_PER_SAMPLE,
                app_config::AUDIO_CHANNELS);
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
                dmaBufferCount,
                dmaBufferSize,
                app_config::I2S_USE_AUDIO_PLL ? "on" : "off");

  const bool started = i2sOut_.begin(config_);
  if (started) {
    rampToFullScale(app_config::AUDIO_UNMUTE_RAMP_MS);
  }
  return started;
}

void AudioOutputController::setChannelMode(app_config::ChannelMode mode) {
  if (channelMode_ == mode) {
    return;
  }

  channelMode_ = mode;
  Serial.printf("[channel] mode=%s\n", app_config::channelModeName(channelMode_));
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
  if (channelMode_ == app_config::ChannelMode::Stereo ||
      config_.channels != 2 ||
      config_.bits_per_sample != 16) {
    return writeRaw(data, length);
  }

  constexpr size_t kRoutedSampleCount = 256;
  int16_t routedSamples[kRoutedSampleCount];
  constexpr size_t kRoutedFrameCount = kRoutedSampleCount / 2;
  constexpr size_t kBytesPerFrame = sizeof(int16_t) * 2;

  const int16_t *inputSamples = reinterpret_cast<const int16_t *>(data);
  const size_t frameCount = length / kBytesPerFrame;
  const size_t trailingBytes = length % kBytesPerFrame;
  size_t totalWritten = 0;
  size_t frameOffset = 0;

  while (frameOffset < frameCount) {
    const size_t framesThisPass = min(kRoutedFrameCount, frameCount - frameOffset);
    for (size_t i = 0; i < framesThisPass; ++i) {
      const size_t sourceIndex = (frameOffset + i) * 2;
      const int16_t selectedSample =
          channelMode_ == app_config::ChannelMode::Left
              ? inputSamples[sourceIndex]
              : inputSamples[sourceIndex + 1];
      routedSamples[i * 2] = selectedSample;
      routedSamples[(i * 2) + 1] = selectedSample;
    }

    totalWritten += writeRaw(reinterpret_cast<const uint8_t *>(routedSamples),
                             framesThisPass * kBytesPerFrame);
    frameOffset += framesThisPass;
  }

  if (trailingBytes > 0) {
    totalWritten += writeRaw(data + (frameCount * kBytesPerFrame), trailingBytes);
  }

  return totalWritten;
}

size_t AudioOutputController::writeRaw(const uint8_t *data, size_t length) {
  static uint32_t windowStartMs = millis();
  static uint32_t windowBytes = 0;
  static uint16_t windowPeak = 0;

  const uint16_t gainQ15 = currentGainQ15();
  const size_t written =
      (config_.bits_per_sample == 16 && gainQ15 < kFullScaleGainQ15)
          ? writeGainAdjusted(data, length, gainQ15)
          : i2sOut_.write(data, length);
  windowBytes += static_cast<uint32_t>(written);

  if (written > 0 && config_.bits_per_sample == 16) {
    const uint16_t chunkPeak = maxAbsPcm16(data, written);
    if (chunkPeak > windowPeak) {
      windowPeak = chunkPeak;
    }
  }

  const uint32_t nowMs = millis();
  if (nowMs - windowStartMs >= app_config::AUDIO_DEBUG_LOG_INTERVAL_MS) {
    Serial.printf("[i2s] pcm bytes=%lu peak16=%u\n",
                  static_cast<unsigned long>(windowBytes),
                  windowPeak);
    windowStartMs = nowMs;
    windowBytes = 0;
    windowPeak = 0;
  }

  return written;
}

void AudioOutputController::rampToMute(uint32_t durationMs) {
  beginGainRamp(0, durationMs);
}

void AudioOutputController::rampToFullScale(uint32_t durationMs) {
  beginGainRamp(kFullScaleGainQ15, durationMs);
}

void AudioOutputController::muteForRestart(uint32_t durationMs) {
  if (!i2sOut_.isActive()) {
    return;
  }

  rampToMute(durationMs);

  uint8_t silence[kGainBufferBytes] = {};
  const uint32_t startMs = millis();
  do {
    writeRaw(silence, sizeof(silence));
    delay(1);
  } while ((millis() - startMs) < durationMs);

  i2sOut_.flush();
}

void AudioOutputController::beginGainRamp(uint16_t targetGainQ15,
                                          uint32_t durationMs) {
  gainStartQ15_ = currentGainQ15();
  gainCurrentQ15_ = gainStartQ15_;
  gainTargetQ15_ = targetGainQ15;
  gainRampStartMs_ = millis();
  gainRampDurationMs_ = durationMs;

  if (durationMs == 0) {
    gainCurrentQ15_ = gainTargetQ15_;
  }
}

uint16_t AudioOutputController::currentGainQ15() {
  if (gainCurrentQ15_ == gainTargetQ15_ || gainRampDurationMs_ == 0) {
    return gainCurrentQ15_;
  }

  const uint32_t elapsedMs = millis() - gainRampStartMs_;
  if (elapsedMs >= gainRampDurationMs_) {
    gainCurrentQ15_ = gainTargetQ15_;
    return gainCurrentQ15_;
  }

  const int32_t gainDelta =
      static_cast<int32_t>(gainTargetQ15_) - static_cast<int32_t>(gainStartQ15_);
  gainCurrentQ15_ =
      static_cast<uint16_t>(static_cast<int32_t>(gainStartQ15_) +
                            ((gainDelta * static_cast<int32_t>(elapsedMs)) /
                             static_cast<int32_t>(gainRampDurationMs_)));
  return gainCurrentQ15_;
}

size_t AudioOutputController::writeGainAdjusted(const uint8_t *data,
                                                size_t length,
                                                uint16_t gainQ15) {
  uint8_t buffer[kGainBufferBytes];
  size_t totalWritten = 0;
  size_t offset = 0;

  while (offset < length) {
    const size_t bytesThisPass = min(sizeof(buffer), length - offset);
    const size_t sampleBytes = bytesThisPass - (bytesThisPass % sizeof(int16_t));

    for (size_t i = 0; i < sampleBytes; i += sizeof(int16_t)) {
      int16_t sample;
      memcpy(&sample, data + offset + i, sizeof(sample));
      const int32_t scaled =
          (static_cast<int32_t>(sample) * static_cast<int32_t>(gainQ15)) /
          static_cast<int32_t>(kFullScaleGainQ15);
      const int16_t outputSample = static_cast<int16_t>(scaled);
      memcpy(buffer + i, &outputSample, sizeof(outputSample));
    }

    if (sampleBytes < bytesThisPass) {
      memcpy(buffer + sampleBytes,
             data + offset + sampleBytes,
             bytesThisPass - sampleBytes);
    }

    totalWritten += i2sOut_.write(buffer, bytesThisPass);
    offset += bytesThisPass;
  }

  return totalWritten;
}

void AudioOutputController::fillConfig(I2SConfig &cfg,
                                       uint32_t sampleRate,
                                       uint8_t channels,
                                       uint8_t bitsPerSample,
                                       uint8_t dmaBufferCount,
                                       uint16_t dmaBufferSize) {
  cfg = i2sOut_.defaultConfig(TX_MODE);
  cfg.sample_rate = sampleRate;
  cfg.channels = channels;
  cfg.bits_per_sample = bitsPerSample;
  cfg.pin_bck = board_config::I2S_BCLK_PIN;
  cfg.pin_ws = board_config::I2S_LRCLK_PIN;
  cfg.pin_data = board_config::I2S_DOUT_PIN;
  cfg.pin_mck = configuredMclkPin();
  cfg.buffer_count = dmaBufferCount;
  cfg.buffer_size = sanitizedDmaBufferSize(dmaBufferSize);
  cfg.use_apll = app_config::I2S_USE_AUDIO_PLL;
  cfg.auto_clear = true;
}
