# CHANGELOG

## [Unreleased]

- Retargeted the firmware to the ESP32-WROVER-IE-N16R8 with a project-local PlatformIO board definition and 16 MB flash partitioning.
- Added centralized board pin mapping, including external DAC MCLK output and a boot-time mode select button.
- Split the firmware into boot-selected Snapclient and Bluetooth receiver modes while keeping the external I2S DAC path shared.
- Enabled PSRAM-aware buffering for more stable playback on the WROVER hardware.
- Updated the README and Snapserver notes for the v0.4.0 prototype revision.
- Changed the boot selector to a momentary startup button with Snapclient as the default mode.
- Added a dedicated mode-status LED pin with steady Snapclient indication and blinking Bluetooth indication.
- Changed the mode button again so cold boot always starts in Snapclient and runtime button presses reboot into the opposite mode.
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
- Added I2S-side PCM activity logging so silent playback can be distinguished from missing decode output during Snapclient debugging.
- Moved the PCM probe into the Snapclient decoded-audio path after confirming the previous I2S-side wrapper was bypassed by the library.
- Added a project-local Snapclient output wrapper so Opus decoder startup failures are logged clearly instead of silently returning zero-byte writes.
- Added a safe fallback to the configured 48 kHz, 16-bit, stereo format when Snapclient Opus audio info arrives invalid.
