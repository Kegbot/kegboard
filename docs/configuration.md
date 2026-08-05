# Configuration Reference

Kegboard is configured in ESPHome YAML. This chapter covers the Kegboard
components; everything else (WiFi, sensors, displays, automations) is stock
ESPHome — see [esphome.io](https://esphome.io/). `examples/` has complete
configs; `packages/` has the composable pieces.

```yaml
external_components:
  - source: github://Kegbot/kegboard@main
```

## `kegboard`

The hub. Required by every other component.

| Option | Default | Notes |
|---|---|---|
| `serial_number` | `kegboard-<mac>` | Device identity in the protocol. Set explicitly to adopt a replaced board's identity. |

## `kegboard_meter`

One entry per flow meter.

```yaml
kegboard_meter:
  - id: flow0
    pin: GPIO4
    meter_number: 0
    total:
      name: Tap 1 Ticks
    pouring:
      name: Tap 1 Pouring
```

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
Triggers: `on_pour_start`, `on_pour_end` (with `ticks`, `volume_ml`,
`duration_ms`).
Actions: `kegboard_meter.reset_total`, `.end_pour`, `.set_calibration`.

## `kegboard_reporter`

Speaks the [Kegboard Event Protocol](kegboard-event-protocol.md) to a server.

| Option | Default | Notes |
|---|---|---|
| `reporting_url` | required | Full URL, path included, e.g. `https://kegbot.example.com/api/kegboard-event`. No credential is configured — the device provisions its own bearer token by pairing via the server dashboard, and it persists in flash. |
| `meters` | `[]` | Meters whose pours are reported. |
| `relays` | `[]` | The device's numbered relays: `relay_number:` plus `relay:` (the relay to drive, e.g. from `packages/relays.yaml`). Reported in the `status` inventory, and the targets of server grants (and of `set_relay`, once that reserved command is specified). |
| `thermo_sensors` | `[]` | `sensor:`/`name:` pairs; any ESPHome sensor works. |
| `heartbeat_interval` | `60s` | Status event cadence; also bounds worst-case command latency. |
| `pour_update_interval` | `1s` | Live `pour_update` cadence; `0s` disables. |
| `retry_interval` | `30s` | Base for exponential backoff, capped at 5 min. |

Optional diagnostic entities: `queue_depth`, `dropped`. A non-zero `dropped`
means events were lost and is worth alerting on.

## `kegboard_auth`

Applies [authenticated pouring](authenticated-pouring.md): server-decided
grants driving valve relays and tagging pours for server-side attribution.
Requires a `kegboard_reporter`. (Serverless installs can gate valves with
plain ESPHome automations on the reader triggers instead.)

| Option | Default | Notes |
|---|---|---|
| `offline_policy` | `deny` | Token presented while the server is unreachable: `deny` (signal refusal), or `guest` (stay silent; pours proceed as guest pours). Neither opens valves. |
| `max_grant_duration` | `5min` | Device-side clamp on server-issued grants: the final bound on valve-open time. |

Actions: `kegboard_auth.token_attached` / `.token_detached` (`device`,
`token`), `.revoke`. Condition: `.is_authorized`. Triggers: `on_authorized`
(`auth_device`, `token`), `on_denied` (`reason`), `on_revoked`. Optional
entities: `authorized`.

Readers feed it through the actions — see `examples/kegbot-full.yaml` for
RFID and iButton wiring.

## `kegboard_onewire`

iButton presence on a 1-Wire bus — ESPHome's `one_wire` enumerates devices
but has no arrive/leave events. Triggers `on_token_attached` and
`on_token_detached` with the ROM code as hex.

| Option | Default | Notes |
|---|---|---|
| `one_wire_id` | required | The bus to poll. Use a bus separate from the thermo sensors. |
| `update_interval` | `1s` | Poll cadence. |
| `max_missed_searches` | `4` | Consecutive misses before a detach is reported. A held iButton makes intermittent contact; reporting on the first miss would flap several times a second. |

## Packages

Plain YAML over stock components; include what you have.

| Package | Provides |
|---|---|
| `packages/base.yaml` | WiFi + fallback AP, OTA, logging, native API, SNTP time, HTTP client. Expects `name` and `friendly_name` substitutions. |
| `packages/relays.yaml` | Two GPIO relays, each with a watchdog: auto-off after `relay_watchdog_timeout` (default `10s`, `0s` disables — but consider what's downstream first). |
| `packages/buzzer.yaml` | Passive piezo via `rtttl`, with the legacy boot/auth/ping melodies as scripts. |
| `boards/*.yaml` | Chip selection + pin-map substitutions. See [Hardware & Wiring](hardware.md). |
