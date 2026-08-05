# Operating Modes

A Kegboard is useful at three levels of integration. The modes are additive,
not exclusive: a board reporting to a Kegbot Server can stay connected to
Home Assistant, and every board works standalone when the network is down.

## Standalone

No server, nothing else on the network. The board meters pours and runs
local automations (`on_pour_start`, `on_pour_end`). Token readers still fire
their triggers, so a serverless board can gate valves with plain ESPHome
automations if wanted. Watch it work with `esphome logs`.

This is also the right mode for bench work: verify wiring and calibration
before pointing the board at anything.

## Home Assistant mode

Enable ESPHome's native `api:` (the `base` package does) and Home Assistant
discovers the board automatically. Every configured entity — tick totals,
pour volume, flow rate, pouring state, temperatures, relays, auth state —
appears in HA for dashboards and automations. No Kegbot Server involved.

`examples/home-assistant-2tap.yaml` is a complete config for this mode.

## Kegbot Server mode

The flagship. Add `kegboard_reporter` with a `reporting_url` and the board
speaks the [Kegboard Event Protocol](kegboard-event-protocol.md):

- Finished pours are POSTed as events with authoritative `volume_ml`,
  attribution, and a diagnostic tick series.
- Events queue during outages and deliver late with correct timestamps.
- The board pairs itself from the server dashboard — no API key to configure.
- Server commands (authorize, deny, valve control) ride back in HTTP
  responses; with `kegboard_auth`, the server decides every token
  presentment.

The server does not have to be Kegbot Server: the protocol is a single
HTTP endpoint with published schemas, and the minimum viable receiver is one
unauthenticated handler.

`examples/kegbot-2tap.yaml` is the starting point;
`examples/kegbot-full.yaml` adds relays, buzzer, RFID, and iButton auth.

## Choosing

| You have | Use |
|---|---|
| A bench and a meter | Standalone |
| Home Assistant | HA mode (keep it enabled in the other modes too — it is how you watch a pour live) |
| A Kegbot Server, or your own receiver | Kegbot Server mode |
