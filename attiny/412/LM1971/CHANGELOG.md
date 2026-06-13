# Changelog

## [Unreleased]

- Correct LM1971 serial writes to send the required address byte before the attenuation byte.
- Correct LM1971 attenuation constants to use `0x00` to `0x3E`, with `0x3F` and above as mute.
- Keep LM1971 LOAD high when idle and low only during a 16-bit transfer.
- Apply the current potentiometer level immediately after the safe startup mute write.
- Add startup settling, repeated initial writes, slower control-bit timing, averaged pot reads, and periodic level refresh for more reliable power-up.
- Add a small ADC mute zone at the bottom of potentiometer travel so minimum volume sends true LM1971 mute instead of the quietest non-mute attenuation.
