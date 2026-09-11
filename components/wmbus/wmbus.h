#pragma once

#include "esphome/core/log.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include <map>
#include <utility>
#include <string>
#include <vector>

#include "rf_cc1101.h"
#include "m_bus_data.h"
#include "crc.h"

#include "utils.h"

namespace esphome {
namespace wmbus {

  class WMBusListener {
    public:
      WMBusListener(const uint32_t id, const std::string type, const std::string key);
      uint32_t id;
      std::string type;
      std::string myKey;
      std::vector<unsigned char> key{};
      std::map<std::pair<std::string, std::string>, sensor::Sensor *> fields{};
      void add_sensor(std::string field, std::string unit, sensor::Sensor *sensor) {
        this->fields[std::pair<std::string, std::string>(field, unit)] = sensor;
      };

      void dump_config();
      int char_to_int(char input);
      bool hex_to_bin(const std::string &src, std::vector<unsigned char> *target) { return hex_to_bin(src.c_str(), target); };
      bool hex_to_bin(const char* src, std::vector<unsigned char> *target);
  };

  struct Cc1101 {
    InternalGPIOPin *mosi{nullptr};
    InternalGPIOPin *miso{nullptr};
    InternalGPIOPin *clk{nullptr};
    InternalGPIOPin *cs{nullptr};
    InternalGPIOPin *gdo0{nullptr};
    InternalGPIOPin *gdo2{nullptr};
  };

  class WMBusComponent : public Component {
    public:
      void setup() override;
      void loop() override;
      void dump_config() override;
      float get_setup_priority() const override { return setup_priority::LATE; }
      void register_wmbus_listener(const uint32_t meter_id, const std::string type, const std::string key);
      void add_cc1101(InternalGPIOPin *mosi, InternalGPIOPin *miso,
                      InternalGPIOPin *clk, InternalGPIOPin *cs,
                      InternalGPIOPin *gdo0, InternalGPIOPin *gdo2,
                      double frequency, bool sync_mode) {
        this->spi_conf_.mosi = mosi;
        this->spi_conf_.miso = miso;
        this->spi_conf_.clk  = clk;
        this->spi_conf_.cs   = cs;
        this->spi_conf_.gdo0 = gdo0;
        this->spi_conf_.gdo2 = gdo2;
        this->frequency_ = frequency;
        this->sync_mode_ = sync_mode;
      }
      void add_sensor(uint32_t meter_id, std::string field, std::string unit, sensor::Sensor *sensor) {
        if (this->wmbus_listeners_.count(meter_id) != 0) {
          this->wmbus_listeners_[meter_id]->add_sensor(field, unit, sensor);
        }
      }
      void set_log_all(bool log_all) { this->log_all_ = log_all; }

    protected:
      void handle_frame(WMbusFrame &mbus_data);
      HighFrequencyLoopRequester high_freq_;
      Cc1101 spi_conf_{};
      double frequency_{};
      bool sync_mode_{false};
      std::map<uint32_t, WMBusListener *> wmbus_listeners_{};
      bool log_all_{false};
      RxLoop rf_mbus_;
  };

}  // namespace wmbus
}  // namespace esphome
