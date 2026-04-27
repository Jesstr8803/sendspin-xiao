## [Unreleased]

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
