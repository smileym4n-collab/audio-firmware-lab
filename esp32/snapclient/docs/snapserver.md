# Snapserver Notes For ESP32 Audio Client v9

This project's **Snapclient mode** now expects a **PCM** Snapserver stream on an **ESP32-WROVER-IE-N16R8** target.

## Recommended stream profile

Use these settings for the stream this client connects to:

- codec: `pcm`
- sample format: `48000:16:2`

That means:

- 48000 Hz
- 16-bit samples
- 2 channels

## Example `snapserver.conf` source line

```ini
source = pipe:///tmp/snapfifo?name=ESP32_WROVER_V9&sampleformat=48000:16:2&codec=pcm
```

## Why this build uses PCM

- the shared ESP32 I2S/DAC path has already been validated through Bluetooth mode
- the remaining distortion and stop behavior were isolated to the Opus Snapclient path
- the project already contains a custom decoder that handles Snapcast's PCM wrapper directly

## After changing Snapserver

Restart the server so the new stream settings are applied.

Typical Linux service command:

```bash
sudo systemctl restart snapserver
```

## What to watch for

- PCM uses more network bandwidth than Opus
- Wi-Fi quality still matters even with larger client-side buffers
- this build still expects a `48000:16:2` playback profile

## Bring-up recommendations

- keep the Snapserver on wired Ethernet if possible
- start with a strong 2.4 GHz Wi-Fi signal at the ESP32
- validate Snapclient mode before adding extra hardware changes around the DAC
