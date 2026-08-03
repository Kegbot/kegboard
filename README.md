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

- **Pours are assembled on the device.** The board detects the start and end of
  a pour, applies calibration, and posts a finished drink to Kegbot Server.
- **Outages are survivable.** Undelivered pours are buffered and retried. Kegbot
  Server's API records a pour by *elapsed time*, so a pour delivered an hour
  late still lands with the correct timestamp — even on a board whose clock
  never synced.
- **No `kegbot-pycore`, no USB cable.** WiFi and HTTP.
- **It's ESPHome.** Adding a display, a pressure sensor, a second thermometer,
  or a different RFID reader is YAML you write, not a firmware release we ship.

## Status

Early. Under active construction — see the table below.

| Area | State |
|---|---|
| Core pour logic (state machine, tick series, queue, API requests) | Done, unit tested |
| `kegboard` hub component | Done |
| `kegboard_meter` flow meter component | Done |
| `kegboard_kegbot` HTTP reporter | Done |
| Temperature via stock `dallas_temp` | Works |
| Relays with watchdog, buzzer, flow LEDs | Done |
| Auth tokens (RFID, iButton) | Done |
| Prebuilt binaries + web installer | Planned |

Nothing has been validated against real hardware and a live Kegbot Server yet.

Reporting is **HTTP only** for now. MQTT, BLE, and WebSocket transports are
planned.

## Configuration

A minimal two-tap board reporting to a Kegbot Server:

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

kegboard_kegbot:
  base_url: !secret kegbot_url
  api_key: !secret kegbot_api_key
  meters: [flow0]
```

See `examples/` for complete configs, and `boards/` for pin maps.

### `kegboard`

| Option | Default | Notes |
|---|---|---|
| `serial_number` | `kegboard-<mac>` | Board identity. Meters are named `<serial>.flow<index>`, which is how Kegbot Server keys a tap — set it explicitly to adopt a replaced board's identity. |

### `kegboard_meter`

| Option | Default | Notes |
|---|---|---|
| `pin` | required | Meter input. Pulled up internally; counts falling edges. |
| `index` | `0` | Used to build the default meter name. |
| `meter_name` | `<serial>.flow<index>` | Override to match an existing server-side meter. |
| `ml_per_tick` | `0.185` | SwissFlow SF800 and clones (~5.4 ticks/mL). |
| `debounce` | `1200us` | Matches the legacy firmware's filter. |
| `idle_timeout` | `10s` | Silence after which a pour is considered finished. |
| `min_pour_ticks` | `3` | Anything shorter is treated as a drip and discarded. |
| `max_pour_duration` | `5min` | Safety cutoff for a stuck meter; `0s` disables. |
| `report_interval` | `250ms` | Throttle for sensor updates during a pour. |
| `series_resolution` | `100ms` | Bucket width for the diagnostic tick series; `0s` disables. |

Optional entities: `total`, `volume`, `flow_rate`, `pouring`.
Triggers: `on_pour_start`, `on_pour_end` (with `ticks`, `volume_ml`, `duration_ms`).
Actions: `kegboard_meter.reset_total`, `.end_pour`, `.set_calibration`.

### `kegboard_kegbot`

| Option | Default | Notes |
|---|---|---|
| `base_url` | required | Server root. A trailing `/` or `/api` is accepted. |
| `api_key` | required | Sent as `X-Kegbot-Api-Key`; needs a staff/superuser key. |
| `meters` | `[]` | Meter IDs whose pours are reported. |
| `thermo_sensors` | `[]` | `sensor:`/`name:` pairs; any ESPHome sensor works. |
| `send_volume` | `false` | Off so the server's own per-meter calibration stays authoritative. |
| `retry_interval` | `30s` | Base for exponential backoff, capped at 5 min. |

Optional diagnostic entities: `queue_depth`, `dropped`. A non-zero `dropped`
means pours were lost and is worth alerting on.

### `kegboard_auth`

Turns token events from any reader into a grant: opens a flow toggle, and
attributes pours to the resolved Kegbot user.

| Option | Default | Notes |
|---|---|---|
| `meters` | `[]` | Meters this grant covers. |
| `toggle` | — | Switch to hold on while authorized, typically a valve relay. |
| `kegbot_id` | — | Reporter to resolve usernames through. Without it, every token pours as guest. |
| `grant_duration` | `30s` | How long a momentary scan lasts. An active pour keeps extending it. |
| `require_known_token` | `false` | `true` refuses unregistered tokens instead of pouring them as guest. |

Actions: `kegboard_auth.token_attached` (`device`, `token`), `.token_detached`
(`token`), `.revoke`. Condition: `.is_authorized`. Triggers: `on_authorized`,
`on_denied`, `on_revoked`. Optional entities: `authorized`, `user`.

Authorization is decided **on the device**, so the tap keeps working when the
server is unreachable and the valve opens at local speed. The trade is that a
token revoked server-side stays valid until it is looked up again.

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
  kegboard_meter/   Flow meter and pour detection
  kegboard_kegbot/  Kegbot Server reporter
packages/       Composable YAML users include
boards/         Pin maps per target board
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
