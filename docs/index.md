# Kegboard Manual

The manual for **Kegboard v4**, the ESP32-based kegerator controller built on
[ESPHome](https://esphome.io/).

New here? Start with the [Overview](overview.md), then pick an
[operating mode](operating-modes.md) and follow
[Installation](installation.md).

```{toctree}
:caption: Manual
:maxdepth: 1

overview
operating-modes
installation
hardware
configuration
operation
developer-notes
```

```{toctree}
:caption: "Appendix: Protocol"
:maxdepth: 1

kegboard-event-protocol
authenticated-pouring
```

The protocol appendix is the device↔server contract; the JSON Schemas in
[`schemas/`](https://github.com/Kegbot/kegboard/tree/main/schemas) are
normative.
