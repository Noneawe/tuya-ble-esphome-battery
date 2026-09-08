#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <iomanip>
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

namespace esphome {
namespace tuya_ble {

#define KEY_SIZE 0x10
#define IV_SIZE 0x10
#define AES_BLOCK_SIZE 0x10
#define META_SIZE 0x0C
#define CRC_SIZE 0x02
#define GATT_MTU 0x14

// Tuya DP (datapoint) TLV wire types: dp_id(1) + dp_type(1) + dp_len(1) + value(dp_len)
enum TuyaBLEDataType {
  DP_TYPE_RAW = 0x00,
  DP_TYPE_BOOL = 0x01,
  DP_TYPE_VALUE = 0x02,
  DP_TYPE_STRING = 0x03,
  DP_TYPE_ENUM = 0x04,
  DP_TYPE_BITMAP = 0x05,
};

enum TuyaBLECode {
  FUN_SENDER_DEVICE_INFO = 0x0000,
  FUN_SENDER_PAIR = 0x0001,
  FUN_SENDER_DPS = 0x0002,
  FUN_SENDER_DEVICE_STATUS = 0x0003,

  FUN_SENDER_UNBIND = 0x0005,
  FUN_SENDER_DEVICE_RESET = 0x0006,

  FUN_SENDER_OTA_START = 0x000C,
  FUN_SENDER_OTA_FILE = 0x000D,
  FUN_SENDER_OTA_OFFSET = 0x000E,
  FUN_SENDER_OTA_UPGRADE = 0x000F,
  FUN_SENDER_OTA_OVER = 0x0010,

  FUN_SENDER_DPS_V4 = 0x0027,

  FUN_RECEIVE_DP = 0x8001,
  FUN_RECEIVE_TIME_DP = 0x8003,
  FUN_RECEIVE_SIGN_DP = 0x8004,
  FUN_RECEIVE_SIGN_TIME_DP = 0x8005,

  FUN_RECEIVE_DP_V4 = 0x8006,
  FUN_RECEIVE_TIME_DP_V4 = 0x8007,

  FUN_RECEIVE_TIME1_REQ = 0x8011,
  FUN_RECEIVE_TIME2_REQ = 0x8012
};

enum Security {
  AUTH_KEY = 0x01,
  LOGIN_KEY = 0x04,
  SESSION_KEY = 0x05,
};

struct TYBLECommand {
  TuyaBLECode code;
  std::vector<unsigned char> data;
  unsigned char *key;
  uint32_t response_to;
  int protocol_version;
};

class TYBLENode {
  public:
    std::string device_id;
    unsigned char local_key[6];
    unsigned char login_key[16];
    unsigned char session_key[16];
    // There's supposedly also an auth_key, but since it's not used, it's not declared either
    std::string uuid;
    bool is_paired = false;
    uint32_t seq_num;
    uint32_t last_detected;
    int rssi;

    virtual bool has_command();
    virtual bool has_session_key();
    virtual void issue_command();
    virtual void pair();
    virtual void request_info();
    virtual void reset_session_key();
    virtual void toggle(bool value);

    // Register a callback for a given DP id; invoked with (dp_type, raw value bytes, value length)
    // whenever the device reports that DP over BLE.
    void register_dp_callback(uint8_t dp_id, std::function<void(uint8_t, unsigned char *, size_t)> &&callback);

    // Parse a decrypted FUN_RECEIVE_DP payload (repeated dp_id(1) + dp_type(1) + dp_len(1) + value(dp_len))
    // and dispatch to any registered callback for each DP found.
    void handle_dp_frame(unsigned char *data, size_t size);

    bool has_dp_listeners() { return !this->dp_callbacks.empty(); }

    // Set once request_status() has been issued for the current connection; reset on reset_session_key().
    bool dp_status_requested = false;

    // Ask the device to report all of its current DP values (needed since, unlike an outgoing
    // command, a plain sensor read has nothing to enqueue that would otherwise keep the
    // connection open long enough to receive a FUN_RECEIVE_DP push).
    virtual void request_status();

  protected:
    std::map<uint8_t, std::function<void(uint8_t, unsigned char *, size_t)>> dp_callbacks;
};

class TYBLEClient {
  esp32_ble_tracker::ClientState state_;
  public:
    virtual TYBLENode *get_node(uint64_t mac_address);
    virtual bool has_node(uint64_t mac_address) = 0;
    virtual void connect_mac_address(const uint64_t mac_address);
    virtual void set_address(uint64_t address) = 0;
    virtual bool connected() { return this->state_ == esp32_ble_tracker::ClientState::ESTABLISHED; }
    virtual void disconnect() = 0;
    virtual void set_disconnect_callback(std::function<void()> &&f);
    virtual bool parse_device(const esp32_ble_tracker::ESPBTDevice &device);
    virtual void write_data(TuyaBLECode code, uint32_t *seq_num, unsigned char *data, size_t size, unsigned char *key, uint32_t response_to = 0, int protocol_version = 3);
    // Must be virtual: tuya_ble_tracker holds nodes by TYBLEClient*, and its
    // connection-timeout check calls state() through that base pointer. A
    // non-virtual state() here would always return this class's own never-
    // updated state_ member instead of dispatching to TuyaBLEClient's real
    // state, silently disabling that timeout.
    virtual esp32_ble_tracker::ClientState state() const { return state_; }
};

  std::string binary_to_string(unsigned char *data, size_t size);

}  // namespace tuya_ble
}  // namespace esphome
