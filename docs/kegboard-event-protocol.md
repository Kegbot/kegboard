# Kegboard Event Protocol

**Version:** 1
**Date:** 2026-08-04

The protocol a Kegboard controller uses to talk to a server. It replaces both
the legacy KBSP serial protocol and the legacy pykeg HTTP API; neither
survives in Kegboard v4.

Companion document: [Authenticated Pouring](authenticated-pouring.md), which
specifies how token presentment, server-side authorization, and valve control
compose on top of this protocol.

## 1. Goals

- **One endpoint, one document.** A third party should be able to receive
  Kegboard data by implementing a single HTTP handler against a single JSON
  Schema. This document plus the schemas is the whole contract.
- **The device is authoritative for volume.** Calibration (`ml_per_tick`)
  lives on the controller. Reports carry `volume_ml` always; raw ticks are
  advisory diagnostics.
- **Outage-proof.** Events queue on the device and deliver late with correct
  timestamps, without requiring the device to have a synchronized clock.
- **Exactly-once effect.** Delivery is at-least-once; event ids make
  processing idempotent, so a retry can never create a duplicate drink.
- **No walking keys around.** An unprovisioned device pairs through an
  allow/deny decision on the server dashboard (§8); the user never handles a
  credential.
- **Transport-portable.** The envelope is defined over HTTP here, but carries
  no HTTP-isms in the body, so the same messages can later ride MQTT, BLE, or
  WebSocket unchanged.

### Non-goals (v1)

- **Server-initiated connections.** The device accepts no inbound
  connections. All server→device traffic (authorization, config push, valve
  commands) rides the response to device-initiated requests (§7).
- **Discovery and fleet management.** Out of scope.

## 2. Transport

```
POST {reporting_url}
Authorization: Bearer <token>
Content-Type: application/json
```

- The reporting URL — path included — is device configuration, used verbatim;
  the device attaches no meaning to it. Servers SHOULD expose the endpoint at
  a stable, documented path such as `/kegboard-event`.
- The request body is a single JSON object (§3); the response body is a
  single JSON object (§7).
- Maximum request body size a server must accept: **16 KiB**. The device
  bounds itself well under this.

### Authentication

Authentication is optional, and the server drives it:

- A device holding a bearer token sends `Authorization: Bearer <token>` on
  every request. A device without one sends **no `Authorization` header at
  all** — there is no placeholder, no empty header, no handshake.
- A server MAY simply accept (2xx) unauthenticated batches. The device then
  never pairs and never holds a token. This is deliberate: the minimum
  viable receiver is a single unauthenticated HTTP handler with no auth
  machinery whatsoever.
- **Only a 401 introduces authentication.** A 401 tells the device this
  server wants a credential, sending it into pairing (§8); once provisioned,
  every subsequent request carries the header. The same 401 later serves as
  revocation — the device drops its token and re-pairs.
- TLS is strongly recommended whenever tokens are in play; the token is a
  plain bearer credential.

### Status-code semantics

The device keys its queue behavior on the status code:

| Status | Device behavior |
|---|---|
| 2xx | Batch accepted (or safely deduplicated). Remove events from queue. Process response body (§7). |
| 401 | Not authorized. Keep events queued; enter/continue pairing (§8). |
| other 4xx | Batch can never succeed (malformed, unsupported version). Drop the batch, surface an error diagnostic. |
| 5xx / network error / timeout | Transient. Keep events queued, retry with exponential backoff. |

A server that accepts a batch MUST have durably processed (or deduplicated)
every event in it before returning 2xx. There is no partial acceptance: if a
server cannot process one event in a batch, it either drops that event
internally (and still returns 2xx) or fails the whole batch with 5xx. This
keeps device logic trivial.

## 3. Request envelope

```json
{
  "v": 1,
  "device": "kegboard-a1b2c3",
  "boot_id": "9f3a2c1b",
  "sent_uptime_ms": 8123456,
  "events": [ ... ]
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `v` | integer | yes | Protocol version. This document describes `1`. |
| `device` | string | yes | Stable device identity, e.g. `kegboard-a1b2c3`. Derived from MAC by default, user-overridable. |
| `boot_id` | string | yes | Opaque id regenerated each boot (e.g. 8 hex chars). Scopes sequence numbers so they need not survive reboot. |
| `sent_uptime_ms` | integer | yes | Device uptime when this request was serialized. Diagnostic. |
| `events` | array | yes | 1–16 events, oldest first. |

## 4. Event envelope

Every element of `events`:

```json
{
  "id": 42,
  "type": "pour",
  "age_ms": 4200,
  "time": "2026-08-03T18:02:11Z",
  "data": { ... }
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `id` | integer | yes | Sequence number, monotonically increasing per boot, starting at 1. `(device, boot_id, id)` is the global dedup key. |
| `type` | string | yes | One of §5's types. Servers MUST ignore (but still 2xx) unknown types. |
| `age_ms` | integer | yes | How long before serialization the event occurred. Recomputed at every (re)send. **This is the authoritative time signal**; see §6. |
| `time` | string (RFC 3339) | no | Wall time of the event, present only if the device clock was synchronized when the event occurred. Informational. |
| `data` | object | yes | Type-specific payload. |

> **Why event ids are boot-scoped integers while `pour_id` (§5.1) is a
> globally unique string:** event ids are transport bookkeeping — consumed
> once at ingest for dedup and ordering, then never referenced again — so a
> composed key is fine and a per-event UUID would spend 36 bytes on every
> heartbeat and temperature reading for no benefit. Pours are domain objects
> that outlive the transport (they become database rows, URLs, log lines), so
> they carry an identifier that is unique as-is, with nothing to compose and
> nothing to get wrong.

## 5. Event types

### 5.1 `pour`

A completed pour: the durable record. Emitted once, when the pour ends.

Like every event, a pour is timed by the envelope's `age_ms` (§4, §6) — there
is no separate timestamp in the payload, and queued pours delivered late keep
correct timing automatically. The anchor is the **end of the pour**: the
event is created the moment the pour finishes, so
`pour_end = server_now - age_ms`, and the start is `pour_end - duration_ms`.

```json
{
  "meter_number": 0,
  "pour_id": "5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415",
  "volume_ml": 355.2,
  "duration_ms": 7100,
  "auth_device": "core.rfid",
  "auth_token": "0089f2c4",
  "grant_id": "g_5501",
  "ticks": 1919,
  "ml_per_tick": 0.185,
  "tick_series": "0:3 100:14 200:31"
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `meter_number` | integer | yes | Meter number on this device, 0-based. `(device, meter_number)` identifies a tap server-side. |
| `pour_id` | string | yes | Globally unique, **opaque** pour identifier; see §5.2. |
| `volume_ml` | number | yes | Poured volume. **Authoritative.** Computed on-device from its own calibration. |
| `duration_ms` | integer | yes | First tick to last tick. |
| `auth_device` | string | no | Reader that authorized the pour (`core.rfid`, `onewire`, ...). |
| `auth_token` | string | no | Token that authorized the pour. |
| `grant_id` | string | no | The server-assigned id of the grant that covered this pour (§7.1). Absent for locally decided and ungated pours. |
| `ticks` | integer | no | Raw tick count. Advisory diagnostic only; servers MUST NOT compute volume from it. |
| `ml_per_tick` | number | no | Calibration in effect when the pour ended. Diagnostic; lets a server sanity-check `ticks * ml_per_tick ≈ volume_ml`. |
| `tick_series` | string | no | Space-separated `<offset_ms>:<ticks>` pairs. Diagnostic. |

There is no user field: **the device never learns identity**. The server
attributes a pour from `grant_id` — which pins it to the server's own
authorization decision, and so stays correct even if the token is reassigned
between the pour and a late queued delivery — or, for locally decided
grants, from `auth_token`. A pour carrying none of these is a guest pour.

### 5.2 `pour_update`

A pour in progress: the live view. Lets a server UI render an odometer while
beer is flowing. Emitted at most every `pour_update_ms` (device-tunable,
default 1000; `0` disables) from pour start until the final `pour` event.

```json
{
  "meter_number": 0,
  "pour_id": "5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415",
  "volume_ml": 120.4,
  "duration_ms": 2400
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `meter_number` | integer | yes | Meter number. |
| `pour_id` | string | yes | Identifies the pour: all updates of one pour and its final `pour` event carry the same value. **Opaque and globally unique** — the device currently generates a UUIDv4, but clients MUST NOT validate the format; it may change. Usable as-is as a key, with no device/boot qualifiers. |
| `volume_ml` | number | yes | Volume so far. |
| `duration_ms` | integer | yes | Elapsed since first tick. |

There is no rate field: flow rate is derivable (`volume_ml / duration_ms`, or
better, the delta between successive updates), and the protocol does not
carry what a receiver can compute.

**`pour_update` is best-effort and ephemeral**, and is the one exception to
§9's delivery guarantees: the device sends updates only when the connection
is healthy, silently discards them rather than queueing on failure, and their
loss does not count toward `events_dropped`. Servers MUST NOT treat updates
as authoritative — the final `pour` event is the record, and a server
receiving updates but no final `pour` (device rebooted mid-pour) must not
synthesize a drink from them.

### 5.3 `temperature`

A sensor reading. Emitted at the device's configured interval.

```json
{ "sensor": "thermo-28ff641d8fbb0517", "temp_c": 4.25 }
```

| Field | Type | Req | Description |
|---|---|---|---|
| `sensor` | string | yes | Device-scoped sensor name. |
| `temp_c` | number | yes | Degrees Celsius. |

### 5.4 `token`

An auth token arrived or left. Central to server-side authorization; see the
[Authenticated Pouring](authenticated-pouring.md) doc for the full flow.

```json
{
  "auth_device": "onewire",
  "token": "0000000012345678",
  "action": "attached",
  "status": "accepted"
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `auth_device` | string | yes | Reader name (`core.rfid`, `onewire`, ...). |
| `token` | string | yes | Token value, lowercase hex by convention. |
| `action` | string | yes | `attached` or `detached`. |
| `status` | string | no | On `attached`: `accepted` or `denied` — present only when the device decided locally. Absent means the device is asking the server to decide (see companion doc). |

The device SHOULD flush a batch immediately when a token is attached, since
authorization latency is the user standing at the tap waiting.

### 5.5 `status`

Device health and configuration. Emitted at boot and then at the heartbeat
interval (default **1 minute**). Also the vehicle for making data loss
visible, for letting the server discover device settings it cannot set,
and for self-describing the device's hardware: `meters` and `relays` are
**exhaustive inventories**, so a server can allocate its records for every
port automatically — and retire records for ports that stop being reported.

```json
{
  "state": "boot",
  "fw_version": "4.0.0",
  "uptime_ms": 12345,
  "wifi_rssi_dbm": -61,
  "events_dropped": 0,
  "config": {
    "heartbeat_ms": 60000,
    "pour_update_ms": 1000,
    "queue_capacity": 16
  },
  "meters": [
    { "meter_number": 0, "total_ticks": 918234, "ml_per_tick": 0.185 },
    { "meter_number": 1, "total_ticks": 40112, "ml_per_tick": 0.185 }
  ],
  "relays": [
    { "relay_number": 0 },
    { "relay_number": 1 }
  ]
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `state` | string | yes | `boot` for the first status after power-on, else `heartbeat`. |
| `fw_version` | string | yes | Firmware version. |
| `uptime_ms` | integer | yes | Uptime at event creation. |
| `wifi_rssi_dbm` | integer | no | Signal strength. |
| `events_dropped` | integer | yes | Lifetime count of events evicted from the queue before delivery. A non-zero delta between heartbeats means data was lost. |
| `config` | object | yes | Operative device settings the server should be able to discover without being able to set them: heartbeat interval, pour-update interval, queue capacity. Extensible; servers MUST ignore unknown keys. |
| `meters` | array | no | Every meter the device has, with lifetime tick totals and current calibration (`ml_per_tick` always present). Exhaustive when present: a meter absent from the list does not exist on the device. Also lets a server detect missed pours by gap analysis. |
| `relays` | array | no | Every relay (valve output) the device has. Exhaustive when present, same rule as `meters`. Entries are objects for extensibility; servers MUST ignore unknown keys. |

A server that auto-provisions from these inventories SHOULD be
conservative about retirement: dropping a record it created is safe, but
a port an operator has configured (e.g. bound to a tap) deserves a
warning rather than silent deletion — a transient misreport must not
sever operator configuration.

### 5.6 `command_result`

Acknowledges a server command (§7), giving the command channel the same
at-least-once/idempotent semantics as the event channel.

```json
{ "command": "cmd_8f21", "result": "ok" }
```

| Field | Type | Req | Description |
|---|---|---|---|
| `command` | string | yes | The `id` of the command being acknowledged. |
| `result` | string | yes | `ok`, `error`, or `unsupported`. |
| `message` | string | no | Human-readable detail on `error`/`unsupported`. |

### 5.7 `grant_end`

Reports that an authorization grant (§7.1) ended, and why. Emitted once per
ending — including partial endings, where only some of a grant's meters are
released — for every grant, server-issued or local, so the server gets the
complete grant lifecycle from this one event type instead of inferring it
from timers of its own.

```json
{
  "meter_numbers": [0],
  "reason": "max_volume",
  "auth_device": "core.rfid",
  "auth_token": "0089f2c4",
  "grant_id": "g_5501",
  "volume_ml": 2004.9,
  "duration_ms": 84200
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `meter_numbers` | array of integer | yes | The meters released by this ending. |
| `reason` | string | yes | Why the grant ended; see below. |
| `auth_device` / `auth_token` | string | no | Echo of the presentment that created the grant, as on `pour` (§5.1). |
| `grant_id` | string | no | The server-assigned id of the grant (§7.1). Absent for locally decided grants. |
| `volume_ml` | number | yes | Total volume poured under the grant, across all its meters — a snapshot at this ending, see below. |
| `duration_ms` | integer | yes | Grant age at this ending. |

| `reason` | Meaning |
|---|---|
| `max_volume` | Cumulative poured volume reached `max_volume_ml`. |
| `max_duration` | Grant lifetime reached `max_duration_ms` — or the device's own `max_grant_duration` clamp (§7.1). |
| `max_idle` | No flow on any granted meter for `max_idle_ms`. |
| `detach` | The grant's token detached (presence readers). |
| `command` | A server `deauthorize` (§7.3). |
| `replaced` | An `authorize` — a new grant, or an update to this one — took the listed meters out of this grant's scope (§7.1). |

`volume_ml` and `duration_ms` are **snapshots of the whole grant, not
deltas**: a partial ending reports the grant's running totals at that
moment, and the same volume appears again — grown — in the grant's later
endings. Servers MUST NOT sum `grant_end` volumes; the `pour` events are
the volume record, and these totals are for cross-checking and display.

A limit ending an in-flight pour ends the pour first, so the final `pour`
event precedes the `grant_end` in the queue. Reason `command` is redundant
with the `deauthorize`'s own `command_result` acknowledgment, deliberately:
a server can track grant lifecycles from `grant_end` alone. Like any event,
`grant_end` queues and delivers at-least-once (§9) — a grant that ends
during an outage is reported when connectivity returns.

## 6. Time model

The device may not have wall-clock time — at boot, before NTP, or on a
network with no time source. The protocol therefore never depends on the
device's clock:

- **`age_ms` is authoritative.** The receiving server computes
  `event_time = server_now - age_ms`. Because `age_ms` is recomputed each
  time the batch is serialized, this stays correct for queued events
  delivered hours late and on devices that never sync.
- **`time` is informational.** Present only when the device clock was synced
  at event creation. Servers MAY log it, SHOULD prefer `age_ms`, and MUST NOT
  reject events over disagreement between the two.

Transit latency adds error on the order of the HTTP round trip; for pours and
temperatures this is noise.

## 7. Response and commands

The response to every authenticated 2xx exchange:

```json
{
  "commands": [
    { "id": "cmd_8f21", "type": "authorize", "data": { ... } }
  ]
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `commands` | array | no | Server→device instructions, oldest first. Absent means none — equivalent to `[]`. |
| `commands[].id` | string | yes | Server-assigned, opaque. Devices MUST deduplicate on it: a server re-sends a command until it sees a `command_result`, so the same command may arrive more than once. A duplicate is not re-applied but SHOULD be acknowledged again — the earlier `command_result` may have been lost before delivery. |
| `commands[].type` | string | yes | Command type. Devices MUST acknowledge unknown types with `result: "unsupported"`. |
| `commands[].data` | object | yes | Type-specific payload. |

This channel is poll-based by design: the device already initiates a request
on every pour, token presentment, and heartbeat, so no listener, NAT
traversal, or second credential is needed. Worst-case command latency equals
the heartbeat interval (1 min default); token-triggered commands arrive in
the same round trip as the token event (see companion doc). A future
transport (WebSocket/MQTT) can push the same command objects unchanged.

The complete command catalog follows. How these commands compose with token
presentment into an authorization flow — including the `server`/`local`/`open`
modes and offline behavior — is specified in
[Authenticated Pouring](authenticated-pouring.md).

### 7.1 `authorize`

Creates — or updates — a grant: the device energizes the grant's relays and
tags pours on the grant's meters with it. One command carries exactly one
grant; a server issuing several grants at once — different taps, different
policy — sends several `authorize` commands in the same response. Typically
sent in the same response as a decision-requesting `token` event (see
companion doc), but valid in any response.

```json
{
  "id": "cmd_8f21",
  "type": "authorize",
  "data": {
    "grant_id": "g_5501",
    "meter_numbers": [0],
    "relay_numbers": [1],
    "max_volume_ml": 2000,
    "max_duration_ms": 120000,
    "max_idle_ms": 30000,
    "auth_device": "core.rfid",
    "token": "0089f2c4"
  }
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `grant_id` | string | yes | Server-assigned grant identifier: opaque to the device, at most 64 chars. Pours and `grant_end` events echo it (§5.1, §5.7); `deauthorize` revokes by it (§7.3). Naming a live grant's id updates that grant in place (below). |
| `meter_numbers` | array of integer | yes | Meters this grant covers: pours on them are tagged with the grant, and their flow feeds the volume and idle limits. **The server decides the set** — this is how one token opens one tap, several, or all. |
| `relay_numbers` | array of integer | no | Relays to energize (typically driving solenoid valves) for the life of the grant. Absent or empty → attribution-only: the meters still meter, no valve is driven. |
| `max_volume_ml` | number | no | Most the grant may pour, summed across its meters. `0` or absent → unlimited. |
| `max_duration_ms` | integer | no | Hard cap on grant lifetime, from grant creation — reaching it ends the grant even mid-pour. `0` or absent → unbounded by the server; the device clamp below still applies. |
| `max_idle_ms` | integer | no | Longest stretch with no flow on any granted meter. Flow resets it, so this — not `max_duration_ms` — is what keeps a slow glass alive. `0` or absent → no idle limit. |
| `auth_device` / `token` | string | no | Echo of the presentment that triggered this grant. The device records them onto resulting `pour` events and uses them to release the grant on the matching detach. |

Device semantics:

- **The meter↔relay association stays on the server.** The device applies
  the two sets verbatim — energize `relay_numbers`, cover `meter_numbers` —
  and holds no mapping between them; nothing in grant behavior depends on
  which relay serves which meter. (In `local` mode, where no server can
  send the sets, they come from device config instead — see companion doc.)
- One active grant **per meter**. A new grant covering an already-covered
  meter takes that meter over — the person at the tap is whoever presented
  most recently. The takeover is reported as `grant_end` with reason
  `replaced` (§5.7).
- **Updates.** An `authorize` (under a new command id) naming a live
  grant's `grant_id` updates it in place: the sets and limits are replaced,
  while poured volume and grant age carry over. This is how a server tops
  up a volume budget or extends a session without ending the grant. Meters
  leaving the scope are reported as `grant_end` with reason `replaced`. An
  update cannot extend a grant past the device clamp (below); for more
  time, issue a new grant.
- A relay is energized while **any** active grant names it, and released
  when the last such grant ends.
- Pours on a granted meter carry the grant's `auth_device`/`auth_token`
  echo and its `grant_id` (§5.1). A grant ending mid-pour ends the pour
  first, so a pour is always tagged with the grant that actually poured
  it. A grant *arriving* mid-pour adopts the in-flight pour, and a grant
  *replacing* another mid-pour splits it — the full pour×grant corner-case
  catalog is in the companion doc (§9).
- **Limits end grants locally.** When any limit is reached the device
  deauthorizes the grant itself — relays released, in-flight pour ended —
  and reports a `grant_end` event (§5.7) naming which limit tripped.
  Volume enforcement is best-effort at the margin: the valve closes the
  moment the limit trips, but beer already in flight still registers, so
  the final pour may slightly overshoot `max_volume_ml`.
- A grant naming a meter or relay the device does not have is acknowledged
  `error` and not applied, in whole.
- **Safety backstop:** the device clamps every grant's total lifetime —
  from creation, updates included — to its own `max_grant_duration`
  (default **5 minutes**), whatever `max_duration_ms` says — including
  "unlimited". A server asking for more
  gets the clamp, silently; the command is still acknowledged `ok`. A valve
  is a thing that pours beer on the floor when software misbehaves, so the
  final bound on "how long can it stay open" belongs to the device.
- Applying the same command id twice is a full no-op — limits, counters,
  and timers are not reset (idempotent, since the server re-sends until
  acked).
- A device in `local` mode (see companion doc) acknowledges `authorize`
  with `result: "unsupported"`.

### 7.2 `deny`

The explicit refusal of a token presentment. The device signals the user
(refusal tone, LED) and acknowledges with `command_result: ok`. No state
changes: existing grants on other meters are untouched.

```json
{
  "id": "cmd_8f23",
  "type": "deny",
  "data": {
    "auth_device": "core.rfid",
    "token": "0089f2c4",
    "reason": "Token not assigned to a user"
  }
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `auth_device` / `token` | string | no | Echo of the refused presentment, so a device with several readers signals at the right one. |
| `reason` | string | no | Human-readable explanation. Devices with a display MAY show it; all devices MAY log it. |

### 7.3 `deauthorize`

Revokes grants by id: releases their relays, ends any in-flight pour on
their meters (still tagged with the grant that poured it), clears them.
This is the server-initiated cutoff — an admin button, a policy engine, an
emergency stop. Detach and the grant's own limits do the same thing
device-side without a command. Each grant ended this way is also reported
as `grant_end` with reason `command` (§5.7).

```json
{
  "id": "cmd_8f22",
  "type": "deauthorize",
  "data": { "grant_ids": ["g_5501"] }
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `grant_ids` | array of string | no | Grants to revoke. **Absent means every active grant** — the emergency stop. |

An id matching no active grant is ignored, and the command still
acknowledges `ok`: the grant may simply have ended on its own before the
command arrived, and the `grant_end` stream already tells the server how.

### 7.4 Reserved types

`set_config`, `set_relay`, and `identify` are reserved for a later
revision; until specified, devices answer them with `unsupported`.

## 8. Pairing

Provisions the bearer token so a user never has to transport a credential.
The flow is: an unprovisioned device announces itself; it appears on the
server dashboard **by name**; a human clicks allow or deny.

An unprovisioned device sends ordinary batches (at minimum its `status`
events — pours queue as usual and deliver after pairing) with **no
`Authorization` header**. A server that does not require authentication
simply accepts them, and pairing never begins (§2). A server that requires
authentication responds `401` with a pairing body:

```json
{ "pairing": { "state": "pending" } }
```

| `pairing.state` | Server meaning | Device behavior |
|---|---|---|
| `pending` | Device is on the dashboard awaiting a decision. | Keep events queued. Poll every 5 s for the first minute, then at the heartbeat interval. |
| `allowed` | A human approved this device. `pairing.token` holds the newly provisioned bearer token. | Store the token in flash, retry immediately with `Authorization` set. Queued events deliver. |
| `denied` | A human refused this device. | Stop polling until reboot. |

```json
{ "pairing": { "state": "allowed", "token": "kbe_9c8ef2a1..." } }
```

Rules:

- The token is delivered exactly once, in the `allowed` response. A server
  MUST treat the device's first authenticated request as confirmation and
  MUST NOT return the token again; a device that loses it re-pairs.
- **Any** 401 on an authenticated request — revoked or rotated token — sends
  the device back into this flow. Key rotation is therefore "revoke, then
  re-allow from the dashboard," with no device-side ceremony.
- Device identity is self-asserted (TOFU): the human clicking *allow* against
  the expected device name is the trust decision. TLS protects the token in
  transit. Servers SHOULD show first-seen time and source IP on the pending
  entry to make that decision an informed one.
- A `denied` device stays visible server-side so the decision can be
  reversed; on its next boot it will ask again.

## 9. Delivery semantics

- **At-least-once** for all types except `pour_update` (§5.2). The device
  retries batches on 5xx/network failure with exponential backoff (base
  30 s, cap 5 min). A new pour or token event resets backoff and triggers an
  immediate attempt.
- **Idempotent processing.** Servers MUST deduplicate on
  `(device, boot_id, id)`. A retention window of 7 days is sufficient; the
  device never re-sends anything older than its queue.
- **Bounded queue, oldest-first eviction.** The queue is fixed-size RAM.
  When full, the oldest event is evicted and `events_dropped` increments —
  during a long outage the most recent pours are the ones worth keeping.
  Events do not survive reboot; `boot_id` makes that harmless for dedup.
- **Ordering.** Events within a batch are oldest-first. `id` orders events
  within a boot. Servers should not assume cross-boot ordering.

## 10. Compatibility rules

- Servers MUST ignore unknown fields anywhere in the request.
- Servers MUST accept (2xx) and ignore unknown event `type`s.
- Devices MUST ignore unknown fields in the response, and MUST answer
  unknown command types with `command_result: unsupported`.
- Additive changes (new optional fields, new event types, new command types)
  do not bump `v`. Breaking changes bump `v`, and a server MAY reject
  versions it does not speak with a non-401 4xx.

## 11. Full example

Device pours twice during a server outage, then connectivity returns; one
batch delivers both pours and a heartbeat:

```
POST /kegboard-event HTTP/1.1
Authorization: Bearer kbe_9c8ef2a1...
Content-Type: application/json

{
  "v": 1,
  "device": "kegboard-a1b2c3",
  "boot_id": "9f3a2c1b",
  "sent_uptime_ms": 7523000,
  "events": [
    {
      "id": 17,
      "type": "pour",
      "age_ms": 912000,
      "data": {
        "meter_number": 0,
        "pour_id": "9b0e6a11-2f4c-49d3-8f6a-c1d2e3f40517",
        "volume_ml": 473.1,
        "duration_ms": 9800,
        "auth_device": "core.rfid",
        "auth_token": "0089f2c4",
        "grant_id": "g_5488",
        "ticks": 2557,
        "ml_per_tick": 0.185
      }
    },
    {
      "id": 18,
      "type": "pour",
      "age_ms": 402000,
      "data": { "meter_number": 1, "pour_id": "0d4f9b82-6e3a-4c15-a7b8-2c9d0e1f6a3b", "volume_ml": 355.0, "duration_ms": 7100 }
    },
    {
      "id": 19,
      "type": "status",
      "age_ms": 0,
      "time": "2026-08-03T18:02:11Z",
      "data": {
        "state": "heartbeat",
        "fw_version": "4.0.0",
        "uptime_ms": 7523000,
        "events_dropped": 0,
        "config": { "heartbeat_ms": 60000, "pour_update_ms": 1000, "queue_capacity": 16 },
        "meters": [
          { "meter_number": 0, "total_ticks": 920791, "ml_per_tick": 0.185 },
          { "meter_number": 1, "total_ticks": 42031, "ml_per_tick": 0.185 }
        ],
        "relays": [ { "relay_number": 0 }, { "relay_number": 1 } ]
      }
    }
  ]
}
```

```
HTTP/1.1 200 OK
Content-Type: application/json

{ "commands": [] }
```

The server records pour 17 as having happened 912 s ago and pour 18 as 402 s
ago, regardless of what the device's clock believed.

## Appendix A. Request JSON Schema

Normative, to ship in-repo as `schemas/kegboard-event.schema.json`.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://kegbot.org/schemas/kegboard-event/1",
  "title": "Kegboard event batch",
  "type": "object",
  "required": [
    "v",
    "device",
    "boot_id",
    "sent_uptime_ms",
    "events"
  ],
  "properties": {
    "v": {
      "const": 1
    },
    "device": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64
    },
    "boot_id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 32
    },
    "sent_uptime_ms": {
      "type": "integer",
      "minimum": 0
    },
    "events": {
      "type": "array",
      "minItems": 1,
      "maxItems": 16,
      "items": {
        "$ref": "#/$defs/event"
      }
    }
  },
  "$defs": {
    "event": {
      "type": "object",
      "required": [
        "id",
        "type",
        "age_ms",
        "data"
      ],
      "properties": {
        "id": {
          "type": "integer",
          "minimum": 1
        },
        "type": {
          "type": "string"
        },
        "age_ms": {
          "type": "integer",
          "minimum": 0
        },
        "time": {
          "type": "string",
          "format": "date-time"
        },
        "data": {
          "type": "object"
        }
      },
      "allOf": [
        {
          "if": {
            "properties": {
              "type": {
                "const": "pour"
              }
            }
          },
          "then": {
            "properties": {
              "data": {
                "$ref": "#/$defs/pour"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "pour_update"
              }
            }
          },
          "then": {
            "properties": {
              "data": {
                "$ref": "#/$defs/pour_update"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "temperature"
              }
            }
          },
          "then": {
            "properties": {
              "data": {
                "$ref": "#/$defs/temperature"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "token"
              }
            }
          },
          "then": {
            "properties": {
              "data": {
                "$ref": "#/$defs/token"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "status"
              }
            }
          },
          "then": {
            "properties": {
              "data": {
                "$ref": "#/$defs/status"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "command_result"
              }
            }
          },
          "then": {
            "properties": {
              "data": {
                "$ref": "#/$defs/command_result"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "grant_end"
              }
            }
          },
          "then": {
            "properties": {
              "data": {
                "$ref": "#/$defs/grant_end"
              }
            }
          }
        }
      ]
    },
    "pour": {
      "type": "object",
      "required": [
        "meter_number",
        "pour_id",
        "volume_ml",
        "duration_ms"
      ],
      "properties": {
        "meter_number": {
          "type": "integer",
          "minimum": 0
        },
        "pour_id": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64
        },
        "volume_ml": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "duration_ms": {
          "type": "integer",
          "minimum": 0
        },
        "auth_device": {
          "type": "string"
        },
        "auth_token": {
          "type": "string"
        },
        "grant_id": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64
        },
        "ticks": {
          "type": "integer",
          "minimum": 0
        },
        "ml_per_tick": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "tick_series": {
          "type": "string",
          "pattern": "^\\d+:\\d+( \\d+:\\d+)*$"
        }
      }
    },
    "pour_update": {
      "type": "object",
      "required": [
        "meter_number",
        "pour_id",
        "volume_ml",
        "duration_ms"
      ],
      "properties": {
        "meter_number": {
          "type": "integer",
          "minimum": 0
        },
        "pour_id": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64
        },
        "volume_ml": {
          "type": "number",
          "minimum": 0
        },
        "duration_ms": {
          "type": "integer",
          "minimum": 0
        }
      }
    },
    "temperature": {
      "type": "object",
      "required": [
        "sensor",
        "temp_c"
      ],
      "properties": {
        "sensor": {
          "type": "string",
          "minLength": 1
        },
        "temp_c": {
          "type": "number"
        }
      }
    },
    "token": {
      "type": "object",
      "required": [
        "auth_device",
        "token",
        "action"
      ],
      "properties": {
        "auth_device": {
          "type": "string",
          "minLength": 1
        },
        "token": {
          "type": "string",
          "minLength": 1
        },
        "action": {
          "enum": [
            "attached",
            "detached"
          ]
        },
        "status": {
          "enum": [
            "accepted",
            "denied"
          ]
        }
      }
    },
    "status": {
      "type": "object",
      "required": [
        "state",
        "fw_version",
        "uptime_ms",
        "events_dropped",
        "config"
      ],
      "properties": {
        "state": {
          "enum": [
            "boot",
            "heartbeat"
          ]
        },
        "fw_version": {
          "type": "string"
        },
        "uptime_ms": {
          "type": "integer",
          "minimum": 0
        },
        "wifi_rssi_dbm": {
          "type": "integer"
        },
        "events_dropped": {
          "type": "integer",
          "minimum": 0
        },
        "config": {
          "type": "object",
          "required": [
            "heartbeat_ms",
            "pour_update_ms",
            "queue_capacity"
          ],
          "properties": {
            "heartbeat_ms": {
              "type": "integer",
              "minimum": 1000
            },
            "pour_update_ms": {
              "type": "integer",
              "minimum": 0
            },
            "queue_capacity": {
              "type": "integer",
              "minimum": 1
            }
          }
        },
        "meters": {
          "type": "array",
          "items": {
            "type": "object",
            "required": [
              "meter_number",
              "total_ticks",
              "ml_per_tick"
            ],
            "properties": {
              "meter_number": {
                "type": "integer",
                "minimum": 0
              },
              "total_ticks": {
                "type": "integer",
                "minimum": 0
              },
              "ml_per_tick": {
                "type": "number",
                "exclusiveMinimum": 0
              }
            }
          }
        },
        "relays": {
          "type": "array",
          "items": {
            "type": "object",
            "required": [
              "relay_number"
            ],
            "properties": {
              "relay_number": {
                "type": "integer",
                "minimum": 0
              }
            }
          }
        }
      }
    },
    "command_result": {
      "type": "object",
      "required": [
        "command",
        "result"
      ],
      "properties": {
        "command": {
          "type": "string",
          "minLength": 1
        },
        "result": {
          "enum": [
            "ok",
            "error",
            "unsupported"
          ]
        },
        "message": {
          "type": "string"
        }
      }
    },
    "grant_end": {
      "type": "object",
      "required": [
        "meter_numbers",
        "reason",
        "volume_ml",
        "duration_ms"
      ],
      "properties": {
        "meter_numbers": {
          "type": "array",
          "minItems": 1,
          "items": {
            "type": "integer",
            "minimum": 0
          }
        },
        "reason": {
          "enum": [
            "max_volume",
            "max_duration",
            "max_idle",
            "detach",
            "command",
            "replaced"
          ]
        },
        "auth_device": {
          "type": "string"
        },
        "auth_token": {
          "type": "string"
        },
        "grant_id": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64
        },
        "volume_ml": {
          "type": "number",
          "minimum": 0
        },
        "duration_ms": {
          "type": "integer",
          "minimum": 0
        }
      }
    }
  }
}
```

## Appendix B. Response JSON Schema

Normative, to ship in-repo as `schemas/kegboard-event-response.schema.json`.
Covers both the authenticated (200) and pairing (401) responses.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://kegbot.org/schemas/kegboard-event-response/1",
  "title": "Kegboard event response",
  "type": "object",
  "properties": {
    "commands": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["id", "type", "data"],
        "properties": {
          "id": { "type": "string", "minLength": 1 },
          "type": { "type": "string" },
          "data": { "type": "object" }
        },
        "allOf": [
          {
            "if": { "properties": { "type": { "const": "authorize" } } },
            "then": { "properties": { "data": { "$ref": "#/$defs/authorize" } } }
          },
          {
            "if": { "properties": { "type": { "const": "deny" } } },
            "then": { "properties": { "data": { "$ref": "#/$defs/deny" } } }
          },
          {
            "if": { "properties": { "type": { "const": "deauthorize" } } },
            "then": { "properties": { "data": { "$ref": "#/$defs/deauthorize" } } }
          }
        ]
      }
    },
    "pairing": {
      "type": "object",
      "required": ["state"],
      "properties": {
        "state": { "enum": ["pending", "allowed", "denied"] },
        "token": { "type": "string", "minLength": 1 }
      },
      "if": { "properties": { "state": { "const": "allowed" } } },
      "then": { "required": ["state", "token"] }
    }
  },
  "$defs": {
    "authorize": {
      "type": "object",
      "required": ["grant_id", "meter_numbers"],
      "properties": {
        "grant_id": { "type": "string", "minLength": 1, "maxLength": 64 },
        "meter_numbers": {
          "type": "array",
          "minItems": 1,
          "items": { "type": "integer", "minimum": 0 }
        },
        "relay_numbers": {
          "type": "array",
          "items": { "type": "integer", "minimum": 0 }
        },
        "max_volume_ml": { "type": "number", "minimum": 0 },
        "max_duration_ms": { "type": "integer", "minimum": 0 },
        "max_idle_ms": { "type": "integer", "minimum": 0 },
        "auth_device": { "type": "string" },
        "token": { "type": "string" }
      }
    },
    "deny": {
      "type": "object",
      "properties": {
        "auth_device": { "type": "string" },
        "token": { "type": "string" },
        "reason": { "type": "string" }
      }
    },
    "deauthorize": {
      "type": "object",
      "properties": {
        "grant_ids": {
          "type": "array",
          "minItems": 1,
          "items": { "type": "string", "minLength": 1, "maxLength": 64 }
        }
      }
    }
  }
}
```
