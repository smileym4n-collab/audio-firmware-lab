# CHANGELOG

## [Unreleased]

- Removed historical change-summary sections from the README so version notes live only in this changelog.

## [0.10.0]

- Added a Snapclient-mode HTTP control API for companion apps to read firmware capabilities and set local channel routing.
- Added persistent Snapclient channel routing modes: stereo, left-to-both-DAC-channels, and right-to-both-DAC-channels.
- Documented the SnapApp control API and clarified that Bluetooth mode ignores local channel routing.

## [0.9.29]

- Moved private Wi-Fi credentials out of the committed configuration and into a local `include/secrets.h` file.
- Added `include/secrets.example.h` as the copyable template for local builds.

## [0.9.28]

- Replaced the fixed `1.0x` Snapclient timing path with a tightly clamped dynamic sync so small long-run clock drift can be corrected without audible pause-and-refill behavior.
- Re-enabled the Snapclient resampler for gentle drift correction while limiting it to a very narrow range around unity.
- Disabled the hard Snapclient rebuffer intervention by default, since the repeated stop-and-refill cycle itself was becoming audible.

## [0.9.27]

- Kept the deeper queue and rebuffer behavior from `0.9.26`.
- Stopped printing repeated `rebuffer-start` and `rebuffer-end` lines while periodic stats are disabled.
- Left warning and fault logging intact so real failures still show up in the serial monitor.

## [0.9.26]

- Increased the Snapclient queue depth and raised the startup/resume cushion so the WROVER keeps a healthier PCM reserve during Wi-Fi jitter.
- Made the low-buffer rebuffer thresholds explicit and more conservative so playback recovers before the live queue is nearly empty.
- Reduced repeated `sync-wait` startup chatter so the serial monitor stays quieter during bring-up.

## [0.9.25]

- Added Snapclient queue re-buffering when the live PCM cushion falls below a low-water mark.
- Added explicit `rebuffer-start` and `rebuffer-end` log lines for the low-buffer recovery path.

## [0.9.24]

- Added a one-shot Snapclient PCM first-write probe so early decoded output can be verified without re-enabling continuous log spam.
- Kept the log volume low so live playback testing still avoids the old serial flood.

## [0.9.23]

- Fixed a Snapclient boot crash caused by re-opening the shared I2S stream during PCM codec-header setup.
- Kept the shared Bluetooth/I2S output ownership unchanged and limited the fix to the Snapclient wrapper path.

## [0.9.22]

- Disabled periodic Snapclient PCM and queue-stat heartbeat logs by default during live playback testing.
- Kept startup, format-change, and warning/error logs active so failure states still show up clearly.

## [0.9.21]

- Reduced the ESP32 core log level so verbose per-packet Snapclient library info logs no longer run during normal playback.
- Moved the Snapclient processing loop back onto its own RTOS task to match the earlier stable bench profile more closely.
- Added a startup log for the dedicated Snapclient task configuration.

## [0.9.20]

- Switched the Snapclient PCM path to fixed Snapcast timing so the ESP32 no longer chases dynamic playback-factor updates during normal PCM playback.
- Disabled the Snapclient-only resampler/boost stage in the project-local Snap output path to prioritize clean PCM output over elastic clock correction.
- Added startup logs for the fixed-sync factor and intentionally disabled Snapclient resampler.

## [0.9.19]

- Added a final Snapclient-only PCM gain stage immediately before I2S so the actual outgoing samples have guaranteed headroom.
- Added a startup log for the final Snapclient PCM gain.
- Left Bluetooth mode, mode switching, and the shared I2S output path unchanged.

## [0.9.18 and earlier]

- Added a Snapclient-only gain trim before the shared resampler/output path to reduce distortion from full-scale PCM material.
- Fixed the Snapclient PCM header handoff so the parsed WAV header format is pushed into the active downstream stream even when the library exposes the target as `Print`.
- Added clearer Snapclient PCM header and I2S format-update logging.
- Reverted Snapclient mode from Opus back to the project-local PCM decoder after isolating distortion and stop behavior to the Opus path.
- Added focused Snapclient queue, PCM-format, playback-idle, and PCM activity logging so the serial monitor shows where playback stalls.
- Retargeted the firmware to the ESP32-WROVER-IE-N16R8 with a project-local PlatformIO board definition and 16 MB flash partitioning.
- Added centralized board pin mapping, including external DAC MCLK output and a boot-time mode select button.
- Split the firmware into boot-selected Snapclient and Bluetooth receiver modes while keeping the external I2S DAC path shared.
- Enabled PSRAM-aware buffering for more stable playback on the WROVER hardware.
- Updated the README and Snapserver notes for the `0.4.0` prototype revision.
- Changed the boot selector to a momentary startup button with Snapclient as the default mode.
- Added a dedicated mode-status LED pin with steady Snapclient indication and blinking Bluetooth indication.
- Changed the mode button so cold boot always starts in Snapclient and runtime button presses reboot into the opposite mode.
- Made I2S MCLK optional through a central board configuration flag, with MCLK disabled by default for PCM5102-style builds.
- Fixed Snapclient PCM playback by replacing the generic WAV decoder with a local decoder that handles the truncated Snapcast PCM wrapper correctly.
- Added serial commands to switch between Snapclient and Bluetooth modes without the physical mode button during bring-up.
- Fixed the reboot handoff for requested mode changes so the next mode survives reset reliably.
- Changed Snapclient mode to use a fixed playback sync factor for more stable PCM bring-up on the ESP32 target.
- Switched Snapclient mode over to the upstream Opus decoder path and updated the Snapserver documentation to use `codec=opus`.
- Switched the Opus Snapclient path back to dynamic clock synchronization and restored a positive `172 ms` processing lag for better playback timing.
- Reverted the experimental dynamic Opus timing change after it caused silence, returning Snapclient mode to the earlier fixed-sync Opus behavior.
- Increased the Snapclient Opus queue and I2S DMA buffering to improve playback stability on the WROVER hardware.
- Fixed the boot loop from the oversize I2S DMA buffer setting and clamped the DMA size to the ESP32 driver's valid range.
- Lowered the Snapclient queue activation threshold so the larger Opus queue starts playback earlier instead of waiting for a near-full buffer.
- Enabled info-level runtime logging to expose the Snapclient queue and synchronization behavior during silent Opus playback debugging.
- Increased the Snapclient RTOS queue entry slot count after logs showed `size_queue full` while plenty of byte-buffer space was still available.
- Added a project-local Snapclient output wrapper so Opus decoder startup failures are logged clearly instead of silently returning zero-byte writes.
- Added a safe fallback to the configured 48 kHz, 16-bit, stereo format when Snapclient Opus audio info arrives invalid.
- Reduced the Snapclient Opus queue size and activation threshold after logs showed the RTOS output task was waiting too long to start playback.
