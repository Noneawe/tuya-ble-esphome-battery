import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.components import tuya_ble_node
from esphome.const import CONF_ID

from .. import tuya_ble_node_ns

DEPENDENCIES = ["tuya_ble_node"]

CONF_DP_ID = "dp_id"

TuyaBLESensor = tuya_ble_node_ns.class_(
    "TuyaBLESensor", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(TuyaBLESensor)
    .extend(
        {
            cv.Required(CONF_DP_ID): cv.int_range(0, 255),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(tuya_ble_node.TUYA_BLE_NODE_SCHEMA)
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    cg.add(var.set_dp_id(config[CONF_DP_ID]))

    parent = await cg.get_variable(config[tuya_ble_node.CONF_TUYA_BLE_NODE_ID])
    cg.add(var.register_node(parent))
