#pragma once

#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/core/component.h"

namespace esphome {
namespace rapid_ac {

class RapidAcClimate : public climate_ir::ClimateIR {
 public:
  RapidAcClimate()
      : climate_ir::ClimateIR(16.0f, 31.0f, 1.0f, true, true, {},
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

  void setup() override;
  void transmit_state() override;

 private:
  void send_frame_(uint8_t command, bool power, climate::ClimateMode mode, float temp,
                   bool swing_on);

  bool last_power_{true};
  climate::ClimateMode last_mode_{climate::CLIMATE_MODE_COOL};
  float last_temp_{24.0f};
  bool last_swing_{false};
};

}  // namespace rapid_ac
}  // namespace esphome
