#include "tuya_ble_sensor.h"

namespace esphome {
namespace tuya_ble {

static const char *const TAG = "tuya_ble_sensor";

void TuyaBLESensor::dump_config() {
  ESP_LOGCONFIG(TAG, "BLE Sensor");
  ESP_LOGCONFIG(TAG, "  DP id: %u", this->dp_id_);
  LOG_SENSOR("  ", "Sensor", this);
}

void TuyaBLESensor::on_dp(uint8_t dp_type, unsigned char *value, size_t len) {
  if(dp_type != DP_TYPE_VALUE && dp_type != DP_TYPE_ENUM && dp_type != DP_TYPE_BITMAP) {
    ESP_LOGW(TAG, "DP%u: unexpected dp_type %u for a numeric sensor", this->dp_id_, dp_type);
    return;
  }
  if(len == 0 || len > 4) {
    ESP_LOGW(TAG, "DP%u: unexpected value length %u", this->dp_id_, len);
    return;
  }

  // Tuya DP "value" type is a big-endian integer, signed when the full 4 bytes are sent.
  int64_t raw = 0;
  for(size_t i = 0; i < len; i++) {
    raw = (raw << 8) | value[i];
  }
  if(len == 4 && (value[0] & 0x80)) {
    raw -= (int64_t) 1 << 32;
  }

  ESP_LOGD(TAG, "DP%u = %lld", this->dp_id_, (long long) raw);
  this->publish_state((float) raw);
}

}  // namespace tuya_ble
}  // namespace esphome
