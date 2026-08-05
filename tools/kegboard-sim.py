# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "textual>=1.0,<4",
#   "httpx>=0.27",
#   "jsonschema>=4.21",
# ]
# ///
"""kegboard-sim: a TUI kegboard for developing receivers.

Simulates a Kegboard speaking the Kegboard Event Protocol
(docs/kegboard-event-protocol.md) so the server side can be developed without
hardware. Faithful where it matters -- envelopes, ids, age_ms, pairing,
commands, queueing -- and simplified where it doesn't (retry timing).

Every outgoing batch is validated against schemas/kegboard-event.schema.json
when run from the repo, so the simulator cannot drift from the protocol.

Usage:
    uv run tools/kegboard-sim.py http://localhost:8000/kegboard-event
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import random
import time
import uuid

import httpx
from rich.markup import escape
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.screen import ModalScreen
from textual.widgets import Footer, Header, OptionList, RichLog, Static
from textual.widgets.option_list import Option

FW_VERSION = "4.0.0-sim"
QUEUE_CAPACITY = 16
POUR_UPDATE_INTERVAL_S = 1.0
PAIRING_POLL_S = 5.0
RETRY_S = 5.0
MAX_GRANT_S = 300.0  # max_grant_duration clamp

TOKEN_PRESETS = [
    ("core.rfid", "0089f2c4", "Alice's fob"),
    ("core.rfid", "00a1b2c3", "Bob's fob"),
    ("core.rfid", "deadbeef", "Unknown fob"),
    ("onewire", "01a2b3c4d5e6f708", "iButton (presence)"),
]

STATE_FILE = Path.home() / ".cache" / "kegboard-sim" / "tokens.json"


def find_schema() -> dict | None:
    for parent in Path(__file__).resolve().parents:
        candidate = parent / "schemas" / "kegboard-event.schema.json"
        if candidate.is_file():
            return json.loads(candidate.read_text())
    return None


class Simulator:
    """Protocol state machine; the TUI is a thin shell around this."""

    def __init__(self, url: str, device: str, heartbeat_s: int, temp_interval_s: int):
        self.url = url
        self.device = device
        self.heartbeat_s = heartbeat_s
        self.temp_interval_s = temp_interval_s

        self.boot_id = f"{random.getrandbits(32):08x}"
        self.next_id = 1
        self.started_mono = time.monotonic()
        self.queue: list[dict] = []  # events + their created monotonic times
        self.dropped = 0
        self.totals = {0: 0, 1: 0}  # meter -> lifetime ticks
        self.ml_per_tick = 0.185

        self.token: str | None = None
        self.denied = False
        self.offline = False
        self.healthy = True
        # meter -> shared grant dict: {grant_id, auth_device, token, relays,
        # max_volume_ml, max_duration_s, max_idle_s, created, last_flow,
        # poured_ml}
        self.grants: dict[int, dict] = {}
        # meter -> in-flight pour: {pour_id, grant, poured, duration_ms, series}
        self.pours: dict[int, dict] = {}
        self.seen_command_ids: dict[str, str] = {}  # id -> result, for re-acks
        self.last_body: str | None = None
        self.presented_presence: tuple[str, str] | None = None

        self.log = lambda msg: None  # installed by the app
        self.on_state_change = lambda: None

        self.schema_validator = None
        schema = find_schema()
        if schema is not None:
            import jsonschema

            self.schema_validator = jsonschema.Draft202012Validator(schema)

        self._client = httpx.AsyncClient(timeout=10.0)
        self._send_lock = asyncio.Lock()
        self._load_token()

    # -- identity & time ----------------------------------------------------

    def _state_key(self) -> str:
        return hashlib.sha256(f"{self.url}|{self.device}".encode()).hexdigest()[:16]

    def _load_token(self) -> None:
        try:
            tokens = json.loads(STATE_FILE.read_text())
            self.token = tokens.get(self._state_key())
        except (OSError, ValueError):
            self.token = None

    def _save_token(self) -> None:
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        try:
            tokens = json.loads(STATE_FILE.read_text())
        except (OSError, ValueError):
            tokens = {}
        if self.token:
            tokens[self._state_key()] = self.token
        else:
            tokens.pop(self._state_key(), None)
        STATE_FILE.write_text(json.dumps(tokens))

    def uptime_ms(self) -> int:
        return int((time.monotonic() - self.started_mono) * 1000)

    # -- event construction -------------------------------------------------

    def make_event(self, type_: str, data: dict) -> dict:
        event = {
            "id": self.next_id,
            "type": type_,
            "age_ms": 0,  # recomputed at send
            "data": data,
            "_created_mono": time.monotonic(),
        }
        self.next_id += 1
        return event

    def enqueue(self, event: dict) -> None:
        self.queue.append(event)
        if len(self.queue) > QUEUE_CAPACITY:
            self.queue.pop(0)
            self.dropped += 1
            self.log(f"[red]queue full; dropped oldest ({self.dropped} total)[/]")
        self.on_state_change()

    def _serialize(self, events: list[dict]) -> str:
        now = time.monotonic()
        batch = {
            "v": 1,
            "device": self.device,
            "boot_id": self.boot_id,
            "sent_uptime_ms": self.uptime_ms(),
            "events": [
                {k: v for k, v in e.items() if not k.startswith("_")}
                | {"age_ms": int((now - e["_created_mono"]) * 1000)}
                for e in events
            ],
        }
        return json.dumps(batch)

    # -- delivery -----------------------------------------------------------

    async def flush(self, ephemeral: list[dict] | None = None) -> bool:
        """Send queued events (plus ephemeral pour_updates). True on 2xx."""
        async with self._send_lock:
            if self.denied or self.offline:
                return False
            queued = self.queue[:QUEUE_CAPACITY]
            events = queued + (ephemeral or [])[: QUEUE_CAPACITY - len(queued)]
            if not events:
                return True

            body = self._serialize(events)
            if self.schema_validator is not None:
                errors = list(self.schema_validator.iter_errors(json.loads(body)))
                for error in errors:
                    self.log(f"[bold red]SIM BUG: schema violation: {error.message}[/]")

            headers = {"Content-Type": "application/json"}
            if self.token:
                headers["Authorization"] = f"Bearer {self.token}"

            self.log(f"[dim]⇢ {escape(body)}[/]")
            try:
                response = await self._client.post(
                    self.url, content=body, headers=headers
                )
            except httpx.HTTPError as err:
                self.healthy = False
                self.log(f"[red]send failed: {err.__class__.__name__}: {err}[/]")
                self.on_state_change()
                return False

            self.last_body = body
            summary = f"{len(events)} event(s), {len(body)} bytes"
            if response.content:
                try:
                    json.loads(response.text)
                    self.log(f"[dim]⇠ {escape(response.text)}[/]")
                except ValueError:
                    ctype = response.headers.get("content-type", "unknown")
                    self.log(
                        f"[dim]⇠ non-JSON body ({ctype}, "
                        f"{len(response.content)} bytes)[/]"
                    )
            if 200 <= response.status_code < 300:
                self.healthy = True
                del self.queue[: len(queued)]
                self.log(f"[green]→ {response.status_code}[/] ({summary})")
                await self._handle_commands(response)
                self.on_state_change()
                return True

            if response.status_code == 401:
                self.healthy = True
                await self._handle_pairing(response)
                self.on_state_change()
                return False

            if 400 <= response.status_code < 500:
                self.log(
                    f"[red]→ {response.status_code}; dropping batch[/] ({summary})"
                )
                del self.queue[: len(queued)]
                self.dropped += len(queued)
                self.on_state_change()
                return False

            self.healthy = False
            self.log(f"[red]→ {response.status_code}; will retry[/] ({summary})")
            self.on_state_change()
            return False

    async def replay_last(self) -> None:
        """Re-send the previous body verbatim: a dedup test for the server."""
        if self.last_body is None:
            self.log("[yellow]nothing sent yet[/]")
            return
        self.log("[cyan]replaying last batch verbatim (server should dedup)[/]")
        try:
            response = await self._client.post(
                self.url,
                content=self.last_body,
                headers={"Content-Type": "application/json"}
                | ({"Authorization": f"Bearer {self.token}"} if self.token else {}),
            )
            self.log(f"replay → {response.status_code}")
        except httpx.HTTPError as err:
            self.log(f"[red]replay failed: {err}[/]")

    # -- responses ----------------------------------------------------------

    async def _handle_pairing(self, response: httpx.Response) -> None:
        if self.token:
            self.log("[yellow]401 with token: dropping it, re-pairing[/]")
            self.token = None
            self._save_token()
        try:
            pairing = response.json().get("pairing", {})
        except ValueError:
            pairing = {}
        state = pairing.get("state", "pending")
        if state == "allowed" and pairing.get("token"):
            self.token = pairing["token"]
            self._save_token()
            self.log("[green]pairing allowed; token provisioned[/]")
            asyncio.create_task(self.flush())
        elif state == "denied":
            self.denied = True
            self.log("[red]pairing denied; stopped until reboot (b)[/]")
        else:
            self.log(
                f"[yellow]pairing pending; approve '{self.device}' "
                f"on the dashboard (retrying every {PAIRING_POLL_S:.0f}s)[/]"
            )

    async def _handle_commands(self, response: httpx.Response) -> None:
        try:
            commands = response.json().get("commands") or []
        except ValueError:
            return
        for cmd in commands:
            cmd_id, cmd_type = cmd.get("id"), cmd.get("type")
            if not cmd_id:
                continue
            if cmd_id in self.seen_command_ids:
                # Not re-applied, but re-acknowledged: the earlier ack may
                # have been evicted before delivery.
                result = self.seen_command_ids[cmd_id]
                self.log(f"[cyan]← command {cmd_id} re-delivered; re-ack {result}[/]")
                self.enqueue(
                    self.make_event(
                        "command_result", {"command": cmd_id, "result": result}
                    )
                )
                continue
            data = cmd.get("data")
            if not isinstance(data, dict):
                data = {}
            result = self._apply_command(cmd_id, cmd_type, data)
            self.seen_command_ids[cmd_id] = result
            self.log(f"[cyan]← command {cmd_type} ({cmd_id}) → {result}[/]")
            self.enqueue(
                self.make_event("command_result", {"command": cmd_id, "result": result})
            )

    @staticmethod
    def _port_number(value) -> bool:
        """A JSON meter/relay number: an int, not a bool (True == 1)."""
        return isinstance(value, int) and not isinstance(value, bool)

    def _apply_command(self, cmd_id: str, cmd_type: str, data: dict) -> str:
        if cmd_type == "authorize":
            grant_id = data.get("grant_id")
            meters = data.get("meter_numbers")
            if not isinstance(grant_id, str) or not isinstance(meters, list) or not meters:
                return "error"
            if any(not self._port_number(m) for m in meters):
                return "error"
            # A grant naming a meter or relay the device does not have is
            # acknowledged `error` and not applied, in whole.
            inventory = set(self.totals)
            relays = data.get("relay_numbers") or []
            if not isinstance(relays, list) or any(
                not self._port_number(r) for r in relays
            ):
                return "error"
            if any(m not in inventory for m in meters) or any(
                r not in inventory for r in relays
            ):
                return "error"
            # Malformed limits are an error ack, never a crashed task: a sim
            # for in-development servers must survive bad payloads.
            limits = {}
            for key in ("max_volume_ml", "max_duration_ms", "max_idle_ms"):
                value = data.get(key)
                if value is None:
                    value = 0
                if isinstance(value, bool) or not isinstance(value, (int, float)):
                    return "error"
                if value < 0:
                    return "error"
                limits[key] = value
            existing = next(
                (g for g in self.grants.values() if g["grant_id"] == grant_id), None
            )
            # Meters taken from other grants, or shed by an update, are replaced.
            self._end_grants(
                [
                    m
                    for m, g in self.grants.items()
                    if (g is not existing and m in meters)
                    or (g is existing and m not in meters)
                ],
                "replaced",
            )
            now = time.monotonic()
            max_duration_s = limits["max_duration_ms"] / 1000
            grant = existing if existing is not None else {
                "grant_id": grant_id,
                "created": now,
                "last_flow": now,
                "poured_ml": 0.0,
            }
            grant.update(
                {
                    "auth_device": data.get("auth_device", ""),
                    "token": data.get("token", ""),
                    "relays": relays,
                    "max_volume_ml": limits["max_volume_ml"],
                    "max_duration_s": (
                        min(max_duration_s, MAX_GRANT_S)
                        if max_duration_s
                        else MAX_GRANT_S
                    ),
                    "max_idle_s": limits["max_idle_ms"] / 1000,
                }
            )
            for meter in meters:
                self.grants[meter] = grant
            self.log(
                f"[green]{'updated' if existing else 'granted'} {grant_id}: "
                f"meters {meters}, relays {grant['relays']}[/]"
            )
            if existing is None:
                asyncio.create_task(self._watch_grant(grant))
            return "ok"
        if cmd_type == "deny":
            self.log(f"[yellow]denied: {data.get('reason', '(no reason)')}[/]")
            return "ok"
        if cmd_type == "deauthorize":
            ids = data.get("grant_ids")
            if ids is not None and not isinstance(ids, list):
                return "error"
            self._end_grants(
                [
                    m
                    for m, g in self.grants.items()
                    if ids is None or g["grant_id"] in ids
                ],
                "command",
            )
            return "ok"
        return "unsupported"

    def _end_grants(self, meters: list[int], reason: str) -> None:
        ended: dict[int, tuple[dict, list[int]]] = {}
        for meter in meters:
            grant = self.grants.pop(meter, None)
            if grant is None:
                continue
            ended.setdefault(id(grant), (grant, []))[1].append(meter)
        for grant, released in ended.values():
            for meter in sorted(released):
                # A grant ending mid-pour ends the pour first, so the final
                # pour event precedes the grant_end.
                self._finish_pour(meter)
            data = {
                "meter_numbers": sorted(released),
                "reason": reason,
                "volume_ml": round(grant["poured_ml"], 3),
                "duration_ms": int((time.monotonic() - grant["created"]) * 1000),
            }
            if grant["auth_device"]:
                data["auth_device"] = grant["auth_device"]
            if grant["token"]:
                data["auth_token"] = grant["token"]
            data["grant_id"] = grant["grant_id"]
            self.enqueue(self.make_event("grant_end", data))
            self.log(
                f"[yellow]grant {grant['grant_id']} ended on meters "
                f"{sorted(released)}: {reason}[/]"
            )
        if ended:
            self.on_state_change()
            # Deliver promptly when the connection is healthy, rather than
            # waiting out the heartbeat.
            asyncio.create_task(self.flush())

    async def _watch_grant(self, grant: dict) -> None:
        while True:
            await asyncio.sleep(0.5)
            meters = [m for m, g in self.grants.items() if g is grant]
            if not meters:
                return
            now = time.monotonic()
            if now - grant["created"] >= grant["max_duration_s"]:
                self._end_grants(meters, "max_duration")
                return
            if grant["max_idle_s"] and now - grant["last_flow"] >= grant["max_idle_s"]:
                self._end_grants(meters, "max_idle")
                return

    # -- simulated activity -------------------------------------------------

    def _finish_pour(self, meter: int) -> None:
        """End the in-flight pour on `meter` (if any) and emit its pour event."""
        state = self.pours.pop(meter, None)
        if state is None:
            return
        poured = state["poured"]
        if poured <= 0:
            # Mirrors the firmware's min_pour_ticks: a pour ended before any
            # volume registered is a drip, not a record.
            self.log(f"[yellow]pour on meter {meter} discarded (no volume)[/]")
            return
        ticks = int(poured / self.ml_per_tick)
        self.totals[meter] = self.totals.get(meter, 0) + ticks
        data = {
            "meter_number": meter,
            "pour_id": state["pour_id"],
            "volume_ml": round(poured, 3),
            "duration_ms": state["duration_ms"],
            "ticks": ticks,
            "ml_per_tick": self.ml_per_tick,
            "tick_series": " ".join(f"{t}:{n}" for t, n in state["series"]),
        }
        grant = state["grant"]
        if grant is not None:
            if grant["auth_device"]:
                data["auth_device"] = grant["auth_device"]
            if grant["token"]:
                data["auth_token"] = grant["token"]
            data["grant_id"] = grant["grant_id"]
        self.enqueue(self.make_event("pour", data))
        self.log(f"[bold]pour complete on meter {meter}: {ticks} ticks[/]")

    async def pour(
        self, meter: int = 0, volume_ml: float = 355.0, duration_s: float = 4.0
    ):
        if meter in self.pours:
            self.log(f"[yellow]meter {meter} is already pouring[/]")
            return
        who = self.grants[meter]["grant_id"] if meter in self.grants else "guest"
        self.log(f"[bold]pouring {volume_ml:.0f} mL on meter {meter} as {who}...[/]")

        def begin_segment(step: int) -> dict:
            state = {
                "pour_id": str(uuid.uuid4()),
                "grant": self.grants.get(meter),
                "poured": 0.0,
                "duration_ms": 0,
                "series": [],
                "_start_step": step,
            }
            self.pours[meter] = state
            return state

        state = begin_segment(0)
        steps = max(1, int(duration_s / POUR_UPDATE_INTERVAL_S))
        total = 0.0  # glass volume so far, across segments
        seg_base = 0.0  # glass volume when the current segment began
        for step in range(steps):
            await asyncio.sleep(POUR_UPDATE_INTERVAL_S)
            if self.pours.get(meter) is not state:
                # A grant boundary ended the pour mid-glass:
                # the beer keeps flowing, so a fresh pour_id opens under
                # whatever covers the meter now — or as a guest pour.
                state = begin_segment(step)
                seg_base = total
                tag = state["grant"]["grant_id"] if state["grant"] else "guest"
                self.log(f"[bold]pour on meter {meter} continues as {tag}[/]")
            prev_total = total
            total = min(
                max(total, volume_ml * (step + 1) / steps * random.uniform(0.97, 1.03)),
                volume_ml,
            )
            state["poured"] = total - seg_base
            state["duration_ms"] = int(
                (step + 1 - state["_start_step"]) * POUR_UPDATE_INTERVAL_S * 1000
            )
            tick_delta = int(total / self.ml_per_tick) - int(prev_total / self.ml_per_tick)
            state["series"].append(((step - state["_start_step"]) * 1000, tick_delta))
            # POLICY — a grant arriving mid-pour adopts it, and attribution
            # is read at pour end (see authenticated-pouring). Limit
            # accounting stays delta-based, so pre-grant volume never counts
            # toward max_volume_ml.
            grant = self.grants.get(meter)
            if grant is not None:
                state["grant"] = grant
                grant["last_flow"] = time.monotonic()
                grant["poured_ml"] += total - prev_total
            if (
                grant is not None
                and grant["max_volume_ml"]
                and grant["poured_ml"] >= grant["max_volume_ml"]
            ):
                # The valve closes the moment the limit trips: the pour ends
                # now (inside _end_grants, before the grant_end is queued).
                # If beer is still coming, the next step opens a guest pour.
                self._end_grants(
                    [m for m, g in self.grants.items() if g is grant], "max_volume"
                )
                continue
            if self.healthy and not self.offline and not self.denied:
                update = self.make_event(
                    "pour_update",
                    {
                        "meter_number": meter,
                        "pour_id": state["pour_id"],
                        "volume_ml": round(state["poured"], 3),
                        "duration_ms": state["duration_ms"],
                    },
                )
                await self.flush(ephemeral=[update])

        self._finish_pour(meter)
        await self.flush()

    def make_status(self, boot: bool) -> dict:
        return self.make_event(
            "status",
            {
                "state": "boot" if boot else "heartbeat",
                "fw_version": FW_VERSION,
                "uptime_ms": self.uptime_ms(),
                "wifi_rssi_dbm": random.randint(-70, -50),
                "events_dropped": self.dropped,
                "config": {
                    "heartbeat_ms": self.heartbeat_s * 1000,
                    "pour_update_ms": int(POUR_UPDATE_INTERVAL_S * 1000),
                    "queue_capacity": QUEUE_CAPACITY,
                },
                "meters": [
                    {"meter_number": m, "total_ticks": t, "ml_per_tick": self.ml_per_tick}
                    for m, t in sorted(self.totals.items())
                ],
                "relays": [{"relay_number": m} for m in sorted(self.totals)],
            },
        )

    async def present_token(self, auth_device: str, token: str, label: str) -> None:
        self.log(f"[bold]presenting {label} ({auth_device}/{token})[/]")
        if auth_device == "onewire":
            self.presented_presence = (auth_device, token)
        # An attached token event is always a question for the server.
        self.enqueue(
            self.make_event(
                "token",
                {"auth_device": auth_device, "token": token, "action": "attached"},
            )
        )
        await self.flush()

    async def detach_token(self) -> None:
        if self.presented_presence is None:
            self.log(
                "[yellow]no presence token to detach (present the iButton first)[/]"
            )
            return
        auth_device, token = self.presented_presence
        self.presented_presence = None
        self._end_grants(
            [
                m
                for m, g in self.grants.items()
                if g.get("token") == token
                and (not g.get("auth_device") or g.get("auth_device") == auth_device)
            ],
            "detach",
        )
        self.log(f"[bold]detaching {auth_device}/{token}[/]")
        self.enqueue(
            self.make_event(
                "token",
                {"auth_device": auth_device, "token": token, "action": "detached"},
            )
        )
        await self.flush()
        self.on_state_change()

    async def send_unknown_event(self) -> None:
        self.log("[cyan]sending unknown event type (server must 2xx and ignore)[/]")
        self.enqueue(self.make_event("future_widget", {"answer": 42}))
        await self.flush()

    def reboot(self) -> None:
        self.boot_id = f"{random.getrandbits(32):08x}"
        self.next_id = 1
        self.started_mono = time.monotonic()
        self.queue.clear()
        self.grants.clear()
        self.pours.clear()
        # RAM state: a real device forgets applied command ids at reboot and
        # relies on re-sent commands being idempotent.
        self.seen_command_ids.clear()
        self.dropped = 0
        self.totals = {0: 0, 1: 0}
        self.denied = False
        self.presented_presence = None
        self.log(f"[bold]rebooted: boot_id={self.boot_id}, ids reset (dedup test)[/]")
        self.enqueue(self.make_status(boot=True))
        self.on_state_change()

    def forget_pairing(self) -> None:
        self.token = None
        self._save_token()
        self.log("[yellow]token forgotten; next send is unauthenticated[/]")
        self.on_state_change()


class TokenPicker(ModalScreen[tuple | None]):
    BINDINGS = [Binding("escape", "dismiss(None)", "Cancel")]

    def compose(self) -> ComposeResult:
        options = [
            Option(f"{label}  [{dev}/{tok}]", id=str(i))
            for i, (dev, tok, label) in enumerate(TOKEN_PRESETS)
        ]
        yield OptionList(*options)

    def on_option_list_option_selected(self, event: OptionList.OptionSelected) -> None:
        self.dismiss(TOKEN_PRESETS[int(event.option.id)])


COMMANDS = [
    # (key, action, footer label, palette help)
    ("p", "pour", "Pour", "~355 mL over ~4 s on meter 0, live updates"),
    ("P", "pour_pint", "Pour pint", "~568 mL over ~8 s on meter 1"),
    ("t", "toggle_temp", "Temp log", "toggle fridge temp reports (10 s)"),
    ("k", "token", "Token", "present a preset fob or iButton"),
    ("d", "detach", "Detach", "remove the iButton; revokes its grants"),
    ("h", "toggle_heartbeat", "Heartbeat", "toggle; off = server should notice"),
    ("o", "toggle_offline", "Offline", "queue events, deliver aged on return"),
    ("r", "replay", "Replay last", "resend verbatim; tests server dedup"),
    ("x", "unknown", "Unknown evt", "unknown type; server must 2xx + ignore"),
    ("b", "reboot", "Reboot", "new boot_id, event ids reset"),
    ("F", "forget", "Forget pairing", "drop token; next send unauthenticated"),
    ("question_mark", "palette", "Help", "show or hide this panel"),
    ("q", "quit", "Quit", "exit the simulator"),
]


class KegboardSimApp(App):
    TITLE = "kegboard-sim"
    CSS = """
    #status { dock: top; height: 3; padding: 0 1; background: $boost; }
    #palette { dock: right; width: 44; padding: 1; background: $boost; }
    TokenPicker { align: center middle; }
    TokenPicker OptionList { width: 60; height: auto; }
    """
    BINDINGS = [Binding(key, action, label) for key, action, label, _ in COMMANDS] + [
        Binding("ctrl+c", "quit", "Quit", show=False, priority=True),
    ]

    def __init__(self, sim: Simulator):
        super().__init__()
        self.sim = sim
        self.heartbeat_on = True
        self.temp_on = False
        self.temp_c = 4.0

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(id="status")
        yield Static(self._palette_text(), id="palette")
        yield RichLog(id="log", markup=True, wrap=True)
        yield Footer()

    @staticmethod
    def _palette_text() -> str:
        lines = ["[bold]Commands[/]  [dim](? hides)[/]", ""]
        for key, _, label, help_text in COMMANDS:
            shown = "?" if key == "question_mark" else key
            lines.append(f" [bold cyan]{shown}[/]  [bold]{label}[/]")
            lines.append(f"    [dim]{help_text}[/]")
        return "\n".join(lines)

    def action_palette(self) -> None:
        palette = self.query_one("#palette", Static)
        palette.display = not palette.display

    def on_mount(self) -> None:
        log = self.query_one("#log", RichLog)
        self.sim.log = log.write
        self.sim.on_state_change = self.refresh_status
        self.refresh_status()
        log.write(f"[bold]kegboard-sim[/] → {self.sim.url}")
        log.write(
            f"device={self.sim.device} boot_id={self.sim.boot_id} "
            f"token={'provisioned' if self.sim.token else 'none'}"
        )
        if self.sim.schema_validator is None:
            log.write("[yellow]schemas/ not found; outgoing batches unvalidated[/]")
        self.sim.enqueue(self.sim.make_status(boot=True))
        self.run_worker(self.background_loop(), exclusive=False)

    def refresh_status(self) -> None:
        s = self.sim
        grants = (
            ", ".join(
                f"m{m}:{g.get('grant_id') or 'guest'}" for m, g in sorted(s.grants.items())
            )
            or "none"
        )
        flags = []
        flags.append("[green]paired[/]" if s.token else "[yellow]unpaired[/]")
        if s.denied:
            flags.append("[red]DENIED[/]")
        if s.offline:
            flags.append("[red]OFFLINE[/]")
        if not s.healthy:
            flags.append("[red]unhealthy[/]")
        flags.append("hb:" + ("[green]on[/]" if self.heartbeat_on else "[red]OFF[/]"))
        flags.append("temp:" + ("[green]on[/]" if self.temp_on else "off"))
        self.query_one("#status", Static).update(
            f"[bold]{s.device}[/]  {' '.join(flags)}\n"
            f"queue: {len(s.queue)}/{QUEUE_CAPACITY}  dropped: {s.dropped}  grants: {grants}"
        )

    async def background_loop(self) -> None:
        last_heartbeat = time.monotonic()
        last_temp = 0.0
        last_retry = 0.0
        last_pairing_poll = 0.0
        while True:
            await asyncio.sleep(0.25)
            now = time.monotonic()

            if self.heartbeat_on and now - last_heartbeat >= self.sim.heartbeat_s:
                last_heartbeat = now
                self.sim.enqueue(self.sim.make_status(boot=False))
                await self.sim.flush()

            if self.temp_on and now - last_temp >= self.sim.temp_interval_s:
                last_temp = now
                self.temp_c += random.uniform(-0.2, 0.2)
                self.temp_c = max(1.0, min(8.0, self.temp_c))
                self.sim.enqueue(
                    self.sim.make_event(
                        "temperature",
                        {"sensor": "thermo-sim0", "temp_c": round(self.temp_c, 2)},
                    )
                )
                await self.sim.flush()

            # Deliver queued events. Healthy and paired sends promptly — the
            # firmware flushes whenever the queue is non-empty and due — so
            # command acks and grant endings never wait out the heartbeat.
            # Unauthenticated pending pairs poll at the pairing cadence; an
            # unhealthy connection backs off at the retry interval.
            if self.sim.queue and not self.sim.offline and not self.sim.denied:
                if not self.sim.token and now - last_pairing_poll >= PAIRING_POLL_S:
                    last_pairing_poll = now
                    await self.sim.flush()
                elif not self.sim.healthy and now - last_retry >= RETRY_S:
                    last_retry = now
                    await self.sim.flush()
                elif self.sim.token and self.sim.healthy:
                    await self.sim.flush()

    def action_pour(self) -> None:
        # No two pours are identical; neither are these.
        self.run_worker(
            self.sim.pour(
                meter=0,
                volume_ml=random.uniform(330.0, 385.0),
                duration_s=random.uniform(3.5, 5.0),
            )
        )

    def action_pour_pint(self) -> None:
        self.run_worker(
            self.sim.pour(
                meter=1,
                volume_ml=random.uniform(545.0, 590.0),
                duration_s=random.uniform(7.0, 9.5),
            )
        )

    def action_toggle_temp(self) -> None:
        self.temp_on = not self.temp_on
        self.sim.log(f"temperature logging {'on' if self.temp_on else 'off'}")
        self.refresh_status()

    def action_token(self) -> None:
        def picked(choice: tuple | None) -> None:
            if choice is not None:
                dev, tok, label = choice
                self.run_worker(self.sim.present_token(dev, tok, label))

        self.push_screen(TokenPicker(), picked)

    def action_detach(self) -> None:
        self.run_worker(self.sim.detach_token())

    def action_toggle_heartbeat(self) -> None:
        self.heartbeat_on = not self.heartbeat_on
        self.sim.log(
            "heartbeat [red]disabled — server should notice the silence[/]"
            if not self.heartbeat_on
            else "heartbeat enabled"
        )
        self.refresh_status()

    def action_toggle_offline(self) -> None:
        self.sim.offline = not self.sim.offline
        if self.sim.offline:
            self.sim.log("[red]offline: events queue with growing age_ms[/]")
        else:
            self.sim.log("[green]online: delivering backlog[/]")
            self.run_worker(self.sim.flush())
        self.refresh_status()

    def action_replay(self) -> None:
        self.run_worker(self.sim.replay_last())

    def action_unknown(self) -> None:
        self.run_worker(self.sim.send_unknown_event())

    def action_reboot(self) -> None:
        self.sim.reboot()
        self.run_worker(self.sim.flush())

    def action_forget(self) -> None:
        self.sim.forget_pairing()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "url", help="Full reporting URL, e.g. http://localhost:8000/kegboard-event"
    )
    parser.add_argument("--device", default="kegboard-sim001", help="Device identity")
    parser.add_argument("--heartbeat", type=int, default=60, help="Heartbeat seconds")
    parser.add_argument(
        "--temp-interval", type=int, default=10, help="Temperature seconds"
    )
    args = parser.parse_args()

    sim = Simulator(args.url, args.device, args.heartbeat, args.temp_interval)
    KegboardSimApp(sim).run()


if __name__ == "__main__":
    main()
