#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/tuya_ble_tracker/common.h"

namespace esphome {
namespace tuya_ble {

class TuyaBLEBinarySensor : public binary_sensor::BinarySensor, public Component {
  public:
    void dump_config() override;
    void set_dp_id(uint8_t dp_id) { this->dp_id_ = dp_id; }

    void register_node(TYBLENode *node) {
      ESP_LOGD("tuya_ble_binary_sensor", "Binary sensor registered for DP%u", this->dp_id_);
      node->register_dp_callback(this->dp_id_, [this](uint8_t dp_type, unsigned char *value, size_t len) {
        this->on_dp(dp_type, value, len);
      });
    }

  protected:
    uint8_t dp_id_;

    void on_dp(uint8_t dp_type, unsigned char *value, size_t len);
};

}  // namespace tuya_ble
}  // namespace esphome
