"""Kegboard hub component.

Holds the board identity that meters and reporters share. Every other
kegboard_* component depends on this one.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@mikey"]

CONF_KEGBOARD_ID = "kegboard_id"
CONF_SERIAL_NUMBER = "serial_number"

kegboard_ns = cg.esphome_ns.namespace("kegboard")
KegboardHub = kegboard_ns.class_("KegboardHub", cg.Component)


def validate_serial_number(value):
    value = cv.string_strict(value)
    if not value:
        raise cv.Invalid("Serial number must not be empty")
    # The serial number becomes part of a meter name, which in turn becomes a
    # URL path segment on Kegbot Server. Keeping it to safe characters avoids
    # surprising encoding behaviour later.
    if any(c.isspace() for c in value):
        raise cv.Invalid("Serial number must not contain whitespace")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(KegboardHub),
        cv.Optional(CONF_SERIAL_NUMBER): validate_serial_number,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_SERIAL_NUMBER in config:
        cg.add(var.set_serial_number(config[CONF_SERIAL_NUMBER]))
