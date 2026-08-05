# Hardware & Wiring

Kegboard v4 targets generic ESP32 devkits. There is no custom PCB (yet); the
legacy kegboard-mini/mega/coaster boards ran the AVR firmware and live on the
[`arduino` branch](https://github.com/Kegbot/kegboard/tree/arduino).

## Supported boards

| Board package | Chip | Notes |
|---|---|---|
| `boards/esp32-s3-devkitc-1.yaml` | ESP32-S3 | **Reference target.** Native USB (no adapter to flash), ample GPIO, PSRAM headroom. |
| `boards/esp32-devkit.yaml` | ESP32 (WROOM-32) | What most people already have in a drawer. |
| `boards/esp32-c6-devkitc-1.yaml` | ESP32-C6 | Forward-looking: WiFi 6, Thread/Zigbee radios. |

Other ESPHome-supported ESP32 variants work; write your own board package
with the same substitution names.

## Pin maps

Defined as substitutions in each board package; override them in your config
to relocate a peripheral.

| Substitution | S3 | Classic | C6 | Used by |
|---|---|---|---|---|
| `meter0_pin`–`meter3_pin` | 4–7 | 4, 5, 13, 14 | 4–7 | Flow meters |
| `onewire_pin` | 15 | 16 | 10 | Thermo 1-Wire bus |
| `relay0_pin`, `relay1_pin` | 16, 17 | 17, 18 | 11, 18 | Relays |
| `buzzer_pin` | 18 | 19 | 19 | Piezo buzzer |
| `led0_pin`, `led1_pin` | 8, 9 | 21, 22 | 20, 21 | Flow LEDs |
| `rfid_rx_pin` | 44 | 23 | 22 | RFID reader UART |
| `onewire_auth_pin` | 21 | 25 | 23 | iButton 1-Wire bus |

The maps avoid each chip's landmines: strapping pins, flash/PSRAM pins,
native-USB pins, and (on the classic ESP32) GPIO 34–39, which are input-only
with no internal pull-ups and so cannot bias an open-collector meter.

## Flow meters

Kegboard supports open-collector meters — typically hall-effect sensors that
pulse once per fixed volume as liquid passes. Meter inputs use the internal
pull-up and count falling edges; wire the meter's output straight to the pin,
its ground to ground.

> **ESP32 GPIOs are not 5 V tolerant.** Open-collector meters are safe on a
> 3.3 V pull-up even when powered from 5 V, because they only ever pull the
> line to ground. Meters with a push-pull 5 V output will damage the ESP32
> and need a level shifter or divider. Check your meter before wiring it.

The default calibration (`ml_per_tick: 0.185`, ~5.4 ticks/mL) matches the
SwissFlow SF800 and its clones. Other meters work; set `ml_per_tick`
accordingly (a Vision 2000 is ~2200 ticks/L → `0.4545`).

## Temperature sensors

DS18B20/DS18S20 sensors on the `onewire_pin` bus, via ESPHome's `one_wire`
and `dallas_temp`. Any number of sensors on one bus; each needs the usual
4.7 kΩ pull-up to 3.3 V (many breakout probes include it).

## Authentication readers

- **125 kHz RFID:** RDM6300-class readers (the ID-12 lineage) on
  `rfid_rx_pin` via UART at 9600 baud, using ESPHome's `rdm6300`. TX-only —
  nothing is wired back to the reader.
- **iButton:** a second 1-Wire bus on `onewire_auth_pin`, read by
  `kegboard_onewire`. Kept separate from the thermo bus so a wet hand stays
  away from the keg sensors.
- **Anything else:** any ESPHome reader (`wiegand`, `pn532`, `rc522`, ...)
  can feed `kegboard_auth` — see
  [extending](developer-notes.md#extending-a-board).

## Relays and valves

`packages/relays.yaml` defines two GPIO relay outputs, each with a watchdog
that switches it off `relay_watchdog_timeout` (default 10 s) after it turns
on — see the package's notes before putting a grant-driven valve on one.
Use a relay module rated for logic-level (3.3 V) input, or a
transistor driver. What's downstream is usually a solenoid valve; give it its
own supply and a flyback diode if the module lacks one.

## Buzzer and LEDs

A **passive** piezo on `buzzer_pin` plays the legacy Kegboard melodies
(`packages/buzzer.yaml`); an active buzzer generates its own tone and will
ignore them. Flow LEDs are ordinary GPIO LEDs with resistors, wired per the
pin map and driven from `on_pour_start`/`on_pour_end` automations.
