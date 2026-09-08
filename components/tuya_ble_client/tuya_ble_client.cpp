#include "tuya_ble_client.h"
#include "esp_random.h"

namespace esphome {
namespace tuya_ble_client {

static const char *const TAG = "tuya_ble_client";

void TuyaBLEClient::set_state(esp32_ble_tracker::ClientState st) {
  esp32_ble_client::BLEClientBase::set_state(st);
  
  switch(st) {
    case esp32_ble_tracker::ClientState::INIT:
      ESP_LOGD(TAG, "INIT");
      break;
    case esp32_ble_tracker::ClientState::DISCONNECTING:
      ESP_LOGD(TAG, "DISCONNECTING");
      break;
    case esp32_ble_tracker::ClientState::IDLE:
      ESP_LOGD(TAG, "IDLE");
      break;
    case esp32_ble_tracker::ClientState::DISCOVERED:
      ESP_LOGD(TAG, "DISCOVERED");
      break;
    case esp32_ble_tracker::ClientState::CONNECTING:
      ESP_LOGD(TAG, "CONNECTING");
      break;
    case esp32_ble_tracker::ClientState::CONNECTED:
      ESP_LOGD(TAG, "CONNECTED");
      break;
    case esp32_ble_tracker::ClientState::ESTABLISHED:
      ESP_LOGD(TAG, "ESTABLISHED");
      break;
    default:
      ESP_LOGD(TAG, "Unknown state");
      break;
  }
}

void TuyaBLEClient::encrypt_data(uint32_t seq_num, TuyaBLECode code, unsigned char *data, size_t size, unsigned char *encrypted_data, size_t encrypted_size, unsigned char *key, unsigned char *iv, uint32_t response_to, uint8_t security_flag) {

  size_t inflated_size = META_SIZE + size + CRC_SIZE + (AES_BLOCK_SIZE - ((META_SIZE + size + CRC_SIZE) % AES_BLOCK_SIZE)); // 12 bytes of meta data + size of data + 2 bytes crc + padding
  unsigned char raw[inflated_size]{0};

  if(inflated_size + sizeof(security_flag) + IV_SIZE > encrypted_size) {
    ESP_LOGE(TAG, "Not enough space allocated for encrypted data!");
    return;
  }

  memcpy(&raw[12], data, size);

  raw[0] = (seq_num >> 24) & 0xff;
  raw[1] = (seq_num >> 16) & 0xff;
  raw[2] = (seq_num >>  8) & 0xff;
  raw[3] = (seq_num >>  0) & 0xff;

  raw[4] = (response_to >> 24) & 0xff;
  raw[5] = (response_to >> 16) & 0xff;
  raw[6] = (response_to >>  8) & 0xff;
  raw[7] = (response_to >>  0) & 0xff;

  raw[8] = (code >> 8) & 0xff;
  raw[9] = (code >> 0) & 0xff;

  raw[10] = (size >> 8) & 0xff;
  raw[11] = (size >> 0) & 0xff;

  uint16_t crc = crc16(raw, size + 12);

  raw[12 + size] = (crc >> 8) & 0xff;
  raw[13 + size] = (crc >> 0) & 0xff;
  
  ESP_LOGV(TAG, "%s", binary_to_string(raw, inflated_size).c_str());
  
  encrypted_data[0] = (unsigned char)security_flag;
  memcpy(&encrypted_data[1], iv, IV_SIZE);

  esp_aes_context aes;
  esp_aes_init(&aes);
  esp_aes_setkey(&aes, (const unsigned char *) key, KEY_SIZE * 8);
  esp_aes_crypt_cbc(&aes, ESP_AES_ENCRYPT, sizeof(raw), iv, (uint8_t*)raw, (uint8_t*)&encrypted_data[sizeof(security_flag) + IV_SIZE]);
  esp_aes_free(&aes);
}

std::tuple<uint32_t, TuyaBLECode, size_t, uint32_t> TuyaBLEClient::decrypt_data(unsigned char *encrypted_data, size_t encrypted_size, unsigned char *data, size_t size, unsigned char *key, unsigned char *iv) {

  if(size % AES_BLOCK_SIZE != 0) {
    ESP_LOGE(TAG, "Size of data to decrypt needs to be a multiple of block size");
    return std::make_tuple(0, TuyaBLECode::FUN_SENDER_DEVICE_INFO, 0, 0);
  }

  uint32_t seq_num;
  TuyaBLECode code;
  size_t decrypted_size;
  uint32_t response_to;

  esp_aes_context aes;
  esp_aes_init(&aes);
  esp_aes_setkey(&aes, (const unsigned char *)key, KEY_SIZE * 8);
  esp_aes_crypt_cbc(&aes, ESP_AES_DECRYPT, size, iv, (uint8_t*)encrypted_data, (uint8_t*)data);
  esp_aes_free(&aes);

  seq_num = (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
  response_to = (data[4] << 24) + (data[5] << 16) + (data[6] << 8) + data[7];
  code = (TuyaBLECode)((data[8] << 8) + data[9]);
  decrypted_size = (data[10] << 8) + data[11];

  ESP_LOGV(TAG, "%s", binary_to_string(data, size).c_str());

  return std::make_tuple(seq_num, code, decrypted_size, response_to);
}

void TuyaBLEClient::write_to_char(esp32_ble_client::BLECharacteristic *write_char, unsigned char *encrypted_data, size_t encrypted_size) {
  
  ESP_LOGV(TAG, "%s", binary_to_string(encrypted_data, encrypted_size).c_str());
  
  size_t pack_size = GATT_MTU - 1;

  for(int i=0; i<ceil(encrypted_size / (double)pack_size); i++) {
    unsigned char packet[GATT_MTU]{0};
    size_t data_size = pack_size;

    packet[0] = i;
    if(i * pack_size + pack_size > encrypted_size) {
      data_size = encrypted_size - (i * pack_size);
    }
    memcpy(&packet[1], &encrypted_data[i*pack_size], data_size);
    
    auto status = write_char->write_value(packet, data_size + 1, ESP_GATT_WRITE_TYPE_RSP);
    if(status) {
      ESP_LOGW(TAG, "write_char->write_value failed, status=%d", status);
    }
  }
}

void TuyaBLEClient::write_data(TuyaBLECode code, uint32_t *seq_num, unsigned char *data, size_t size, unsigned char *key, uint32_t response_to, int protocol_version) {

  ESP_LOGV(TAG, "write_data: %s", binary_to_string(data, size).c_str());
  
  size_t encrypted_size = META_SIZE + size + CRC_SIZE + (AES_BLOCK_SIZE - ((META_SIZE + size + CRC_SIZE) % AES_BLOCK_SIZE)) + 1 + IV_SIZE;
  unsigned char encrypted_data[encrypted_size + 2]{0};
  uint8_t security_flag = Security::SESSION_KEY;
  if(protocol_version == 2) {
    security_flag = Security::LOGIN_KEY; // adjust security flag for some reason
  }

  unsigned char iv[IV_SIZE]{0};
  esp_fill_random(iv, IV_SIZE);
  encrypt_data(*seq_num, code, data, size, &encrypted_data[2], encrypted_size, key, (unsigned char *)iv, response_to, security_flag);

  encrypted_data[0] = (uint8_t)(encrypted_size);
  encrypted_data[1] = (protocol_version << 4);

  this->data_collection_state = DataCollectionState::EXPECTING;

  write_to_char(this->write_char, encrypted_data, encrypted_size + 2);

  (*seq_num)++;
}

void TuyaBLEClient::collect_data(unsigned char *data, size_t size) {

  ESP_LOGV(TAG, "%s", binary_to_string(data, size).c_str());

  size_t data_starts_at = 1;
  size_t concatenated_length = 0;

  // A malformed/truncated GATT notification (size 0-2) would otherwise read
  // data[0]/data[1]/data[2] out of bounds below - these accesses are
  // unconditional before any length check existed.
  if(size < 1) {
    ESP_LOGW(TAG, "Received empty BLE notification, ignoring");
    this->reset_rx_state();
    return;
  }

  if(data[0] != 0x00 && data[0] != this->data_collection_incrementor + 1) {
    ESP_LOGW(TAG, "Received data packet with incorrect incrementor. Expected %i, got %i. Data rejected.", this->data_collection_incrementor + 1, data[0]);
    this->reset_rx_state();
    return;
  }
  
  this->data_collection_incrementor = data[0];

  if(data[0] == 0x00) { // if new sequence of packets is received; reset (do not wait for potential existing sequence to finish)
    // Thresholds match the final data_starts_at this branch computes (3, or
    // 4 when the varint continuation byte is present), not just what's
    // needed to safely read data[1]/data[2] - a fragment that clears the
    // per-byte read but is still shorter than data_starts_at would underflow
    // `size - data_starts_at` below (size_t, so this wraps to a huge value).
    if(size < 3) {
      ESP_LOGW(TAG, "Initial BLE fragment too short, ignoring");
      this->reset_rx_state();
      return;
    }
    this->data_collection_state = DataCollectionState::COLLECTING;
    this->data_collection_expected_size = data[1];
    if(data[1] >= 128) {
      if(size < 4) {
        ESP_LOGW(TAG, "Initial BLE varint fragment too short, ignoring");
        this->reset_rx_state();
        return;
      }
      data_starts_at ++;
      if(data[2] > 1) {
        ESP_LOGE(TAG, "Length (%i) of received data above 255. No code written to handle this!", data[1]);
        // TODO: Instead of this error message, the int from data[1] should be read as variable length and handled accordingly
      }
    }
    data_starts_at += 2;

    // resize(), not reserve(): the vector's size() must actually cover
    // data_collection_expected_size before we write into it via operator[]
    // below, or every write past index 0 is undefined behavior even though
    // the underlying allocation happens to be large enough.
    this->data_collected.clear();
    this->data_collected.resize(this->data_collection_expected_size);
  }
  else {
    concatenated_length = this->data_collection_incrementor * 19 - 2;
    if(this->data_collection_expected_size > 127) {
      concatenated_length --;
    }
  }

  // Belt-and-suspenders on top of the size checks above: catches any future
  // header-format change that under-guards data_starts_at, and prevents the
  // size_t underflow on the next line from ever being computed.
  if(size < data_starts_at) {
    ESP_LOGW(TAG, "BLE fragment shorter than its own header, ignoring");
    this->reset_rx_state();
    return;
  }

  if(concatenated_length + (size - data_starts_at) > this->data_collection_expected_size) {
    ESP_LOGW(TAG, "Not enough space allocated for received data!");
    this->reset_rx_state();
    return;
  }
  memcpy(&this->data_collected[concatenated_length], &data[data_starts_at], size - data_starts_at);
  concatenated_length += (size - data_starts_at);
  ESP_LOGV(TAG, "Collected %i/%i", concatenated_length, this->data_collection_expected_size);
  if(concatenated_length >= this->data_collection_expected_size)
  {
    ESP_LOGD(TAG, "Data collected!");
    this->data_collection_state = DataCollectionState::COLLECTED;
  }
}

void TuyaBLEClient::reset_rx_state() {
  this->data_collected.clear();
  this->data_collection_state = DataCollectionState::NO_DATA;
}

void TuyaBLEClient::process_data(TYBLENode *node) {
  if(this->data_collection_state != DataCollectionState::COLLECTED || this->data_collection_expected_size <= IV_SIZE + 1) { // Should be a multiple of 16 as well?
    ESP_LOGW(TAG, "Attempt to process received data aborted");
    this->reset_rx_state();
    return;
  }

  uint8_t security_flag = (uint8_t)this->data_collected[0];

  unsigned char *key;
  if(security_flag == Security::LOGIN_KEY) {
    key = node->login_key;
  }
  else if(security_flag == Security::AUTH_KEY) {
    // AUTH_KEY-secured frames aren't supported: there's no auth_key material
    // anywhere in this component to decrypt them with. Bail out cleanly
    // instead of falling through to decrypt_data() with an uninitialized
    // key pointer.
    ESP_LOGW(TAG, "Received AUTH_KEY-secured frame, which isn't supported. Dropping it.");
    this->reset_rx_state();
    return;
  }
  else if(security_flag == Security::SESSION_KEY) {
    key = node->session_key;
  }
  else {
    ESP_LOGW(TAG, "Received frame with unrecognized security_flag=0x%02x. Dropping it.", security_flag);
    this->reset_rx_state();
    return;
  }

  unsigned char first_decrypted_part[AES_BLOCK_SIZE]{0};
  const size_t data_size_first_block = AES_BLOCK_SIZE - META_SIZE;
  size_t start_pos = sizeof(security_flag) + IV_SIZE;
  uint32_t seq_num;
  TuyaBLECode code;
  size_t decrypted_size;
  uint32_t response_to;

  // decrypt_data()'s 4th arg (AES_BLOCK_SIZE here) is what actually drives
  // esp_aes_crypt_cbc's read length from data_collected - not the
  // encrypted_size passed as the 2nd arg, which the function never uses.
  // The top-of-function guard above only requires expected_size > IV_SIZE+1
  // (i.e. >17), which is not enough to guarantee a full AES block is
  // actually present past start_pos - an expected_size of e.g. 20 would
  // pass that guard and then read 16 bytes from data_collected[17], well
  // past the vector's real size(). Check the real requirement here instead.
  if(this->data_collection_expected_size < start_pos + AES_BLOCK_SIZE) {
    ESP_LOGW(TAG, "Encrypted frame too short to contain one AES block (expected_size=%u, need >= %u). Dropping it.",
             this->data_collection_expected_size, (unsigned)(start_pos + AES_BLOCK_SIZE));
    this->reset_rx_state();
    return;
  }

  // Decrypt first AES_BLOCK_SIZE (16 bytes), to get the meta data (Meta data is only 12 bytes long though, so there might be up to 4 bytes of data in this block as well):
  std::tie(seq_num, code, decrypted_size, response_to) = decrypt_data(&this->data_collected[start_pos], this->data_collection_expected_size - start_pos, first_decrypted_part, AES_BLOCK_SIZE, key, &this->data_collected[1]);
  
  if(code == TuyaBLECode::FUN_SENDER_DEVICE_INFO) { // Don't need the full 84 bytes of data
    decrypted_size = 12; // Only need the srand, set to 30 to get the auth_key as well
  }

  // decrypted_size comes straight from the (possibly-garbage, if the key is
  // wrong or the frame is corrupted) decrypted bytes and directly sizes a
  // stack buffer below. A bogus large value here would blow the task's
  // stack instead of just failing to parse - a real risk while debugging a
  // not-yet-correct local_key, which is exactly when this is most likely to
  // happen. No real Tuya BLE DP frame is anywhere near this size.
  constexpr size_t MAX_PLAUSIBLE_DECRYPTED_SIZE = 512;

  // Separately, decrypted_size also drives how many bytes the second
  // decrypt_data() call below reads out of data_collected - and it reads
  // that many bytes regardless of how much ciphertext is actually left
  // there (esp_aes_crypt_cbc's length comes from the destination buffer
  // size, not from what's actually available at the source). A decrypted
  // frame is only trustworthy up to however many real bytes we collected
  // over BLE, so clamp against that too, not just the fixed cap above -
  // otherwise a wrong key can still cause an out-of-bounds read of
  // data_collected even while staying under MAX_PLAUSIBLE_DECRYPTED_SIZE.
  size_t max_size_from_received_data = data_size_first_block;
  if(this->data_collection_expected_size > start_pos + AES_BLOCK_SIZE) {
    max_size_from_received_data += this->data_collection_expected_size - start_pos - AES_BLOCK_SIZE;
  }

  if(decrypted_size > MAX_PLAUSIBLE_DECRYPTED_SIZE || decrypted_size > max_size_from_received_data) {
    ESP_LOGW(TAG, "Decrypted frame claims size %u but only %u bytes were actually received (code=0x%04X) - likely a wrong local_key or corrupted frame. Dropping it.",
             decrypted_size, max_size_from_received_data, code);
    this->reset_rx_state();
    return;
  }

  if(decrypted_size > 0) {
    size_t blocks = (decrypted_size - data_size_first_block) / AES_BLOCK_SIZE + ((decrypted_size - data_size_first_block) % AES_BLOCK_SIZE > 0); // Size needs to be a multiple of AES_BLOCK_SIZE
    if(decrypted_size <= data_size_first_block) {
      blocks = 0;
    }
    unsigned char decrypted_data[blocks * AES_BLOCK_SIZE + data_size_first_block]{0};

    memcpy(decrypted_data, &first_decrypted_part[META_SIZE], std::min((size_t)data_size_first_block, decrypted_size)); // Copy over existing data from first decrypted part (up to 4 bytes of the end)

    if(decrypted_size > data_size_first_block) { // if there's also data past the first decrypted part, add this as well
      decrypt_data(&this->data_collected[start_pos + AES_BLOCK_SIZE], this->data_collection_expected_size - start_pos - AES_BLOCK_SIZE, &decrypted_data[data_size_first_block], blocks * AES_BLOCK_SIZE, key, &this->data_collected[1]);
    }
  
    switch(code) {
      case TuyaBLECode::FUN_SENDER_DEVICE_INFO:
        {
          if(decrypted_size < 12) {
            ESP_LOGD(TAG, "DEVICE INFO response too short");
            this->reset_rx_state();
            return;
          }
          // Stack-allocated: this runs on every reconnect now that polling
          // exists, so a `new` here with no matching `delete` (as upstream
          // had it) would leak 16+ bytes of heap on every single poll cycle.
          MD5Digest md5digest;

          md5digest.init();
          md5digest.add(node->local_key, 6);
          md5digest.add(&decrypted_data[6], 6);
          md5digest.calculate();
          md5digest.get_bytes(&node->session_key[0]);
          ESP_LOGD(TAG, "Session key set!");
          // Not logging the session key itself, even at VERBOSE - it's
          // derived from the secret local_key and just as sensitive.
        }
        break;

      case TuyaBLECode::FUN_SENDER_PAIR:
        {
          if(decrypted_size < 1) {
            ESP_LOGD(TAG, "PAIR response too short");
            this->reset_rx_state();
            return;
          }
          if(decrypted_data[0] == 0 || decrypted_data[0] == 2) { // Pair success or already paired
            node->is_paired = true;
            ESP_LOGD(TAG, "Paired succesfully!");
          }
        }
        break;

      case TuyaBLECode::FUN_RECEIVE_DP:
        node->handle_dp_frame(decrypted_data, decrypted_size);
        break;

      default:
        ESP_LOGD(TAG, "Unhandled code 0x%04X (%u bytes), ignoring", code, decrypted_size);
        break;
    }
  }
  
  this->reset_rx_state();
}

bool TuyaBLEClient::register_for_notifications() {
  this->notification_char = this->get_characteristic(esp32_ble_tracker::ESPBTUUID::from_raw(uuid_info_service), esp32_ble_tracker::ESPBTUUID::from_raw(uuid_notification_char));
  this->write_char = this->get_characteristic(esp32_ble_tracker::ESPBTUUID::from_raw(uuid_info_service), esp32_ble_tracker::ESPBTUUID::from_raw(uuid_write_char));

  if(this->notification_char == nullptr || this->write_char == nullptr) {
    ESP_LOGE(TAG, "Could not find Tuya BLE info service characteristics! notification=%p write=%p", this->notification_char, this->write_char);
    return false;
  }

  ESP_LOGD(TAG, "notification_char handle=0x%x properties=0x%x, write_char handle=0x%x properties=0x%x",
           this->notification_char->handle, this->notification_char->properties, this->write_char->handle, this->write_char->properties);

  ESP_LOGD(TAG, "Listen for notifications");
  esp_err_t status = esp_ble_gattc_register_for_notify(this->get_gattc_if(), this->get_remote_bda(), this->notification_char->handle);
  if(status) {
    ESP_LOGW(TAG, "[%d] [%s] esp_ble_gattc_register_for_notify failed, status=%d", this->get_conn_id(), this->address_str_, status);
  }

  // esp_ble_gattc_register_for_notify() only tells the local BLE stack to accept notifications;
  // the peer still needs its Client Characteristic Configuration Descriptor (0x2902) written to
  // actually start sending them. Without this, the device never pushes any DP data. The caller
  // must wait for ESP_GATTC_WRITE_DESCR_EVT before sending anything encrypted: sending the first
  // request before the peer has actually enabled notifications is a race that can silently drop
  // the response.
  auto *cccd = this->notification_char->get_descriptor(esp32_ble_tracker::ESPBTUUID::from_uint16(0x2902));
  if(cccd == nullptr) {
    ESP_LOGW(TAG, "No CCCD (0x2902) descriptor found on notification characteristic; device may never send notifications");
    return false;
  }

  uint16_t notify_enable = 1;
  esp_err_t descr_status = esp_ble_gattc_write_char_descr(this->get_gattc_if(), this->get_conn_id(), cccd->handle,
                                                           sizeof(notify_enable), (uint8_t *) &notify_enable,
                                                           ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if(descr_status) {
    ESP_LOGW(TAG, "[%d] [%s] esp_ble_gattc_write_char_descr (CCCD) failed, status=%d", this->get_conn_id(), this->address_str_, descr_status);
    return false;
  }

  this->cccd_handle_ = cccd->handle;
  return true;
}

void TuyaBLEClient::register_node(uint64_t mac_address, TYBLENode *tuyaBLENode) {

  if(mac_address == 0) {
    ESP_LOGE(TAG, "Attempted to register node with mac address 00:00:00:00:00:00");
    return;
  }

  this->nodes.insert(std::make_pair(mac_address, tuyaBLENode));
  
  ESP_LOGD(TAG, "Added: %llX from config", mac_address);
}

void TuyaBLEClient::set_disconnect_after(uint16_t disconnect_after) {

  this->disconnect_after = disconnect_after;
}

bool TuyaBLEClient::has_node(uint64_t mac_address) {
  return this->nodes.count(mac_address) > 0;
}

TYBLENode *TuyaBLEClient::get_node(uint64_t mac_address) {
  return this->nodes[mac_address];
}

void TuyaBLEClient::on_shutdown() {
}

bool TuyaBLEClient::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
  ESP_LOGV(TAG, "[%d] [%s] gattc_event_handler: event=%d gattc_if=%d", this->connection_index_, this->address_str_, event, gattc_if);

  if (!esp32_ble_client::BLEClientBase::gattc_event_handler(event, gattc_if, param))
    return false;
  
  uint64_t mac_address = this->get_address();

  if(!this->has_node(mac_address)) {
    return true;
  }
  TYBLENode *node = this->get_node(mac_address);

  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGD(TAG, "Disconnected!");

      if(node->has_session_key()) {
        this->set_address(0);
      }
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
    case ESP_GATTC_OPEN_EVT: {
      if(esp32_ble_client::BLEClientBase::state() == esp32_ble_tracker::ClientState::ESTABLISHED) {
        if(!node->has_session_key()) {
          this->should_disconnect = false;
          node->seq_num = 1;
          if(!this->register_for_notifications() && !this->device_info_requested_) {
            // No CCCD to wait for (or it failed) - nothing will confirm
            // notifications are enabled, so send the request immediately
            // as before rather than waiting forever.
            this->device_info_requested_ = true;
            node->request_info();
          }
          // else: wait for ESP_GATTC_WRITE_DESCR_EVT below.
        }
      }
      break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT: {
      if(param->write.handle == this->cccd_handle_ && !node->has_session_key() && !this->device_info_requested_) {
        // Some BLE stacks deliver this event more than once for the same
        // write (observed on real hardware); device_info_requested_ makes
        // sure we only ever act on the first one per connection.
        this->device_info_requested_ = true;
        if(param->write.status == ESP_GATT_OK) {
          ESP_LOGD(TAG, "CCCD write confirmed, notifications are enabled");
        } else {
          ESP_LOGW(TAG, "CCCD write failed with status=%d, requesting device info anyway", param->write.status);
        }
        node->request_info();
      }
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      ESP_LOGV(TAG, "Notification received!");
      this->collect_data(param->notify.value, param->notify.value_len);

      if(this->data_collection_state == DataCollectionState::COLLECTED) {

        this->process_data(node);
        
        if(node->has_session_key()) {
          if(!node->is_paired && node->uuid.size() > 0 && node->device_id.size() > 0) {
            node->pair();
          }
          else if(node->has_dp_listeners() && !node->dp_status_requested) {
            node->request_status();
          }
          else if(!node->has_command()) {
            this->disconnect_when_appropriate();
          }
        }
      }
      break;
    }
  }
  return true;
}

void TuyaBLEClient::connect_mac_address(const uint64_t mac_address) {
  ESP_LOGV(TAG, "Connecting to %llu", mac_address);

  TYBLENode *node = this->get_node(mac_address);

  this->set_address(mac_address);
  this->set_state(esp32_ble_tracker::ClientState::DISCOVERED);
  this->remote_bda_[0] = (mac_address >> 40) & 0xFF;
  this->remote_bda_[1] = (mac_address >> 32) & 0xFF;
  this->remote_bda_[2] = (mac_address >> 24) & 0xFF;
  this->remote_bda_[3] = (mac_address >> 16) & 0xFF;
  this->remote_bda_[4] = (mac_address >> 8) & 0xFF;
  this->remote_bda_[5] = (mac_address >> 0) & 0xFF;
  this->remote_addr_type_ = BLE_ADDR_TYPE_PUBLIC;

  node->reset_session_key(); // New session key every new connection?
  this->device_info_requested_ = false;
}

void TuyaBLEClient::disconnect_when_appropriate() {
  this->should_disconnect = true;
  this->should_disconnect_timer = esphome::millis() + this->disconnect_after;
}

void TuyaBLEClient::disconnect_check() {
  ESP_LOGV(TAG, "disconnect_check. should_disconnect: %i, data_collection_state: %i, should_disconnect_timer: %i, millis: %i", this->should_disconnect, this->data_collection_state, this->should_disconnect_timer, esphome::millis());
  if(this->should_disconnect && this->data_collection_state == DataCollectionState::NO_DATA && esphome::millis() > this->should_disconnect_timer) {
    this->disconnect();
    this->should_disconnect = false;
  }
}

void TuyaBLEClient::set_disconnect_callback(std::function<void()> &&f) { this->disconnect_callback = std::move(f); }

void TuyaBLEClient::loop() {
  uint64_t address = this->get_address();

  if(address != 0) {
    if(this->has_node(address)) {
      TYBLENode *node = this->get_node(address);

      if(!node->has_command()) {
        this->disconnect_check();
      }

      if(node->has_session_key()) {
        // Prevent continuous reconnecting
        if(esp32_ble_client::BLEClientBase::state() == esp32_ble_tracker::ClientState::IDLE && !node->has_command()) { // TODO: OR when session_key is expired
          return;
        }

        // Check if commands are queued for this node, and issue if needed:
        if(this->connected() && node->has_command()) {
          node->issue_command();
        }
      }
    }
  }
  else if(this->nodes.size() > 0) {
    // nodes_i is initialized to nodes.begin() while the map is still empty
    // (i.e. it's effectively nodes.end() until the first node is
    // registered, and register_node() never updates it). Incrementing an
    // iterator that's already at end() - which the old
    // `nodes_i = next(nodes_i); if (nodes_i == end()) ...` order did on
    // every first call - is undefined behavior, so check for end() before
    // advancing, not after.
    if(this->nodes_i == this->nodes.end()) {
      this->nodes_i = this->nodes.begin();
    } else {
      ++this->nodes_i;
      if(this->nodes_i == this->nodes.end()) {
        this->nodes_i = this->nodes.begin();
      }
    }
    if(this->nodes_i->first != 0) {
      if(this->nodes_i->second->has_command() && this->nodes_i->second->has_session_key()) {
        this->should_disconnect = false;
        this->connect_mac_address(this->nodes_i->first);
      }
    }
  }
  esp32_ble_client::BLEClientBase::loop();
}

}  // namespace tuya_ble_client
}  // namespace esphome
