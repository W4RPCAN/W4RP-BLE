#include "W4RPBLE.h"

#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <Preferences.h>
#include <driver/twai.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <algorithm>
#include <cmath>
#include <set>

#define FW_VERSION_DEFAULT "0.5.0"
#define HW_MODEL_DEFAULT "esp32c3-mini-1"
// Defaults - can be overridden via setPins/setCanTiming
#define DEFAULT_CAN_TX_PIN GPIO_NUM_21
#define DEFAULT_CAN_RX_PIN GPIO_NUM_20
#define DEFAULT_LED_PIN 8

#define W4RP_SERVICE_UUID "0000fff0-5734-5250-5734-525000000000"
#define W4RP_RX_UUID "0000fff1-5734-5250-5734-525000000000"
#define W4RP_TX_UUID "0000fff2-5734-5250-5734-525000000000"
#define W4RP_STATUS_UUID "0000fff3-5734-5250-5734-525000000000"

#define JSON_CAPACITY 16384
#define MAX_SIGNALS 128
#define MAX_NODES 64
#define MAX_FLOWS 32

// Debugging macros
#define LOG_TAG(tag, fmt, ...)                                                 \
  Serial.printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define LOG_BLE(fmt, ...) LOG_TAG("BLE", fmt, ##__VA_ARGS__)
#define LOG_CAN(fmt, ...) LOG_TAG("CAN", fmt, ##__VA_ARGS__)
#define LOG_NVS(fmt, ...) LOG_TAG("NVS", fmt, ##__VA_ARGS__)
#define LOG_SYS(fmt, ...) LOG_TAG("SYS", fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) LOG_TAG("ERR", fmt, ##__VA_ARGS__)

static const char *NVS_NS = "w4rp";
static const char *NVS_KEY_CURRENT = "rules_current";
static const char *NVS_KEY_BACKUP = "rules_backup";

struct Signal {
  char id[32];
  char key[16];
  uint32_t can_id;
  uint16_t start_bit;
  uint8_t bit_length;
  bool big_endian;
  float factor;
  float offset;
  float min_value;
  float max_value;
  float value;
  float last_value;
  uint32_t last_update_ms;
  bool ever_set;
  float last_debug_value;
};

enum Operation {
  OP_EQ,
  OP_NE,
  OP_GT,
  OP_GE,
  OP_LT,
  OP_LE,
  OP_WITHIN,
  OP_OUTSIDE,
  OP_HOLD
};

enum NodeType { NODE_CONDITION, NODE_ACTION };

struct NodeConfig {
  uint8_t signal_idx;
  Operation operation;
  float value;
  float value2;
  uint32_t hold_ms;
  char capability_id[32];
  std::map<String, String> params;
  uint32_t hold_start_ms;
  bool hold_active;
};

struct Node {
  char id[32];
  NodeType type;
  char name[64];
  bool is_root;
  std::vector<uint8_t> wires;
  NodeConfig config;
  bool last_result;
  uint32_t last_evaluation_ms;
  bool last_debug_result;
};

struct Flow {
  char id[32];
  std::vector<uint8_t> root_node_indices;
  uint16_t debounce_ms;
  uint16_t cooldown_ms;
  uint32_t last_trigger_ms;
  uint32_t last_condition_change_ms;
  bool last_condition_state;
};

static uint32_t crc32_ieee(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
  }
  return ~crc;
}

/**
 * @brief Streaming writer for BLE that chunks data and calculates CRC on the
 * fly.
 *
 * This class implements the Print interface, allowing ArduinoJson to serialize
 * directly to BLE without holding the entire JSON in memory. Data is buffered
 * and sent in 180-byte chunks. CRC32 is calculated incrementally.
 */
class BleStreamWriter : public Print {
public:
  static constexpr size_t CHUNK_SIZE = 180;

  BleStreamWriter(BLECharacteristic *tx_char)
      : tx_char_(tx_char), total_bytes_(0), crc_(0xFFFFFFFF) {
    buffer_.reserve(CHUNK_SIZE);
  }

  size_t write(uint8_t c) override {
    buffer_.push_back(c);
    total_bytes_++;
    // Update CRC incrementally
    crc_ ^= c;
    for (int j = 0; j < 8; j++) {
      crc_ = (crc_ >> 1) ^ (0xEDB88320 & (-(crc_ & 1)));
    }
    if (buffer_.size() >= CHUNK_SIZE) {
      flush();
    }
    return 1;
  }

  size_t write(const uint8_t *buf, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      write(buf[i]);
    }
    return size;
  }

  void flush() {
    if (buffer_.empty() || !tx_char_)
      return;
    tx_char_->setValue(buffer_.data(), buffer_.size());
    tx_char_->notify();
    buffer_.clear();
    delay(3); // Small delay for BLE stack
  }

  void finalize() {
    // Flush remaining buffer
    flush();
  }

  size_t totalBytes() const { return total_bytes_; }
  uint32_t crc32() const { return ~crc_; }

private:
  BLECharacteristic *tx_char_;
  std::vector<uint8_t> buffer_;
  size_t total_bytes_;
  uint32_t crc_;
};

struct W4RPBLE::ImplState {
  std::vector<Signal> signals;
  std::vector<Node> nodes;
  std::vector<Flow> flows;
  std::map<String, uint8_t> signal_id_to_idx;
  std::map<String, uint8_t> node_id_to_idx;

  BLECharacteristic *rx_char = nullptr;
  BLECharacteristic *tx_char = nullptr;
  BLECharacteristic *status_char = nullptr;
  bool client_connected = false;

  BLEServer *server = nullptr;
  BLEAdvertising *advertising = nullptr;

  bool led_blink_state = false;
  uint32_t last_led_blink_ms = 0;

  bool adv_verification_pending = false;
  uint32_t adv_verification_deadline_ms = 0;
  uint8_t adv_restart_attempts = 0;
  bool adv_error_state = false;
  bool adv_needs_deep_reset = false;
  uint32_t last_disconnect_ms = 0;
  uint8_t consecutive_failures = 0;
  uint32_t last_successful_connect_ms = 0;
  bool ble_stack_healthy = true;

  bool can_started = false;
  uint32_t frames_received = 0;
  uint32_t flows_triggered = 0;

  char module_id[32] = "W4RP-XXXX";
  String module_id_override;

  String hw_model = HW_MODEL_DEFAULT;
  String fw_version = FW_VERSION_DEFAULT;
  String serial;
  String device_name;
  String ble_name_override;

  String last_ruleset_json;
  String ruleset_dialect;
  uint32_t ruleset_crc32 = 0;
  String ruleset_last_update;

  std::vector<uint8_t> stream_buffer;
  bool stream_active = false;
  size_t stream_expected_len = 0;
  uint32_t stream_expected_crc = 0;
  bool stream_is_persistent = false;

  Preferences prefs;

  std::map<String, W4RPBLE::CapabilityHandler> capability_handlers;
  std::map<String, W4RPBLE::CapabilityMeta> capability_meta;

  uint32_t last_status_update_ms = 0;
  uint32_t last_health_check_ms = 0;

  bool debug_mode = false;
  uint32_t last_debug_update_ms = 0;
  std::vector<Signal> debug_signals;
  bool stream_is_debug_watch = false;

  // Hardware Config
  gpio_num_t pin_can_tx = (gpio_num_t)DEFAULT_CAN_TX_PIN;
  gpio_num_t pin_can_rx = (gpio_num_t)DEFAULT_CAN_RX_PIN;
  gpio_num_t pin_led = (gpio_num_t)DEFAULT_LED_PIN;
  twai_timing_config_t can_timing = TWAI_TIMING_CONFIG_500KBITS();
  W4RPBLE::CanMode can_mode = W4RPBLE::CanMode::NORMAL;
};

static W4RPBLE *s_instance = nullptr;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    if (s_instance) {
      // Optimize connection parameters for iOS/Android (Latency vs Throughput)
      // Min Interval: 6 (7.5ms)
      // Max Interval: 12 (15ms)
      // Latency: 0
      // Timeout: 400 (4000ms)
      auto connId = server->getConnId();
      server->updateConnParams(connId, 6, 12, 0, 400);

      s_instance->onBleConnect(server);
    }
  }
  void onDisconnect(BLEServer *server) override {
    if (s_instance)
      s_instance->onBleDisconnect(server);
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (s_instance)
      s_instance->onBleWrite(characteristic);
  }
};

static bool float_eq(float a, float b) { return fabsf(a - b) < 0.001f; }

static uint64_t extract_bits(const uint8_t data[8], uint16_t start, uint8_t len,
                             bool big_endian) {
  if (len == 0 || len > 64)
    return 0;

  uint64_t result = 0;

  if (!big_endian) {
    for (uint8_t i = 0; i < len; i++) {
      uint16_t bit_pos = start + i;
      uint8_t byte_idx = bit_pos / 8;
      uint8_t bit_idx = bit_pos % 8;
      if (byte_idx < 8) {
        uint8_t bit = (data[byte_idx] >> bit_idx) & 1;
        result |= ((uint64_t)bit << i);
      }
    }
  } else {
    for (uint8_t i = 0; i < len; i++) {
      int bit_pos = start - i;
      if (bit_pos < 0 || bit_pos >= 64)
        continue;
      uint8_t byte_idx = bit_pos / 8;
      uint8_t bit_idx = bit_pos % 8;
      uint8_t bit = (data[byte_idx] >> bit_idx) & 1;
      result = (result << 1) | bit;
    }
  }

  return result;
}

static float decode_signal(const Signal &sig, const uint8_t data[8]) {
  uint64_t raw =
      extract_bits(data, sig.start_bit, sig.bit_length, sig.big_endian);
  return (float)raw * sig.factor + sig.offset;
}

static Operation parse_operation(const char *op_str) {
  if (!op_str)
    return OP_EQ;
  if (strcmp(op_str, "==") == 0)
    return OP_EQ;
  if (strcmp(op_str, "!=") == 0)
    return OP_NE;
  if (strcmp(op_str, ">") == 0)
    return OP_GT;
  if (strcmp(op_str, ">=") == 0)
    return OP_GE;
  if (strcmp(op_str, "<") == 0)
    return OP_LT;
  if (strcmp(op_str, "<=") == 0)
    return OP_LE;
  if (strcmp(op_str, "within") == 0)
    return OP_WITHIN;
  if (strcmp(op_str, "outside") == 0)
    return OP_OUTSIDE;
  if (strcmp(op_str, "hold") == 0)
    return OP_HOLD;
  return OP_EQ;
}

W4RPBLE::W4RPBLE() { impl_ = new ImplState(); }

const char *W4RPBLE::getModuleId() const { return impl_->module_id; }

const char *W4RPBLE::getFwVersion() const { return impl_->fw_version.c_str(); }

void W4RPBLE::setModuleHardware(const String &hw) { impl_->hw_model = hw; }

void W4RPBLE::setModuleFirmware(const String &fw) { impl_->fw_version = fw; }

void W4RPBLE::setModuleSerial(const String &serial) { impl_->serial = serial; }

void W4RPBLE::setModuleIdOverride(const String &id) {
  impl_->module_id_override = id;
}

void W4RPBLE::setBleName(const String &name) {
  impl_->ble_name_override = name;
}

void W4RPBLE::setPins(int8_t can_tx, int8_t can_rx, int8_t led) {
  if (impl_->can_started) {
    LOG_ERR("Cannot change pins after begin()");
    return;
  }
  impl_->pin_can_tx = (gpio_num_t)can_tx;
  impl_->pin_can_rx = (gpio_num_t)can_rx;
  impl_->pin_led = (gpio_num_t)led;
}

void W4RPBLE::setCanTiming(const twai_timing_config_t &config) {
  if (impl_->can_started) {
    LOG_ERR("Cannot change CAN timing after begin()");
    return;
  }
  impl_->can_timing = config;
}

void W4RPBLE::setCanMode(CanMode mode) {
  if (impl_->can_started) {
    LOG_ERR("Cannot change CAN mode after begin()");
    return;
  }
  impl_->can_mode = mode;
}

bool W4RPBLE::nvsWrite(const char *key, const String &value) {
  impl_->prefs.begin(NVS_NS, false);
  bool ok = impl_->prefs.putString(key, value) > 0;
  impl_->prefs.end();
  return ok;
}

String W4RPBLE::nvsRead(const char *key) {
  impl_->prefs.begin(NVS_NS, true);
  String val = impl_->prefs.getString(key, "");
  impl_->prefs.end();
  return val;
}

void W4RPBLE::blinkLed(uint8_t times, uint16_t ms) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(impl_->pin_led, LOW);
    delay(ms);
    digitalWrite(impl_->pin_led, HIGH);
    delay(ms);
  }
}

void W4RPBLE::initCan() {
  // Convert CanMode enum to TWAI mode constant
  twai_mode_t twai_mode;
  const char *mode_str;
  switch (impl_->can_mode) {
  case CanMode::LISTEN_ONLY:
    twai_mode = TWAI_MODE_LISTEN_ONLY;
    mode_str = "LISTEN_ONLY";
    break;
  case CanMode::NO_ACK:
    twai_mode = TWAI_MODE_NO_ACK;
    mode_str = "NO_ACK";
    break;
  case CanMode::NORMAL:
  default:
    twai_mode = TWAI_MODE_NORMAL;
    mode_str = "NORMAL";
    break;
  }

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      impl_->pin_can_tx, impl_->pin_can_rx, twai_mode);
  twai_timing_config_t t_config = impl_->can_timing;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    LOG_ERR("CAN Driver install failed");
    impl_->can_started = false;
    return;
  }

  if (twai_start() != ESP_OK) {
    LOG_ERR("CAN Start failed");
    twai_driver_uninstall();
    impl_->can_started = false;
    return;
  }

  impl_->can_started = true;
  LOG_CAN("Started @ 500kbps, Mode: %s", mode_str);
}

static bool can_receive_internal(bool can_started, uint32_t &id,
                                 uint8_t data[8], uint8_t &dlc) {
  if (!can_started)
    return false;

  twai_message_t msg;
  if (twai_receive(&msg, 0) == ESP_OK) {
    id = msg.identifier;
    dlc = msg.data_length_code;
    memcpy(data, msg.data, dlc);
    return true;
  }
  return false;
}

static bool evaluate_condition_node(Node &node, std::vector<Signal> &signals,
                                    uint32_t now_ms) {
  if (node.type != NODE_CONDITION)
    return false;

  NodeConfig &cfg = node.config;
  if (cfg.signal_idx >= signals.size())
    return false;

  Signal &sig = signals[cfg.signal_idx];
  if (!sig.ever_set)
    return false;

  float val = sig.value;

  if (cfg.operation == OP_HOLD) {
    bool active = !float_eq(val, 0.0f);

    if (active) {
      if (!cfg.hold_active) {
        cfg.hold_active = true;
        cfg.hold_start_ms = now_ms;
      }
      uint32_t held_for = now_ms - cfg.hold_start_ms;
      return held_for >= cfg.hold_ms;
    } else {
      cfg.hold_active = false;
      cfg.hold_start_ms = 0;
      return false;
    }
  }

  switch (cfg.operation) {
  case OP_EQ:
    return float_eq(val, cfg.value);
  case OP_NE:
    return !float_eq(val, cfg.value);
  case OP_GT:
    return val > cfg.value;
  case OP_GE:
    return val > cfg.value || float_eq(val, cfg.value);
  case OP_LT:
    return val < cfg.value;
  case OP_LE:
    return val < cfg.value || float_eq(val, cfg.value);
  case OP_WITHIN:
    return val >= cfg.value && val <= cfg.value2;
  case OP_OUTSIDE:
    return val < cfg.value || val > cfg.value2;
  default:
    return false;
  }
}

static void
execute_action_node(const Node &node,
                    std::map<String, W4RPBLE::CapabilityHandler> &handlers) {
  if (node.type != NODE_ACTION)
    return;

  const char *cap_id = node.config.capability_id;
  if (!cap_id || !cap_id[0])
    return;

  auto it = handlers.find(String(cap_id));
  if (it == handlers.end()) {
    Serial.printf("[ACTION] Unknown capability: %s\n", cap_id);
    return;
  }

  it->second(node.config.params);
}

static bool traverse_flow_graph(
    uint8_t node_idx, std::vector<Node> &nodes, std::vector<Signal> &signals,
    std::map<String, W4RPBLE::CapabilityHandler> &handlers, uint32_t now_ms) {
  if (node_idx >= nodes.size())
    return false;

  Node &node = nodes[node_idx];

  if (node.type == NODE_CONDITION) {
    bool result = evaluate_condition_node(node, signals, now_ms);
    node.last_result = result;
    node.last_evaluation_ms = now_ms;

    if (!result)
      return false;

    bool any_success = false;
    for (uint8_t next_idx : node.wires) {
      if (traverse_flow_graph(next_idx, nodes, signals, handlers, now_ms)) {
        any_success = true;
      }
    }
    return any_success;
  }

  if (node.type == NODE_ACTION) {
    execute_action_node(node, handlers);
    for (uint8_t next_idx : node.wires) {
      traverse_flow_graph(next_idx, nodes, signals, handlers, now_ms);
    }
    return true;
  }

  return false;
}

void W4RPBLE::evaluateFlowsInternal() {
  uint32_t now_ms = millis();

  for (Flow &flow : impl_->flows) {
    if (flow.root_node_indices.empty())
      continue;

    bool any_root_true = false;

    for (uint8_t root_idx : flow.root_node_indices) {
      if (root_idx >= impl_->nodes.size())
        continue;

      Node &root = impl_->nodes[root_idx];
      if (root.type != NODE_CONDITION)
        continue;

      if (evaluate_condition_node(root, impl_->signals, now_ms)) {
        any_root_true = true;
        break;
      }
    }

    if (any_root_true != flow.last_condition_state) {
      flow.last_condition_state = any_root_true;
      flow.last_condition_change_ms = now_ms;
    }

    if (!any_root_true) {
      continue;
    }

    bool debounced =
        (now_ms - flow.last_condition_change_ms) >= flow.debounce_ms;
    bool cooldown_passed = (now_ms - flow.last_trigger_ms) >= flow.cooldown_ms;

    if (!debounced || !cooldown_passed) {
      continue;
    }

    bool any_triggered = false;
    for (uint8_t root_idx : flow.root_node_indices) {
      if (root_idx >= impl_->nodes.size())
        continue;
      if (traverse_flow_graph(root_idx, impl_->nodes, impl_->signals,
                              impl_->capability_handlers, now_ms)) {
        any_triggered = true;
      }
    }

    if (any_triggered) {
      flow.last_trigger_ms = now_ms;
      impl_->flows_triggered++;
    }
  }
}

bool W4RPBLE::applyRuleset(JsonDocument &doc) {
  impl_->signals.clear();
  impl_->nodes.clear();
  impl_->flows.clear();
  impl_->signal_id_to_idx.clear();
  impl_->node_id_to_idx.clear();

  JsonArray signals_arr = doc["signals"];
  if (!signals_arr) {
    LOG_ERR("Parse: Missing signals array");
    return false;
  }

  for (JsonObject sig_obj : signals_arr) {
    Signal sig = {};
    String id_str = sig_obj["id"].as<String>();
    strncpy(sig.id, id_str.c_str(), sizeof(sig.id) - 1);

    strncpy(sig.key, sig_obj["key"] | "", sizeof(sig.key) - 1);

    sig.can_id = sig_obj["can_id"] | 0;
    sig.start_bit = sig_obj["start"] | 0;
    sig.bit_length = sig_obj["len"] | 0;
    sig.big_endian = sig_obj["be"] | true;
    sig.factor = sig_obj["factor"] | 1.0f;
    sig.offset = sig_obj["offset"] | 0.0f;

    JsonVariant minVar = sig_obj["min"];
    JsonVariant maxVar = sig_obj["max"];

    if (!minVar.isNull()) {
      sig.min_value = minVar.as<float>();
    } else {
      sig.min_value = NAN;
    }

    if (!maxVar.isNull()) {
      sig.max_value = maxVar.as<float>();
    } else {
      sig.max_value = NAN;
    }

    sig.value = 0.0f;
    sig.last_value = 0.0f;
    sig.last_update_ms = 0;
    sig.ever_set = false;
    sig.last_debug_value = 0.0f;

    impl_->signal_id_to_idx[String(sig.id)] = impl_->signals.size();
    impl_->signals.push_back(sig);
  }

  JsonArray nodes_arr = doc["nodes"];
  if (!nodes_arr) {
    LOG_ERR("Parse: Missing nodes array");
    return false;
  }

  for (JsonObject node_obj : nodes_arr) {
    Node node = {};
    strncpy(node.id, node_obj["id"] | "", sizeof(node.id) - 1);
    strncpy(node.name, node_obj["name"] | "", sizeof(node.name) - 1);

    const char *type_str = node_obj["type"] | "condition";

    if (strcmp(type_str, "action") == 0) {
      node.type = NODE_ACTION;
    } else {
      node.type = NODE_CONDITION;
    }

    node.is_root = node_obj["root"] | false;

    JsonObject cfg = node_obj["config"];
    node.config.signal_idx = 255;
    node.config.hold_ms = 0;
    node.config.hold_active = false;
    node.config.hold_start_ms = 0;
    node.config.value = 0.0f;
    node.config.value2 = 0.0f;
    node.config.capability_id[0] = '\0';

    if (node.type == NODE_CONDITION) {
      const char *sig_id = cfg["signal_id"];
      if (sig_id) {
        auto it = impl_->signal_id_to_idx.find(String(sig_id));
        if (it != impl_->signal_id_to_idx.end()) {
          node.config.signal_idx = it->second;
        }
      }

      const char *op_str = cfg["operation"];
      node.config.operation = parse_operation(op_str);

      bool is_range = false;
      float v1 = 0.0f;
      float v2 = 0.0f;

      if (node.config.operation == OP_HOLD) {
        node.config.hold_ms = cfg["value"] | 0;
        node.config.value = 0.0f;
        node.config.value2 = 0.0f;
      } else {
        if (cfg["value"].is<JsonArray>()) {
          JsonArray range = cfg["value"];
          v1 = range[0] | 0.0f;
          v2 = range[1] | 0.0f;
          if (v2 < v1)
            std::swap(v1, v2);
          node.config.value = v1;
          node.config.value2 = v2;
          is_range = true;
        } else {
          v1 = cfg["value"] | 0.0f;
          v2 = v1;
          node.config.value = v1;
          node.config.value2 = 0.0f;
          is_range = false;
        }
      }

      if (node.config.operation != OP_HOLD &&
          node.config.signal_idx < impl_->signals.size()) {

        const Signal &sig = impl_->signals[node.config.signal_idx];
        bool out_of_range = false;

        if (!isnan(sig.min_value)) {
          if (v1 < sig.min_value)
            out_of_range = true;
        }
        if (!isnan(sig.max_value)) {
          if (v2 > sig.max_value)
            out_of_range = true;
        }

        if (out_of_range) {
          LOG_ERR("Condition %s out of [%f, %f], disabling", node.id,
                  sig.min_value, sig.max_value);
          node.config.signal_idx = 255;
        }
      }

    } else {
      const char *cap_id = cfg["capability_id"];
      if (cap_id) {
        strncpy(node.config.capability_id, cap_id,
                sizeof(node.config.capability_id) - 1);
        node.config.capability_id[sizeof(node.config.capability_id) - 1] = '\0';
      }

      JsonArray params_arr = cfg["params"];
      if (params_arr) {
        for (JsonObject param : params_arr) {
          const char *key = param["key"];
          const char *val = param["value"];
          if (key && val) {
            node.config.params[String(key)] = String(val);
          }
        }
      }
    }

    node.last_result = false;
    node.last_evaluation_ms = 0;
    node.last_debug_result = false;

    impl_->node_id_to_idx[String(node.id)] = impl_->nodes.size();
    impl_->nodes.push_back(node);
  }

  {
    uint8_t node_idx = 0;
    for (JsonObject node_obj : nodes_arr) {
      JsonArray wires_arr = node_obj["wires"];
      if (wires_arr) {
        for (const char *wire_id : wires_arr) {
          auto it = impl_->node_id_to_idx.find(String(wire_id));
          if (it != impl_->node_id_to_idx.end()) {
            impl_->nodes[node_idx].wires.push_back(it->second);
          }
        }
      }
      node_idx++;
    }
  }

  JsonArray flows_arr = doc["flows"];
  if (!flows_arr) {
    LOG_ERR("Parse: Missing flows array");
    return false;
  }

  for (JsonObject flow_obj : flows_arr) {
    Flow flow = {};
    strncpy(flow.id, flow_obj["id"] | "", sizeof(flow.id) - 1);

    flow.root_node_indices.clear();

    JsonVariant root_var = flow_obj["root"];
    if (root_var.is<const char *>()) {
      const char *root_id = root_var.as<const char *>();
      auto it = impl_->node_id_to_idx.find(String(root_id));
      if (it != impl_->node_id_to_idx.end()) {
        flow.root_node_indices.push_back(it->second);
      } else {
        LOG_ERR("Parse: Flow %s root not found: %s", flow.id, root_id);
      }
    } else if (root_var.is<JsonArray>()) {
      JsonArray roots_arr = root_var.as<JsonArray>();
      for (const char *root_id : roots_arr) {
        auto it = impl_->node_id_to_idx.find(String(root_id));
        if (it != impl_->node_id_to_idx.end()) {
          flow.root_node_indices.push_back(it->second);
        } else {
          Serial.printf("[PARSE] Flow %s root not found: %s\n", flow.id,
                        root_id);
        }
      }
    } else {
      Serial.printf("[PARSE] Flow %s has invalid root field\n", flow.id);
    }

    if (flow.root_node_indices.empty()) {
      Serial.printf("[PARSE] Flow %s has no valid roots, skipped\n", flow.id);
      continue;
    }

    flow.debounce_ms = flow_obj["debounce_ms"] | 0;
    flow.cooldown_ms = flow_obj["cooldown_ms"] | 0;
    flow.last_trigger_ms = 0;
    flow.last_condition_change_ms = 0;
    flow.last_condition_state = false;

    impl_->flows.push_back(flow);
  }

  impl_->ruleset_dialect = doc["dialect"] | "unknown";

  JsonObject meta = doc["meta"];
  if (meta && meta.containsKey("updated_at")) {
    impl_->ruleset_last_update = meta["updated_at"].as<String>();
  } else {
    impl_->ruleset_last_update = "2025-11-15T00:00:00Z";
  }

  LOG_SYS("Applied rules: %u signals, %u nodes, %u flows",
          impl_->signals.size(), impl_->nodes.size(), impl_->flows.size());

  return true;
}

void W4RPBLE::sendModuleProfile() {
  if (!impl_->tx_char) {
    LOG_BLE("No TX characteristic");
    return;
  }

  // Build the JSON document (still needed for structure, but we'll stream it
  // out)
  DynamicJsonDocument doc(JSON_CAPACITY);

  JsonObject module = doc.createNestedObject("module");
  module["id"] = impl_->module_id;
  module["hw"] = impl_->hw_model;
  module["fw"] = impl_->fw_version;
  if (impl_->serial.length() > 0) {
    module["serial"] = impl_->serial;
  } else {
    module["serial"] = (const char *)nullptr;
  }

  JsonObject runtime = doc.createNestedObject("runtime");
  runtime["uptime_ms"] = millis();
  runtime["boot_count"] = 1;

  if (impl_->signals.size() == 0) {
    runtime["mode"] = "empty";
  } else if (impl_->last_ruleset_json.startsWith("{") &&
             impl_->last_ruleset_json.length() > 100) {
    String nvs_current = nvsRead(NVS_KEY_CURRENT);
    runtime["mode"] = (nvs_current == impl_->last_ruleset_json) ? "nvs" : "ram";
  } else {
    runtime["mode"] = "empty";
  }

  JsonObject rules = doc.createNestedObject("rules");
  rules["dialect"] = impl_->ruleset_dialect.length() > 0
                         ? impl_->ruleset_dialect.c_str()
                         : (const char *)nullptr;
  rules["crc32"] = impl_->ruleset_crc32;
  rules["last_update"] = impl_->ruleset_last_update.length() > 0
                             ? impl_->ruleset_last_update.c_str()
                             : (const char *)nullptr;

  if (impl_->last_ruleset_json.length() > 0) {
    DynamicJsonDocument ruleset_doc(JSON_CAPACITY);
    DeserializationError err =
        deserializeJson(ruleset_doc, impl_->last_ruleset_json);
    if (!err) {
      rules["data"] = ruleset_doc.as<JsonVariant>();
    }
  } else {
    rules["data"] = (const char *)nullptr;
  }

  JsonObject ble = doc.createNestedObject("ble");
  ble["connected"] = impl_->client_connected;
  ble["rssi"] = (int)nullptr;
  ble["mtu"] = 247;

  JsonObject limits = doc.createNestedObject("limits");
  limits["max_signals"] = MAX_SIGNALS;
  limits["max_nodes"] = MAX_NODES;
  limits["max_flows"] = MAX_FLOWS;

  JsonObject caps = doc.createNestedObject("capabilities");
  for (const auto &entry : impl_->capability_meta) {
    const CapabilityMeta &meta = entry.second;
    JsonObject cap_obj = caps.createNestedObject(meta.id);
    if (meta.label.length())
      cap_obj["label"] = meta.label;
    if (meta.description.length())
      cap_obj["description"] = meta.description;
    if (meta.category.length())
      cap_obj["category"] = meta.category;

    JsonArray params = cap_obj.createNestedArray("params");
    for (const auto &p : meta.params) {
      JsonObject param = params.createNestedObject();
      param["name"] = p.name;
      param["type"] = p.type;
      param["required"] = p.required;
      if (p.min != 0 || p.max != 0) {
        param["min"] = p.min;
        param["max"] = p.max;
      }
      if (p.description.length()) {
        param["description"] = p.description;
      }
    }
  }

  // --- Streaming Serialization with POST-CRC ---
  // Send BEGIN header (no length/crc yet, just a marker)
  const char *begin_marker = "BEGIN";
  impl_->tx_char->setValue((uint8_t *)begin_marker, strlen(begin_marker));
  impl_->tx_char->notify();
  delay(5);

  // Create streaming writer and serialize directly to BLE
  BleStreamWriter stream(impl_->tx_char);
  serializeJson(doc, stream);
  stream.finalize();

  // Send END with length and CRC (POST-CRC protocol)
  char footer[64];
  snprintf(footer, sizeof(footer), "END:%u:%lu",
           (unsigned int)stream.totalBytes(), (unsigned long)stream.crc32());

  delay(5);
  impl_->tx_char->setValue((uint8_t *)footer, strlen(footer));
  impl_->tx_char->notify();

  LOG_BLE("Streamed %u bytes, CRC32=0x%08lX", (unsigned int)stream.totalBytes(),
          (unsigned long)stream.crc32());
}

void W4RPBLE::sendStatusUpdate() {
  if (!impl_->status_char)
    return;

  StaticJsonDocument<512> doc;
  doc["module"] = impl_->module_id;
  doc["name"] = impl_->device_name;
  doc["hw"] = impl_->hw_model;
  doc["fw"] = impl_->fw_version;
  if (impl_->serial.length() > 0) {
    doc["serial"] = impl_->serial;
  }
  doc["uptime_ms"] = millis();

  if (impl_->signals.size() == 0) {
    doc["mode"] = "empty";
  } else {
    String nvs_current = nvsRead(NVS_KEY_CURRENT);
    doc["mode"] = (nvs_current == impl_->last_ruleset_json) ? "nvs" : "ram";
  }

  doc["rules"] = impl_->flows.size();
  doc["signals"] = impl_->signals.size();

  std::set<uint32_t> unique_ids;
  for (const Signal &sig : impl_->signals) {
    unique_ids.insert(sig.can_id);
  }
  doc["ids"] = unique_ids.size();

  String json_str;
  serializeJson(doc, json_str);

  impl_->status_char->setValue((uint8_t *)json_str.c_str(), json_str.length());
  impl_->status_char->notify();
}

void W4RPBLE::deepResetBle() {
  LOG_BLE("Performing DEEP BLE reset...");

  impl_->consecutive_failures++;

  if (impl_->advertising) {
    impl_->advertising->stop();
  }

  // Cleanup pointers (BLEDevice::deinit handles memory)
  impl_->server = nullptr;
  impl_->rx_char = nullptr;
  impl_->tx_char = nullptr;
  impl_->status_char = nullptr;
  impl_->advertising = nullptr;

  delay(200);

  BLEDevice::deinit(true);

  delay(500);

  // Re-init using standard initBle which creates new objects
  initBle();

  impl_->adv_restart_attempts = 0;
  impl_->adv_verification_pending = true;
  impl_->adv_verification_deadline_ms = millis() + 3000;
  impl_->adv_error_state = false;
  impl_->adv_needs_deep_reset = false;

  LOG_BLE("Deep reset complete, advertising restarted");
}

bool W4RPBLE::restartAdvertising() {
  if (!impl_->advertising || !impl_->server) {
    LOG_BLE("restartAdvertising: missing BLE instances");
    impl_->adv_needs_deep_reset = true;
    return false;
  }

  LOG_BLE("Advertising restart attempt #%u",
          (unsigned int)(impl_->adv_restart_attempts + 1));

  impl_->advertising->stop();
  delay(100);

  impl_->server->startAdvertising();

  impl_->adv_restart_attempts++;
  impl_->adv_verification_pending = true;
  impl_->adv_verification_deadline_ms = millis() + 3000;
  impl_->adv_error_state = false;

  return true;
}

void W4RPBLE::forceRestartAdvertising() {
  LOG_BLE("Force restart requested");

  impl_->adv_restart_attempts = 0;
  impl_->adv_error_state = false;
  impl_->adv_verification_pending = false;
  impl_->adv_verification_deadline_ms = 0;
  impl_->consecutive_failures = 0;
  impl_->adv_needs_deep_reset = false;

  uint32_t time_since_last_connect =
      millis() - impl_->last_successful_connect_ms;
  if (time_since_last_connect > 60000) {
    LOG_BLE("No connection for >60s, using deep reset");
    deepResetBle();
  } else {
    restartAdvertising();
  }
}

void W4RPBLE::verifyAdvertisingActive() {
  if (!impl_->adv_verification_pending) {
    if (impl_->adv_needs_deep_reset && !impl_->client_connected) {
      uint32_t time_since_disconnect = millis() - impl_->last_disconnect_ms;
      if (time_since_disconnect > 1000) {
        deepResetBle();
      }
    }
    return;
  }

  if (!impl_->advertising || !impl_->server) {
    LOG_BLE("Verification failed: missing BLE instances");
    impl_->adv_verification_pending = false;
    impl_->adv_needs_deep_reset = true;
    return;
  }

  if (impl_->client_connected) {
    impl_->adv_verification_pending = false;
    impl_->adv_restart_attempts = 0;
    impl_->adv_error_state = false;
    impl_->consecutive_failures = 0;
    return;
  }

  uint32_t now = millis();
  if ((int32_t)(now - impl_->adv_verification_deadline_ms) < 0) {
    return;
  }

  impl_->adv_verification_pending = false;

  bool isAdv = impl_->advertising->isAdvertising();

  if (isAdv) {
    LOG_BLE("Advertising verification OK");
    impl_->adv_restart_attempts = 0;
    impl_->adv_error_state = false;
    impl_->consecutive_failures = 0;
    return;
  }

  LOG_BLE("Advertising verification FAILED");

  if (impl_->adv_restart_attempts >= 3) {
    LOG_BLE("Max restart attempts reached, triggering deep reset");
    impl_->adv_error_state = true;
    impl_->adv_needs_deep_reset = true;
    return;
  }

  restartAdvertising();
}

void W4RPBLE::checkBleHealth() {
  if (impl_->client_connected)
    return;

  uint32_t now = millis();
  uint32_t time_since_disconnect = now - impl_->last_disconnect_ms;

  if (time_since_disconnect > 120000 && !impl_->adv_verification_pending) {
    LOG_BLE("Health check: long disconnection, triggering deep reset");
    deepResetBle();
    impl_->last_disconnect_ms = now;
  }
}

void W4RPBLE::onBleConnect(BLEServer *server) {
  impl_->client_connected = true;
  impl_->last_successful_connect_ms = millis();

  impl_->consecutive_failures = 0;
  impl_->ble_stack_healthy = true;

  digitalWrite(impl_->pin_led, HIGH);

  LOG_BLE("Client connected successfully");

  impl_->adv_verification_pending = false;
  impl_->adv_restart_attempts = 0;
  impl_->adv_error_state = false;
  impl_->adv_needs_deep_reset = false;

  delay(100);
  sendStatusUpdate();
}

void W4RPBLE::onBleDisconnect(BLEServer *server) {
  impl_->client_connected = false;
  impl_->last_disconnect_ms = millis();

  LOG_BLE("Client disconnected");

  uint32_t connection_duration = millis() - impl_->last_successful_connect_ms;
  bool was_quick_disconnect = connection_duration < 5000;

  if (was_quick_disconnect) {
    LOG_BLE("Quick disconnect detected (%lu ms), potential issue",
            connection_duration);
    impl_->consecutive_failures++;
  } else {
    impl_->consecutive_failures = 0;
  }

  impl_->last_led_blink_ms = millis();
  impl_->led_blink_state = false;
  digitalWrite(impl_->pin_led, LOW);

  if (impl_->consecutive_failures >= 3 || impl_->adv_needs_deep_reset) {
    LOG_BLE("Multiple failures detected, performing deep reset");
    deepResetBle();
  } else {
    impl_->adv_error_state = false;
    impl_->adv_verification_pending = false;
    impl_->adv_verification_deadline_ms = 0;
    impl_->adv_restart_attempts = 0;

    delay(100);

    if (impl_->advertising) {
      impl_->advertising->stop();
      delay(10);
      impl_->advertising->addServiceUUID(W4RP_SERVICE_UUID);
      impl_->advertising->setScanResponse(true);
      impl_->advertising->setMinPreferred(
          0x06); // functions that help with iPhone connections issue
      impl_->advertising->setMaxPreferred(0x12);
      impl_->advertising->start();
      LOG_BLE("Advertising restarted with full config");
    } else {
      LOG_BLE("Missing advertising instance, needs deep reset");
      impl_->adv_needs_deep_reset = true;
    }
  }
}

void W4RPBLE::onBleWrite(BLECharacteristic *characteristic) {
  String packet = characteristic->getValue();
  if (!packet.length())
    return;

  if (packet == "GET:PROFILE") {
    LOG_BLE("CMD: GET:PROFILE");
    sendModuleProfile();
    return;
  }

  if (packet == "RESET:BLE") {
    LOG_BLE("Manual BLE reset requested");
    deepResetBle();
    return;
  }

  if (packet == "DEBUG:START") {
    impl_->debug_mode = true;
    LOG_BLE("Debug Mode STARTED");

    // Force immediate refresh of all signals and nodes
    for (Signal &sig : impl_->signals) {
      sig.last_debug_value = -999999.9f;
    }
    for (Node &node : impl_->nodes) {
      node.last_debug_result = !node.last_result;
    }
    return;
  }

  if (packet == "DEBUG:STOP") {
    impl_->debug_mode = false;
    impl_->debug_signals.clear();
    LOG_BLE("Debug Mode STOPPED");
    return;
  }

  if (packet.startsWith("DEBUG:WATCH:")) {
    // Format: DEBUG:WATCH:<len>:<crc>
    const int start = 12;
    int colon1 = packet.indexOf(':', start);

    if (colon1 < 0)
      return;

    String len_str = packet.substring(start, colon1);
    String crc_str = packet.substring(colon1 + 1);

    impl_->stream_expected_len = len_str.toInt();
    impl_->stream_expected_crc = strtoul(crc_str.c_str(), nullptr, 10);
    impl_->stream_is_persistent = false;
    impl_->stream_is_debug_watch = true;
    impl_->stream_buffer.clear();
    if (impl_->stream_expected_len > 0) {
      impl_->stream_buffer.reserve(impl_->stream_expected_len);
    }
    impl_->stream_active = true;
    LOG_BLE("DEBUG:WATCH started");
    return;
  }

  if (packet.startsWith("SET:RULES:")) {
    const int start = 10;

    int colon1 = packet.indexOf(':', start);
    int colon2 = (colon1 >= 0) ? packet.indexOf(':', colon1 + 1) : -1;
    int colon3 = (colon2 >= 0) ? packet.indexOf(':', colon2 + 1) : -1;

    if (colon1 < 0 || colon2 < 0) {
      LOG_ERR("Invalid SET:RULES header: %s", packet.c_str());
      return;
    }

    String mode = packet.substring(start, colon1);
    String len_str = packet.substring(colon1 + 1, colon2);
    String crc_str;

    if (colon3 < 0) {
      crc_str = packet.substring(colon2 + 1);
    } else {
      crc_str = packet.substring(colon2 + 1, colon3);
    }

    impl_->stream_expected_len = len_str.toInt();
    impl_->stream_expected_crc = strtoul(crc_str.c_str(), nullptr, 10);
    impl_->stream_is_persistent = (mode == "NVS");
    impl_->stream_buffer.clear();

    if (impl_->stream_expected_len > 0) {
      impl_->stream_buffer.reserve(impl_->stream_expected_len);
    }

    impl_->stream_active = true;

    LOG_BLE("SET:RULES:%s - expect %u bytes, CRC=0x%08lX", mode.c_str(),
            impl_->stream_expected_len,
            (unsigned long)impl_->stream_expected_crc);
    return;
  }

  if (impl_->stream_active && packet != "END") {
    const uint8_t *ptr = (const uint8_t *)packet.c_str();
    impl_->stream_buffer.insert(impl_->stream_buffer.end(), ptr,
                                ptr + packet.length());
    return;
  }

  if (impl_->stream_active && packet == "END") {
    impl_->stream_active = false;

    if (impl_->stream_buffer.size() != impl_->stream_expected_len) {
      LOG_ERR("Length mismatch: expected %u, got %u",
              impl_->stream_expected_len,
              (unsigned int)impl_->stream_buffer.size());
      impl_->stream_buffer.clear();
      return;
    }

    uint32_t actual_crc =
        crc32_ieee(impl_->stream_buffer.data(), impl_->stream_buffer.size());
    if (actual_crc != impl_->stream_expected_crc) {
      LOG_ERR("CRC mismatch: expected 0x%08lX, got 0x%08lX",
              (unsigned long)impl_->stream_expected_crc,
              (unsigned long)actual_crc);
      LOG_ERR("Buffer size: %u", (unsigned int)impl_->stream_buffer.size());
      impl_->stream_buffer.clear();
      return;
    }

    StaticJsonDocument<JSON_CAPACITY> doc;
    DeserializationError err = deserializeJson(doc, impl_->stream_buffer.data(),
                                               impl_->stream_buffer.size());

    if (err) {
      LOG_ERR("JSON parse error: %s", err.c_str());
      impl_->stream_buffer.clear();
      return;
    }

    if (doc.containsKey("persist")) {
      bool persist_flag = doc["persist"] | false;
      impl_->stream_is_persistent = persist_flag;
      LOG_NVS("Persist from JSON: %s", persist_flag ? "NVS" : "RAM");
    }

    if (impl_->stream_is_debug_watch) {
      impl_->debug_signals.clear();
      JsonArray signals_arr = doc["signals"];
      if (signals_arr) {
        for (JsonObject sig_obj : signals_arr) {
          Signal sig = {};
          String id_str = sig_obj["id"].as<String>();
          strncpy(sig.id, id_str.c_str(), sizeof(sig.id) - 1);
          strncpy(sig.key, sig_obj["key"] | "", sizeof(sig.key) - 1);
          sig.can_id = sig_obj["can_id"] | 0;
          sig.start_bit = sig_obj["start"] | 0;
          sig.bit_length = sig_obj["len"] | 0;
          sig.big_endian = sig_obj["be"] | true;
          sig.factor = sig_obj["factor"] | 1.0f;
          sig.offset = sig_obj["offset"] | 0.0f;
          sig.value = 0.0f;
          sig.last_value = 0.0f;
          sig.last_debug_value = -999999.9f;
          impl_->debug_signals.push_back(sig);
        }
      }
      LOG_BLE("Watching %u signals", impl_->debug_signals.size());
      impl_->stream_buffer.clear();
      impl_->stream_is_debug_watch = false;
      return;
    }

    if (!applyRuleset(doc)) {
      LOG_ERR("Failed to apply ruleset");
      impl_->stream_buffer.clear();
      return;
    }

    impl_->last_ruleset_json = String((const char *)impl_->stream_buffer.data(),
                                      impl_->stream_buffer.size());
    impl_->ruleset_crc32 = actual_crc;

    if (impl_->stream_is_persistent) {
      String current = nvsRead(NVS_KEY_CURRENT);
      if (current.length() > 0) {
        nvsWrite(NVS_KEY_BACKUP, current);
      }
      nvsWrite(NVS_KEY_CURRENT, impl_->last_ruleset_json);
      LOG_NVS("Ruleset persisted");
    } else {
      LOG_BLE("Ruleset applied (non-persistent)");
    }

    impl_->stream_buffer.clear();
    sendStatusUpdate();
    blinkLed(3, 100);
    LOG_BLE("Ruleset applied successfully");
    return;
  }

  LOG_ERR("Unknown command: %s", packet.c_str());
}

void W4RPBLE::initBle() {
  BLEDevice::init(impl_->device_name.c_str());
  BLEDevice::setMTU(247);

  impl_->server = BLEDevice::createServer();
  impl_->server->setCallbacks(new ServerCallbacks());

  BLEService *service = impl_->server->createService(W4RP_SERVICE_UUID);

  impl_->rx_char = service->createCharacteristic(
      W4RP_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  impl_->rx_char->setCallbacks(new RxCallbacks());

  impl_->tx_char = service->createCharacteristic(
      W4RP_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  impl_->tx_char->addDescriptor(new BLE2902());

  impl_->status_char = service->createCharacteristic(
      W4RP_STATUS_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  impl_->status_char->addDescriptor(new BLE2902());

  service->start();

  impl_->advertising = BLEDevice::getAdvertising();
  if (impl_->advertising) {
    impl_->advertising->addServiceUUID(W4RP_SERVICE_UUID);
    impl_->advertising->setScanResponse(true);
    impl_->advertising->setMinPreferred(0x06);
    impl_->advertising->setMaxPreferred(0x12);
    impl_->advertising->start();
  }

  LOG_BLE("Advertising started as '%s'", impl_->device_name.c_str());
}

void W4RPBLE::deriveModuleId() {
  if (impl_->module_id_override.length() > 0) {
    strncpy(impl_->module_id, impl_->module_id_override.c_str(),
            sizeof(impl_->module_id) - 1);
    impl_->module_id[sizeof(impl_->module_id) - 1] = '\0';
  } else {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(impl_->module_id, sizeof(impl_->module_id), "W4RP-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
  }
}

void W4RPBLE::loadRulesFromNvs() {
  String nvs_current = nvsRead(NVS_KEY_CURRENT);
  if (nvs_current.length() > 0) {
    LOG_NVS("Loading persisted ruleset...");

    DynamicJsonDocument doc(JSON_CAPACITY);
    DeserializationError err = deserializeJson(doc, nvs_current);

    if (!err && applyRuleset(doc)) {
      impl_->last_ruleset_json = nvs_current;
      impl_->ruleset_crc32 = crc32_ieee((const uint8_t *)nvs_current.c_str(),
                                        nvs_current.length());
      LOG_NVS("Loaded: %u signals, %u nodes, %u flows", impl_->signals.size(),
              impl_->nodes.size(), impl_->flows.size());
      blinkLed(2, 50);
      return;
    }

    LOG_ERR("Failed to load ruleset - trying backup");
    String nvs_backup = nvsRead(NVS_KEY_BACKUP);
    if (nvs_backup.length() > 0) {
      DynamicJsonDocument backup_doc(JSON_CAPACITY);
      DeserializationError backup_err = deserializeJson(backup_doc, nvs_backup);
      if (!backup_err && applyRuleset(backup_doc)) {
        impl_->last_ruleset_json = nvs_backup;
        impl_->ruleset_crc32 = crc32_ieee((const uint8_t *)nvs_backup.c_str(),
                                          nvs_backup.length());
        nvsWrite(NVS_KEY_CURRENT, nvs_backup);
        LOG_NVS("Backup restored successfully");
        blinkLed(3, 50);
        return;
      }
    }

    LOG_ERR("Backup also failed - starting empty");
  } else {
    LOG_NVS("No persisted ruleset - starting empty");
  }
}

void W4RPBLE::registerCapability(const String &id, CapabilityHandler handler) {
  impl_->capability_handlers[id] = handler;
}

void W4RPBLE::registerCapability(const CapabilityMeta &meta,
                                 CapabilityHandler handler) {
  impl_->capability_handlers[meta.id] = handler;
  impl_->capability_meta[meta.id] = meta;
}

void W4RPBLE::begin() {
  s_instance = this;

  delay(500);

  pinMode(impl_->pin_led, OUTPUT);
  digitalWrite(impl_->pin_led, LOW);
  impl_->led_blink_state = false;
  impl_->last_led_blink_ms = millis();

  impl_->advertising = nullptr;
  impl_->server = nullptr;
  impl_->adv_verification_pending = false;
  impl_->adv_verification_deadline_ms = 0;
  impl_->adv_restart_attempts = 0;
  impl_->adv_error_state = false;
  impl_->adv_needs_deep_reset = false;
  impl_->last_disconnect_ms = 0;
  impl_->consecutive_failures = 0;
  impl_->last_successful_connect_ms = 0;
  impl_->ble_stack_healthy = true;

  deriveModuleId();

  if (impl_->ble_name_override.length() > 0) {
    impl_->device_name = impl_->ble_name_override;
  } else {
    impl_->device_name = String(impl_->module_id);
  }

  LOG_SYS("W4RP Firmware Setup");
  LOG_SYS("HW: %s | FW: %s", impl_->hw_model.c_str(),
          impl_->fw_version.c_str());
  LOG_SYS("Module ID: %s", impl_->module_id);
  LOG_SYS("BLE Name: %s", impl_->device_name.c_str());

  {
    CapabilityMeta logMeta;
    logMeta.id = "log";
    logMeta.label = "Log";
    logMeta.description = "Emit a log entry when rule fires";
    logMeta.category = "debug";

    CapabilityParamMeta logParam;
    logParam.name = "msg";
    logParam.type = "string";
    logParam.required = true;
    logParam.description = "Text message";
    logMeta.params.push_back(logParam);

    registerCapability(logMeta, [](const ParamMap &params) {
      auto it = params.find("msg");
      if (it == params.end())
        return;
      LOG_TAG("LOG", "%s", it->second.c_str());
    });
  }

  initBle();
  initCan();
  loadRulesFromNvs();

  impl_->last_status_update_ms = 0;
  impl_->last_health_check_ms = millis();

  LOG_SYS("Ready");
}

void W4RPBLE::processCan() {
  uint32_t now = millis();

  for (int i = 0; i < 16; i++) {
    uint32_t can_id;
    uint8_t data[8];
    uint8_t dlc;

    if (!can_receive_internal(impl_->can_started, can_id, data, dlc))
      break;

    impl_->frames_received++;

    for (Signal &sig : impl_->signals) {
      if (sig.can_id == can_id) {
        sig.last_value = sig.value;
        sig.value = decode_signal(sig, data);
        sig.last_update_ms = now;
        sig.ever_set = true;
      }
    }

    for (Signal &sig : impl_->debug_signals) {
      if (sig.can_id == can_id) {
        sig.last_value = sig.value;
        sig.value = decode_signal(sig, data);
        sig.last_update_ms = now;
        sig.ever_set = true;
      }
    }
  }
}

void W4RPBLE::sendStatusIfNeeded() {
  uint32_t now = millis();
  if (impl_->client_connected && (now - impl_->last_status_update_ms > 5000)) {
    sendStatusUpdate();
    impl_->last_status_update_ms = now;
  }
}

void W4RPBLE::sendDebugUpdates() {
  if (!impl_->debug_mode || !impl_->client_connected || !impl_->tx_char)
    return;

  uint32_t now = millis();
  if (now - impl_->last_debug_update_ms < 300) // Max ~3Hz (Reliability > Speed)
    return;

  impl_->last_debug_update_ms = now;

  int updates_sent = 0;
  const int MAX_UPDATES_PER_LOOP = 20; // Allow more updates per cycle

  // Send Signal Updates
  for (Signal &sig : impl_->signals) {
    if (updates_sent >= MAX_UPDATES_PER_LOOP)
      break;

    if (fabsf(sig.value - sig.last_debug_value) > 0.01f) {
      char buf[64];
      // Format: D:S:<id>:<value>
      snprintf(buf, sizeof(buf), "D:S:%s:%.2f", sig.id, sig.value);
      impl_->tx_char->setValue((uint8_t *)buf, strlen(buf));
      impl_->tx_char->notify();
      sig.last_debug_value = sig.value;
      delay(10); // Increased delay to prevent congestion
      updates_sent++;
    }
  }

  // Send Node Updates
  for (Node &node : impl_->nodes) {
    if (updates_sent >= MAX_UPDATES_PER_LOOP)
      break;

    if (node.last_result != node.last_debug_result) {
      char buf[64];
      // Format: D:N:<id>:<1|0>
      snprintf(buf, sizeof(buf), "D:N:%s:%d", node.id,
               node.last_result ? 1 : 0);
      impl_->tx_char->setValue((uint8_t *)buf, strlen(buf));
      impl_->tx_char->notify();
      node.last_debug_result = node.last_result;
      delay(10);
      updates_sent++;
    }
  }

  // Send Debug Signal Updates
  for (Signal &sig : impl_->debug_signals) {
    if (updates_sent >= MAX_UPDATES_PER_LOOP)
      break;
    if (fabsf(sig.value - sig.last_debug_value) > 0.01f) {
      char buf[64];
      snprintf(buf, sizeof(buf), "D:S:%s:%.2f", sig.id, sig.value);
      impl_->tx_char->setValue((uint8_t *)buf, strlen(buf));
      impl_->tx_char->notify();
      sig.last_debug_value = sig.value;
      delay(10);
      updates_sent++;
    }
  }
}

void W4RPBLE::loop() {
  processCan();

  if (!impl_->flows.empty()) {
    evaluateFlowsInternal();
  }

  sendDebugUpdates();
  sendStatusIfNeeded();
  verifyAdvertisingActive();

  uint32_t now = millis();
  if (now - impl_->last_health_check_ms > 10000) {
    checkBleHealth();
    impl_->last_health_check_ms = now;
  }

  const uint32_t BLINK_INTERVAL_OK_MS = 500;
  const uint32_t BLINK_INTERVAL_ERROR_MS = 100;

  if (impl_->client_connected) {
    digitalWrite(impl_->pin_led, HIGH);
  } else {
    uint32_t interval =
        impl_->adv_error_state ? BLINK_INTERVAL_ERROR_MS : BLINK_INTERVAL_OK_MS;

    if (now - impl_->last_led_blink_ms >= interval) {
      impl_->led_blink_state = !impl_->led_blink_state;
      digitalWrite(impl_->pin_led, impl_->led_blink_state ? HIGH : LOW);
      impl_->last_led_blink_ms = now;
    }
  }

  delay(1);
}