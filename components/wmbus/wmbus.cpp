#include "wmbus.h"
#include "version.h"

#include "meters.h"

#include "address.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"

#include <memory>
#include <time.h>

namespace esphome {
namespace wmbus {

  static const char *TAG = "wmbus";

  void WMBusComponent::setup() {
    this->high_freq_.start();
    if (!rf_mbus_.init(this->spi_conf_.mosi->get_pin(), this->spi_conf_.miso->get_pin(),
                       this->spi_conf_.clk->get_pin(),  this->spi_conf_.cs->get_pin(),
                       this->spi_conf_.gdo0->get_pin(), this->spi_conf_.gdo2->get_pin(),
                       this->frequency_, this->sync_mode_)) {
      this->mark_failed();
      ESP_LOGE(TAG, "RF chip initialization failed");
      return;
    }
  }

  void WMBusComponent::loop() {
    if (rf_mbus_.task()) {
      ESP_LOGVV(TAG, "Have data from RF ...");
      WMbusFrame mbus_data = rf_mbus_.get_frame();
      this->handle_frame(mbus_data);
    }
  }

  void WMBusComponent::handle_frame(WMbusFrame &mbus_data) {
    std::string telegram = format_hex_pretty(mbus_data.frame);
    telegram.erase(std::remove(telegram.begin(), telegram.end(), '.'), telegram.end());

    // Telegram is a large object (many strings/vectors); keep it off the
    // small ESP8266 main stack.
    std::unique_ptr<Telegram> t(new Telegram());
    if (t->parseHeader(mbus_data.frame) && t->addresses.empty()) {
      ESP_LOGE(TAG, "Address is empty! T: %s", telegram.c_str());
      return;
    }
    if (t->addresses.empty()) {
      ESP_LOGW(TAG, "Can't parse header T: %s", telegram.c_str());
      return;
    }

    uint32_t meter_id = (uint32_t)strtoul(t->addresses[0].id.c_str(), nullptr, 16);
    bool meter_in_config = (this->wmbus_listeners_.count(meter_id) == 1);

    // No need to do anything if logging is disabled and meter is not configured
    if (!this->log_all_ && !meter_in_config) {
      return;
    }

    auto detected_drv_info      = pickMeterDriver(t.get());
    std::string detected_driver = detected_drv_info.name().str();

    // If the driver was explicitly stated in meter config, use that driver instead of the detected one
    auto used_drv_info      = detected_drv_info;
    std::string used_driver = detected_driver;
    if (meter_in_config) {
      auto *sensor = this->wmbus_listeners_[meter_id];
      used_driver = ((sensor->type).empty() ? detected_driver : sensor->type);
      if (!(sensor->type).empty()) {
        auto *used_drv_info_ptr = lookupDriver(used_driver);
        if (used_drv_info_ptr == nullptr) {
          used_driver = detected_driver;
          used_drv_info = detected_drv_info;
          ESP_LOGW(TAG, "Selected driver %s doesn't exist, using %s", (sensor->type).c_str(), used_driver.c_str());
        }
        else {
          used_drv_info = *used_drv_info_ptr;
          ESP_LOGD(TAG, "Using selected driver %s (detected driver was %s)", used_driver.c_str(), detected_driver.c_str());
        }
      }
    }

    ESP_LOGI(TAG, "%s [0x%08x] RSSI: %ddBm T: %s %c1 %c",
             (used_driver.empty() ? "Unknown!" : used_driver.c_str()),
             meter_id,
             mbus_data.rssi,
             telegram.c_str(),
             mbus_data.mode,
             mbus_data.block);

    if (!meter_in_config) {
      return;
    }

    bool supported_link_mode{false};
    if (used_drv_info.linkModes().empty()) {
      supported_link_mode = true;
      ESP_LOGW(TAG, "Link modes not defined in driver %s. Processing anyway.",
               (used_driver.empty() ? "Unknown!" : used_driver.c_str()));
    }
    else {
      supported_link_mode = ( ((mbus_data.mode == 'T') && (used_drv_info.linkModes().has(LinkMode::T1))) ||
                              ((mbus_data.mode == 'C') && (used_drv_info.linkModes().has(LinkMode::C1))) );
    }

    if (used_driver.empty()) {
      ESP_LOGW(TAG, "Can't find driver for T: %s", telegram.c_str());
      return;
    }
    if (!supported_link_mode) {
      ESP_LOGW(TAG, "Link mode %c1 not supported in driver %s", mbus_data.mode, used_driver.c_str());
      return;
    }

    auto *sensor = this->wmbus_listeners_[meter_id];

    bool id_match;
    MeterInfo mi;
    mi.parse("ESPHome", used_driver, t->addresses[0].id + ",", sensor->myKey);
    auto meter = createMeter(&mi);
    std::vector<Address> addresses;
    // No time component in this build: use the libc clock (seconds since boot
    // unless SNTP is configured). Only used for the meter's "timestamp" field.
    AboutTelegram about{"ESPHome wM-Bus", mbus_data.rssi, FrameType::WMBUS, ::time(nullptr)};
    meter->handleTelegram(about, mbus_data.frame, false, &addresses, &id_match, t.get());
    if (!id_match) {
      ESP_LOGE(TAG, "Not for me T: %s", telegram.c_str());
      return;
    }

    for (auto const& field : sensor->fields) {
      std::string field_name = field.first.first;
      std::string unit = field.first.second;
      if (field_name == "rssi") {
        field.second->publish_state(mbus_data.rssi);
      }
      else if (field.second->get_unit_of_measurement_ref().empty()) {
        ESP_LOGW(TAG, "Fields without unit not supported as sensor.");
      }
      else {
        Unit field_unit = toUnit(field.second->get_unit_of_measurement_ref());
        if (field_unit != Unit::Unknown) {
          double value  = meter->getNumericValue(field_name, field_unit);
          if (!std::isnan(value)) {
            field.second->publish_state(value);
          }
          else {
            ESP_LOGW(TAG, "Can't get requested field '%s' with unit '%s'", field_name.c_str(), unit.c_str());
          }
        }
        else {
          ESP_LOGW(TAG, "Can't get proper unit from '%s'", unit.c_str());
        }
      }
    }
  }

  void WMBusComponent::register_wmbus_listener(const uint32_t meter_id, const std::string type, const std::string key) {
    if (this->wmbus_listeners_.count(meter_id) == 0) {
      WMBusListener *listener = new wmbus::WMBusListener(meter_id, type, key);
      this->wmbus_listeners_.insert({meter_id, listener});
    }
  }

  void WMBusComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "wM-Bus v%s-%s:", MY_VERSION, WMBUSMETERS_VERSION);
    ESP_LOGCONFIG(TAG, "  CC1101 frequency: %3.3f MHz", this->frequency_);
    ESP_LOGCONFIG(TAG, "  Sync mode: %s", YESNO(this->sync_mode_));
    ESP_LOGCONFIG(TAG, "  Log all: %s", YESNO(this->log_all_));
    ESP_LOGCONFIG(TAG, "  CC1101 SPI bus:");
    if (this->is_failed()) {
      ESP_LOGE(TAG, "   Check connection to CC1101!");
    }
    LOG_PIN("    MOSI Pin: ", this->spi_conf_.mosi);
    LOG_PIN("    MISO Pin: ", this->spi_conf_.miso);
    LOG_PIN("    CLK Pin:  ", this->spi_conf_.clk);
    LOG_PIN("    CS Pin:   ", this->spi_conf_.cs);
    LOG_PIN("    GDO0 Pin: ", this->spi_conf_.gdo0);
    LOG_PIN("    GDO2 Pin: ", this->spi_conf_.gdo2);
    std::string drivers = "";
    verifyDriverLookupCreated();
    for (DriverInfo* p : allDrivers()) {
      drivers += p->name().str() + ", ";
    }
    if (drivers.size() >= 2) {
      drivers.erase(drivers.size() - 2);
    }
    ESP_LOGCONFIG(TAG, "  Available drivers: %s", drivers.c_str());
    for (const auto &ele : this->wmbus_listeners_) {
      ele.second->dump_config();
    }
  }

  ///////////////////////////////////////

  void WMBusListener::dump_config() {
    std::string key = format_hex_pretty(this->key);
    key.erase(std::remove(key.begin(), key.end(), '.'), key.end());
    if (key.size()) {
      key.erase(key.size() - 5);
    }
    ESP_LOGCONFIG(TAG, "  Meter:");
    ESP_LOGCONFIG(TAG, "    ID: %u [0x%08X]", (unsigned) this->id, (unsigned) this->id);
    ESP_LOGCONFIG(TAG, "    Type: %s", ((this->type).empty() ? "auto detect" : this->type.c_str()));
    ESP_LOGCONFIG(TAG, "    Key: '%s'", key.c_str());
    for (const auto &ele : this->fields) {
      ESP_LOGCONFIG(TAG, "    Field: '%s'", ele.first.first.c_str());
      LOG_SENSOR("     ", "Name:", ele.second);
    }
  }

  WMBusListener::WMBusListener(const uint32_t id, const std::string type, const std::string key) {
    this->id = id;
    this->type = type;
    this->myKey = key;
    hex_to_bin(key, &(this->key));
  }

  int WMBusListener::char_to_int(char input)
  {
    if(input >= '0' && input <= '9') {
      return input - '0';
    }
    if(input >= 'A' && input <= 'F') {
      return input - 'A' + 10;
    }
    if(input >= 'a' && input <= 'f') {
      return input - 'a' + 10;
    }
    return -1;
  }

  bool WMBusListener::hex_to_bin(const char* src, std::vector<unsigned char> *target)
  {
    if (!src) return false;
    while(*src && src[1]) {
      if (*src == ' ' || *src == '#' || *src == '|' || *src == '_') {
        // Ignore space and hashes and pipes and underlines.
        src++;
      }
      else {
        int hi = char_to_int(*src);
        int lo = char_to_int(src[1]);
        if (hi<0 || lo<0) return false;
        target->push_back(hi*16 + lo);
        src += 2;
      }
    }
    return true;
  }

}  // namespace wmbus
}  // namespace esphome
