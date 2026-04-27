## [Unreleased]

## [v0.2.5] - 2026-04-27

### Fixed
- XSMT now also unmutes inside `on_audio_write` as a belt-and-suspenders
  trigger. v0.2.4 only unmuted in `on_stream_start`, but that event isn't
  guaranteed to fire on every resume (e.g. mid-track resume after a
  device reboot), leaving the DAC muted while audio data was actually
  flowing.

## [v0.2.4] - 2026-04-27

### Added
- Hardware mute support via PCM5102A's XSMT pin. When `CONFIG_PCM_XSMT_GPIO`
  is set (default GPIO 7 / D8), the firmware drives XSMT low between tracks
  and high during playback — silences the DAC's idle noise floor.
  Requires lifting the H3L jumper on the DAC breakout and wiring XSMT to
  the GPIO. Set `CONFIG_PCM_XSMT_GPIO = -1` to disable and keep the H3L
  jumper bridged (original behavior).

## [v0.2.3] - 2026-04-27

### Added
- `GET /status` now returns version, build date, ESP-IDF version, uptime,
  free / min-free heap, RSSI, and MAC alongside the running OTA partition.
  Lets you verify which firmware is actually running after an OTA without
  needing serial access.

## [v0.2.2] - 2026-04-27

### Changed
- `software_version` reported to Music Assistant is now auto-derived from
  `git describe --tags --dirty` at build time (via ESP-IDF's
  `esp_app_get_description()`). No more manually bumping a hard-coded
  string each release.

### Fixed
- Pages workflow now correctly redeploys the web flasher after every
  release. The `workflow_run` trigger had a `branches: [main]` filter
  that silently never matched tag-triggered upstream runs (their
  `head_branch` is the tag, not main).

## [v0.2.1] - 2026-04-27

### Fixed
- Software version reported to Music Assistant was stale (`0.1.0`) — now
  matches the release tag.

## [v0.2.0] - 2026-04-27

### Added
- WiFi setup page now has a **Device name** field. Persisted to NVS and used
  for both mDNS advertisement and the Sendspin client identity. Empty
  submission falls back to the Kconfig default at next boot.

## [v0.1.0] - 2026-04-27

Initial public release.

### Features
- Native ESP-IDF firmware for the Seeed XIAO ESP32-S3 + PCM5102A DAC,
  speaking the [Sendspin](https://github.com/Sendspin/sendspin-cpp) protocol
  for Music Assistant.
- Captive-portal WiFi provisioning (SoftAP + DNS hijack).
- OTA updates via HTTP POST to `/ota`, optional bearer-token auth.
- mDNS service advertisement (`_sendspin._tcp` + `_sendspin-ota._tcp`).
- Status LED reflects connection state.
- Persisted volume, mute, and last-server hash across reboots.
- Web installer at <https://Jesstr8803.github.io/sendspin-xiao/> (Chrome/Edge,
  WebSerial). Merged binary served from GitHub Pages with CORS so the
  installer works without a proxy.
