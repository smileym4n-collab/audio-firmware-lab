Project rules:
- This is a working ESP32 speaker project. Stability is the top priority.
- Preserve existing Bluetooth audio functionality unless explicitly required otherwise.
- Prefer minimal diffs and incremental changes.
- Do not rewrite unrelated code.
- Reuse proven ESP32 AirPlay approaches where possible.
- Keep the existing I2S output structure if feasible.
- Comment clearly around audio-path changes.
- If a feature is uncertain or risky, implement it behind a compile-time option.
- Explain tradeoffs before large architectural changes.
