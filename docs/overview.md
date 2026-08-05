# Kegboard Overview

## What is a Kegboard?

*Kegboard* is the controller board in a [Kegbot](https://kegbot.org/) system:
the device that watches the flow meters, temperature sensors, and auth tokens
on a kegerator, and drives its valves, relays, and buzzer.

Kegboard v4 is a **networked appliance**. The board detects pours itself,
applies its own calibration, and reports finished drinks over WiFi — there is
no host daemon and no USB cable. It is built as a set of
[ESPHome](https://esphome.io/) external components, so a Kegboard is defined
by a YAML file and extended the same way.

Previous Kegboard designs (v1–v3) were Arduino- and PIC-based sensor boards
that streamed raw ticks over serial; they are preserved, feature-frozen, on
the [`arduino` branch](https://github.com/Kegbot/kegboard/tree/arduino).

## Features

- **Flow sensing.** Any number of meters, limited only by GPIO. Pour
  detection runs on the device: start, end, volume, flow rate, and a
  diagnostic tick time series per pour.
- **On-device calibration.** `ml_per_tick` lives on the board; reported
  volumes are authoritative.
- **Temperature sensing.** DS18B20/DS18S20 sensors on a 1-Wire bus, any
  number of them, via ESPHome's stock `dallas_temp`.
- **Authentication.** 125 kHz RFID readers and iButtons out of the box; any
  reader ESPHome supports can feed the auth component.
- **Authenticated pouring.** Per-meter grants — decided by the server or
  locally — drive valve relays and pour attribution. See
  [Authenticated Pouring](authenticated-pouring.md).
- **Relay control with watchdog.** Each relay switches itself off after a
  timeout, so a crashed controller never leaves a valve open; grant-held
  relays are bounded by the grant clamp instead.
- **Buzzer.** The classic Kegboard melodies, transcribed from the AVR
  firmware.
- **Outage-proof reporting.** Events queue on the device and deliver late
  with correct timestamps; retries can never create a duplicate drink.
- **Dashboard pairing.** No API keys: an unprovisioned board appears on the
  server dashboard by name and provisions itself when approved.
- **Home Assistant, free.** Every meter, sensor, and relay is an ESPHome
  entity; a server is optional.
- **Open protocol.** Everything is JSON to a single HTTP endpoint, specified
  in the [Kegboard Event Protocol](kegboard-event-protocol.md) with normative
  schemas. Any server can implement it.

Everything is optional except a meter. A board with no thermo sensor, no
reader, and no server still meters pours.

## Requirements

- **An ESP32 board.** Any ESPHome-supported variant works; tested pin maps
  ship for the ESP32-S3-DevKitC-1 (reference), the classic ESP32 DevKitC, and
  the ESP32-C6-DevKitC-1. See [Hardware & Wiring](hardware.md).
- **Flow meters.** Open-collector hall-effect meters such as the SwissFlow
  SF800. **ESP32 GPIOs are not 5 V tolerant** — see the
  [hardware notes](hardware.md#flow-meters).
- **WiFi.**
- **ESPHome** to build and flash, until prebuilt binaries ship. See
  [Installation](installation.md).
- Optionally, a **[Kegbot Server](https://github.com/Kegbot/kegbot-server)**
  (or anything else implementing the event protocol) to record drinks.

## Status

Kegboard v4 is under active construction; see the status table in the
[README](https://github.com/Kegbot/kegboard#status). Reporting is HTTP-only for now — MQTT, BLE,
and WebSocket transports are planned.
