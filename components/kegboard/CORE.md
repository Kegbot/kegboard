# Kegboard core

The files listed below are the **framework-agnostic core**: the logic that is
genuinely kegboard-specific rather than ESPHome-specific.

- `pour_session.h` / `.cpp` — pour detection state machine
- `tick_series.h` / `.cpp` — bounded `<offset_ms>:<ticks>` diagnostic series
- `kegbot_request.h` / `.cpp` — Kegbot Server API request construction
- `ring_queue.h` — bounded FIFO for offline report buffering

## Rules

1. **No framework headers.** Core files must not include anything from
   `esphome/`, Arduino, or ESP-IDF. Only the C++ standard library. CI enforces
   this (`script/check-core-purity.py`).
2. **No I/O, no clocks, no timers.** Time is passed in as arguments. This is
   what lets the whole state machine run under host unit tests.
3. **Every core file has host tests** in `tests/core/`, run with plain `g++`
   on every push. No hardware, no toolchain download.

## Why

Choosing ESPHome buys an enormous amount of infrastructure, but it is a
third-party framework that makes breaking changes to its external-component
API on a regular cadence. Keeping the kegboard-specific logic behind this
boundary means the ESPHome components stay thin adapters: if ESPHome ever
becomes untenable, porting to bare ESP-IDF is an adapter rewrite rather than a
firmware rewrite.

It also has an immediate payoff, which is the real reason to bother: pour
detection, calibration, and the queue-and-retry path are the parts most likely
to have subtle bugs, and this is what makes them testable in milliseconds
without flashing a board.

## Note on layout

These files sit directly in `components/kegboard/` rather than a `core/`
subdirectory because ESPHome only copies source files found at the top level
of an external component — `ComponentManifest.resources` descends into
subdirectories only for manifests constructed with `recursive_sources=True`,
which is reserved for ESPHome's own internal components.
