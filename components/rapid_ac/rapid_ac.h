#pragma once

#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/core/component.h"

namespace esphome {
namespace rapid_ac {

class RapidAcClimate : public climate_ir::ClimateIR {
 public:
  RapidAcClimate()
      : climate_ir::ClimateIR(16.0f, 31.0f, 1.0f, true, true,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                               climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL},
                              {climate::CLIMATE_PRESET_SLEEP, climate::CLIMATE_PRESET_BOOST}) {
    this->set_supported_custom_presets({"light"});
  }

  void setup() override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

 private:
  void send_frame_(uint8_t command, bool power, climate::ClimateMode mode, float temp,
                   climate::ClimateFanMode fan, bool swing_on, bool sleep_on, bool turbo_on,
                   bool light_on);

  bool last_power_{true};
  climate::ClimateMode last_mode_{climate::CLIMATE_MODE_COOL};
  float last_temp_{24.0f};
  climate::ClimateFanMode last_fan_{climate::CLIMATE_FAN_AUTO};
  bool last_swing_{false};
  bool last_sleep_{false};
  bool last_turbo_{false};
  bool last_light_{false};
};

}  // namespace rapid_ac
}  // namespace esphome
