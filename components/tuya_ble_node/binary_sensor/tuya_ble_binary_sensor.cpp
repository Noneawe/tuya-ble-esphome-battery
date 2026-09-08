#include "tuya_ble_binary_sensor.h"

namespace esphome {
namespace tuya_ble {

static const char *const TAG = "tuya_ble_binary_sensor";

void TuyaBLEBinarySensor::dump_config() {
  ESP_LOGCONFIG(TAG, "BLE Binary Sensor");
  ESP_LOGCONFIG(TAG, "  DP id: %u", this->dp_id_);
  LOG_BINARY_SENSOR("  ", "Binary Sensor", this);
}

void TuyaBLEBinarySensor::on_dp(uint8_t dp_type, unsigned char *value, size_t len) {
  if(dp_type != DP_TYPE_BOOL || len != 1) {
    ESP_LOGW(TAG, "DP%u: unexpected dp_type %u / length %u for a binary sensor", this->dp_id_, dp_type, len);
    return;
  }

  bool state = value[0] != 0;
  ESP_LOGD(TAG, "DP%u = %s", this->dp_id_, state ? "true" : "false");
  this->publish_state(state);
}

}  // namespace tuya_ble
}  // namespace esphome
