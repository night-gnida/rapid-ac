#include "rapid_ac.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace rapid_ac {

static const char *const TAG = "rapid_ac.climate";

// Rapid (YTF Remote IR) protocol timing, 38 kHz
static const uint32_t HDR_MARK = 6200;
static const uint32_t HDR_SPACE = 7480;
static const uint32_t BIT_MARK = 560;
static const uint32_t ONE_SPACE = 1640;
static const uint32_t ZERO_SPACE = 550;
static const uint32_t FOOTER_SPACE = 7480;
static const uint32_t GAP = 10125;

void RapidAcClimate::setup() {
  climate_ir::ClimateIR::setup();
  this->last_power_ = (this->mode != climate::CLIMATE_MODE_OFF);
  this->last_mode_ = this->mode;
  this->last_temp_ = this->target_temperature;
  this->last_swing_ = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL);
}

void RapidAcClimate::transmit_state() {
  bool power = (this->mode != climate::CLIMATE_MODE_OFF);
  bool swing_on = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL);

  uint8_t command = 0x01;
  if (power != this->last_power_) {
    command = 0x00;
  } else if (power && swing_on != this->last_swing_) {
    command = 0x04;
  } else if (power) {
    if (this->mode != this->last_mode_) {
      command = 0x01;
    } else if (this->target_temperature > this->last_temp_) {
      command = 0x02;
    } else if (this->target_temperature < this->last_temp_) {
      command = 0x03;
    }
  }

  climate::ClimateMode frame_mode = power ? this->mode : this->last_mode_;
  this->send_frame_(command, power, frame_mode, this->target_temperature, swing_on);

  this->last_power_ = power;
  if (power) {
    this->last_mode_ = this->mode;
  }
  this->last_temp_ = this->target_temperature;
  this->last_swing_ = swing_on;
}

void RapidAcClimate::send_frame_(uint8_t command, bool power, climate::ClimateMode mode,
                                 float temp, bool swing_on) {
  uint8_t r3;
  if (command == 0x04) {
    switch (mode) {
      case climate::CLIMATE_MODE_COOL:
      case climate::CLIMATE_MODE_FAN_ONLY:
        r3 = 0x3A;
        break;
      default:
        r3 = 0x7A;
        break;
    }
    if (swing_on) {
      r3 ^= 0x0C;
    }
  } else {
    if (mode == climate::CLIMATE_MODE_COOL) {
      r3 = 0x3A;
    } else {
      r3 = 0x7A;
    }
    if (!power) {
      r3 &= ~0x02;
    }
    if (swing_on && power) {
      r3 ^= 0x0C;
    }
  }

  uint8_t mode_code;
  switch (mode) {
    case climate::CLIMATE_MODE_HEAT_COOL:
      mode_code = 0;
      break;
    case climate::CLIMATE_MODE_COOL:
      mode_code = 1;
      break;
    case climate::CLIMATE_MODE_DRY:
      mode_code = 2;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      mode_code = 3;
      break;
    case climate::CLIMATE_MODE_HEAT:
      mode_code = 4;
      break;
    default:
      mode_code = 0;
      break;
  }

  uint8_t temp_val = (uint8_t) roundf(temp) - 16;
  uint8_t r4 = (uint8_t)((mode_code << 1) << 4) | temp_val;

  const uint8_t real[6] = {0x00, 0x00, command, r3, r4, 0x55};
  uint8_t wire[12];
  for (int i = 0; i < 6; i++) {
    wire[i * 2] = (uint8_t)~real[i];
    wire[i * 2 + 1] = real[i];
  }

  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(38000);

  data->mark(HDR_MARK);
  data->space(HDR_SPACE);

  for (int i = 0; i < 12; i++) {
    for (int bit = 0; bit < 8; bit++) {
      data->mark(BIT_MARK);
      data->space((wire[i] >> bit) & 0x01 ? ONE_SPACE : ZERO_SPACE);
    }
  }

  data->mark(BIT_MARK);
  data->space(FOOTER_SPACE);
  data->mark(BIT_MARK);
  data->space(GAP);

  call.set_send_times(1);
  call.perform();

  ESP_LOGV(TAG, "Sent cmd=0x%02X power=%d mode=%d temp=%.0f wire=FF 00 FF 00 %02X %02X %02X %02X "
                "%02X %02X AA 55",
           command, power ? 1 : 0, (int) mode, temp, wire[4], wire[5], wire[6], wire[7], wire[8],
           wire[9]);
}

}  // namespace rapid_ac
}  // namespace esphome
