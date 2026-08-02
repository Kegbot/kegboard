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
| `kegboard` hub component | In progress |
| `kegboard_meter` flow meter component | In progress |
| `kegboard_kegbot` HTTP reporter | In progress |
| Temperature, relays, buzzer, LEDs | Planned |
| Auth tokens (RFID, iButton) | Planned |
| Prebuilt binaries + web installer | Planned |

Reporting is **HTTP only** for now. MQTT, BLE, and WebSocket transports are
planned.

## Repository layout

```
components/     ESPHome external components (this repo is the component source)
  kegboard/     Hub component + the framework-agnostic core (see CORE.md)
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
