## [Unreleased]

## [v0.2.13] - 2026-05-01

### Changed
- Back to 1000ms / 32 time-burst (matches v0.2.11). On further listening
  the tighter sync did help; reverting v0.2.12's revert.

## [v0.2.12] - 2026-05-01

### Reverted
- `time_burst_interval_ms` 1000 → 2000 and `time_burst_size` 32 → 16
  (back to v0.2.10 values). v0.2.11's tighter bursts didn't produce
  audible improvement worth the doubled sync chatter.

## [v0.2.11] - 2026-04-28

### Changed
- `time_burst_interval_ms` 2000 → 1000 and `time_burst_size` 16 → 32.
  Kalman time-sync filter now gets twice as many bursts per second
  with each burst twice as long. Tightens timing precision for
  multi-room alignment with minimal network overhead (sync packets
  are tiny).

## [v0.2.10] - 2026-04-28

### Fixed
- `set_xsmt()` is now edge-triggered: it only writes the GPIO and
  increments `xsmt_toggles` when the state actually changes. Previously
  every `on_audio_write` redundantly re-drove the pin high, thrashing
  the register thousands of times per second and making the counter
  meaningless. The metric now reports real mute/unmute transitions.

## [v0.2.9] - 2026-04-28

### Added
- `/status` now reports an `audio` metrics block: total writes, bytes
  requested vs written, current frames buffered, stream-start/clear
  counts, reconfigures, XSMT toggles, and max sub-second audio gap (a
  proxy for WiFi jitter during playback). Use this to tune buffer
  size and detect dropouts from data rather than vibes.

## [v0.2.8] - 2026-04-28

### Changed
- Tuned `audio_buffer_capacity` to 2 MB (was 1 MB in v0.2.7, 4 MB before).
  v0.2.7's 1 MB reduced track-transition delays but also surfaced
  audible Kalman sync-correction clicks that the larger buffer had
  been masking. 2 MB splits the difference.

## [v0.2.7] - 2026-04-28

### Changed
- Reduced `audio_buffer_capacity` from 4 MB to 1 MB (~5 seconds @ 48k/16/2).
  Caps the worst-case accumulated transition delay between tracks; still
  ample cushion for WiFi jitter. Experimental — revert if it causes more
  underruns than the larger buffer prevented.

## [v0.2.6] - 2026-04-28

### Fixed
- XSMT now goes back to muted after ~1s of silence even if the stream
  is still technically active. Previously a paused MA player would leave
  XSMT high (no on_stream_clear fires on pause), so the chip stayed
  unmuted and the rail noise leaked through during silence.

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
