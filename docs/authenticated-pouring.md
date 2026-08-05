# Authenticated Pouring

**Status:** DRAFT — companion to the [Kegboard Event Protocol](kegboard-event-protocol.md)
**Date:** 2026-08-03

How a token presented at a Kegboard becomes an open valve and an attributed
pour. This document specifies the interaction between device and server; the
message envelopes it uses (`token` events, commands, `command_result`) are
defined in the main protocol doc.

## 1. Background

A Kegboard install always has flow meters on its beer lines: every pour is
measured, whether or not anyone is identified. Many installs add more, per
tap: a solenoid valve on a relay, so beer only flows for someone
authorized, and a token reader nearby (RFID, iButton, ...) to identify who
that is. Others have no valves at all — anyone can pour at any time, and
authentication only decides who a pour is attributed to.

**Everything in this document is optional.** A monitoring-only Kegboard —
just meters, no readers, no valves — needs none of it: it simply reports
guest pours, and its config never mentions authentication at all.

A presented token means something — which user, which taps, how much — and
the token→user database that answers this can be large. It lives on the
server; the valves and the pour detection live on the device. This doc
defines who decides what, and how the two sides stay simple.

## 2. Summary

- A **pour** is metered flow, reported to the server as an event
  (main doc §5.1) — authenticated or not.
- A **grant** is permission to pour. The server creates one with an
  `authorize` command, naming the meters it covers, the relays (valves) it
  opens, and its limits: max volume, max total time, max idle time. One
  active grant per meter.
- Presenting a physical auth token sends a `token` event to the server.
  In response, the server decides whether to grant access (`authorize`
  command), opening any relevant valves; or to deny it (`deny` command).
- Pours are tagged with the covering grant's id; the server maps that to a
  user. **Identity never reaches the device.**
- A grant ends when a limit is hit, its token detaches, the server revokes
  it, or a newer grant takes its meters. Every ending is reported as a
  `grant_end` event with its reason.
- No server, or server unreachable? The device can decide by itself, per
  its configured mode (§3) and offline policy (§6).

The rest of this doc unpacks each of these.

## 3. Authorization modes

A device operates in exactly one mode per auth gate, chosen in device
configuration:

| Mode | Who decides | Needs server online? | Use case |
|---|---|---|---|
| `server` | Server, per token presentment | Yes (with a configurable offline fallback) | The default with a server configured. Scales to any token database; policy lives in one place. |
| `local` | Device, from its own config | No | Serverless installs; every token pours as guest. |
| `open` | Nobody — no gating | No | Meters-only installs, no valves. Pours are guest-attributed unless a token event happens to resolve. Not a config value: `open` is simply the absence of an auth component (or a meter outside every gate). |

The device is **stateless about policy in `server` mode**: it holds no
token database and no meter↔relay map — only the currently active
grant(s), each of which arrives naming the meters it covers and the
relays it opens (see main doc §7.1). The meter↔relay association is
backend configuration, owned in exactly one place: the server composes
each grant's sets from it, and the device applies them verbatim. Only
`local` mode, where no server can send them, wires that association into
device config.

## 4. `server` mode: the core flow

1. **Token presented.** The reader reports a token; the device emits a
   `token` event with `action: "attached"` and **no `status` field** — the
   absent status is the signal that the device is asking the server to
   decide. The device flushes the batch immediately.
2. **Server decides.** The server looks up the token, applies whatever
   policy it likes (user standing, keg access, time of day), and responds —
   in the same HTTP response — with an `authorize` or a `deny` command.
   Servers SHOULD always answer a decision-requesting token event with one
   of the two.
3. **Device acts.** On `authorize`, the device opens the relays named in
   the grant, tags subsequent pours on the granted meters with the
   grant, and acknowledges with a `command_result` event. On `deny`, the
   tap stays closed and the device SHOULD signal the user (e.g. a refusal
   tone). If the response carries neither — a server bug, defensively —
   the device treats the presentment as denied, without the user signal.
4. **Grant ends** by whichever comes first: token detach (presence
   readers), any of the grant's limits — volume poured, total time, idle
   time — being reached, or a server `deauthorize` command. The device
   closes the valve, ends any in-flight pour (still tagged with the
   departing grant), clears the grant, and reports a `grant_end` event
   naming the reason (main doc §5.7), so the server never has to guess
   why a tap went cold.

Identity never travels down. The server attributes the resulting pours
itself, from each pour's `grant_id` — the server's own identifier for the
grant (main doc §5.1, §7.1). The device acts on tokens and grants; it
never knows who a user is.

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
  |                   | open valve(s), record grant         |
  |                   |                                     |
  |                   | ...pour happens...                  |
  |                   | POST [pour grant_id=g_1,            |
  |                   |       command_result ok] ---------->|
```

## 5. Commands

The three commands this flow uses are fully specified in the main protocol
doc (§7.1–7.3); this section describes only their role in the flow.

- **`authorize`** carries exactly one grant: a server-assigned id, the
  meters it covers and the relays it opens (if any). The server composes
  these sets from its own meter and relay configuration, and any limits. The device
  applies it verbatim: relays open, and pours on the granted meters are
  tagged with the grant, until a limit (volume, total time, idle time) or
  the device's own clamp ends it. Several grants at once — different
  taps, different policy — are simply several `authorize` commands in
  one response.
- **`deny`** is the explicit refusal: the device signals the user, and no
  state changes.
- **`deauthorize`** is a server-initiated cutoff, revoking grants by
  id — an admin button, a policy engine, an emergency stop. Detach and
  the grant's own limits do the same thing device-side without a
  command.

## 6. Offline behavior in `server` mode

The decision-maker being remote means presentment can race an outage. The
device applies a configured `offline_policy` when a token event cannot be
delivered (network error / 5xx / no response before a short timeout,
suggested 5 s):

| `offline_policy` | Behavior |
|---|---|
| `deny` (default) | Tap stays closed. Correct for installs where gating is the point. |
| `guest` | **No valves open.** The device records an attribution-only grant covering every meter **no active grant already covers** (§9, case 6) — a failed offline presentment must never disturb a live, authorized pour. Pours carry the presentment's token echo (no `grant_id`, since no command granted them), and the queued `token` event reaches the server later, preserving the audit trail. Opening valves offline may be revisited later; for now an offline server never results in an opened valve. |

Note what is deliberately absent: a device-side token cache. Caching
assignments would reintroduce the state this design removes and creates
stale-revocation problems. If an install needs offline attribution, that is
`local` mode.

## 7. `local` mode

For serverless installs, or installs that prefer the tap to work identically
with the network down. The device decides by itself: every token is
accepted, as guest. (A device-side allow-list may come later; for now,
restricting *who* pours requires a server.)

This is the one place the meter↔relay association lives on the device:
with no server to send grants, the device's own gate config (meter plus
optional relay) says which valve a locally accepted token opens. Pours
still carry the accepted token's echo, so a server can attribute them
after the fact from its own token database.

`token` events are still emitted (with `status` set, since the device
decided) whenever a server is configured, so the server keeps an audit trail
and can drive "assign this recently seen token" flows. `authorize` commands
received in `local` mode are acknowledged with `result: "unsupported"`.

## 8. Multi-meter, multi-user

Grants are per meter, so a two-tap device can simultaneously have Alice on
meter 0 and Bob on meter 1:

- Alice presents; the server responds
  `authorize {grant_id: "g_1", meter_numbers: [0], relay_numbers: [0], ...}`,
  recording `g_1` as Alice's.
- Bob presents; the server responds
  `authorize {grant_id: "g_2", meter_numbers: [1], relay_numbers: [1], ...}`,
  recording `g_2` as Bob's.
- Each meter's pours arrive tagged with their own grant's `grant_id`, so
  the server attributes meter 0's pours to Alice and meter 1's to Bob.
  Detach, limits, and `deauthorize` affect only their own grant's meters.

Which meter a presentment maps to is **server policy**, not protocol. The
`token` event tells the server which reader saw the token (`auth_device`);
an install with one reader per tap can name readers accordingly (e.g.
`core.rfid.0`) and the server maps reader → meter. An install with one
shared reader can grant all meters, or apply fancier policy (the user's
reserved tap, the tap with their keg on it). The protocol only carries the
outcome: each grant's `meter_numbers` and `relay_numbers`.

## 9. Grant and pour corner cases

Every way a pour and a grant can interact, and the rule for each. Two
principles cover them all:

- **Attribution is decided when the pour ends.** The pour is tagged
  (`auth_device`, `auth_token`, `grant_id` — main doc §5.1) with the grant
  covering its meter at that moment, in full. The server resolves the user
  from `grant_id` — pinned to its own authorization decision, so a
  late-delivered pour attributes correctly even if the token was reassigned
  in the meantime — or from the token echo for locally decided grants.
- **Limit accounting is decided as flow is observed.** Flow counts toward
  the grant covering the meter at the moment it flows — toward its
  `max_volume_ml`, and resetting its `max_idle_ms` — and is never
  retroactive.

Where the two disagree (case 2 below), that is deliberate: the rules stay
simple, and the mismatch is confined to a corner. In general this catalog
favors the simple implementation over the clever one, accepting that a
handful of corner cases do slightly surprising things.

1. **Pour with no grant.** An unauthenticated guest pour: no auth fields
   at all. This is every pour in `open` mode, and any pour on a meter
   nobody has authorized — installs without valves meter everything, all
   the time.
2. **Grant arrives mid-pour: it adopts the pour.** The pour keeps its
   `pour_id` and, at its end, is attributed to the grant **in full**,
   including the volume poured before the grant arrived. This is the
   headline corner case on valve-less installs: someone starts pouring,
   realizes they forgot to authenticate, and keys in mid-glass — the whole
   glass lands on their tab. Per the second principle, the pre-grant
   volume does *not* count toward the grant's limits. Adoption is a
   single, clearly marked policy point in the firmware, so it can become
   "split into a new pour" later without disturbing anything else.
3. **Grant replaces another mid-pour: the pour splits.** The in-flight
   pour ends immediately, tagged with the departing grant; flow that
   continues opens a new pour (fresh `pour_id`) under the new grant. Pour
   boundaries always align with authorization boundaries, so consecutive
   drinkers never share a pour record.
4. **Grant updated (same `grant_id`) mid-pour: nothing happens.** It is
   the same grant; the pour continues under it, counters intact.
5. **Grant ends mid-pour** — limit reached, token detached, `deauthorize`:
   the pour ends first, tagged with the grant, and its event precedes the
   `grant_end` (main doc §5.7). Flow that continues — after the valve
   closes, or on a meter that never had one — is case 1 again: a new,
   unauthenticated guest pour.
6. **Offline-guest grants never disturb live grants** (§6): they cover
   only the meters no active grant already covers. If every meter is
   covered, the presentment creates no grant at all; the queued `token`
   event still preserves the audit trail.
