"""Kegbot Server reporter.

Posts completed pours and temperature readings to a Kegbot Server, buffering
them across outages. This is what replaces kegbot-pycore.
"""

import esphome.codegen as cg
from esphome.components import sensor, time
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
DEPENDENCIES = ["kegboard", "http_request", "time"]
AUTO_LOAD = ["sensor", "json"]

CONF_API_KEY = "api_key"
CONF_BASE_URL = "base_url"
CONF_DROPPED = "dropped"
CONF_METERS = "meters"
CONF_QUEUE_DEPTH = "queue_depth"
CONF_RETRY_INTERVAL = "retry_interval"
CONF_SEND_VOLUME = "send_volume"
CONF_THERMO_SENSORS = "thermo_sensors"

kegboard_kegbot_ns = cg.esphome_ns.namespace("kegboard_kegbot")
KegbotReporter = kegboard_kegbot_ns.class_("KegbotReporter", cg.Component)


def validate_base_url(value):
    value = cv.url(value)
    if not value.startswith(("http://", "https://")):
        raise cv.Invalid("Server URL must start with http:// or https://")
    return value


THERMO_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        # The name Kegbot Server files the reading under. The legacy firmware
        # used "thermo-<1-wire address>"; keeping that convention lets an
        # existing server see continuity when a board is swapped.
        cv.Required(CONF_NAME): cv.string_strict,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(KegbotReporter),
        cv.GenerateID(CONF_KEGBOARD_ID): cv.use_id(KegboardHub),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(HttpRequestComponent),
        cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_BASE_URL): validate_base_url,
        cv.Required(CONF_API_KEY): cv.string_strict,
        cv.Optional(CONF_METERS, default=[]): cv.ensure_list(cv.use_id(KegboardMeter)),
        cv.Optional(CONF_THERMO_SENSORS, default=[]): cv.ensure_list(
            THERMO_SENSOR_SCHEMA
        ),
        # Off by default: Kegbot Server stores ml_per_tick per meter and has a
        # calibration UI. Enabling this makes the device authoritative instead.
        cv.Optional(CONF_SEND_VOLUME, default=False): cv.boolean,
        cv.Optional(
            CONF_RETRY_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_QUEUE_DEPTH): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Worth surfacing: a non-zero value means pours were lost.
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

    cg.add(var.set_base_url(config[CONF_BASE_URL]))
    cg.add(var.set_api_key(config[CONF_API_KEY]))
    cg.add(var.set_send_volume(config[CONF_SEND_VOLUME]))
    cg.add(var.set_retry_interval_ms(config[CONF_RETRY_INTERVAL]))

    for meter_id in config[CONF_METERS]:
        cg.add(var.add_meter(await cg.get_variable(meter_id)))

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
