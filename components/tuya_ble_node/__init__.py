import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import tuya_ble_tracker, tuya_ble_client
from esphome.const import CONF_ID, CONF_MAC_ADDRESS

AUTO_LOAD = ["md5"]
DEPENDENCIES = ["tuya_ble_client", "esp32"]

tuya_ble_node_ns = cg.esphome_ns.namespace("tuya_ble_node")

TuyaBLENode = tuya_ble_node_ns.class_("TuyaBLENode", cg.PollingComponent)

CONF_DEVICE_ID = 'device_id'
CONF_LOCAL_KEY = 'local_key'
CONF_MAX_QUEUED = 'max_queued'
CONF_UUID = 'uuid'

MULTI_CONF = True


def validate_local_key(value):
    value = cv.string_strict(value)
    if len(value) != 16:
        raise cv.Invalid(
            f"local_key must be exactly 16 characters (got {len(value)}). "
            "A wrong-length value here will pair/decrypt incorrectly at "
            "runtime instead of failing loudly like this - see "
            "docs/getting-the-local-key.md for how to get a real one."
        )
    try:
        value.encode("ascii")
    except UnicodeEncodeError:
        raise cv.Invalid("local_key must contain only ASCII characters") from None
    return value


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TuyaBLENode),
            cv.Required(CONF_MAC_ADDRESS): cv.mac_address,
            cv.Required(CONF_LOCAL_KEY): validate_local_key,
            cv.Optional(CONF_DEVICE_ID): cv.string,
            cv.Optional(CONF_UUID): cv.string,
            cv.Optional(CONF_MAX_QUEUED, default=1): cv.int_range(1, 10),
        }
    )
    # Gives us `update_interval:` (default 60s) for free, handled by
    # cg.register_component() below. A node with only write-capable
    # `output` DPs and no sensors doesn't need this, but update() is a
    # no-op in that case (see tuya_ble_node.cpp) so it's harmless to always
    # have it configurable.
    .extend(cv.polling_component_schema("60s"))
    .extend(tuya_ble_client.TUYA_BLE_CLIENT_SCHEMA)
)

CONF_TUYA_BLE_NODE_ID = "tuya_ble_node_id"

TUYA_BLE_NODE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TUYA_BLE_NODE_ID): cv.use_id(TuyaBLENode),
    }
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_local_key(config[CONF_LOCAL_KEY]))
    if CONF_DEVICE_ID in config:
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    if CONF_UUID in config:
        cg.add(var.set_uuid(config[CONF_UUID]))
    cg.add(var.set_max_queued(config[CONF_MAX_QUEUED]))

    await tuya_ble_client.register_tuya_node(var, config)

    parent = await cg.get_variable(config[tuya_ble_client.CONF_TUYA_BLE_CLIENT_ID])
    cg.add(var.register_client(parent))
