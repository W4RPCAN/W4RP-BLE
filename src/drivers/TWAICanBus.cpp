/**
 * @file TWAICanBus.cpp
 * @brief ESP32 TWAI CAN Bus driver implementation
 * @version 1.0.0
 * @see
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html
 */

#include "TWAICanBus.h"
#include <cstring>
#include <esp_log.h>

static const char *TAG = "TWAICanBus";

namespace W4RP {

namespace {
constexpr uint32_t DEFAULT_RX_QUEUE_LEN = 64;
constexpr uint32_t DEFAULT_TX_QUEUE_LEN = 16;
constexpr TickType_t DEFAULT_TX_TIMEOUT_MS = 100;
} // namespace

TWAICanBus::TWAICanBus(gpio_num_t txPin, gpio_num_t rxPin,
                       twai_timing_config_t timing, twai_mode_t mode)
    : txPin_(txPin), rxPin_(rxPin), timing_(timing), mode_(mode) {}

TWAICanBus::~TWAICanBus() { cleanup(); }

bool TWAICanBus::begin() {
  return begin(DEFAULT_RX_QUEUE_LEN, DEFAULT_TX_QUEUE_LEN);
}

bool TWAICanBus::begin(uint32_t rxQueueLen, uint32_t txQueueLen) {
  if (running_) {
    return true;
  }

  if (rxQueueLen == 0 || txQueueLen == 0) {
    ESP_LOGE(TAG, "Invalid queue lengths: RX=%lu, TX=%lu", rxQueueLen,
             txQueueLen);
    return false;
  }

  twai_general_config_t generalConfig =
      TWAI_GENERAL_CONFIG_DEFAULT(txPin_, rxPin_, mode_);
  generalConfig.rx_queue_len = rxQueueLen;
  generalConfig.tx_queue_len = txQueueLen;
  generalConfig.alerts_enabled = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                                 TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS |
                                 TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL;

  twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err;

  if (!installed_) {
    err = twai_driver_install(&generalConfig, &timing_, &filterConfig);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Driver install failed: %s", esp_err_to_name(err));
      return false;
    }
    installed_ = true;
  }

  err = twai_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Driver start failed: %s", esp_err_to_name(err));
    twai_driver_uninstall();
    installed_ = false;
    return false;
  }

  running_ = true;
  ESP_LOGI(TAG, "Started on TX=GPIO%d, RX=GPIO%d", txPin_, rxPin_);
  return true;
}

void TWAICanBus::stop() {
  if (!running_) {
    return;
  }

  esp_err_t err = twai_stop();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Stop failed: %s", esp_err_to_name(err));
    return;
  }

  running_ = false;
  ESP_LOGI(TAG, "Stopped");
}

void TWAICanBus::resume() {
  if (running_) {
    return;
  }

  if (!installed_) {
    begin();
    return;
  }

  esp_err_t err = twai_start();
  if (err == ESP_OK) {
    running_ = true;
    ESP_LOGI(TAG, "Resumed");
  } else {
    ESP_LOGE(TAG, "Resume failed: %s", esp_err_to_name(err));
  }
}

bool TWAICanBus::receive(CanFrame &frame) {
  if (!running_) {
    return false;
  }

  twai_message_t msg;
  esp_err_t err = twai_receive(&msg, 0);

  if (err != ESP_OK) {
    return false;
  }

  frame.id = msg.identifier;
  frame.dlc = msg.data_length_code;
  frame.extended = msg.extd;
  frame.rtr = msg.rtr;

  const uint8_t copyLen = (frame.dlc > 8) ? 8 : frame.dlc;
  std::memcpy(frame.data, msg.data, copyLen);

  return true;
}

bool TWAICanBus::transmit(const CanFrame &frame) {
  if (!running_) {
    return false;
  }

  if (frame.dlc > 8) {
    ESP_LOGE(TAG, "Invalid DLC: %d", frame.dlc);
    return false;
  }

  twai_message_t msg = {};
  msg.identifier = frame.id;
  msg.data_length_code = frame.dlc;
  msg.extd = frame.extended;
  msg.rtr = frame.rtr;

  const uint8_t copyLen = (frame.dlc > 8) ? 8 : frame.dlc;
  std::memcpy(msg.data, frame.data, copyLen);

  return twai_transmit(&msg, pdMS_TO_TICKS(DEFAULT_TX_TIMEOUT_MS)) == ESP_OK;
}

bool TWAICanBus::isRunning() const { return running_; }

bool TWAICanBus::isInstalled() const { return installed_; }

BusStatus TWAICanBus::getStatus() const {
  if (!installed_) {
    return BusStatus::NOT_INSTALLED;
  }
  if (!running_) {
    return BusStatus::STOPPED;
  }

  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) {
    return BusStatus::ERROR;
  }

  if (status.state == TWAI_STATE_BUS_OFF) {
    return BusStatus::BUS_OFF;
  } else if (status.state == TWAI_STATE_RECOVERING) {
    return BusStatus::RECOVERING;
  }

  return BusStatus::RUNNING;
}

uint32_t TWAICanBus::getErrorCount() const {
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) {
    return 0;
  }
  return status.tx_error_counter + status.rx_error_counter;
}

bool TWAICanBus::recover() {
  if (!installed_ || !running_) {
    ESP_LOGE(TAG, "Cannot recover: driver not running");
    return false;
  }

  esp_err_t err = twai_initiate_recovery();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Recovery failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Bus recovery initiated");
  return true;
}

void TWAICanBus::cleanup() {
  if (running_) {
    stop();
  }

  if (installed_) {
    twai_driver_uninstall();
    installed_ = false;
  }
}

} // namespace W4RP