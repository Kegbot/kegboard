"""Authorization for taps.

Turns token events from any reader into a grant: opens the flow toggle,
attributes pours to the resolved Kegbot user, and closes up on detach or
expiry.
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, switch, text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_DEVICE, CONF_ID, CONF_TRIGGER_ID

from ..kegboard_kegbot import KegbotReporter
from ..kegboard_meter import KegboardMeter

CODEOWNERS = ["@mikey"]
DEPENDENCIES = ["kegboard"]
AUTO_LOAD = ["binary_sensor", "text_sensor"]

CONF_AUTHORIZED = "authorized"
CONF_GRANT_DURATION = "grant_duration"
CONF_KEGBOT_ID = "kegbot_id"
CONF_METERS = "meters"
CONF_ON_AUTHORIZED = "on_authorized"
CONF_ON_DENIED = "on_denied"
CONF_ON_REVOKED = "on_revoked"
CONF_REQUIRE_KNOWN_TOKEN = "require_known_token"
CONF_TOGGLE = "toggle"
CONF_TOKEN = "token"
CONF_USER = "user"

kegboard_auth_ns = cg.esphome_ns.namespace("kegboard_auth")
KegboardAuth = kegboard_auth_ns.class_("KegboardAuth", cg.Component)

AuthorizedTrigger = kegboard_auth_ns.class_(
    "AuthorizedTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
DeniedTrigger = kegboard_auth_ns.class_(
    "DeniedTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
RevokedTrigger = kegboard_auth_ns.class_(
    "RevokedTrigger", automation.Trigger.template()
)

TokenAttachedAction = kegboard_auth_ns.class_("TokenAttachedAction", automation.Action)
TokenDetachedAction = kegboard_auth_ns.class_("TokenDetachedAction", automation.Action)
RevokeAction = kegboard_auth_ns.class_("RevokeAction", automation.Action)
AuthorizedCondition = kegboard_auth_ns.class_(
    "AuthorizedCondition", automation.Condition
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(KegboardAuth),
        # Optional: without a server every token is accepted and pours are
        # recorded against the guest user.
        cv.Optional(CONF_KEGBOT_ID): cv.use_id(KegbotReporter),
        cv.Optional(CONF_METERS, default=[]): cv.ensure_list(cv.use_id(KegboardMeter)),
        # Typically a relay driving a solenoid valve.
        cv.Optional(CONF_TOGGLE): cv.use_id(switch.Switch),
        cv.Optional(
            CONF_GRANT_DURATION, default="30s"
        ): cv.positive_time_period_milliseconds,
        # Off by default: a party where an unregistered fob pours as guest is
        # friendlier than one where it does nothing. Turn on to lock the tap.
        cv.Optional(CONF_REQUIRE_KNOWN_TOKEN, default=False): cv.boolean,
        cv.Optional(CONF_AUTHORIZED): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_USER): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_ON_AUTHORIZED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(AuthorizedTrigger)}
        ),
        cv.Optional(CONF_ON_DENIED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DeniedTrigger)}
        ),
        cv.Optional(CONF_ON_REVOKED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(RevokedTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_KEGBOT_ID in config:
        cg.add(var.set_kegbot(await cg.get_variable(config[CONF_KEGBOT_ID])))
    if CONF_TOGGLE in config:
        cg.add(var.set_toggle(await cg.get_variable(config[CONF_TOGGLE])))

    cg.add(var.set_grant_duration_ms(config[CONF_GRANT_DURATION]))
    cg.add(var.set_require_known_token(config[CONF_REQUIRE_KNOWN_TOKEN]))

    for meter_id in config[CONF_METERS]:
        cg.add(var.add_meter(await cg.get_variable(meter_id)))

    if CONF_AUTHORIZED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_AUTHORIZED])
        cg.add(var.set_authorized_binary_sensor(sens))
    if CONF_USER in config:
        sens = await text_sensor.new_text_sensor(config[CONF_USER])
        cg.add(var.set_user_text_sensor(sens))

    for conf in config.get(CONF_ON_AUTHORIZED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_authorized_trigger(trigger))
        await automation.build_automation(
            trigger, [(cg.std_string, "device"), (cg.std_string, "token")], conf
        )

    for conf in config.get(CONF_ON_DENIED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_denied_trigger(trigger))
        await automation.build_automation(
            trigger, [(cg.std_string, "device"), (cg.std_string, "token")], conf
        )

    for conf in config.get(CONF_ON_REVOKED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_revoked_trigger(trigger))
        await automation.build_automation(trigger, [], conf)


@automation.register_action(
    "kegboard_auth.token_attached",
    TokenAttachedAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(KegboardAuth),
            # Kegbot files tokens under this name; "core.rfid" and "onewire"
            # are what the legacy firmware reported.
            cv.Required(CONF_DEVICE): cv.templatable(cv.string_strict),
            cv.Required(CONF_TOKEN): cv.templatable(cv.string_strict),
        }
    ),
)
async def token_attached_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(
        var.set_device(await cg.templatable(config[CONF_DEVICE], args, cg.std_string))
    )
    cg.add(var.set_token(await cg.templatable(config[CONF_TOKEN], args, cg.std_string)))
    return var


@automation.register_action(
    "kegboard_auth.token_detached",
    TokenDetachedAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(KegboardAuth),
            cv.Required(CONF_TOKEN): cv.templatable(cv.string_strict),
        }
    ),
)
async def token_detached_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_token(await cg.templatable(config[CONF_TOKEN], args, cg.std_string)))
    return var


@automation.register_action(
    "kegboard_auth.revoke",
    RevokeAction,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(KegboardAuth)}),
)
async def revoke_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_condition(
    "kegboard_auth.is_authorized",
    AuthorizedCondition,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(KegboardAuth)}),
)
async def is_authorized_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
