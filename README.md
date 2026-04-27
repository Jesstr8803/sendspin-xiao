# sendspin-xiao

Standalone ESP-IDF firmware for a **Seeed Studio XIAO ESP32-S3** + **PCM5102A** analog DAC speaking the [Sendspin protocol](https://www.sendspin-audio.com/) natively. Designed to be a compact, synchronized multi-room audio endpoint for [Music Assistant](https://music-assistant.io).

Repository: <https://github.com/Jesstr8803/sendspin-xiao>

## Quick install (no toolchain needed)

If you just want to flash this onto your XIAO and use it (not build/develop):

1. Open <https://Jesstr8803.github.io/sendspin-xiao/> in **Chrome, Edge, or Opera**
2. Plug your XIAO ESP32-S3 in via USB-C
3. Click **Install** — your browser writes the firmware directly via WebSerial. ~30 seconds.
4. Device boots into WiFi setup mode (open SSID `SendspinXIAO-XXXXXX`); follow the on-device prompts. You can name the endpoint here too (shows up in Music Assistant).
5. After it joins WiFi, Music Assistant should auto-discover the new player.

Updates after the first install are pushed over WiFi — see the OTA section below.

Versions and what changed in each: see [CHANGELOG.md](CHANGELOG.md).

For the manual `esptool.py` route or to build from source, see the rest of this README.

No ESPHome required — this is pure ESP-IDF consuming the official `sendspin-cpp` SDK directly.

## Why another Sendspin endpoint?

The excellent [SendspinZero](https://github.com/RealDeco/SendspinZero) project by @RealDeco showed that a ~$10 ESP32-S3 board + PCM5102A DAC makes a brilliant little Music Assistant endpoint, running ESPHome. This project keeps the same hardware concept but swaps the firmware to a standalone ESP-IDF build — useful when you want full control of the audio pipeline, non-ESPHome architecture, or have no need for ESPHome's larger feature set on a dedicated audio device.

## Hardware

Exact same BOM as SendspinZero's analog variant:

| Component | Notes |
|-----------|-------|
| Seeed Studio **XIAO ESP32-S3** | 8MB flash, 8MB octal PSRAM. Onboard PCB antenna + U.FL for external. |
| **PCM5102A** breakout board | Texas Instruments stereo I²S DAC. ~2Vrms line level. |
| 3.5mm audio jack / RCA | Whatever output you prefer to feed into your amp/speaker. |

Wiring:

| XIAO pin | GPIO | PCM5102A pin |
|----------|------|--------------|
| D3 | GPIO4 | LRCK (LCK / WS) |
| D4 | GPIO5 | BCK |
| D5 | GPIO6 | DIN |
| 3V3 | — | VIN |
| GND | — | GND |

DAC solder jumpers (on the back of most PCM5102A breakouts):
- `H1L → L` (FLT: normal filter)
- `H2L → L` (DEMP: de-emphasis off)
- `H3L → H` (XSMT: unmuted — required, or output is silent)
- `H4L → L` (FMT: I²S format)
- Front jumper: `SCK → GND` (enables internal PLL — required for ESP32 I²S which has no MCLK out by default)

## Features

- Sample-accurate sync with other Sendspin players (µs-level alignment via the protocol's Kalman time-filter)
- FLAC / Opus / PCM codec support (FLAC preferred)
- Software volume + mute with perceptual curve, persisted via NVS
- Auto-discovery in Music Assistant via mDNS
- Status LED (XIAO onboard orange LED) showing WiFi / MA / playback state
- Tuned WiFi stack (permanent `PS_NONE`, max TX power, big TCP windows, 802.11 bgn)
- Time-sync gate on boot: outputs silence until the Kalman filter converges, then clean audio

## Credits and acknowledgements

This project stands on the shoulders of several brilliant open-source efforts:

**Protocol and SDK (Open Home Foundation):**
- [**Sendspin protocol**](https://github.com/Sendspin/spec) — open, documented multi-room audio streaming protocol used by Music Assistant. Formerly "Resonate".
- [**sendspin-cpp**](https://github.com/Sendspin/sendspin-cpp) — the official C++ SDK that implements the protocol. This project is essentially a thin I²S-backed integration of this SDK. Apache 2.0.
- [**Music Assistant**](https://github.com/music-assistant) — the media server this endpoint talks to.

**Hardware and firmware reference:**
- [**SendspinZero**](https://github.com/RealDeco/SendspinZero) by @RealDeco — original ESPHome-based XIAO ESP32-S3 + PCM5102A design. This project uses the same hardware and was inspired by SendspinZero's elegant minimalism. Go buy one of those first; use this firmware if/when you want a pure ESP-IDF stack instead.
- [**xiaozhi-esphome**](https://github.com/RealDeco/xiaozhi-esphome) — ESPHome configurations and modular Sendspin component references.
- [**ESPHome i2s_audio speaker**](https://github.com/esphome/esphome/tree/dev/esphome/components/i2s_audio) — reviewed this code to understand the I²S event-callback + queue pattern that informed our notify-timing architecture.

**Transitive dependencies (pulled in via ESP-IDF Component Manager):**
- [**micro-flac**](https://github.com/esphome/micro-flac) and [**micro-opus**](https://github.com/esphome/micro-opus) — codec decoders. Small footprint, ESP32-friendly.
- [**ArduinoJson**](https://github.com/bblanchon/ArduinoJson) by @bblanchon — JSON parsing for the Sendspin control messages.
- [**esp_websocket_client**](https://components.espressif.com/components/espressif/esp_websocket_client) — Espressif's WebSocket client.
- [**ESP-IDF**](https://github.com/espressif/esp-idf) — Espressif's IoT development framework.

**Development assistance:**
- Substantial portions of this firmware (architecture, debugging, and implementation) were written in pair-programming sessions with [Claude](https://claude.ai) (Anthropic's AI assistant, running as Claude Code). All design decisions and final code review were human.

If any of this firmware is useful to you, please also star / contribute to the above projects — they did the hard work.

## Build requirements

- ESP-IDF **v5.5 or later** (required by `sendspin-cpp`)
- Python 3.8+
- Target: `esp32s3`

## Setup

```bash
git clone https://github.com/Jesstr8803/sendspin-xiao.git
cd sendspin-xiao
idf.py set-target esp32s3
idf.py menuconfig      # Configure under "Sendspin XIAO" menu
```

Under `menuconfig → Sendspin XIAO`, set:
- WiFi SSID + password
- Device name (shown in Music Assistant)
- I²S pins (defaults match the wiring table above)
- Status LED GPIO (default 21, active-low — the XIAO's onboard orange LED)

## Build and flash

```bash
idf.py build
idf.py -p COM7 flash monitor   # adjust port
```

On first build, the Component Manager fetches `sendspin/sendspin-cpp` and its dependencies (micro-flac, micro-opus, ArduinoJson, esp_websocket_client, mdns). Expect 3-5 minutes for the initial build; subsequent builds are much faster (ccache-friendly).

## Boot flow

0. Init NVS. Try saved WiFi credentials → Kconfig defaults. If neither is usable (no NVS entry AND Kconfig SSID is the placeholder `"your-wifi-ssid"`, OR STA connect failed after retries), drop into the captive-portal provisioning flow described above and stay there until the user submits creds.
1. Init WiFi STA, wait for IP. Max TX power, permanent `WIFI_PS_NONE`, 802.11 b/g/n.
2. Advertise `_sendspin._tcp` on port 8928 via mDNS, TXT `path=/sendspin`.
3. Construct `SendspinClient` with time-burst interval 2s × 16 samples for fast Kalman convergence.
4. Add the player role with FLAC/Opus/PCM format advertisements.
5. Wire `I2sAudioSink` as the `PlayerRoleListener`; decoded PCM goes through software volume scaling → 32KB scratch buffer → I²S DMA → PCM5102A.
6. Wire `NvsPersistence` as the `SendspinPersistenceProvider`; volume / mute / static_delay / last_server_hash all restored on boot.
7. Pin SDK pthreads (sync task, WebSocket client) to core 1 so they don't contend with WiFi/lwIP on core 0.
8. `client.start_server()` — MA discovers and connects.
9. Audio output is gated on `client.is_time_synced()`: silence is emitted until the Kalman filter converges, then true audio. Avoids desync-on-boot artifacts.

## Expected behavior

- Device appears in Music Assistant as whatever you set as its name.
- Can be grouped with other Sendspin players for multi-room sync.
- Volume, mute, track navigation controlled via MA UI.
- Reconnects automatically after network drops.
- Volume and mute state persist across reboots.

## WiFi provisioning (no flashing required)

The device handles its own WiFi setup — you don't need to hardcode credentials in `menuconfig` if you don't want to.

**Boot logic:**
1. Try WiFi credentials saved in NVS first (set during a previous provisioning)
2. If those fail OR no creds exist, fall back to Kconfig defaults (whatever's in `WIFI_SSID`/`WIFI_PASSWORD`)
3. If both fail OR `WIFI_SSID` is the placeholder `"your-wifi-ssid"`, drop into provisioning mode

**Provisioning mode (SoftAP captive portal):**

When the device can't connect, it spins up an open SoftAP named `SendspinXIAO-XXXXXX` (where `XXXXXX` is the last 6 hex digits of its MAC). 

1. Connect a phone or laptop to that network
2. The captive portal popup should appear automatically. If it doesn't, open a browser and go to **`http://192.168.4.1/`**
3. (Optional) Type a friendly **device name** at the top — this is how the endpoint shows up in Music Assistant. Leave blank to keep the Kconfig default (`Sendspin XIAO Native`).
4. Pick your WiFi network from the dropdown (or type a hidden SSID manually), enter the password
5. Hit **Save and connect** — the device saves credentials + name to NVS and reboots into normal STA mode

**Re-trigger provisioning** without flashing: the OTA server has a `/forget-wifi` endpoint:

```bash
curl -X POST http://<device-ip>:8080/forget-wifi \
     -H "Authorization: Bearer <your-token>"
```

(Omit the `-H` line if no token is configured.) Device clears its NVS WiFi creds and reboots into provisioning mode.

## OTA updates (over WiFi)

Once the device is on your network, you can push new firmware wirelessly — no USB required.

**One-time setup:**

1. (Optional) In `menuconfig → Sendspin XIAO`, set **OTA bearer token** if you want to require auth. By default it's empty, meaning anyone on your local network can push firmware. For a personal device on a trusted home network, that's fine.
2. First-time install still needs a USB flash because the partition table is changing from single-app to dual-OTA layout.

**Subsequent updates:**

```bash
idf.py build
python tools/ota_push.py <device-ip>                 # no auth
python tools/ota_push.py <device-ip> --token <your-token>   # if you set one
```

The push script can also auto-discover the device via mDNS if you have the `zeroconf` Python package installed (`pip install zeroconf`):

```bash
pip install zeroconf
python tools/ota_push.py --token <your-token>           # auto-finds the device
python tools/ota_push.py --discover-only --token foo    # just lists devices
```

**Endpoints exposed by the OTA server:**
- `POST /ota` — accepts a firmware binary as the request body. Requires `Authorization: Bearer <token>` header (if a token is set).
- `GET /status` — returns JSON with the currently running partition and address.
- `POST /forget-wifi` — clears stored WiFi credentials and reboots into provisioning mode. Same auth as `/ota`.

**How it works:** the device runs a tiny HTTP server on port 8080 (configurable). On a successful push, the new firmware is written to the inactive OTA slot, marked as the next boot partition, and the device reboots. If the bearer token is missing or wrong, the request is rejected.

**Security notes:**
- The OTA endpoint is plain HTTP (not HTTPS) for simplicity. Treat it like any local IoT device: only put it on a trusted network.
- The bearer token is the only access control. Use a long random string.
- The first byte of the upload is checked against ESP's image magic byte (`0xE9`) so non-firmware uploads are rejected before flash.

## Status LED patterns

The XIAO's onboard orange LED (GPIO21, active-low by default) blinks to indicate state:

| Pattern | Meaning |
|---------|---------|
| Slow blink (1s on / 1s off) | No WiFi |
| Fast blink (200ms on / 200ms off) | WiFi OK, not connected to Music Assistant |
| Brief heartbeat blip every 3s | Connected, idle |
| Solid on | Playing audio |

## Configuration knobs worth tuning

All under `menuconfig → Sendspin XIAO`:

- `WIFI_SSID`, `WIFI_PASSWORD` — *fallback only*; the captive portal flow above is the normal way to set these. Leave them as the defaults (`your-wifi-ssid` / `your-wifi-password`) and the firmware will go straight to provisioning on a fresh NVS.
- `DEVICE_NAME` — Music Assistant display name. Also overridable from the captive portal (saved to NVS, takes precedence over this Kconfig value at next boot).
- `SENDSPIN_PORT` / `SENDSPIN_PATH` — defaults 8928 and `/sendspin` match the Sendspin spec defaults
- `I2S_LRCK_GPIO`, `I2S_BCK_GPIO`, `I2S_DOUT_GPIO` — in case you want different wiring
- `STATUS_LED_GPIO`, `STATUS_LED_ACTIVE_LOW` — set GPIO to -1 to disable

In `main/main.cpp`, also worth editing:
- `PlayerRoleConfig::audio_formats` — reorder if you prefer a different codec
- `SendspinClientConfig::time_burst_interval_ms` — lower for faster Kalman convergence, higher for less chatter

## Troubleshooting

- **Component Manager errors on first build** — ensure ESP-IDF is v5.5+ (`idf.py --version`). The sendspin-cpp component requires modern Component Manager features.
- **WiFi fails to connect** — XIAO ESP32-S3 is 2.4GHz only. On mesh networks try using a dedicated IoT SSID on 2.4GHz. Signal below -75dBm can cause intermittent drops; the tuned WiFi settings in `sdkconfig.defaults` help but aren't magic.
- **Doesn't show up in MA** — confirm same VLAN as the MA server, mDNS multicast isn't blocked. From Linux: `avahi-browse -r _sendspin._tcp`. From macOS: `dns-sd -B _sendspin._tcp`.
- **Wrong-pitch audio or silence** — check DAC jumpers (especially `H3L → H` for unmute and `SCK → GND` for PLL enable). Watch the boot log for `I2S reconfigured: X Hz, Y ch, Z-bit` to confirm sample rate.
- **Choppy / skipping audio** — check the RSSI in boot log. Anything above -65 dBm is fine; below that, move closer or add an external antenna via the U.FL connector.

## Known limitations

- **16-bit audio pipeline only.** Our software volume scaling and the software muting path are written for 16-bit samples. 24/32-bit audio would work as pass-through but volume/mute wouldn't apply. Music Assistant typically serves 16-bit so this isn't a practical issue.
- **Opus decode sounds wrong** if advertised first. The Opus decoder path in our integration has some format mismatch we haven't debugged yet; leaving FLAC preferred sidesteps it.
- **Volume is software-only.** The PCM5102A supports hardware mute via the XSMT pin, which we don't yet drive (planned).

## Roadmap

- [x] OTA updates over WiFi
- [x] SoftAP captive portal for first-boot WiFi provisioning
- [ ] Hardware-level mute via PCM5102A's XSMT pin (low-priority polish — software mute is fine in practice)
- [ ] Fade-in on `stream_start` (decided against — would fade between tracks in a queue too)
- [ ] Debug the Opus decoder format path
- [x] Expanded `/status` endpoint with version, uptime, RSSI, heap, MAC for monitoring

## License

This firmware is released under the **Apache License 2.0**, matching `sendspin-cpp`.

Hardware design credit to @RealDeco / SendspinZero.
Protocol by the [Open Home Foundation](https://www.openhomefoundation.org/) / Sendspin.
