# Snapserver Notes For ESP32 Audio Client v4

This project's **Snapclient mode** is intended to use a **PCM** Snapserver stream on an **ESP32-WROVER-IE-N16R8** target.

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
source = pipe:///tmp/snapfifo?name=ESP32_WROVER_V4&sampleformat=48000:16:2&codec=pcm
```

## Why PCM is still the preferred default here

- it keeps decode work off the ESP32
- it fits the "stable appliance" goal better than reintroducing Opus
- the WROVER revision gives more headroom for buffering, but Wi-Fi quality still matters

## After changing Snapserver

Restart the server so the new stream settings are applied.

Typical Linux service command:

```bash
sudo systemctl restart snapserver
```

## What to watch for

- PCM uses much more network bandwidth than Opus
- 48 kHz / 16-bit / stereo PCM is about 1.536 Mbit/s before overhead
- Wi-Fi quality still matters even with larger client-side buffers

## Bring-up recommendations

- keep the Snapserver on wired Ethernet if possible
- start with a strong 2.4 GHz Wi-Fi signal at the ESP32
- validate Snapclient mode before adding extra hardware changes around the DAC
