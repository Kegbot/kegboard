# Kegboard Overview

## What is a Kegboard?

*Kegboard* is the controller board in a [Kegbot](https://kegbot.org/) system.
It's the device that monitors flow meters and publishes this data to a system
like [Kegbot Backend](https://github.com/kegbot/kegbot-backend).

A kegboard can also monitor additional, optional accessories like temperature sensors
and OneWire-based authentication devices; and it can drive valves,
relays, and buzzers.

An open source Kegboard project has existed since around 2005. Today, it is
built for esp32-based devices, and leverages the [ESPHome](https://esphome.io/)
framework.

You can flash this firmware to a device and use it in standalone mode, or
point it at a [Kegbot Server](https://github.com/Kegbot/kegbot-server) instance
for full functionality.

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
- **Authenticated pouring.** Server-decided grants drive valve relays and
  pour attribution. See
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
