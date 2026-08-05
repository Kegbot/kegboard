# Theory of Operation

The internal design of the firmware. Skippable; read it before hacking on the
components or debugging a deployment.

## Structure

Kegboard-specific logic lives in a framework-agnostic core (`kbcore`
namespace, plain C++, no ESPHome/ESP-IDF headers, time passed in as
arguments) that runs under host unit tests. The ESPHome components are thin
adapters around it. Rationale and rules in
[`components/kegboard/CORE.md`](https://github.com/Kegbot/kegboard/blob/main/components/kegboard/CORE.md).

## Flow sensing and pour detection

Each meter pin counts falling edges in an interrupt handler, with a software
debounce (default 1200 µs, matching the legacy firmware). Open-collector
hall-effect meters emit a fixed volume per pulse, so volume is tick count ×
`ml_per_tick` — an odometer.

On top of the counter runs a pour state machine:

- The first tick after idle **starts a pour** (`on_pour_start`, `pouring`
  goes on).
- Silence for `idle_timeout` **ends it**. Pours shorter than
  `min_pour_ticks` are discarded as drips; pours exceeding
  `max_pour_duration` are cut off as a stuck meter.
- The finished pour carries ticks, `volume_ml`, duration, attribution from
  the meter's grant (if any), and a bounded tick time series
  (`series_resolution` buckets) for diagnostics.

Calibration is applied on the device; the reported `volume_ml` is
authoritative, raw ticks are advisory.

## Reporting

The reporter batches events — pours, live pour updates, temperatures, token
presentments, heartbeats, command results — and POSTs them as JSON to
`reporting_url` per the [event protocol](kegboard-event-protocol.md).

- **Queueing.** Events that can't be delivered wait in a bounded queue and
  retry with exponential backoff (base `retry_interval`, capped at 5 min).
  Each event carries its age, so a batch delivered late lands with correct
  timestamps even on a board whose clock never synced. The queue is bounded
  RAM: when full, the oldest events are evicted first — during a long outage
  the most recent pours are the ones worth keeping — and the `dropped`
  counter advances. Events do not survive reboot.
- **Idempotency.** Delivery is at-least-once; `(device, boot_id, id)` makes
  processing idempotent, so a retry can never create a duplicate drink.
- **Heartbeats** every `heartbeat_interval` give the server a liveness signal
  and bound worst-case command latency, since server commands ride only in
  HTTP responses.
- **Pairing.** A 401 sends the device into pairing: it appears on the server
  dashboard, and on approval receives a bearer token, persisted in flash.
  A later 401 revokes it and restarts pairing.

## Authorization

`kegboard_auth` holds no token database and no meter↔relay map — only the
currently active grants, one per meter, each naming its own meters and
relays from the server. A token presentment is flushed to the server
immediately and the decision (`authorize`/`deny`) returns in the same HTTP
round trip; grants end at their server-set limits — volume poured, total
time, idle time — with total time always clamped to `max_grant_duration`,
on token detach, or on a server `deauthorize`. Every ending is reported
upstream as a `grant_end` event naming the reason. Full semantics in
[Authenticated Pouring](authenticated-pouring.md).

## Relays

A relay left on is usually a valve held open. Each relay in
`packages/relays.yaml` starts a watchdog timer when switched on and switches
itself off after `relay_watchdog_timeout`. The timer runs from the on-edge
and cannot be refreshed while the relay is on; it protects relays driven
manually or from Home Assistant. Grant-driven relays are bounded by the
grant clamp (`max_grant_duration`) instead — **set the watchdog longer than
the clamp** (or `0s`) on relays the server grants, or the watchdog will
close the valve mid-grant.

## Buzzer

When a passive piezo is connected, the board plays melodies transcribed from
the AVR firmware:

| Event | Sound |
|---|---|
| Boot complete | Short musical tune (`play_boot_melody`) |
| Token authorized | Rising three-note chirp (`play_auth_melody`) |
| Ping | Two notes (`play_ping_melody`) |

The scripts are plain `rtttl`; wire them to any trigger you like.
