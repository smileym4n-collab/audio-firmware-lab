# CHANGELOG

## [Unreleased]

- Switched the ESP32 v3 prototype from Opus decoding to PCM-first Snapserver playback.
- Removed the Opus build dependency and increased I2S DMA buffering for cleaner playback on standard ESP32 hardware.
- Added Snapserver-side configuration notes for `codec=pcm` and `sampleformat=48000:16:2`.
