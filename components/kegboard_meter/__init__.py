"""Flow meter component.

Counts pulses in an ISR and hands them to the framework-agnostic pour state
machine in `components/kegboard`. One entry per physical meter.
"""

from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import binary_sensor, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INDEX,
    CONF_PIN,
    CONF_TRIGGER_ID,
    CONF_VALUE,
    ICON_PULSE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
)

from ..kegboard import CONF_KEGBOARD_ID, KegboardHub

CODEOWNERS = ["@mikey"]
DEPENDENCIES = ["kegboard"]
AUTO_LOAD = ["sensor", "binary_sensor"]
MULTI_CONF = True

CONF_DEBOUNCE = "debounce"
CONF_FLOW_RATE = "flow_rate"
CONF_IDLE_TIMEOUT = "idle_timeout"
CONF_MAX_POUR_DURATION = "max_pour_duration"
CONF_METER_NAME = "meter_name"
CONF_MIN_POUR_TICKS = "min_pour_ticks"
CONF_ML_PER_TICK = "ml_per_tick"
CONF_ON_POUR_END = "on_pour_end"
CONF_ON_POUR_START = "on_pour_start"
CONF_POURING = "pouring"
CONF_REPORT_INTERVAL = "report_interval"
CONF_SERIES_RESOLUTION = "series_resolution"
CONF_TOTAL = "total"
CONF_VOLUME = "volume"

UNIT_MILLILITER = "mL"
UNIT_MILLILITER_PER_MINUTE = "mL/min"
UNIT_TICKS = "ticks"

kegboard_meter_ns = cg.esphome_ns.namespace("kegboard_meter")
KegboardMeter = kegboard_meter_ns.class_("KegboardMeter", cg.Component)

PourStartTrigger = kegboard_meter_ns.class_(
    "PourStartTrigger", automation.Trigger.template()
)
PourEndTrigger = kegboard_meter_ns.class_(
    "PourEndTrigger", automation.Trigger.template(cg.uint32, cg.float_, cg.uint32)
)

ResetTotalAction = kegboard_meter_ns.class_("ResetTotalAction", automation.Action)
EndPourAction = kegboard_meter_ns.class_("EndPourAction", automation.Action)
SetCalibrationAction = kegboard_meter_ns.class_(
    "SetCalibrationAction", automation.Action
)

# The SwissFlow SF800 and its clones -- by far the most common Kegbot meter --
# produce about 5.4 ticks per mL.
DEFAULT_ML_PER_TICK = 0.185


def validate_meter_name(value):
    value = cv.string_strict(value)
    if any(c.isspace() for c in value):
        raise cv.Invalid("Meter name must not contain whitespace")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(KegboardMeter),
        cv.GenerateID(CONF_KEGBOARD_ID): cv.use_id(KegboardHub),
        cv.Required(CONF_PIN): pins.internal_gpio_input_pin_schema,
        # Defaults to "<board serial>.flow<index>", which is what Kegbot Server
        # expects a controller to call its meters.
        cv.Optional(CONF_METER_NAME): validate_meter_name,
        cv.Optional(CONF_INDEX, default=0): cv.uint8_t,
        cv.Optional(CONF_ML_PER_TICK, default=DEFAULT_ML_PER_TICK): cv.positive_float,
        cv.Optional(
            CONF_DEBOUNCE, default="1200us"
        ): cv.positive_time_period_microseconds,
        cv.Optional(
            CONF_IDLE_TIMEOUT, default="10s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MIN_POUR_TICKS, default=3): cv.uint32_t,
        # Zero disables the cutoff entirely.
        cv.Optional(
            CONF_MAX_POUR_DURATION, default="5min"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_REPORT_INTERVAL, default="250ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_SERIES_RESOLUTION, default="100ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TOTAL): sensor.sensor_schema(
            unit_of_measurement=UNIT_TICKS,
            icon=ICON_PULSE,
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_VOLUME): sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLILITER,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_FLOW_RATE): sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLILITER_PER_MINUTE,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_POURING): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_ON_POUR_START): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PourStartTrigger)}
        ),
        cv.Optional(CONF_ON_POUR_END): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PourEndTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    hub = await cg.get_variable(config[CONF_KEGBOARD_ID])
    cg.add(var.set_hub(hub))

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))

    cg.add(var.set_index(config[CONF_INDEX]))
    if CONF_METER_NAME in config:
        cg.add(var.set_meter_name(config[CONF_METER_NAME]))

    cg.add(var.set_ml_per_tick(config[CONF_ML_PER_TICK]))
    cg.add(var.set_filter_us(config[CONF_DEBOUNCE]))
    cg.add(var.set_idle_timeout_ms(config[CONF_IDLE_TIMEOUT]))
    cg.add(var.set_min_pour_ticks(config[CONF_MIN_POUR_TICKS]))
    cg.add(var.set_max_duration_ms(config[CONF_MAX_POUR_DURATION]))
    cg.add(var.set_report_interval_ms(config[CONF_REPORT_INTERVAL]))
    cg.add(var.set_series_resolution_ms(config[CONF_SERIES_RESOLUTION]))

    if CONF_TOTAL in config:
        cg.add(var.set_total_sensor(await sensor.new_sensor(config[CONF_TOTAL])))
    if CONF_VOLUME in config:
        cg.add(var.set_volume_sensor(await sensor.new_sensor(config[CONF_VOLUME])))
    if CONF_FLOW_RATE in config:
        cg.add(
            var.set_flow_rate_sensor(await sensor.new_sensor(config[CONF_FLOW_RATE]))
        )
    if CONF_POURING in config:
        cg.add(
            var.set_pouring_binary_sensor(
                await binary_sensor.new_binary_sensor(config[CONF_POURING])
            )
        )

    for conf in config.get(CONF_ON_POUR_START, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_pour_start_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_POUR_END, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_pour_end_trigger(trigger))
        await automation.build_automation(
            trigger,
            [
                (cg.uint32, "ticks"),
                (cg.float_, "volume_ml"),
                (cg.uint32, "duration_ms"),
            ],
            conf,
        )


METER_ACTION_SCHEMA = cv.Schema({cv.Required(CONF_ID): cv.use_id(KegboardMeter)})


@automation.register_action(
    "kegboard_meter.reset_total", ResetTotalAction, METER_ACTION_SCHEMA
)
@automation.register_action(
    "kegboard_meter.end_pour", EndPourAction, METER_ACTION_SCHEMA
)
async def meter_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "kegboard_meter.set_calibration",
    SetCalibrationAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(KegboardMeter),
            cv.Required(CONF_VALUE): cv.templatable(cv.positive_float),
        }
    ),
)
async def set_calibration_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_VALUE], args, cg.float_)
    cg.add(var.set_ml_per_tick(template_))
    return var
