import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate_ir, sensor
from esphome.const import CONF_ID

from . import RapidAcClimate

AUTO_LOAD = ["climate_ir"]

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(RapidAcClimate).extend(
    {
        cv.Optional("power_sensor_id"): cv.use_id(sensor.Sensor),
    }
)


async def to_code(config):
    await climate_ir.new_climate_ir(config)
    if "power_sensor_id" in config:
        var = await cg.get_variable(config[CONF_ID])
        sens = await cg.get_variable(config["power_sensor_id"])
        cg.add(var.set_power_sensor(sens))
