import esphome.config_validation as cv
from esphome.components import climate_ir

from . import RapidAcClimate

AUTO_LOAD = ["climate_ir"]

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(RapidAcClimate)


async def to_code(config):
    await climate_ir.new_climate_ir(config)
