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
  this->last_fan_ = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  this->last_swing_ = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL);
  this->last_sleep_ = (this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_SLEEP);
  this->last_turbo_ = (this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_BOOST);
  this->last_light_ = (this->get_custom_preset() == "light");
  this->update_action_();

  if (this->power_sensor_ != nullptr) {
    this->power_sensor_->add_on_state_callback([this](float) {
      this->update_action_();
      this->publish_state();
    });
  }

  this->publish_state();
}

void RapidAcClimate::update_action_() {
  bool power_known = this->power_sensor_ != nullptr && this->power_sensor_->has_state() &&
                     !std::isnan(this->power_sensor_->state);
  bool compressor = power_known && this->power_sensor_->state > COMPRESSOR_POWER_THRESHOLD_W;
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
      this->action = compressor ? climate::CLIMATE_ACTION_COOLING : climate::CLIMATE_ACTION_IDLE;
      break;
    case climate::CLIMATE_MODE_HEAT:
      this->action = compressor ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
      break;
    case climate::CLIMATE_MODE_DRY:
      this->action = compressor ? climate::CLIMATE_ACTION_DRYING : climate::CLIMATE_ACTION_IDLE;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      this->action = climate::CLIMATE_ACTION_FAN;
      break;
    case climate::CLIMATE_MODE_OFF:
      this->action = climate::CLIMATE_ACTION_OFF;
      break;
    default:
      this->action = climate::CLIMATE_ACTION_IDLE;
      break;
  }
}

void RapidAcClimate::transmit_state() {
  bool power = (this->mode != climate::CLIMATE_MODE_OFF);
  bool swing_on = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL);
  climate::ClimateFanMode fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  bool sleep_on = (this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_SLEEP);
  bool turbo_on = (this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_BOOST);
  bool light_on = (this->get_custom_preset() == "light");

  uint8_t command = 0x01;
  if (power != this->last_power_) {
    command = 0x00;
  } else if (power && swing_on != this->last_swing_) {
    command = 0x04;
  } else if (power && fan != this->last_fan_) {
    command = 0x05;
  } else if (power && sleep_on != this->last_sleep_) {
    command = 0x09;
  } else if (power && turbo_on != this->last_turbo_) {
    command = 0x0A;
  } else if (power && light_on != this->last_light_) {
    command = 0x0B;
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
  this->send_frame_(command, power, frame_mode, this->target_temperature, fan, swing_on,
                    sleep_on, turbo_on, light_on);
  this->update_action_();

  this->last_power_ = power;
  if (power) {
    this->last_mode_ = this->mode;
    this->last_fan_ = fan;
  }
  this->last_temp_ = this->target_temperature;
  this->last_swing_ = swing_on;
  this->last_sleep_ = sleep_on;
  this->last_turbo_ = turbo_on;
  this->last_light_ = light_on;
}

void RapidAcClimate::send_frame_(uint8_t command, bool power, climate::ClimateMode mode,
                                 float temp, climate::ClimateFanMode fan, bool swing_on,
                                 bool sleep_on, bool turbo_on, bool light_on, uint8_t r0,
                                 uint8_t r1_extra, uint8_t r3_clear, uint8_t r3_set) {
  uint8_t fan_bits;
  switch (fan) {
    case climate::CLIMATE_FAN_HIGH:
      fan_bits = 0b01;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      fan_bits = 0b10;
      break;
    case climate::CLIMATE_FAN_LOW:
      fan_bits = 0b11;
      break;
    default:
      fan_bits = 0b00;
      break;
  }

  uint8_t r1 = 0;
  r1 |= (light_on ? (uint8_t) 0x01 : 0x00);  // Light: bit0
  r1 |= (turbo_on ? (uint8_t) 0x08 : 0x00);  // Turbo: bit3
  r1 |= r1_extra;

  uint8_t r3 = 0;
  r3 |= (sleep_on ? (uint8_t) 0x01 : 0x00);  // Sleep: bit0
  r3 |= (power ? (uint8_t) 0x02 : 0x00);     // Power: bit1
  r3 |= (swing_on ? (uint8_t) 0x04 : 0x08);  // Swing: 0b01=on, 0b10=off
  r3 |= 0x10;                                // AirFlow: fixed 1
  r3 |= fan_bits << 5;
  r3 = (uint8_t) ((r3 & ~r3_clear) | r3_set);

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

  const uint8_t real[6] = {r0, r1, command, r3, r4, 0x55};
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

  ESP_LOGV(TAG, "Sent cmd=0x%02X power=%d mode=%d temp=%.0f wire=%02X %02X %02X %02X %02X %02X "
                "%02X %02X %02X %02X %02X %02X",
           command, power ? 1 : 0, (int) mode, temp, wire[0], wire[1], wire[2], wire[3], wire[4],
           wire[5], wire[6], wire[7], wire[8], wire[9], wire[10], wire[11]);
}

void RapidAcClimate::set_timer_hours(int hours) {
  if (hours < 0)
    hours = 0;
  if (hours > 24)
    hours = 24;
  uint8_t r0 = hours == 0 ? 0x00 : (uint8_t) (0xA0 | hours);
  bool power = (this->mode != climate::CLIMATE_MODE_OFF);
  climate::ClimateMode frame_mode = power ? this->mode : this->last_mode_;
  this->send_frame_(0x03, power, frame_mode, this->target_temperature,
                    this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO),
                    this->swing_mode == climate::CLIMATE_SWING_VERTICAL,
                    this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_SLEEP,
                    this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_BOOST,
                    this->get_custom_preset() == "light", r0);
  this->last_timer_hours_ = hours;
  ESP_LOGD(TAG, "set timer: %dh (R0=%02X)", hours, r0);
}

void RapidAcClimate::send_probe(uint8_t command, uint8_t r0, uint8_t r1_extra,
                                uint8_t r3_clear, uint8_t r3_set) {
  bool power = (this->mode != climate::CLIMATE_MODE_OFF);
  climate::ClimateMode frame_mode = power ? this->mode : this->last_mode_;
  this->send_frame_(command, power, frame_mode, this->target_temperature,
                    this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO),
                    this->swing_mode == climate::CLIMATE_SWING_VERTICAL,
                    this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_SLEEP,
                    this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_BOOST,
                    this->get_custom_preset() == "light", r0, r1_extra, r3_clear, r3_set);
  ESP_LOGD(TAG, "probe: cmd=0x%02X R0=%02X R1+=%02X R3clear=%02X R3set=%02X", command, r0,
           r1_extra, r3_clear, r3_set);
}

bool RapidAcClimate::on_receive(remote_base::RemoteReceiveData data) {
  const auto matches = [](int32_t value, uint32_t target) {
    uint32_t v = std::abs(value);
    return v >= target * 75 / 100 && v <= target * 125 / 100;
  };
  const int32_t n = data.size();
  uint32_t idx = 0;

  if (idx + 1 >= (uint32_t) n) {
    ESP_LOGD(TAG, "reject: too short (n=%d)", (int) n);
    return false;
  }
  if (!matches(data[idx], HDR_MARK) || !matches(data[idx + 1], HDR_SPACE)) {
    ESP_LOGD(TAG, "reject: header mismatch (idx=%u)", idx);
    return false;
  }
  idx += 2;

  uint8_t wire[12];
  for (int i = 0; i < 12; i++) {
    uint8_t byte = 0;
    for (int bit = 0; bit < 8; bit++) {
      if (idx + 1 >= (uint32_t) n) {
        ESP_LOGD(TAG, "reject: too short at bit byte=%d bit=%d (idx=%u)", i, bit, idx);
        return false;
      }
      if (!matches(data[idx], BIT_MARK)) {
        ESP_LOGD(TAG, "reject: bit mark fail byte=%d bit=%d val=%d (idx=%u)", i, bit,
                 (int) data[idx], idx);
        return false;
      }
      if (matches(data[idx + 1], ONE_SPACE)) {
        byte |= (uint8_t) 1 << bit;
      } else if (!matches(data[idx + 1], ZERO_SPACE)) {
        ESP_LOGD(TAG, "reject: bit space fail byte=%d bit=%d val=%d (idx=%u)", i, bit,
                 (int) data[idx + 1], idx);
        return false;
      }
      idx += 2;
    }
    wire[i] = byte;
  }

  if (idx + 1 >= (uint32_t) n) {
    ESP_LOGD(TAG, "reject: too short at footer (idx=%u)", idx);
    return false;
  }
  if (!matches(data[idx], BIT_MARK) || !matches(data[idx + 1], FOOTER_SPACE)) {
    ESP_LOGD(TAG, "reject: footer mismatch (idx=%u)", idx);
    return false;
  }
  idx += 2;
  if (idx >= (uint32_t) n || !matches(data[idx], BIT_MARK)) {
    ESP_LOGD(TAG, "reject: final mark mismatch (idx=%u)", idx);
    return false;
  }

  for (int i = 0; i < 6; i++) {
    if (wire[i * 2] != (uint8_t) ~wire[i * 2 + 1]) {
      ESP_LOGD(TAG, "reject: pair complement fail i=%d %02X vs ~%02X", i, wire[i * 2], wire[i * 2 + 1]);
      return false;
    }
  }

  uint8_t r0 = wire[1];
  uint8_t r1 = wire[3];
  uint8_t command = wire[5];
  uint8_t r3 = wire[7];
  uint8_t r4 = wire[9];

  this->last_timer_hours_ = (r0 >= 0xA1 && r0 <= 0xB8) ? (uint8_t) (r0 & 0x1F) : 0;

  if (r0 >= 0xA1 && r0 <= 0xB8) {
    ESP_LOGD(TAG, "timer: cmd=0x%02X R0=%02X -> %uh", command, r0, r0 & 0x1F);
  } else if (command == 0x0D) {
    // Timer-mode frame; payload fields are not climate state.
    ESP_LOGD(TAG, "timer-mode: cmd=0x0D R0=%02X R1=%02X", r0, r1);
    return true;
  }

  bool power = (r3 & 0x02) != 0;
  bool sleep_on = (r3 & 0x01) != 0;
  bool swing_on = ((r3 >> 2) & 0x03) == 0x01;
  uint8_t fan_bits = (r3 >> 5) & 0x03;
  bool turbo_on = (r1 & 0x08) != 0;
  bool light_on = (r1 & 0x01) != 0;

  climate::ClimateMode mode = climate::CLIMATE_MODE_OFF;
  if (power) {
    switch ((r4 >> 5) & 0x07) {
      case 0:
        mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
      case 1:
        mode = climate::CLIMATE_MODE_COOL;
        break;
      case 2:
        mode = climate::CLIMATE_MODE_DRY;
        break;
      case 3:
        mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case 4:
        mode = climate::CLIMATE_MODE_HEAT;
        break;
      default:
        mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
    }
  }

  climate::ClimateFanMode fan;
  switch (fan_bits) {
    case 0b01:
      fan = climate::CLIMATE_FAN_HIGH;
      break;
    case 0b10:
      fan = climate::CLIMATE_FAN_MEDIUM;
      break;
    case 0b11:
      fan = climate::CLIMATE_FAN_LOW;
      break;
    default:
      fan = climate::CLIMATE_FAN_AUTO;
      break;
  }

  climate::ClimateSwingMode swing_mode =
      swing_on ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;

  climate::ClimatePreset preset = climate::CLIMATE_PRESET_NONE;
  const char *custom_preset = nullptr;
  if (sleep_on) {
    preset = climate::CLIMATE_PRESET_SLEEP;
  } else if (turbo_on) {
    preset = climate::CLIMATE_PRESET_BOOST;
  } else if (light_on) {
    custom_preset = "light";
  }

  this->mode = mode;
  this->target_temperature = (r4 & 0x1F) + 16;
  this->fan_mode = fan;
  this->swing_mode = swing_mode;
  this->preset = preset;
  if (custom_preset != nullptr) {
    this->set_supported_custom_presets({"light"});
    this->set_custom_preset_(custom_preset);
  } else {
    this->clear_custom_preset_();
  }

  this->last_power_ = power;
  if (power) {
    this->last_mode_ = mode;
    this->last_fan_ = fan;
  }
  this->last_temp_ = this->target_temperature;
  this->last_swing_ = swing_on;
  this->last_sleep_ = sleep_on;
  this->last_turbo_ = turbo_on;
  this->last_light_ = light_on;

  ESP_LOGD(TAG,
           "accepted: cmd=0x%02X R0=%02X R1=%02X R3=%02X R4=%02X power=%d mode=%d temp=%.0f "
           "fan=%d swing=%d sleep=%d turbo=%d light=%d",
           command, r0, r1, r3, r4, power ? 1 : 0, (int) mode, this->target_temperature,
           (int) fan_bits, swing_on ? 1 : 0, sleep_on ? 1 : 0, turbo_on ? 1 : 0,
           light_on ? 1 : 0);
  this->update_action_();
  this->publish_state();
  return true;
}

}  // namespace rapid_ac
}  // namespace esphome
