# Kegboard Event Protocol

**Version:** 1
**Status:** DRAFT — not yet implemented
**Date:** 2026-08-03

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
  "meter": 0,
  "pour_id": "5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415",
  "volume_ml": 355.2,
  "duration_ms": 7100,
  "user": "mikey",
  "auth_device": "core.rfid",
  "auth_token": "0089f2c4",
  "ticks": 1919,
  "ml_per_tick": 0.185,
  "tick_series": "0:3 100:14 200:31"
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `meter` | integer | yes | Meter number on this device, 0-based. `(device, meter)` identifies a tap server-side. |
| `pour_id` | string | yes | Globally unique, **opaque** pour identifier; see §5.2. |
| `volume_ml` | number | yes | Poured volume. **Authoritative.** Computed on-device from its own calibration. |
| `duration_ms` | integer | yes | First tick to last tick. |
| `user` | string | no | Username the pour is attributed to. Absent means unattributed/guest. |
| `auth_device` | string | no | Reader that authorized the pour (`core.rfid`, `onewire`, ...). |
| `auth_token` | string | no | Token that authorized the pour. |
| `ticks` | integer | no | Raw tick count. Advisory diagnostic only; servers MUST NOT compute volume from it. |
| `ml_per_tick` | number | no | Calibration in effect when the pour ended. Diagnostic; lets a server sanity-check `ticks * ml_per_tick ≈ volume_ml`. |
| `tick_series` | string | no | Space-separated `<offset_ms>:<ticks>` pairs. Diagnostic. |

### 5.2 `pour_update`

A pour in progress: the live view. Lets a server UI render an odometer while
beer is flowing. Emitted at most every `pour_update_ms` (device-tunable,
default 1000; `0` disables) from pour start until the final `pour` event.

```json
{
  "meter": 0,
  "pour_id": "5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415",
  "volume_ml": 120.4,
  "duration_ms": 2400
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `meter` | integer | yes | Meter number. |
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
  "status": "accepted",
  "user": "mikey"
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `auth_device` | string | yes | Reader name (`core.rfid`, `onewire`, ...). |
| `token` | string | yes | Token value, lowercase hex by convention. |
| `action` | string | yes | `attached` or `detached`. |
| `status` | string | no | On `attached`: `accepted` or `denied` — present only when the device decided locally. Absent means the device is asking the server to decide (see companion doc). |
| `user` | string | no | Username the token resolved to, if decided locally. |

The device SHOULD flush a batch immediately when a token is attached, since
authorization latency is the user standing at the tap waiting.

### 5.5 `status`

Device health and configuration. Emitted at boot and then at the heartbeat
interval (default **1 minute**). Also the vehicle for making data loss
visible and for letting the server discover device settings it cannot set.

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
    { "meter": 0, "total_ticks": 918234, "ml_per_tick": 0.185 },
    { "meter": 1, "total_ticks": 40112, "ml_per_tick": 0.185 }
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
| `meters` | array | no | Per-meter lifetime tick totals and current calibration (`ml_per_tick` always present). Lets a server detect missed pours by gap analysis. |

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
| `commands[].id` | string | yes | Server-assigned, opaque. Devices MUST deduplicate on it: a server re-sends a command until it sees a `command_result`, so the same command may arrive more than once. |
| `commands[].type` | string | yes | Command type. Devices MUST acknowledge unknown types with `result: "unsupported"`. |
| `commands[].data` | object | yes | Type-specific payload. |

This channel is poll-based by design: the device already initiates a request
on every pour, token presentment, and heartbeat, so no listener, NAT
traversal, or second credential is needed. Worst-case command latency equals
the heartbeat interval (1 min default); token-triggered commands arrive in
the same round trip as the token event (see companion doc). A future
transport (WebSocket/MQTT) can push the same command objects unchanged.

**Command catalog:** `authorize`, `deny`, and `deauthorize` are specified in
[Authenticated Pouring](authenticated-pouring.md). Further types
(`set_config`, `set_output`, `identify`) are reserved for a later revision;
until specified, devices answer them with `unsupported`.

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
        "meter": 0,
        "pour_id": "9b0e6a11-2f4c-49d3-8f6a-c1d2e3f40517",
        "volume_ml": 473.1,
        "duration_ms": 9800,
        "user": "mikey",
        "ticks": 2557,
        "ml_per_tick": 0.185
      }
    },
    {
      "id": 18,
      "type": "pour",
      "age_ms": 402000,
      "data": { "meter": 1, "pour_id": "0d4f9b82-6e3a-4c15-a7b8-2c9d0e1f6a3b", "volume_ml": 355.0, "duration_ms": 7100 }
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
          { "meter": 0, "total_ticks": 920791, "ml_per_tick": 0.185 },
          { "meter": 1, "total_ticks": 42031, "ml_per_tick": 0.185 }
        ]
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
        }
      ]
    },
    "pour": {
      "type": "object",
      "required": [
        "meter",
        "pour_id",
        "volume_ml",
        "duration_ms"
      ],
      "properties": {
        "meter": {
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
        "user": {
          "type": "string"
        },
        "auth_device": {
          "type": "string"
        },
        "auth_token": {
          "type": "string"
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
        "meter",
        "pour_id",
        "volume_ml",
        "duration_ms"
      ],
      "properties": {
        "meter": {
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
        },
        "user": {
          "type": "string"
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
              "meter",
              "total_ticks",
              "ml_per_tick"
            ],
            "properties": {
              "meter": {
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
        }
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
  }
}
```
