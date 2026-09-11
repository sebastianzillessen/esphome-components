# wM-Bus for ESP8266 + CC1101 (trimmed fork)

Stripped-down fork of the `wmbus` component from
[SzczepanLeon/esphome-components](https://github.com/SzczepanLeon/esphome-components)
branch `version_4`, cut down to what a single Diehl/Hydrometer **hydrus** water meter
on a **Wemos D1 mini (ESP8266)** with a **CC1101** needs.

Upstream `version_4` refuses to build for ESP8266 (`#error`, see upstream issue #131)
because the full build does not fit into the ESP8266's RAM. This fork removes the
`#error` and everything that is not needed so that it does fit.

## What was removed compared to upstream `version_4`

- All meter drivers except `hydrus` (and the internal `unknown` fallback).
- The 1334-entry manufacturer name table (it was built on the heap at boot).
- MQTT (both ESPHome MQTT and the embedded PubSubClient), TCP/UDP "clients",
  raw-frame forwarding, LED blinking, the `time` dependency, the `ethernet`
  component, the `text_sensor` platform and the `all_drivers` option.
- ESP32 specific code.

The CC1101 receive loop and the wmbusmeters decoding core are unchanged.

## Usage

See [`wasserzaehler.yaml`](wasserzaehler.yaml) for a full configuration and
[`secrets.yaml.example`](secrets.yaml.example) for the secrets it expects.

```yaml
external_components:
  - source: github://sebastianzillessen/esphome-components@main
    components: [wmbus]

wmbus:
  mosi_pin: GPIO13
  miso_pin: GPIO12
  clk_pin: GPIO14
  cs_pin: GPIO15
  gdo0_pin: GPIO5
  gdo2_pin: GPIO4
  frequency: 868.950
  log_all: false

sensor:
  - platform: wmbus
    meter_id: 0x12345678
    type: hydrus
    key: "00000000000000000000000000000000"
    sensors:
      - name: "Water total"
        field: "total"
        unit_of_measurement: "m³"
        accuracy_decimals: 3
        device_class: water
        state_class: total_increasing
```

### `wmbus` options

- **mosi_pin / miso_pin / clk_pin / cs_pin** (*Optional*): CC1101 SPI pins.
- **gdo0_pin / gdo2_pin** (*Optional*): CC1101 GDO0 / GDO2 pins.
- **frequency** (*Optional*): Rx frequency in MHz. Defaults to `868.950`.
- **sync_mode** (*Optional*): Read a whole telegram inside one `loop()` call. Defaults to `false`.
- **log_all** (*Optional*): Log every received telegram, not only configured meters. Defaults to `false`.

### `sensor` platform `wmbus`

- **meter_id** (*Optional*): Meter ID (hex or decimal).
- **type** (*Optional*): Driver name. Only `hydrus` ships in this fork; the driver decides
  which link modes (T1/C1) it accepts, so there is no `mode:` option any more.
- **key** (*Optional*): 32 hex characters AES key.
- **sensors**: list of sensors with **field** (e.g. `total`, `rssi`, `flow`, `flow_temperature_c`)
  and a mandatory **unit_of_measurement** (`m³`, `dBm`, ...).

## Adding another driver

Copy the matching `driver_<name>.cpp` from upstream `version_4` into `components/wmbus/`
and set `type: <name>` on the sensor. Only referenced drivers are compiled.

## License

GPL-3.0-or-later, same as upstream and wmbusmeters.
