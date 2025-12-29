/**
 * @file CAN.h
 * @brief W4RP::CAN - CAN Layer communication interface
 * @version 1.0.0
 *
 * Receives vehicle CAN frames for signal decoding and rule evaluation
 */
#pragma once
#include <Arduino.h>

namespace W4RP {

struct CanFrame {
  uint32_t id;
  uint8_t data[8];
  uint8_t dlc;
  bool extended;
  bool rtr;
};

/**
 * @interface CAN
 * @brief CAN bus interface
 */
class CAN {
public:
  virtual ~CAN() = default;

  /**
   * @brief Initialize CAN controller
   * @return true on success
   */
  virtual bool begin() = 0;

  /**
   * @brief Read frame from bus
   * @param frame Output frame
   * @return true if frame received
   */
  virtual bool receive(CanFrame &frame) = 0;

  /**
   * @brief Write frame to bus
   * @param frame Frame to transmit
   * @return true on success
   */
  virtual bool transmit(const CanFrame &frame) = 0;

  /**
   * @brief Stop bus activity
   */
  virtual void stop() = 0;

  /**
   * @brief Resume bus activity
   */
  virtual void resume() = 0;

  /**
   * @brief Check if bus is active
   * @return true if running
   */
  virtual bool isRunning() const = 0;
};

} // namespace W4RP