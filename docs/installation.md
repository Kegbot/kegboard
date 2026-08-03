# Installation

Kegboard is flashed like any ESPHome project: write a YAML config, build,
flash once over USB, then update over the air. Prebuilt binaries and a
browser-based installer are planned but not yet available.

## Install ESPHome

Any current ESPHome (2025.x or later) works. With [uv](https://docs.astral.sh/uv/):

```console
$ uv tool install esphome
```

or `pip install esphome`. See the
[ESPHome installation guide](https://esphome.io/guides/installing_esphome)
for other options, including the Home Assistant add-on.

## Get a configuration

Start from an example:

```console
$ git clone https://github.com/Kegbot/kegboard.git
$ cd kegboard/examples
$ cp secrets.yaml.example secrets.yaml   # fill in WiFi + reporting URL
```

Pick the example matching your [operating mode](operating-modes.md) —
`kegbot-2tap.yaml`, `home-assistant-2tap.yaml`, or `kegbot-full.yaml` — and
trim it to the hardware you actually have.

Configs outside this repository consume the components directly:

```yaml
external_components:
  - source: github://Kegbot/kegboard@main
```

## Board configuration

Each config includes a board package from `boards/`, which selects the chip
and defines the pin map as substitutions:

```yaml
packages:
  board: !include ../boards/esp32-s3-devkitc-1.yaml
  base: !include ../packages/base.yaml
```

To move a peripheral to a different pin, override the substitution in your
config rather than editing the board file. See
[Hardware & Wiring](hardware.md) for the maps and which pins are safe.

The `base` package provides WiFi (with a fallback `kegboard-setup` access
point and captive portal), OTA, logging, the native API, SNTP time, and the
HTTP client.

## Flash

First flash is over USB:

```console
$ esphome run kegbot-2tap.yaml
```

`esphome run` compiles, prompts for the port, flashes, and tails the log.
Every later `esphome run` offers OTA over WiFi instead — the cable is only
ever needed once.

If the board can't reach your WiFi it raises the `kegboard-setup` access
point; connect to it and enter credentials in the captive portal.

## Pair with the server

Nothing to configure: no API key, no token. On first contact with a server
that requires authentication, the board enters pairing and appears on the
server dashboard under its `serial_number` (default `kegboard-<mac>`).
Approve it there; the board stores its credential in flash and is
provisioned from then on. Revoking the credential server-side sends the
board back into pairing, so key rotation is "revoke, then re-allow" with no
device-side ceremony. Details in [protocol §8](kegboard-event-protocol.md).

Events recorded before approval are queued, not lost — they deliver, with
correct timestamps, once pairing completes.

## Verify

- `esphome logs kegbot-2tap.yaml` and blow through a meter (or short the
  meter pin to ground a few times): tick counts should move and a pour
  should end after `idle_timeout`.
- Check the server dashboard for the device and the pour.
- For calibration, pour a known volume and scale `ml_per_tick`; see
  [`kegboard_meter`](configuration.md#kegboard_meter).
