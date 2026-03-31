## PreAmpv2 file-specific rules

Applies to:
- `PreAmpv2.ino`

### Current working hardware facts
- The working LCD implementation is `LiquidCrystal_I2C`.
- The LCD I2C pins must be forced in code with:
  - `Wire.pins(PIN_PA1, PIN_PA2);`
- Do not use:
  - `hd44780`
  - `hd44780_I2Cexp`
  - `Wire.swap(...)`
  - I2C route auto-detection
  - route scanning/fallback logic
- These are proven hardware facts, not optional preferences.

### Change scope
- Before modifying `PreAmpv2.ino`, always read the current contents of the file from the active worktree.
- Do not assume a previous Codex task version is current.
- If the file has changed since a previous attempt, discard the old patch plan and re-read the file before editing.
- Keep patches as small and focused as possible.

### Versioning
- The file header contains a line in the format:
  - `Version: x.y.z`
- Whenever modifying `PreAmpv2.ino`, increment the patch version by 1 unless explicitly instructed otherwise.
- Preserve the version line format exactly.
- Mention old and new version numbers in the summary.

### Do not change unless explicitly asked
- input selection logic
- volume mapping logic
- relay logic
- output delay logic
- PGA2310 control logic
- pin assignments, except where explicitly required for proven LCD operation

### Display formatting defaults
- Keep LCD formatting changes minimal.
- Unless explicitly asked otherwise:
  - line 1 shows the selected input
  - line 2 shows volume in dB
