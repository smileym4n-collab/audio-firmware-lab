#pragma once

#include "AudioTools.h"
#include "api/SnapOutput.h"

class ProjectSnapOutput : public snap_arduino::SnapOutput {
 public:
  explicit ProjectSnapOutput(audio_tools::AudioInfo fallbackInfo)
      : fallbackInfo_(fallbackInfo) {}

  ProjectSnapOutput(audio_tools::AudioInfo fallbackInfo,
                    bool useResampler,
                    bool allowBoost)
      : fallbackInfo_(fallbackInfo),
        useResampler_(useResampler),
        allowBoost_(allowBoost) {}

  bool begin() override {
    ESP_LOGI(TAG, "begin");
    is_sync_started = false;
    return beginAudioChain();
  }

  void setAudioInfo(audio_tools::AudioInfo info) override {
    const audio_tools::AudioInfo chosen = sanitizeAudioInfo(info);
    ESP_LOGI(TAG, "audio info: %d Hz, %d-bit, %d ch",
             chosen.sample_rate,
             chosen.bits_per_sample,
             chosen.channels);
    SnapOutput::setAudioInfo(chosen);
  }

 private:
  audio_tools::AudioInfo fallbackInfo_;
  bool useResampler_ = true;
  bool allowBoost_ = true;

  audio_tools::AudioInfo sanitizeAudioInfo(audio_tools::AudioInfo info) const {
    if (info.sample_rate <= 0 || info.channels <= 0 ||
        info.bits_per_sample <= 0) {
      return fallbackInfo_;
    }
    return info;
  }

  bool beginAudioChain() {
    if (out == nullptr) {
      ESP_LOGI(TAG, "out is null");
      return false;
    }
    if (p_snap_time_sync == nullptr) {
      ESP_LOGI(TAG, "p_snap_time_sync is null");
      return false;
    }

    audio_info = sanitizeAudioInfo(out->audioInfo());

    auto vol_cfg = vol_stream.defaultConfig();
    vol_cfg.copyFrom(audio_info);
    vol_cfg.allow_boost = allowBoost_;
    vol_stream.begin(vol_cfg);
    vol_stream.setVolume(vol * vol_factor);

    if (useResampler_) {
      resample.setOutput(*out);
      vol_stream.setStream(resample);
    } else {
      vol_stream.setStream(*out);
    }
    decoder_stream.setStream(&vol_stream);

    out->setAudioInfo(audio_info);
    out->begin();

    auto dec_cfg = decoder_stream.defaultConfig();
    dec_cfg.copyFrom(audio_info);
    if (!decoder_stream.begin(dec_cfg)) {
      ESP_LOGE(TAG, "decoder begin failed for %d Hz, %d-bit, %d ch",
               audio_info.sample_rate,
               audio_info.bits_per_sample,
               audio_info.channels);
      is_audio_begin_called = false;
      return false;
    }
    decoder_stream.addNotifyAudioChange(*this);

    if (useResampler_) {
      auto res_cfg = resample.defaultConfig();
      res_cfg.step_size = p_snap_time_sync->getFactor();
      res_cfg.copyFrom(audio_info);
      if (!resample.begin(res_cfg)) {
        ESP_LOGE(TAG, "resample begin failed for %d Hz, %d-bit, %d ch",
                 audio_info.sample_rate,
                 audio_info.bits_per_sample,
                 audio_info.channels);
        is_audio_begin_called = false;
        return false;
      }
    }

    ESP_LOGI(TAG, "decoder/output ready at %d Hz, %d-bit, %d ch, resampler=%s, boost=%s",
             audio_info.sample_rate,
             audio_info.bits_per_sample,
             audio_info.channels,
             useResampler_ ? "on" : "off",
             allowBoost_ ? "on" : "off");
    is_audio_begin_called = true;
    return true;
  }
};
