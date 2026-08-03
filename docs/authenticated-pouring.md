# Authenticated Pouring

**Status:** DRAFT — companion to the [Kegboard Event Protocol](kegboard-event-protocol.md)
**Date:** 2026-08-03

How a token presented at a Kegboard becomes an open valve and an attributed
pour. This document specifies the interaction between device and server; the
message envelopes it uses (`token` events, commands, `command_result`) are
defined in the main protocol doc.

## 1. The problem

A reader (RFID, iButton, ...) produces token values. Someone has to decide
what a token means — which user, which meters it may open, for how long —
and something has to act on that decision by driving a valve relay and
attributing the resulting pour.

The token→user database can be large (hundreds or thousands of assignments)
and it lives on the server. The valve and the pour detection live on the
device. This doc defines who decides what, in three modes.

## 2. Authorization modes

A device operates in exactly one mode per auth gate, chosen in device
configuration:

| Mode | Who decides | Needs server online? | Use case |
|---|---|---|---|
| `server` | Server, per token presentment | Yes (with a configurable offline fallback) | The default with a server configured. Scales to any token database; policy lives in one place. |
| `local` | Device, from its own config | No | Serverless installs; simple allow-all-tokens or fixed-list setups. |
| `open` | Nobody — no gating | No | Meters-only installs, no valves. Pours are guest-attributed unless a token event happens to resolve. |

The device is **stateless about token assignments in `server` mode**: it
holds no token database, only the currently active grant(s).

## 3. `server` mode: the core flow

1. **Token presented.** The reader reports a token; the device emits a
   `token` event with `action: "attached"` and **no `status` field** — the
   absent status is the signal that the device is asking the server to
   decide. The device flushes the batch immediately.
2. **Server decides.** The server looks up the token, applies whatever
   policy it likes (user standing, keg access, time of day), and responds —
   in the same HTTP response — with an `authorize` or a `deny` command.
   Servers SHOULD always answer a decision-requesting token event with one
   of the two.
3. **Device acts.** On `authorize`, the device opens the toggles for the
   named meters, attributes subsequent pours to the given user, and
   acknowledges with a `command_result` event. On `deny`, the tap stays
   closed and the device SHOULD signal the user (e.g. a refusal tone). If
   the response carries neither — a server bug, defensively — the device
   treats the presentment as denied, without the user signal.
4. **Grant ends** by whichever comes first: token detach (presence readers),
   `duration_ms` expiry (extended automatically while a pour is running), or
   a server `deauthorize` command. The device closes the valve, ends any
   in-flight pour (still attributed to the departing user), and clears the
   grant.

Authorization latency is one HTTP round trip, because the decision rides the
response to the token event itself:

```
reader              device                                server
  |                   |                                     |
  | token 0089f2c4    |                                     |
  |------------------>| POST /kegboard-event                |
  |                   |   [token attached, no status] ----->|
  |                   |                                     | lookup, policy
  |                   |        200 {commands:[authorize]}   |
  |                   |<------------------------------------|
  |                   | open valve(s), set user             |
  |                   |                                     |
  |                   | ...pour happens...                  |
  |                   | POST [pour user=mikey,              |
  |                   |       command_result ok] ---------->|
```

## 4. Commands

### 4.1 `authorize`

```json
{
  "id": "cmd_8f21",
  "type": "authorize",
  "data": {
    "meters": [0, 2],
    "user": "mikey",
    "duration_ms": 30000,
    "auth_device": "core.rfid",
    "token": "0089f2c4"
  }
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `meters` | array of integer | yes | Meter numbers this grant opens. **The server decides the meter set** — this is how one token opens one tap, several, or all. |
| `user` | string | no | Username to attribute pours to. Absent → guest. |
| `duration_ms` | integer | yes | Grant lifetime. The device extends expiry while a pour is actively running on a granted meter, so a slow glass is never cut off. |
| `auth_device` / `token` | string | no | Echo of the presentment that triggered this grant. The device records them onto resulting `pour` events and uses them to release the grant on the matching detach. |

Device semantics:

- One active grant **per meter**. A new `authorize` naming an already-granted
  meter replaces that meter's grant (new user, fresh expiry) — the person at
  the tap is whoever presented most recently.
- The device opens each granted meter's configured toggle (typically a valve
  relay) if it has one; meters without a toggle simply gain attribution.
- **Safety backstop:** the device clamps `duration_ms` to its own
  `max_grant_duration` (default **5 minutes**). A server asking for more
  gets the clamp, silently; the command is still acknowledged `ok`. A valve
  is a thing that pours beer on the floor when software misbehaves, so the
  final bound on "how long can it stay open" belongs to the device.
- Applying the same command id twice is a no-op beyond refreshing expiry
  (idempotent, since the server re-sends until acked).

### 4.2 `deny`

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

The explicit refusal. The device signals the user (refusal tone, LED) and
acknowledges with `command_result: ok`. No state changes: existing grants on
other meters are untouched.

### 4.3 `deauthorize`

```json
{
  "id": "cmd_8f22",
  "type": "deauthorize",
  "data": { "meters": [0, 2] }
}
```

| Field | Type | Req | Description |
|---|---|---|---|
| `meters` | array of integer | no | Meters to revoke. **Absent means all meters.** |

Closes the toggles, ends any in-flight pour on those meters (attributed to
the user who poured it), clears the grants. This is the server-initiated
cutoff — an admin button, a policy engine, an emergency stop. Detach and
expiry do the same thing device-side without a command.

## 5. Offline behavior in `server` mode

The decision-maker being remote means presentment can race an outage. The
device applies a configured `offline_policy` when a token event cannot be
delivered (network error / 5xx / no response before a short timeout,
suggested 5 s):

| `offline_policy` | Behavior |
|---|---|
| `deny` (default) | Tap stays closed. Correct for installs where gating is the point. |
| `guest` | **No valves open.** The device records a guest grant for attribution only — a meter without a toggle still meters, and the queued `token` event reaches the server later, preserving the audit trail. Opening valves offline may be revisited later; for now an offline server never results in an opened valve. |

Note what is deliberately absent: a device-side token cache. Caching
assignments would reintroduce the state this design removes and creates
stale-revocation problems. If an install needs offline attribution, that is
`local` mode.

## 6. `local` mode

For serverless installs, or installs that prefer the tap to work identically
with the network down. The device decides from its own configuration:

- Every token is accepted as guest (`require_known_token: false`), or
- Only tokens on a small fixed list in device config are accepted.

`token` events are still emitted (with `status` set, since the device
decided) whenever a server is configured, so the server keeps an audit trail
and can drive "assign this recently seen token" flows. `authorize` commands
received in `local` mode are acknowledged with `result: "unsupported"`.

## 7. Multi-meter, multi-user

Grants are per meter, so a two-tap device can simultaneously have Alice on
meter 0 and Bob on meter 1:

- Alice presents; server responds `authorize {meters: [0], user: "alice"}`.
- Bob presents; server responds `authorize {meters: [1], user: "bob"}`.
- Each meter's pours are attributed to its own grant. Detach/expiry/
  `deauthorize` affect only the named meters.

Which meter a presentment maps to is **server policy**, not protocol. The
`token` event tells the server which reader saw the token (`auth_device`);
an install with one reader per tap can name readers accordingly (e.g.
`core.rfid.0`) and the server maps reader → meter. An install with one
shared reader can grant all meters, or apply fancier policy (the user's
reserved tap, the tap with their keg on it). The protocol only carries the
outcome: `meters: [...]`.

## 8. Interaction with pour reporting

- `pour` events carry `user`, `auth_device`, and `auth_token` from the meter's
  grant at pour time (see main doc §5.1).
- A pour with no active grant (in `open` mode, or `local` allow-all) simply
  omits them; the server records a guest pour.
- A grant ending mid-pour ends the pour first, so attribution reflects who
  actually poured. The next pour on that meter is whoever authorizes next.

