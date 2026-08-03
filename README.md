# Kegboard

Kegboard is the kegerator controller firmware from the [Kegbot][kegbot] project.
It reads flow meters, temperature sensors, and auth tokens, drives valves and
relays, and reports pours to a [Kegbot Server][kegbot-server].

This is **Kegboard 2.x**, a ground-up rewrite for **ESP32** built on
[ESPHome][esphome].

> **Looking for the Arduino version?** The original AVR firmware lives on the
> [`arduino`](https://github.com/Kegbot/kegboard/tree/arduino) branch. It is
> feature-frozen and does not speak the same protocol as this one.

## What changed, and why

The old Kegboard was a dumb sensor pipe: it streamed raw tick counts over USB
serial, and a host daemon (`kegbot-pycore`) assembled those ticks into pours and
posted them to the server. That meant a cable, a always-on host, and a lost pour
whenever the host was down.

Kegboard 2.x is a networked appliance:

- **Pours are assembled on the device.** The board detects the start and end
  of a pour, applies its own calibration, and reports a finished pour with an
  authoritative `volume_ml`.
- **One simple protocol.** Everything is JSON batches to a single endpoint
  (`POST /kegboard-event`), specified in
  [docs/kegboard-event-protocol.md](docs/kegboard-event-protocol.md) with
  normative JSON Schemas. Any server can implement it.
- **Outages are survivable.** Events queue and deliver late with correct
  timestamps — even on a board whose clock never synced — and retries can
  never create a duplicate drink.
- **No API keys to carry around.** An unprovisioned board shows up on the
  server dashboard by name; click allow and it provisions itself.
- **No `kegbot-pycore`, no USB cable.** WiFi and HTTP.
- **It's ESPHome.** Adding a display, a pressure sensor, a second thermometer,
  or a different RFID reader is YAML you write, not a firmware release we ship.

## Status

Early. Under active construction — see the table below.

| Area | State |
|---|---|
| Core logic (pour detection, event protocol, grants) | Done, unit tested |
| `kegboard` hub + `kegboard_meter` | Done |
| `kegboard_reporter` (event protocol, pairing, commands) | Done |
| `kegboard_auth` (server-decided grants, local mode) | Done |
| Temperature via stock `dallas_temp` | Works |
| Relays with watchdog, buzzer, flow LEDs | Done |
| Auth readers (RFID, iButton) | Done |
| Server implementing the protocol | In progress (kegbot-server) |
| Prebuilt binaries + web installer | Planned |

Nothing has been validated against real hardware and a live Kegbot Server yet.

Reporting is **HTTP only** for now. MQTT, BLE, and WebSocket transports are
planned.

## Configuration

A minimal two-tap board reporting to a server:

```yaml
external_components:
  - source: github://Kegbot/kegboard@main

kegboard:
  id: kb

kegboard_meter:
  - id: flow0
    pin: GPIO4
    index: 0
    total:
      name: Tap 1 Ticks
    pouring:
      name: Tap 1 Pouring

kegboard_reporter:
  reporting_url: !secret kegboard_reporting_url
  meters: [flow0]
```

See `examples/` for complete configs, and `boards/` for pin maps.

### `kegboard`

| Option | Default | Notes |
|---|---|---|
| `serial_number` | `kegboard-<mac>` | Device identity in the protocol. Set explicitly to adopt a replaced board's identity. |

### `kegboard_meter`

| Option | Default | Notes |
|---|---|---|
| `pin` | required | Meter input. Pulled up internally; counts falling edges. |
| `meter_number` | `0` | The protocol's meter number: `(device, meter_number)` identifies a tap server-side. Must be unique per meter (validated at build). The YAML `id` is a config-internal reference and is never reported. |
| `ml_per_tick` | `0.185` | SwissFlow SF800 and clones (~5.4 ticks/mL). The device's calibration is authoritative: reported volume comes from this. |
| `debounce` | `1200us` | Matches the legacy firmware's filter. |
| `idle_timeout` | `10s` | Silence after which a pour is considered finished. |
| `min_pour_ticks` | `3` | Anything shorter is treated as a drip and discarded. |
| `max_pour_duration` | `5min` | Safety cutoff for a stuck meter; `0s` disables. |
| `report_interval` | `250ms` | Throttle for sensor updates during a pour. |
| `series_resolution` | `100ms` | Bucket width for the diagnostic tick series; `0s` disables. |

Optional entities: `total`, `volume`, `flow_rate`, `pouring`.
Triggers: `on_pour_start`, `on_pour_end` (with `ticks`, `volume_ml`, `duration_ms`).
Actions: `kegboard_meter.reset_total`, `.end_pour`, `.set_calibration`.

### `kegboard_reporter`

Speaks the [Kegboard Event Protocol](docs/kegboard-event-protocol.md).

| Option | Default | Notes |
|---|---|---|
| `reporting_url` | required | Full URL, path included, e.g. `https://kegbot.example.com/api/kegboard-event`. No credential is configured — the device provisions its own bearer token by pairing via the server dashboard, and it persists in flash. |
| `meters` | `[]` | Meters whose pours are reported. |
| `thermo_sensors` | `[]` | `sensor:`/`name:` pairs; any ESPHome sensor works. |
| `heartbeat_interval` | `60s` | Status event cadence; also bounds worst-case command latency. |
| `pour_update_interval` | `1s` | Live `pour_update` cadence; `0s` disables. |
| `retry_interval` | `30s` | Base for exponential backoff, capped at 5 min. |

Optional diagnostic entities: `queue_depth`, `dropped`. A non-zero `dropped`
means events were lost and is worth alerting on.

### `kegboard_auth`

Applies [authenticated pouring](docs/authenticated-pouring.md): per-meter
grants, decided by the server (or locally), driving valve toggles and pour
attribution.

| Option | Default | Notes |
|---|---|---|
| `mode` | `server` | `server`: every token presentment is decided by the server, which chooses the meters, user, and duration. `local`: every token pours as guest, serverlessly. |
| `gates` | `[]` | `meter:` plus optional `toggle:` (valve relay). A gate without a toggle gets attribution only. |
| `offline_policy` | `deny` | Token presented while the server is unreachable: `deny`, or `guest` (attribution only — never opens valves). |
| `max_grant_duration` | `5min` | Device-side clamp on server-issued grants: the final bound on valve-open time. |
| `local_grant_duration` | `30s` | Grant length in `local` mode and for offline-guest grants. |

Actions: `kegboard_auth.token_attached` / `.token_detached` (`device`,
`token`), `.revoke`. Condition: `.is_authorized`. Triggers: `on_authorized`
(`user`), `on_denied` (`reason`), `on_revoked`. Optional entities:
`authorized`, `user`.

### `kegboard_onewire`

iButton presence on a 1-Wire bus — ESPHome's `one_wire` enumerates devices but
has no arrive/leave events. Triggers `on_token_attached` and
`on_token_detached` with the ROM code as hex.

`max_missed_searches` (default `4`) is how many consecutive misses before a
detach is reported. A held iButton makes intermittent contact, so reporting on
the first miss would make it flap several times a second.

### Relays and buzzer

`packages/relays.yaml` and `packages/buzzer.yaml` are plain YAML over stock
components. The relay watchdog is worth keeping from the AVR firmware: a relay
left on is usually a valve held open, so each one switches itself off after
`relay_watchdog_timeout` (default `10s`) unless something turns it on again.

## Repository layout

```
components/     ESPHome external components (this repo is the component source)
  kegboard/     Hub component + the framework-agnostic core (see CORE.md)
  kegboard_meter/     Flow meter and pour detection
  kegboard_reporter/  Event protocol client (batching, pairing, commands)
  kegboard_auth/      Per-meter authorization
  kegboard_onewire/   iButton presence
packages/       Composable YAML users include
boards/         Pin maps per target board
docs/           Protocol specifications
schemas/        Normative JSON Schemas for the protocol
examples/       Worked configurations
tests/core/     Host unit tests -- plain g++, no hardware, no toolchain
script/         CI helpers
```

## Development

Host unit tests for the core logic need nothing but a C++ compiler:

```console
$ make -C tests/core
$ make -C tests/core STRICT=1   # warnings as errors, as CI runs it
```

Build output is scoped by OS and architecture (`build/Darwin-arm64/`, etc.), so
a checkout shared between a host and a container or VM won't hand one
platform's binaries to the other.

Formatting and lint mirror ESPHome's own conventions, since these components
compile into ESPHome's tree:

```console
$ pip install pre-commit && pre-commit install
$ pre-commit run --all-files
```

## Debugging

The reporter logs one line per delivery at `DEBUG` (status, event count,
bytes). To see the actual protocol traffic — every request and response body,
including `pour_update`s while beer is flowing — raise its log level to
`VERY_VERBOSE`:

```yaml
logger:
  logs:
    kegboard_reporter: VERY_VERBOSE
```

Then attach with `esphome logs <config>.yaml`. Two notes:

- `VERY_VERBOSE` lines are compiled out at default log levels, so production
  builds pay nothing for this machinery.
- The logger truncates lines to its buffer (default 512 bytes); a full batch
  body will clip. Add `logger: { tx_buffer_size: 2048 }` if that bites.

## Hardware notes

**ESP32 GPIOs are not 5 V tolerant.** Most beer flow meters are open-collector
hall-effect sensors, which are safe on a 3.3 V pull-up because they only ever
pull the line to ground. Meters with a push-pull 5 V output will damage the
ESP32 and need a level shifter or divider. Check your meter before wiring it.

## License and copyright

Kegboard 2.x is offered under the **MIT** license, matching the rest of the
Kegbot project; see `LICENSE.txt`.

Two notes on what that does and doesn't cover:

- **Built firmware images are GPLv3.** ESPHome's C++ runtime is GPLv3, and a
  compiled Kegboard image links against it. MIT is GPL-compatible, so this is
  fine and is how ESPHome external components normally work — but if you
  distribute binaries, you are distributing GPLv3 binaries. The source in this
  repository remains MIT and is reusable as such.
- **The `arduino` branch is still GPLv2-or-later.** The legacy AVR firmware was
  written under that license and had outside contributors; nothing here
  relicenses it.

Copyright 2003-2026 The Kegbot Project Contributors <info@kegbot.org>

[kegbot]: https://kegbot.org/
[kegbot-server]: https://github.com/Kegbot/kegbot-server
[esphome]: https://esphome.io/
