import esphome.codegen as cg
from esphome.components import climate_ir

DEPENDENCIES = ["number", "button"]

rapid_ac_ns = cg.esphome_ns.namespace("rapid_ac")
RapidAcClimate = rapid_ac_ns.class_("RapidAcClimate", climate_ir.ClimateIR)
