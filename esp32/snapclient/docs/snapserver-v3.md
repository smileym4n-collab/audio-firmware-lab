# Snapserver Notes For ESP32 Snapclient v3

This v3 firmware is intended to be used with a **PCM** Snapserver stream for a plain ESP32 dev board with no PSRAM.

## Why this changed

The ESP32 transport side was already workable, but Opus decode appears to be the unstable part on this hardware target. Switching Snapserver to PCM moves that complexity off the ESP32.

## Required stream profile

Use these settings for the stream that this ESP32 client will connect to:

- codec: `pcm`
- sample format: `48000:16:2`

That means:

- 48000 Hz
- 16-bit samples
- 2 channels

## Example `snapserver.conf` source line

For a FIFO/pipe source, the important part is that the source advertises PCM explicitly:

```ini
source = pipe:///tmp/snapfifo?name=ESP32_PCM_V3&sampleformat=48000:16:2&codec=pcm
```

If you already have a working source line, the minimum required change is usually:

- change `codec=opus` to `codec=pcm`
- make sure `sampleformat=48000:16:2` matches the firmware defaults

## After changing Snapserver

Restart the server so the new stream settings are applied.

Typical Linux service command:

```bash
sudo systemctl restart snapserver
```

## What to watch for

- PCM uses much more network bandwidth than Opus
- 48 kHz / 16-bit / stereo PCM is about 1.536 Mbit/s before overhead
- weak Wi-Fi can still cause dropouts even though CPU load is lower

## Recommended bring-up conditions

- keep the Snapserver on wired Ethernet if possible
- keep the ESP32 close to the access point for initial tests
- avoid heavily congested Wi-Fi during first validation
- do not enable extra processing on the ESP32 until playback is clean
