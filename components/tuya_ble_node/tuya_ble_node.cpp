#include "tuya_ble_node.h"

namespace esphome {
namespace tuya_ble_node {

static const char *const TAG = "tuya_ble_node";

void TuyaBLENode::update() {
  if(!this->has_dp_listeners()) {
    // Nothing registered wants periodic DP data (e.g. an output-only node);
    // there's nothing to do here.
    return;
  }
  if(this->has_client && this->client->connected()) {
    // A BLE exchange (for this node or another one sharing the same
    // tuya_ble_client) is already in flight; don't interrupt it. It'll be
    // picked up again on the next update_interval tick.
    ESP_LOGD(TAG, "Skipping scheduled update, a BLE exchange is already in progress");
    return;
  }
  ESP_LOGD(TAG, "Scheduled update: will request a fresh DP status read");
  // Resetting the session key is enough: tuya_ble_tracker only initiates a
  // new connection for a node once has_session_key() is false, so this
  // alone triggers the whole connect -> pair -> request_status flow again
  // next time this device's advertisement is seen, without needing any
  // separate reconnect path.
  this->reset_session_key();
}

void TuyaBLENode::enqueue_command(TYBLECommand *command) {
  
  while(this->command_queue.size() >= this->max_queued) {
    this->command_queue.pop_back();
  }

  this->command_queue.push_front(*command);
  
  ESP_LOGV(TAG, "enqueue_command: %s", binary_to_string(&command->data[0], command->data.size()).c_str());
}

bool TuyaBLENode::has_command() {
  return this->command_queue.size() > 0;
}

bool TuyaBLENode::has_session_key() {
  return !std::all_of(this->session_key, this->session_key + KEY_SIZE, [](unsigned char x) { return x == '\0'; });
}

void TuyaBLENode::issue_command() {
  if(!this->has_client) {
    ESP_LOGW(TAG, "No client registered at node");
    return;
  }

  if(!this->has_command()) {
    ESP_LOGW(TAG, "No commands to issue");
  }

  TYBLECommand *command = &this->command_queue.back();
  ESP_LOGV(TAG, "issue_command: %s", binary_to_string(&command->data[0], command->data.size()).c_str());
  this->client->write_data(command->code, &this->seq_num, &command->data[0], command->data.size(), command->key, command->response_to, command->protocol_version);
  this->command_queue.pop_back();
}

void TuyaBLENode::set_device_id(std::string device_id) {
  this->device_id = device_id;
}

void TuyaBLENode::set_local_key(const char *local_key) {

  memcpy(this->local_key, local_key, 6);

  MD5Digest md5digest;

  md5digest.init();
  md5digest.add(local_key, 6);
  md5digest.calculate();
  md5digest.get_bytes(&this->login_key[0]);
  
  // Deliberately not logging local_key or the derived login_key, even at
  // VERBOSE: both are secrets, and this component's whole README is about
  // how much trouble a leaked/wrong one causes.
  ESP_LOGV(TAG, "local_key configured, login_key derived");
}

void TuyaBLENode::set_max_queued(uint8_t max) {
  this->max_queued = max;
}

void TuyaBLENode::set_uuid(std::string uuid) {
  this->uuid = uuid;
}

void TuyaBLENode::pair() {
  ESP_LOGD(TAG, "Pairing device...");

  // https://github.com/airy10/ha_tuya_ble/blob/LightStrip/custom_components/tuya_ble/tuya_ble/tuya_ble.py#L315
  size_t data_size = 44;
  size_t uuid_size = this->uuid.size();
  size_t device_id_size = this->device_id.size();
  unsigned char data[data_size]{0};

  if(device_id_size == 0 || uuid_size == 0) {
    ESP_LOGE(TAG, "Cannot pair if device_id and uuid are not set");
    return;
  }

  if(device_id_size + 6 + uuid_size > data_size) {
    ESP_LOGE(TAG, "Size of device_id + uuid is too big");
    return;
  }
  
  memcpy(data, this->uuid.c_str(), uuid_size);
  memcpy(&data[uuid_size], this->local_key, 6);
  memcpy(&data[uuid_size + 6], this->device_id.c_str(), device_id_size);

  this->client->write_data(TuyaBLECode::FUN_SENDER_PAIR, &this->seq_num, data, data_size, this->session_key);
}

void TuyaBLENode::request_info() {
  
  // About to get DEVICE_INFO, this should be limited to whenever session_key is unusable:
  if(!this->has_session_key()) { // TODO: OR when session_key is expired
    ESP_LOGD(TAG, "Requesting device info...");

    this->client->write_data(TuyaBLECode::FUN_SENDER_DEVICE_INFO, &this->seq_num, {0}, 0, this->login_key, 0, 2);
  }
}

void TuyaBLENode::request_status() {
  ESP_LOGD(TAG, "Requesting DP status...");

  this->dp_status_requested = true;
  this->client->write_data(TuyaBLECode::FUN_SENDER_DEVICE_STATUS, &this->seq_num, {0}, 0, this->session_key);
}

void TuyaBLENode::reset_session_key() {
  std::fill(this->session_key, this->session_key + KEY_SIZE, 0);
  this->dp_status_requested = false;
}

void TuyaBLENode::toggle(bool value) {

  if(!this->has_client) {
    ESP_LOGW(TAG, "No client registered at node");
    return;
  }

  TYBLECommand command = {
    TuyaBLECode::FUN_SENDER_DPS,
    { 0x14, 0x01, 0x01, (unsigned char)value },
    this->session_key, // Since we don't mind if session_key gets updated, we directly use the pointer to this node's session_key
    0,
    3,
  };

  this->enqueue_command(&command);
}

}  // namespace tuya_ble_node
}  // namespace esphome
