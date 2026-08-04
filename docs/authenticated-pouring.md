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
3. **Device acts.** On `authorize`, the device opens the relays for the
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

The three commands this flow uses are fully specified in the main protocol
doc (§7.1–7.3); this section describes only their role in the flow.

- **`authorize`** creates a grant: the device opens the relays for the
  server-chosen meter set and attributes subsequent pours to the given
  user, for a server-chosen (device-clamped) duration.
- **`deny`** is the explicit refusal: the device signals the user, and no
  state changes.
- **`deauthorize`** is the server-initiated cutoff — an admin button, a
  policy engine, an emergency stop. Detach and expiry do the same thing
  device-side without a command.

## 5. Offline behavior in `server` mode

The decision-maker being remote means presentment can race an outage. The
device applies a configured `offline_policy` when a token event cannot be
delivered (network error / 5xx / no response before a short timeout,
suggested 5 s):

| `offline_policy` | Behavior |
|---|---|
| `deny` (default) | Tap stays closed. Correct for installs where gating is the point. |
| `guest` | **No valves open.** The device records a guest grant for attribution only — a meter without a relay still meters, and the queued `token` event reaches the server later, preserving the audit trail. Opening valves offline may be revisited later; for now an offline server never results in an opened valve. |

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

- Alice presents; server responds `authorize {meter_numbers: [0], user: "alice"}`.
- Bob presents; server responds `authorize {meter_numbers: [1], user: "bob"}`.
- Each meter's pours are attributed to its own grant. Detach/expiry/
  `deauthorize` affect only the named meters.

Which meter a presentment maps to is **server policy**, not protocol. The
`token` event tells the server which reader saw the token (`auth_device`);
an install with one reader per tap can name readers accordingly (e.g.
`core.rfid.0`) and the server maps reader → meter. An install with one
shared reader can grant all meters, or apply fancier policy (the user's
reserved tap, the tap with their keg on it). The protocol only carries the
outcome: `meter_numbers: [...]`.

## 8. Interaction with pour reporting

- `pour` events carry `user`, `auth_device`, and `auth_token` from the meter's
  grant at pour time (see main doc §5.1).
- A pour with no active grant (in `open` mode, or `local` allow-all) simply
  omits them; the server records a guest pour.
- A grant ending mid-pour ends the pour first, so attribution reflects who
  actually poured. The next pour on that meter is whoever authorizes next.
