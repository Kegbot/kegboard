"""Kegboard Event Protocol reporter.

Speaks docs/kegboard-event-protocol.md: batched events over a single HTTP
endpoint, pairing, and server command dispatch.
"""

import esphome.codegen as cg
from esphome.components import sensor, switch, time
from esphome.components.http_request import (
    CONF_HTTP_REQUEST_ID,
    HttpRequestComponent,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_SENSOR,
    CONF_TIME_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
)

from ..kegboard import CONF_KEGBOARD_ID, KegboardHub
from ..kegboard_meter import KegboardMeter

CODEOWNERS = ["@mikey"]
DEPENDENCIES = ["kegboard", "http_request", "time", "json"]
AUTO_LOAD = ["sensor"]

CONF_REPORTING_URL = "reporting_url"
CONF_DROPPED = "dropped"
CONF_HEARTBEAT_INTERVAL = "heartbeat_interval"
CONF_METERS = "meters"
CONF_POUR_UPDATE_INTERVAL = "pour_update_interval"
CONF_QUEUE_DEPTH = "queue_depth"
CONF_RELAY = "relay"
CONF_RELAY_NUMBER = "relay_number"
CONF_RELAYS = "relays"
CONF_RETRY_INTERVAL = "retry_interval"
CONF_THERMO_SENSORS = "thermo_sensors"

kegboard_reporter_ns = cg.esphome_ns.namespace("kegboard_reporter")
KegboardReporter = kegboard_reporter_ns.class_("KegboardReporter", cg.Component)


def validate_reporting_url(value):
    value = cv.url(value)
    if not value.startswith(("http://", "https://")):
        raise cv.Invalid("Reporting URL must start with http:// or https://")
    return value


THERMO_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_NAME): cv.string_strict,
    }
)

RELAY_SCHEMA = cv.Schema(
    {
        # The protocol's relay number: reported in the status inventory and
        # the target of grant relay sets (and, later, set_relay).
        cv.Required(CONF_RELAY_NUMBER): cv.int_range(min=0, max=255),
        cv.Required(CONF_RELAY): cv.use_id(switch.Switch),
    }
)


def _validate_unique_relay_numbers(relays):
    seen = set()
    for relay in relays:
        number = relay[CONF_RELAY_NUMBER]
        if number in seen:
            raise cv.Invalid(f"Duplicate relay_number {number}")
        seen.add(number)
    return relays

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(KegboardReporter),
        cv.GenerateID(CONF_KEGBOARD_ID): cv.use_id(KegboardHub),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(HttpRequestComponent),
        cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        # Full URL, path included, e.g.
        # https://kegbot.example.com/api/kegboard-event
        # No credential is configured: the device provisions its own bearer
        # token by pairing via the server dashboard.
        cv.Required(CONF_REPORTING_URL): validate_reporting_url,
        cv.Optional(CONF_METERS, default=[]): cv.ensure_list(cv.use_id(KegboardMeter)),
        cv.Optional(CONF_RELAYS, default=[]): cv.All(
            cv.ensure_list(RELAY_SCHEMA), _validate_unique_relay_numbers
        ),
        cv.Optional(CONF_THERMO_SENSORS, default=[]): cv.ensure_list(
            THERMO_SENSOR_SCHEMA
        ),
        # The status schema requires heartbeat_ms >= 1000.
        cv.Optional(CONF_HEARTBEAT_INTERVAL, default="60s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=1)),
        ),
        # 0s disables pour_update events.
        cv.Optional(
            CONF_POUR_UPDATE_INTERVAL, default="1s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_RETRY_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_QUEUE_DEPTH): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_DROPPED): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_hub(await cg.get_variable(config[CONF_KEGBOARD_ID])))
    cg.add(var.set_http_request(await cg.get_variable(config[CONF_HTTP_REQUEST_ID])))
    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))

    cg.add(var.set_reporting_url(config[CONF_REPORTING_URL]))
    cg.add(var.set_heartbeat_interval_ms(config[CONF_HEARTBEAT_INTERVAL]))
    cg.add(var.set_pour_update_interval_ms(config[CONF_POUR_UPDATE_INTERVAL]))
    cg.add(var.set_retry_interval_ms(config[CONF_RETRY_INTERVAL]))

    for meter_id in config[CONF_METERS]:
        cg.add(var.add_meter(await cg.get_variable(meter_id)))

    for conf in config[CONF_RELAYS]:
        relay = await cg.get_variable(conf[CONF_RELAY])
        cg.add(var.add_relay(conf[CONF_RELAY_NUMBER], relay))

    for conf in config[CONF_THERMO_SENSORS]:
        thermo = await cg.get_variable(conf[CONF_SENSOR])
        cg.add(var.add_thermo_sensor(thermo, conf[CONF_NAME]))

    if CONF_QUEUE_DEPTH in config:
        cg.add(
            var.set_queue_depth_sensor(
                await sensor.new_sensor(config[CONF_QUEUE_DEPTH])
            )
        )
    if CONF_DROPPED in config:
        cg.add(var.set_dropped_sensor(await sensor.new_sensor(config[CONF_DROPPED])))
