"""iButton presence on a 1-Wire bus.

ESPHome's one_wire bus enumerates devices but has no arrive/leave events,
which is the entire point of an iButton reader. This adds them.
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components.one_wire import CONF_ONE_WIRE_ID, OneWireBus
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CODEOWNERS = ["@mikey"]
DEPENDENCIES = ["one_wire"]
MULTI_CONF = True

CONF_MAX_MISSED_SEARCHES = "max_missed_searches"
CONF_ON_TOKEN_ATTACHED = "on_token_attached"
CONF_ON_TOKEN_DETACHED = "on_token_detached"

kegboard_onewire_ns = cg.esphome_ns.namespace("kegboard_onewire")
KegboardOneWire = kegboard_onewire_ns.class_("KegboardOneWire", cg.PollingComponent)

TokenAttachedTrigger = kegboard_onewire_ns.class_(
    "TokenAttachedTrigger", automation.Trigger.template(cg.std_string)
)
TokenDetachedTrigger = kegboard_onewire_ns.class_(
    "TokenDetachedTrigger", automation.Trigger.template(cg.std_string)
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(KegboardOneWire),
        cv.GenerateID(CONF_ONE_WIRE_ID): cv.use_id(OneWireBus),
        # A held iButton makes intermittent contact, so a single missed search
        # is normal. Four matches the AVR firmware.
        cv.Optional(CONF_MAX_MISSED_SEARCHES, default=4): cv.uint8_t,
        cv.Optional(CONF_ON_TOKEN_ATTACHED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TokenAttachedTrigger)}
        ),
        cv.Optional(CONF_ON_TOKEN_DETACHED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TokenDetachedTrigger)}
        ),
    }
).extend(cv.polling_component_schema("1s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_bus(await cg.get_variable(config[CONF_ONE_WIRE_ID])))
    cg.add(var.set_max_missed_searches(config[CONF_MAX_MISSED_SEARCHES]))

    for conf in config.get(CONF_ON_TOKEN_ATTACHED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_attached_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)

    for conf in config.get(CONF_ON_TOKEN_DETACHED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_detached_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)
