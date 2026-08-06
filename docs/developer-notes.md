# Developer Notes

## Repository layout

```
components/     ESPHome external components (this repo is the component source)
  kegboard/     Hub component + the framework-agnostic core (see CORE.md)
  kegboard_meter/     Flow meter and pour detection
  kegboard_reporter/  Event protocol client (batching, pairing, commands)
  kegboard_auth/      Per-meter authorization
  kegboard_onewire/   iButton presence
packages/       Composable YAML users include
boards/         Pin maps per target board
docs/           This manual + protocol specifications
schemas/        Normative JSON Schemas for the protocol
examples/       Worked configurations
tests/core/     Host unit tests — plain g++, no hardware, no toolchain
tools/          kegboard-sim, the TUI simulator
script/         CI helpers
```

## Building and testing

Host unit tests for the core logic need nothing but a C++ compiler:

```console
$ make -C tests/core
$ make -C tests/core STRICT=1   # warnings as errors, as CI runs it
```

Firmware builds are plain ESPHome:

```console
$ esphome compile examples/kegbot-2tap.yaml
```

Formatting and lint mirror ESPHome's conventions (clang-format, ruff,
yamllint), since these components compile into ESPHome's tree:

```console
$ pip install pre-commit && pre-commit install
$ pre-commit run --all-files
```

## Hacking on the core

The rules, enforced by CI (`script/check-core-purity.py`):

1. Core files (`components/kegboard/*.cpp/h` in the `kbcore` namespace)
   include only the C++ standard library — no ESPHome, Arduino, or ESP-IDF
   headers.
2. No I/O, no clocks, no timers; time is passed in as arguments.
3. Every core file has host tests in `tests/core/`.

This is what keeps pour detection, the grant table, and the queue-and-retry
path testable in milliseconds without flashing a board — and what keeps the
ESPHome components thin enough to port away from ESPHome if that ever becomes
necessary. Details in
[`components/kegboard/CORE.md`](https://github.com/Kegbot/kegboard/blob/main/components/kegboard/CORE.md).

`script/check-events-schema.py` keeps the C++ event serializers and the JSON
Schemas in agreement.

## Extending a board

Most extensions are YAML, not firmware:

- **More taps:** another `kegboard_meter` entry with a fresh `meter_number`.
- **A different reader:** any ESPHome reader component whose trigger calls
  `kegboard_auth.token_attached` with a `device` name and token string. The
  string must match how the token is registered server-side — log the first
  scan and register that exact value. `examples/kegbot-full.yaml` shows RFID
  (`rdm6300`) and iButton (`kegboard_onewire`).
- **Displays, pressure sensors, more relays, anything ESPHome supports:**
  stock components alongside the Kegboard ones; `on_pour_*` triggers and the
  `pouring`/`authorized` entities are the integration points.

## The simulator

`tools/kegboard-sim.py` is a TUI kegboard for developing receivers without
hardware. It speaks the full protocol — pairing, batching, `age_ms`,
commands, dedup — and validates every outgoing batch against the schemas, so
it cannot teach a server the wrong protocol.

```console
$ uv run tools/kegboard-sim.py http://localhost:8000/kegboard-event
```

Single keys pour a beer (with live `pour_update`s), toggle temperature
logging, present preset tokens, kill the heartbeat, go offline to build a
backlog that delivers late, replay the last batch to exercise dedup, send an
unknown event type, and reboot to reset `boot_id`.

## Protocol traffic logging

The reporter logs one line per delivery at `DEBUG`. For full request and
response bodies:

```yaml
logger:
  logs:
    kegboard_reporter: VERY_VERBOSE
```

`VERY_VERBOSE` lines are compiled out at default log levels, so production
builds pay nothing. The logger truncates lines to its buffer (default 512
bytes); add `logger: { tx_buffer_size: 2048 }` if a batch body clips.

## Cutting a release

The canonical firmware version is `KEGBOARD_VERSION` in
`components/kegboard/kegboard.cpp`; `docs/conf.py` and `packages/base.yaml`
mirror it and must always agree. Don't edit them by hand — release with:

```console
$ script/bump.py --dry-run   # preview
$ script/bump.py             # 4.0.1 -> 4.0.2; a -pre/-dev suffix is dropped
$ script/bump.py 4.1.0       # or pick the version explicitly
```

`bump.py` verifies the tree is clean and the tag is free, updates all three
version files, dates the changelog's "Current version (in development)"
section as `## vX.Y.Z (YYYY-MM-DD)` (leaving a fresh open section above it),
commits, and creates the `vX.Y.Z` tag. It does not push; publish with:

```console
$ git push origin HEAD vX.Y.Z
```

## Protocol changes

The [event protocol](kegboard-event-protocol.md) and
[authenticated pouring](authenticated-pouring.md) docs are the contract, and
the schemas in `schemas/` are normative. Changing the wire format means
updating the spec, the schemas, the core serializers, the simulator, and the
schema-agreement check together.
