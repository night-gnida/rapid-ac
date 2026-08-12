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
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

  void setup() override;
  void transmit_state() override;

 private:
  void send_frame_(uint8_t command, bool power, climate::ClimateMode mode, float temp,
                   climate::ClimateFanMode fan, bool swing_on);

  bool last_power_{true};
  climate::ClimateMode last_mode_{climate::CLIMATE_MODE_COOL};
  float last_temp_{24.0f};
  climate::ClimateFanMode last_fan_{climate::CLIMATE_FAN_AUTO};
  bool last_swing_{false};
};

}  // namespace rapid_ac
}  // namespace esphome
