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
