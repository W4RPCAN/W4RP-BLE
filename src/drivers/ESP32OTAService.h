/**
 * @file ESP32OTAService.h
 * @brief DRIVERS:ESP32OTAService - OTA firmware updates
 * @version 1.0.0
 *
 * Implements OTA interface for full and delta firmware updates.
 * Delta updates use janpatch with ring buffer + background task.
 *
 * Full:  OTA:BEGIN → writeFirmwareChunk() → finalize → reboot
 * Delta: OTA:DELTA → writeDeltaChunk() → finalize → janpatch → reboot
 */
#pragma once
#include "../interfaces/OTA.h"
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

namespace W4RP {

#define OTA_RING_BUFFER_SIZE 8192
#define OTA_WRITE_BUFFER_SIZE 4096

/**
 * @class ESP32OTAService
 * @brief ESP32 OTA service with full and delta support
 */
class ESP32OTAService : public OTA {
public:
  ESP32OTAService();
  ~ESP32OTAService();

  /**
   * @brief Create ring buffer, get running partition
   * @return true on success
   */
  bool begin() override;

  /**
   * @brief Stop delta task, abort OTA handle
   */
  void abort() override;

  /**
   * @brief Begin full firmware update
   * @param expectedSize Firmware size
   * @param crc32 Expected CRC32
   * @return true on success
   */
  bool startFirmwareUpdate(uint32_t expectedSize, uint32_t crc32) override;

  /**
   * @brief Write chunk to OTA partition
   * @param data Chunk data
   * @param len Chunk length
   * @return true on success
   */
  bool writeFirmwareChunk(const uint8_t *data, size_t len) override;

  /**
   * @brief Validate CRC, set boot partition
   * @return true on success
   */
  bool finalizeFirmwareUpdate() override;

  /**
   * @brief Begin delta update
   * @param patchSize Patch size
   * @param sourceCRC Current firmware CRC32
   * @return true on success
   */
  bool startDeltaUpdate(uint32_t patchSize, uint32_t sourceCRC) override;

  /**
   * @brief Push chunk to ring buffer
   * @param data Chunk data
   * @param len Chunk length
   * @return true on success
   */
  bool writeDeltaChunk(const uint8_t *data, size_t len) override;

  /**
   * @brief Start background janpatch task
   * @return true on success
   */
  bool finalizeDeltaUpdate() override;

  /**
   * @brief Get current status
   * @return OTAStatus enum
   */
  OTAStatus getStatus() const override { return status_; }

  /**
   * @brief Set progress callback
   * @param cb Progress handler
   */
  void setProgressCallback(OTAProgressCallback cb) override {
    progressCb_ = cb;
  }

  /**
   * @brief Set completion callback
   * @param cb Completion handler
   */
  void setCompleteCallback(OTACompleteCallback cb) override {
    completeCb_ = cb;
  }

  /**
   * @brief Check if Controller should pause
   * @return true during APPLYING/VALIDATING
   */
  bool needsPause() const override {
    return status_ == OTAStatus::APPLYING || status_ == OTAStatus::VALIDATING;
  }

  /**
   * @brief Check if delta task completed
   */
  void loop() override;

private:
  OTAStatus status_ = OTAStatus::IDLE;

  esp_ota_handle_t otaHandle_ = 0;
  const esp_partition_t *updatePartition_ = nullptr;
  const esp_partition_t *runningPartition_ = nullptr;

  uint32_t expectedSize_ = 0;
  uint32_t expectedCRC_ = 0;
  uint32_t receivedBytes_ = 0;
  uint32_t calculatedCRC_ = 0;

  RingbufHandle_t ringBuffer_ = nullptr;
  TaskHandle_t deltaTask_ = nullptr;
  bool isDelta_ = false;
  uint32_t sourceCRC_ = 0;
  volatile bool deltaComplete_ = false;
  volatile OTAStatus deltaResult_ = OTAStatus::IDLE;

  OTAProgressCallback progressCb_;
  OTACompleteCallback completeCb_;

  void notifyProgress();
  void notifyComplete(OTAStatus status);
  static void deltaWorkerTask(void *params);
  void processDelta();
};

} // namespace W4RP
