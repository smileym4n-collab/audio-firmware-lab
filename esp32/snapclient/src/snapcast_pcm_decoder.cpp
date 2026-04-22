#include "snapcast_pcm_decoder.h"

namespace {

bool hasTag(const std::array<uint8_t, SnapcastPcmDecoder::kHeaderProbeBytes> &data,
            size_t offset,
            const char *tag) {
  return data[offset + 0] == static_cast<uint8_t>(tag[0]) &&
         data[offset + 1] == static_cast<uint8_t>(tag[1]) &&
         data[offset + 2] == static_cast<uint8_t>(tag[2]) &&
         data[offset + 3] == static_cast<uint8_t>(tag[3]);
}

}  // namespace

bool SnapcastPcmDecoder::begin() {
  resetState();
  return true;
}

void SnapcastPcmDecoder::setFormatTarget(audio_tools::AudioInfoSupport &target) {
  formatTarget_ = &target;
}

void SnapcastPcmDecoder::setOutput(audio_tools::AudioStream &out_stream) {
  outputStream_ = &out_stream;
  AudioDecoder::setOutput(out_stream);
}

void SnapcastPcmDecoder::setOutput(Print &out_stream) {
  outputStream_ = nullptr;
  AudioDecoder::setOutput(out_stream);
}

void SnapcastPcmDecoder::end() {
  resetState();
}

size_t SnapcastPcmDecoder::write(const uint8_t *data, size_t length) {
  if (p_print == nullptr) {
    Serial.println("[snapclient] PCM decoder has no output stream");
    return 0;
  }

  size_t consumed = 0;

  if (!headerProcessed_) {
    const size_t headerBytesMissing = kHeaderProbeBytes - headerBytesRead_;
    const size_t bytesToCopy =
        length < headerBytesMissing ? length : headerBytesMissing;

    if (bytesToCopy > 0) {
      memcpy(headerBytes_.data() + headerBytesRead_, data, bytesToCopy);
      headerBytesRead_ += bytesToCopy;
      consumed += bytesToCopy;
      data += bytesToCopy;
      length -= bytesToCopy;
    }

    if (headerBytesRead_ < kHeaderProbeBytes) {
      return consumed;
    }

    headerProcessed_ = true;
    applyHeaderAudioInfo();
  }

  if (length == 0) {
    return consumed;
  }

  return consumed + p_print->write(const_cast<uint8_t *>(data), length);
}

void SnapcastPcmDecoder::resetState() {
  headerBytes_.fill(0);
  headerBytesRead_ = 0;
  headerProcessed_ = false;
  headerLogged_ = false;
}

bool SnapcastPcmDecoder::applyHeaderAudioInfo() {
  if (!hasStandardWavPreamble()) {
    if (!headerLogged_) {
      Serial.println(
          "[snapclient] PCM header parse skipped, using configured audio format");
      headerLogged_ = true;
    }
    return false;
  }

  const uint16_t format = readLe16(20);
  const uint16_t channels = readLe16(22);
  const uint32_t sampleRate = readLe32(24);
  const uint16_t bitsPerSample = readLe16(34);

  if (format != 1 || channels == 0 || sampleRate == 0 || bitsPerSample == 0) {
    if (!headerLogged_) {
      Serial.printf(
          "[snapclient] unsupported PCM WAV wrapper: format=%u, rate=%lu, bits=%u, ch=%u\n",
          format,
          static_cast<unsigned long>(sampleRate),
          bitsPerSample,
          channels);
      headerLogged_ = true;
    }
    return false;
  }

  const audio_tools::AudioInfo info(sampleRate, channels, bitsPerSample);
  setAudioInfo(info);
  if (formatTarget_ != nullptr) {
    formatTarget_->setAudioInfo(info);
    Serial.printf(
        "[snapclient] applied PCM header to output: %lu Hz, %u-bit, %u ch\n",
        static_cast<unsigned long>(sampleRate),
        bitsPerSample,
        channels);
  } else if (outputStream_ != nullptr) {
    outputStream_->setAudioInfo(info);
    Serial.printf(
        "[snapclient] applied PCM header to output via stream: %lu Hz, %u-bit, %u ch\n",
        static_cast<unsigned long>(sampleRate),
        bitsPerSample,
        channels);
  } else if (!headerLogged_) {
    Serial.println(
        "[snapclient] PCM header parsed without output format target");
  }

  if (!headerLogged_) {
    Serial.printf("[snapclient] PCM wrapper parsed: %lu Hz, %u-bit, %u ch\n",
                  static_cast<unsigned long>(sampleRate),
                  bitsPerSample,
                  channels);
    headerLogged_ = true;
  }

  return true;
}

bool SnapcastPcmDecoder::hasStandardWavPreamble() const {
  return hasTag(headerBytes_, 0, "RIFF") &&
         hasTag(headerBytes_, 8, "WAVE") &&
         hasTag(headerBytes_, 12, "fmt ");
}

uint16_t SnapcastPcmDecoder::readLe16(size_t offset) const {
  return static_cast<uint16_t>(headerBytes_[offset]) |
         (static_cast<uint16_t>(headerBytes_[offset + 1]) << 8);
}

uint32_t SnapcastPcmDecoder::readLe32(size_t offset) const {
  return static_cast<uint32_t>(headerBytes_[offset]) |
         (static_cast<uint32_t>(headerBytes_[offset + 1]) << 8) |
         (static_cast<uint32_t>(headerBytes_[offset + 2]) << 16) |
         (static_cast<uint32_t>(headerBytes_[offset + 3]) << 24);
}
