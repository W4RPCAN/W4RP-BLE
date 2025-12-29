/**
 * @file Communication.h
 * @brief W4RP::Communication - Bridge layer communication
 * @version 1.0.0
 *
 * Protocol: GET:PROFILE, SET:RULES, DEBUG:WATCH, OTA:*
 */
#pragma once
#include <Arduino.h>
#include <functional>

namespace W4RP {

using TransportRxCallback =
    std::function<void(const uint8_t *data, size_t len)>;
using TransportConnCallback = std::function<void(bool connected)>;

/**
 * @interface Communication
 * @brief Transport layer for W4RP protocol commands
 */
class Communication {
public:
  virtual ~Communication() = default;

  /**
   * @brief Initialize transport
   * @param deviceName Advertised device name
   * @return true on success
   */
  virtual bool begin(const char *deviceName) = 0;

  /**
   * @brief Check connection state
   * @return true if connected
   */
  virtual bool isConnected() const = 0;

  /**
   * @brief Send binary data
   * @param data Buffer pointer
   * @param len Byte count
   */
  virtual void send(const uint8_t *data, size_t len) = 0;

  /**
   * @brief Send string
   * @param str Null-terminated string
   */
  virtual void send(const char *str) {
    send((const uint8_t *)str, strlen(str));
  }

  /**
   * @brief Send status message
   * @param data Buffer pointer
   * @param len Byte count
   */
  virtual void sendStatus(const uint8_t *data, size_t len) = 0;

  /**
   * @brief Register receive callback
   * @param callback Handler for incoming data
   */
  virtual void onReceive(TransportRxCallback callback) = 0;

  /**
   * @brief Register connection callback
   * @param callback Handler for connection state changes
   */
  virtual void onConnectionChange(TransportConnCallback callback) = 0;

  /**
   * @brief Process transport events
   */
  virtual void loop() = 0;

  /**
   * @brief Get maximum transmission unit
   * @return MTU in bytes (default 128)
   */
  virtual size_t getMTU() const { return 128; }
};

} // namespace W4RP