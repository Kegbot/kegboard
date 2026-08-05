"""Authorization for taps, per docs/authenticated-pouring.md.

Turns token events from any reader into server-decided grants: each grant
arrives naming the meters it covers, the relays it opens, and its limits.
The component drives relays and tags pours for server-side attribution; the
device never learns user identity.
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_DEVICE, CONF_ID, CONF_TRIGGER_ID
import esphome.final_validate as fv

from ..kegboard_reporter import KegboardReporter

CODEOWNERS = ["@mikey"]
DEPENDENCIES = ["kegboard"]
AUTO_LOAD = ["binary_sensor"]

CONF_AUTHORIZED = "authorized"
CONF_MAX_GRANT_DURATION = "max_grant_duration"
CONF_OFFLINE_POLICY = "offline_policy"
CONF_ON_AUTHORIZED = "on_authorized"
CONF_ON_DENIED = "on_denied"
CONF_ON_REVOKED = "on_revoked"
CONF_REPORTER_ID = "reporter_id"
CONF_TOKEN = "token"

kegboard_auth_ns = cg.esphome_ns.namespace("kegboard_auth")
KegboardAuth = kegboard_auth_ns.class_("KegboardAuth", cg.Component)

OfflinePolicy = kegboard_auth_ns.enum("OfflinePolicy", is_class=True)
OFFLINE_POLICIES = {"deny": OfflinePolicy.DENY, "guest": OfflinePolicy.GUEST}

AuthorizedTrigger = kegboard_auth_ns.class_(
    "AuthorizedTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
DeniedTrigger = kegboard_auth_ns.class_(
    "DeniedTrigger", automation.Trigger.template(cg.std_string)
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


def _final_validate(config):
    """Bind to the config's kegboard_reporter automatically.

    Like every other *_id reference, users should never have to write
    reporter_id when there is only one reporter -- which is every real
    config. It stays available for the exotic multiple-reporter case.
    """
    if CONF_REPORTER_ID in config:
        return config

    reporter = fv.full_config.get().get("kegboard_reporter")
    if not reporter:
        raise cv.Invalid(
            "kegboard_auth needs a kegboard_reporter configured: the server "
            "decides every presentment. (Boards without a server can gate "
            "valves with plain ESPHome automations on the reader triggers.)"
        )
    # Not MULTI_CONF today, so this is a single config dict; keep the list
    # branch in case that ever changes.
    if isinstance(reporter, list):
        if len(reporter) > 1:
            raise cv.Invalid(
                "Multiple kegboard_reporter instances; set reporter_id on "
                "kegboard_auth to choose one."
            )
        reporter = reporter[0]
    config[CONF_REPORTER_ID] = reporter[CONF_ID]
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KegboardAuth),
            cv.Optional(CONF_REPORTER_ID): cv.use_id(KegboardReporter),
            cv.Optional(CONF_OFFLINE_POLICY, default="deny"): cv.one_of(
                *OFFLINE_POLICIES, lower=True
            ),
            # Device-side safety backstop on server-issued grants. Bounded
            # so the rollover-safe expiry math (32-bit signed millisecond
            # differences) stays valid.
            cv.Optional(CONF_MAX_GRANT_DURATION, default="5min"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(hours=24)),
            ),
            cv.Optional(CONF_AUTHORIZED): binary_sensor.binary_sensor_schema(),
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
    ).extend(cv.COMPONENT_SCHEMA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_REPORTER_ID in config:
        cg.add(var.set_reporter(await cg.get_variable(config[CONF_REPORTER_ID])))

    cg.add(var.set_offline_policy(OFFLINE_POLICIES[config[CONF_OFFLINE_POLICY]]))
    cg.add(var.set_max_grant_duration_ms(config[CONF_MAX_GRANT_DURATION]))

    if CONF_AUTHORIZED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_AUTHORIZED])
        cg.add(var.set_authorized_binary_sensor(sens))

    for conf in config.get(CONF_ON_AUTHORIZED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_authorized_trigger(trigger))
        await automation.build_automation(
            trigger,
            [(cg.std_string, "auth_device"), (cg.std_string, "token")],
            conf,
        )

    for conf in config.get(CONF_ON_DENIED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_denied_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "reason")], conf)

    for conf in config.get(CONF_ON_REVOKED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_revoked_trigger(trigger))
        await automation.build_automation(trigger, [], conf)


TOKEN_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(KegboardAuth),
        cv.Required(CONF_DEVICE): cv.templatable(cv.string_strict),
        cv.Required(CONF_TOKEN): cv.templatable(cv.string_strict),
    }
)


@automation.register_action(
    "kegboard_auth.token_attached",
    TokenAttachedAction,
    TOKEN_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "kegboard_auth.token_detached",
    TokenDetachedAction,
    TOKEN_ACTION_SCHEMA,
    synchronous=True,
)
async def token_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(
        var.set_device(await cg.templatable(config[CONF_DEVICE], args, cg.std_string))
    )
    cg.add(var.set_token(await cg.templatable(config[CONF_TOKEN], args, cg.std_string)))
    return var


@automation.register_action(
    "kegboard_auth.revoke",
    RevokeAction,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(KegboardAuth)}),
    synchronous=True,
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
