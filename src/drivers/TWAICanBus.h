/**
 * @file TWAICanBus.h
 * @brief DRIVERS:TWAICanBus - ESP32 TWAI CAN driver
 * @version 1.0.0
 *
 * Implements CAN interface using ESP32's native TWAI peripheral.
 * @see
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html
 */
#pragma once
#include "../interfaces/CAN.h"
#include <driver/gpio.h>
#include <driver/twai.h>

namespace W4RP {

enum class BusStatus {
  NOT_INSTALLED,
  STOPPED,
  RUNNING,
  RECOVERING,
  BUS_OFF,
  ERROR
};

/**
 * @class TWAICanBus
 * @brief ESP32 TWAI CAN bus driver
 */
class TWAICanBus : public CAN {
public:
  /**
   * @brief Construct TWAI driver
   * @param txPin GPIO for CAN TX
   * @param rxPin GPIO for CAN RX
   * @param timing Baud rate config
   * @param mode Operating mode
   */
  TWAICanBus(gpio_num_t txPin = GPIO_NUM_21, gpio_num_t rxPin = GPIO_NUM_20,
             twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS(),
             twai_mode_t mode = TWAI_MODE_LISTEN_ONLY);

  ~TWAICanBus();

  /**
   * @brief Initialize CAN controller
   * @return true on success
   */
  bool begin() override;

  /**
   * @brief Read frame from bus
   * @param frame Output frame
   * @return true if frame received
   */
  bool receive(CanFrame &frame) override;

  /**
   * @brief Write frame to bus
   * @param frame Frame to transmit
   * @return true on success
   */
  bool transmit(const CanFrame &frame) override;

  /**
   * @brief Stop bus activity
   */
  void stop() override;

  /**
   * @brief Resume bus activity
   */
  void resume() override;

  /**
   * @brief Check if bus is active
   * @return true if running
   */
  bool isRunning() const override;

  /**
   * @brief Initialize with custom queue lengths
   * @param rxQueueLen RX queue size
   * @param txQueueLen TX queue size
   * @return true on success
   */
  bool begin(uint32_t rxQueueLen, uint32_t txQueueLen);

  /**
   * @brief Check if driver installed
   * @return true if installed
   */
  bool isInstalled() const;

  /**
   * @brief Get bus status
   * @return BusStatus enum
   */
  BusStatus getStatus() const;

  /**
   * @brief Get error count
   * @return Error count
   */
  uint32_t getErrorCount() const;

  /**
   * @brief Attempt bus recovery
   * @return true on success
   */
  bool recover();

  TWAICanBus(const TWAICanBus &) = delete;
  TWAICanBus &operator=(const TWAICanBus &) = delete;

private:
  void cleanup();

  gpio_num_t txPin_;
  gpio_num_t rxPin_;
  twai_timing_config_t timing_;
  twai_mode_t mode_;
  bool running_ = false;
  bool installed_ = false;
};

} // namespace W4RP
