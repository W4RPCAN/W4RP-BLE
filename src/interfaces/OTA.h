/**
 * @file OTA.h
 * @brief W4RP::OTA
 * @version 1.0.0
 *
 * W4RP::Controller - Firmware updates
 * Commands: OTA:BEGIN, OTA:DELTA
 * Note: Optional interface
 */

#pragma once

#include <Arduino.h>
#include <functional>

namespace W4RP {

enum class OTAStatus : uint8_t {
  IDLE,
  RECEIVING,
  VALIDATING,
  APPLYING,
  SUCCESS,
  ERROR_SPACE,
  ERROR_CRC,
  ERROR_SIGNATURE,
  ERROR_FLASH,
  ERROR_TIMEOUT
};

struct OTAProgress {
  uint32_t bytesReceived;
  uint32_t totalBytes;
  uint8_t percentage;
};

using OTAProgressCallback = std::function<void(const OTAProgress &)>;
using OTACompleteCallback = std::function<void(OTAStatus)>;

class OTA {
public:
  virtual ~OTA() = default;

  virtual bool begin() = 0;
  virtual void abort() = 0;
  virtual bool startFirmwareUpdate(uint32_t expectedSize, uint32_t crc32) = 0;
  virtual bool writeFirmwareChunk(const uint8_t *data, size_t len) = 0;
  virtual bool finalizeFirmwareUpdate() = 0;
  virtual bool startDeltaUpdate(uint32_t patchSize, uint32_t sourceCRC) = 0;
  virtual bool writeDeltaChunk(const uint8_t *data, size_t len) = 0;
  virtual bool finalizeDeltaUpdate() = 0;
  virtual OTAStatus getStatus() const = 0;
  virtual void setProgressCallback(OTAProgressCallback cb) = 0;
  virtual void setCompleteCallback(OTACompleteCallback cb) = 0;
  virtual bool needsPause() const = 0;
  virtual void loop() = 0;
};

} // namespace W4RP
