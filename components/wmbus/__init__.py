from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.core import CORE
from esphome.helpers import copy_file_if_changed
from esphome.const import (
    CONF_ID,
    CONF_MOSI_PIN,
    CONF_MISO_PIN,
    CONF_CLK_PIN,
    CONF_CS_PIN,
    CONF_FREQUENCY,
)

CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_LOG_ALL = "log_all"
CONF_SYNC_MODE = "sync_mode"
CONF_RODATA_IN_FLASH = "rodata_in_flash"

CODEOWNERS = ["@SzczepanLeon"]

AUTO_LOAD = ["sensor"]

wmbus_ns = cg.esphome_ns.namespace("wmbus")
WMBusComponent = wmbus_ns.class_("WMBusComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WMBusComponent),
        cv.Optional(CONF_MOSI_PIN, default=13): pins.internal_gpio_output_pin_schema,
        cv.Optional(CONF_MISO_PIN, default=12): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_CLK_PIN, default=14): pins.internal_gpio_output_pin_schema,
        cv.Optional(CONF_CS_PIN, default=2): pins.internal_gpio_output_pin_schema,
        cv.Optional(CONF_GDO0_PIN, default=5): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_GDO2_PIN, default=4): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_LOG_ALL, default=False): cv.boolean,
        cv.Optional(CONF_FREQUENCY, default=868.950): cv.float_range(min=300, max=928),
        cv.Optional(CONF_SYNC_MODE, default=False): cv.boolean,
        cv.Optional(CONF_RODATA_IN_FLASH, default=True): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    mosi = await cg.gpio_pin_expression(config[CONF_MOSI_PIN])
    miso = await cg.gpio_pin_expression(config[CONF_MISO_PIN])
    clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
    cs = await cg.gpio_pin_expression(config[CONF_CS_PIN])
    gdo0 = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
    gdo2 = await cg.gpio_pin_expression(config[CONF_GDO2_PIN])

    cg.add(
        var.add_cc1101(
            mosi, miso, clk, cs, gdo0, gdo2, config[CONF_FREQUENCY], config[CONF_SYNC_MODE]
        )
    )
    cg.add(var.set_log_all(config[CONF_LOG_ALL]))

    cg.add_library("SPI", None)
    cg.add_library("LSatan/SmartRC-CC1101-Driver-Lib", "2.5.7")

    # Only compile the drivers that are actually referenced by a sensor
    # (sensor platform adds "+<**/wmbus/driver_<type>.cpp>" per meter type).
    cg.add_platformio_option("build_src_filter", ["+<*>", "-<.git/>", "-<.svn/>"])
    cg.add_platformio_option("build_src_filter", ["-<**/wmbus/driver_*.cpp>"])
    cg.add_platformio_option("build_src_filter", ["+<**/wmbus/driver_unknown.cpp>"])

    if CORE.is_esp8266 and config[CONF_RODATA_IN_FLASH]:
        # Move the wmbus .rodata (~30 KB of strings/tables) from DRAM to flash,
        # see rodata_to_flash.py.script. NON32XFER_HANDLER makes the Arduino core
        # emulate 8/16-bit reads from flash so that this is safe.
        cg.add_build_flag("-DNON32XFER_HANDLER")
        copy_file_if_changed(
            Path(__file__).parent / "rodata_to_flash.py.script",
            CORE.relative_build_path("wmbus_rodata_to_flash.py"),
        )
        cg.add_platformio_option("extra_scripts", ["post:wmbus_rodata_to_flash.py"])
