#include "common.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tuya_ble {

static const char *const TAG = "tuya_ble_dp";

  std::string binary_to_string(unsigned char *data, size_t size) {
    std::stringstream buffer;
    for(int i=0; i<size; i++) {
      buffer << std::hex << std::setfill('0');
      buffer << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    std::string hexString = buffer.str();
    buffer.clear();
    return hexString;
  }

  // Base-class virtual stubs: TYBLENode/TYBLEClient are only ever instantiated as their
  // concrete subclasses (TuyaBLENode/TuyaBLEClient), which override all of these. But the
  // Itanium C++ ABI still needs each declared-non-pure-virtual to have a body *somewhere* to
  // emit the base class's vtable (the "key function" rule) - without it, linking fails with
  // "undefined reference to vtable for ...", even though these bodies are never actually called.
  bool TYBLENode::has_command() { return false; }
  bool TYBLENode::has_session_key() { return false; }
  void TYBLENode::issue_command() {}
  void TYBLENode::pair() {}
  void TYBLENode::request_info() {}
  void TYBLENode::reset_session_key() {}
  void TYBLENode::toggle(bool value) {}
  void TYBLENode::request_status() {}

  TYBLENode *TYBLEClient::get_node(uint64_t mac_address) { return nullptr; }
  void TYBLEClient::connect_mac_address(const uint64_t mac_address) {}
  void TYBLEClient::set_disconnect_callback(std::function<void()> &&f) {}
  bool TYBLEClient::parse_device(const esp32_ble_tracker::ESPBTDevice &device) { return false; }
  void TYBLEClient::write_data(TuyaBLECode code, uint32_t *seq_num, unsigned char *data, size_t size, unsigned char *key, uint32_t response_to, int protocol_version) {}

  void TYBLENode::register_dp_callback(uint8_t dp_id, std::function<void(uint8_t, unsigned char *, size_t)> &&callback) {
    this->dp_callbacks[dp_id] = std::move(callback);
  }

  void TYBLENode::handle_dp_frame(unsigned char *data, size_t size) {
    size_t offset = 0;
    while(offset + 3 <= size) {
      uint8_t dp_id = data[offset];
      uint8_t dp_type = data[offset + 1];
      uint8_t dp_len = data[offset + 2];
      offset += 3;

      if(offset + dp_len > size) {
        ESP_LOGW(TAG, "DP%u claims length %u, exceeds remaining frame data. Dropping rest of frame.", dp_id, dp_len);
        break;
      }

      ESP_LOGD(TAG, "DP%u type=%u len=%u raw=%s", dp_id, dp_type, dp_len, binary_to_string(&data[offset], dp_len).c_str());

      auto it = this->dp_callbacks.find(dp_id);
      if(it != this->dp_callbacks.end()) {
        it->second(dp_type, &data[offset], dp_len);
      }

      offset += dp_len;
    }
  }

}  // namespace tuya_ble
}  // namespace esphome
