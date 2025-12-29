/**
 * @file ESP32OTAService.cpp
 * @brief ESP32 OTA service implementation
 *
 * Full firmware: Direct write to OTA partition
 * Delta update: Ring buffer + background janpatch task
 */

#include "ESP32OTAService.h"
#include <esp_crc.h>
#include <esp_log.h>
#include <esp_partition.h>

// Janpatch configuration
#include <stdio.h>
#define JANPATCH_STREAM FILE
#include "../../janpatch.h"

static const char *TAG = "ESP32OTA";

namespace W4RP {

// ============================================================================
// JANPATCH STREAM STRUCTURES
// ============================================================================

struct OtaStream {
  ESP32OTAService *service;
  const esp_partition_t *partition;
  esp_ota_handle_t otaHandle;
  RingbufHandle_t ringBuffer;
  long offset;
  bool isSource; // Reading from running partition
  bool isPatch;  // Reading from ring buffer
  bool isTarget; // Writing to OTA partition

  // Page cache for source partition reads
  uint8_t *pageCache;
  uint32_t cachedPage;
  bool cacheValid;
};

#define JANPATCH_PAGE_SIZE 1024

// ============================================================================
// JANPATCH CALLBACKS
// ============================================================================

static size_t ota_fread(void *ptr, size_t size, size_t count, FILE *stream) {
  OtaStream *s = reinterpret_cast<OtaStream *>(stream);
  size_t total = size * count;

  if (s->isSource) {
    // Read from running partition with page caching
    uint32_t pageIdx = s->offset / JANPATCH_PAGE_SIZE;

    if (!s->cacheValid || s->cachedPage != pageIdx) {
      // Load new page
      esp_err_t err =
          esp_partition_read(s->partition, pageIdx * JANPATCH_PAGE_SIZE,
                             s->pageCache, JANPATCH_PAGE_SIZE);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Source read failed at 0x%lx", s->offset);
        return 0;
      }
      s->cachedPage = pageIdx;
      s->cacheValid = true;
    }

    // Copy from cache
    uint32_t pageOffset = s->offset % JANPATCH_PAGE_SIZE;
    size_t available = JANPATCH_PAGE_SIZE - pageOffset;
    size_t toCopy = (total > available) ? available : total;
    memcpy(ptr, s->pageCache + pageOffset, toCopy);
    s->offset += toCopy;
    return toCopy / size;
  }

  if (s->isPatch) {
    // Read from ring buffer
    size_t itemSize;
    void *item = xRingbufferReceiveUpTo(s->ringBuffer, &itemSize, 0, total);
    if (item == nullptr) {
      return 0;
    }
    memcpy(ptr, item, itemSize);
    vRingbufferReturnItem(s->ringBuffer, item);
    s->offset += itemSize;
    return itemSize / size;
  }

  return 0;
}

static size_t ota_fwrite(const void *ptr, size_t size, size_t count,
                         FILE *stream) {
  OtaStream *s = reinterpret_cast<OtaStream *>(stream);

  if (!s->isTarget)
    return 0;

  size_t total = size * count;
  esp_err_t err = esp_ota_write(s->otaHandle, ptr, total);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
    return 0;
  }

  s->offset += total;
  return count;
}

static int ota_fseek(FILE *stream, long offset, int origin) {
  OtaStream *s = reinterpret_cast<OtaStream *>(stream);

  switch (origin) {
  case SEEK_SET:
    s->offset = offset;
    break;
  case SEEK_CUR:
    s->offset += offset;
    break;
  case SEEK_END:
    if (s->partition) {
      s->offset = s->partition->size + offset;
    }
    break;
  }

  // Invalidate cache on seek
  if (s->isSource) {
    s->cacheValid = false;
  }

  return 0;
}

static long ota_ftell(FILE *stream) {
  OtaStream *s = reinterpret_cast<OtaStream *>(stream);
  return s->offset;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ESP32OTAService::ESP32OTAService() {}

ESP32OTAService::~ESP32OTAService() { abort(); }

bool ESP32OTAService::begin() {
  // Pre-allocate ring buffer for delta updates
  ringBuffer_ = xRingbufferCreate(OTA_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
  if (!ringBuffer_) {
    ESP_LOGE(TAG, "Failed to create ring buffer");
    return false;
  }

  runningPartition_ = esp_ota_get_running_partition();
  ESP_LOGI(TAG, "OTA service initialized. Running: %s",
           runningPartition_->label);
  return true;
}

void ESP32OTAService::abort() {
  if (status_ == OTAStatus::IDLE)
    return;

  // Stop delta task if running
  if (deltaTask_) {
    vTaskDelete(deltaTask_);
    deltaTask_ = nullptr;
  }

  // End OTA session
  if (otaHandle_) {
    esp_ota_abort(otaHandle_);
    otaHandle_ = 0;
  }

  // Clear ring buffer
  if (ringBuffer_) {
    // Drain all items
    size_t itemSize;
    void *item;
    while ((item = xRingbufferReceive(ringBuffer_, &itemSize, 0)) != nullptr) {
      vRingbufferReturnItem(ringBuffer_, item);
    }
  }

  status_ = OTAStatus::IDLE;
  isDelta_ = false;
  receivedBytes_ = 0;
  calculatedCRC_ = 0;
  deltaComplete_ = false;

  ESP_LOGI(TAG, "OTA aborted");
}

// ============================================================================
// FULL FIRMWARE UPDATE
// ============================================================================

bool ESP32OTAService::startFirmwareUpdate(uint32_t expectedSize,
                                          uint32_t crc32) {
  if (status_ != OTAStatus::IDLE) {
    ESP_LOGE(TAG, "Already in progress");
    return false;
  }

  updatePartition_ = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition_) {
    ESP_LOGE(TAG, "No update partition");
    return false;
  }

  // Check size fits
  if (expectedSize > updatePartition_->size) {
    ESP_LOGE(TAG, "Firmware too large: %u > %u", expectedSize,
             updatePartition_->size);
    return false;
  }

  esp_err_t err =
      esp_ota_begin(updatePartition_, OTA_SIZE_UNKNOWN, &otaHandle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Begin failed: %s", esp_err_to_name(err));
    return false;
  }

  expectedSize_ = expectedSize;
  expectedCRC_ = crc32;
  receivedBytes_ = 0;
  calculatedCRC_ = 0;
  isDelta_ = false;
  status_ = OTAStatus::RECEIVING;

  ESP_LOGI(TAG, "Started full update: %u bytes -> %s", expectedSize,
           updatePartition_->label);
  return true;
}

bool ESP32OTAService::writeFirmwareChunk(const uint8_t *data, size_t len) {
  if (status_ != OTAStatus::RECEIVING || isDelta_) {
    return false;
  }

  // Check for overflow
  if (receivedBytes_ + len > expectedSize_) {
    ESP_LOGE(TAG, "Overflow: %u + %u > %u", receivedBytes_, len, expectedSize_);
    status_ = OTAStatus::ERROR_SPACE;
    notifyComplete(status_);
    return false;
  }

  // Write to flash
  esp_err_t err = esp_ota_write(otaHandle_, data, len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(err));
    status_ = OTAStatus::ERROR_FLASH;
    notifyComplete(status_);
    return false;
  }

  // Update progress
  receivedBytes_ += len;
  calculatedCRC_ = esp_crc32_le(calculatedCRC_, data, len);
  notifyProgress();

  return true;
}

bool ESP32OTAService::finalizeFirmwareUpdate() {
  if (status_ != OTAStatus::RECEIVING || isDelta_) {
    return false;
  }

  status_ = OTAStatus::VALIDATING;

  // Verify size
  if (receivedBytes_ != expectedSize_) {
    ESP_LOGE(TAG, "Size mismatch: %u != %u", receivedBytes_, expectedSize_);
    status_ = OTAStatus::ERROR_SPACE;
    notifyComplete(status_);
    return false;
  }

  // Verify CRC
  if (calculatedCRC_ != expectedCRC_) {
    ESP_LOGE(TAG, "CRC mismatch: 0x%08X != 0x%08X", calculatedCRC_,
             expectedCRC_);
    status_ = OTAStatus::ERROR_CRC;
    notifyComplete(status_);
    return false;
  }

  // Commit
  esp_err_t err = esp_ota_end(otaHandle_);
  otaHandle_ = 0;
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "End failed: %s", esp_err_to_name(err));
    status_ = OTAStatus::ERROR_FLASH;
    notifyComplete(status_);
    return false;
  }

  err = esp_ota_set_boot_partition(updatePartition_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Set boot failed: %s", esp_err_to_name(err));
    status_ = OTAStatus::ERROR_FLASH;
    notifyComplete(status_);
    return false;
  }

  status_ = OTAStatus::SUCCESS;
  notifyComplete(status_);

  ESP_LOGI(TAG, "SUCCESS! Reboot to apply.");
  return true;
}

// ============================================================================
// DELTA UPDATE (Janpatch in Background Task)
// ============================================================================

bool ESP32OTAService::startDeltaUpdate(uint32_t patchSize, uint32_t sourceCRC) {
  if (status_ != OTAStatus::IDLE) {
    ESP_LOGE(TAG, "Already in progress");
    return false;
  }

  updatePartition_ = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition_) {
    ESP_LOGE(TAG, "No update partition");
    return false;
  }

  // Verify source CRC matches running partition
  // (Skip for now - would need to read entire partition)

  esp_err_t err =
      esp_ota_begin(updatePartition_, OTA_SIZE_UNKNOWN, &otaHandle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Begin failed: %s", esp_err_to_name(err));
    return false;
  }

  expectedSize_ = patchSize;
  sourceCRC_ = sourceCRC;
  receivedBytes_ = 0;
  isDelta_ = true;
  deltaComplete_ = false;
  deltaResult_ = OTAStatus::IDLE;
  status_ = OTAStatus::RECEIVING;

  // Drain ring buffer
  size_t itemSize;
  void *item;
  while ((item = xRingbufferReceive(ringBuffer_, &itemSize, 0)) != nullptr) {
    vRingbufferReturnItem(ringBuffer_, item);
  }

  ESP_LOGI(TAG, "Started delta update: %u bytes patch", patchSize);
  return true;
}

bool ESP32OTAService::writeDeltaChunk(const uint8_t *data, size_t len) {
  if (status_ != OTAStatus::RECEIVING || !isDelta_) {
    return false;
  }

  // Push to ring buffer (with timeout)
  if (xRingbufferSend(ringBuffer_, data, len, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Ring buffer full!");
    return false;
  }

  receivedBytes_ += len;
  notifyProgress();
  return true;
}

bool ESP32OTAService::finalizeDeltaUpdate() {
  if (status_ != OTAStatus::RECEIVING || !isDelta_) {
    return false;
  }

  status_ = OTAStatus::APPLYING;
  ESP_LOGI(TAG, "Starting delta patch task...");

  // Start background task
  BaseType_t result = xTaskCreate(deltaWorkerTask, "OTA_Delta", 8192, this,
                                  tskIDLE_PRIORITY + 2, &deltaTask_);

  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create delta task");
    status_ = OTAStatus::ERROR_FLASH;
    notifyComplete(status_);
    return false;
  }

  return true;
}

void ESP32OTAService::deltaWorkerTask(void *params) {
  ESP32OTAService *self = static_cast<ESP32OTAService *>(params);
  self->processDelta();
  vTaskDelete(nullptr);
}

void ESP32OTAService::processDelta() {
  ESP_LOGI(TAG, "Delta worker started");

  // Allocate page cache for source reads
  uint8_t *pageCache =
      (uint8_t *)heap_caps_malloc(JANPATCH_PAGE_SIZE, MALLOC_CAP_8BIT);
  if (!pageCache) {
    ESP_LOGE(TAG, "Failed to allocate page cache");
    deltaResult_ = OTAStatus::ERROR_FLASH;
    deltaComplete_ = true;
    return;
  }

  // Setup streams
  OtaStream sourceStream = {};
  sourceStream.service = this;
  sourceStream.partition = runningPartition_;
  sourceStream.offset = 0;
  sourceStream.isSource = true;
  sourceStream.pageCache = pageCache;
  sourceStream.cacheValid = false;

  OtaStream patchStream = {};
  patchStream.service = this;
  patchStream.ringBuffer = ringBuffer_;
  patchStream.offset = 0;
  patchStream.isPatch = true;

  OtaStream targetStream = {};
  targetStream.service = this;
  targetStream.partition = updatePartition_;
  targetStream.otaHandle = otaHandle_;
  targetStream.offset = 0;
  targetStream.isTarget = true;

  // Allocate janpatch buffers
  uint8_t *buffer1 =
      (uint8_t *)heap_caps_malloc(JANPATCH_PAGE_SIZE, MALLOC_CAP_8BIT);
  uint8_t *buffer2 =
      (uint8_t *)heap_caps_malloc(JANPATCH_PAGE_SIZE, MALLOC_CAP_8BIT);

  if (!buffer1 || !buffer2) {
    ESP_LOGE(TAG, "Failed to allocate janpatch buffers");
    free(pageCache);
    if (buffer1)
      free(buffer1);
    if (buffer2)
      free(buffer2);
    deltaResult_ = OTAStatus::ERROR_FLASH;
    deltaComplete_ = true;
    return;
  }

  // Setup janpatch context
  janpatch_ctx ctx = {};
  ctx.fread = ota_fread;
  ctx.fwrite = ota_fwrite;
  ctx.fseek = ota_fseek;
  ctx.ftell = ota_ftell;

  // Initialize source buffer
  ctx.source_buffer.buffer = buffer1;
  ctx.source_buffer.size = JANPATCH_PAGE_SIZE;

  // Initialize patch buffer
  ctx.patch_buffer.buffer = buffer2;
  ctx.patch_buffer.size = JANPATCH_PAGE_SIZE;

  // Apply patch
  ESP_LOGI(TAG, "Applying janpatch...");
  int result = janpatch(ctx, (FILE *)&sourceStream, (FILE *)&patchStream,
                        (FILE *)&targetStream);

  // Cleanup
  free(pageCache);
  free(buffer1);
  free(buffer2);

  if (result != 0) {
    ESP_LOGE(TAG, "Janpatch failed: %d", result);
    esp_ota_abort(otaHandle_);
    otaHandle_ = 0;
    deltaResult_ = OTAStatus::ERROR_FLASH;
    deltaComplete_ = true;
    return;
  }

  // Finalize OTA
  esp_err_t err = esp_ota_end(otaHandle_);
  otaHandle_ = 0;
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
    deltaResult_ = OTAStatus::ERROR_FLASH;
    deltaComplete_ = true;
    return;
  }

  err = esp_ota_set_boot_partition(updatePartition_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Set boot failed: %s", esp_err_to_name(err));
    deltaResult_ = OTAStatus::ERROR_FLASH;
    deltaComplete_ = true;
    return;
  }

  ESP_LOGI(TAG, "Delta patch SUCCESS!");
  deltaResult_ = OTAStatus::SUCCESS;
  deltaComplete_ = true;
}

void ESP32OTAService::loop() {
  // Check if delta task completed
  if (isDelta_ && deltaComplete_) {
    deltaComplete_ = false;
    deltaTask_ = nullptr;
    status_ = deltaResult_;
    notifyComplete(status_);
  }
}

// ============================================================================
// PROGRESS / CALLBACKS
// ============================================================================

void ESP32OTAService::notifyProgress() {
  if (progressCb_ && expectedSize_ > 0) {
    OTAProgress progress;
    progress.bytesReceived = receivedBytes_;
    progress.totalBytes = expectedSize_;
    progress.percentage = (receivedBytes_ * 100) / expectedSize_;
    progressCb_(progress);
  }
}

void ESP32OTAService::notifyComplete(OTAStatus status) {
  status_ = status;
  if (completeCb_) {
    completeCb_(status);
  }
}

} // namespace W4RP
