# OTA Driver

`ESP32OTAService` implements the `OTA` interface for firmware updates over BLE.

Source: `src/drivers/ESP32OTAService.h`, `src/drivers/ESP32OTAService.cpp`

## Constants

```cpp
#define OTA_RING_BUFFER_SIZE 8192
#define OTA_WRITE_BUFFER_SIZE 4096
#define JANPATCH_PAGE_SIZE 1024
```

## Interface Methods

| Method | Behavior |
|--------|----------|
| `begin()` | Create ring buffer, get running partition |
| `abort()` | Delete delta task, abort OTA handle, drain ring buffer |
| `startFirmwareUpdate(size, crc)` | Begin full OTA |
| `writeFirmwareChunk(data, len)` | Write to OTA partition |
| `finalizeFirmwareUpdate()` | Validate CRC, set boot partition |
| `startDeltaUpdate(size, sourceCRC)` | Begin delta OTA |
| `writeDeltaChunk(data, len)` | Push to ring buffer |
| `finalizeDeltaUpdate()` | Start background janpatch task |
| `getStatus()` | Returns current OTAStatus |
| `needsPause()` | True during APPLYING/VALIDATING |
| `loop()` | Check if delta task completed |

## Update Flows

### Full Firmware

```
1. OTA:BEGIN:<size>:<crc>
2. startFirmwareUpdate(size, crc)
   → esp_ota_get_next_update_partition()
   → esp_ota_begin(partition)
3. writeFirmwareChunk() × N
   → esp_ota_write()
   → Update CRC32
4. finalizeFirmwareUpdate()
   → Verify size and CRC
   → esp_ota_end()
   → esp_ota_set_boot_partition()
5. Reboot
```

### Delta Update

```
1. OTA:DELTA:<size>:<sourceCRC>
2. startDeltaUpdate(size, sourceCRC)
   → Verify running partition CRC
3. writeDeltaChunk() × N
   → xRingbufferSend()
4. finalizeDeltaUpdate()
   → Create FreeRTOS task
   → janpatch: source + patch → target
5. loop() checks deltaComplete_
   → On success: esp_ota_set_boot_partition()
6. Reboot
```

## needsPause()

```cpp
bool needsPause() const override {
  return status_ == OTAStatus::APPLYING || 
         status_ == OTAStatus::VALIDATING;
}
```

Controller checks this to stop CAN/rules processing during CPU-intensive phases.

## Delta Patching (janpatch)

Uses custom stream callbacks for ESP32:

```cpp
struct OtaStream {
  const esp_partition_t *partition;
  esp_ota_handle_t otaHandle;
  RingbufHandle_t ringBuffer;
  long offset;
  bool isSource;  // Read from running partition
  bool isPatch;   // Read from ring buffer
  bool isTarget;  // Write to OTA partition
  
  // Page cache for source reads
  uint8_t *pageCache;
  uint32_t cachedPage;
  bool cacheValid;
};
```

| Callback | Source | Patch | Target |
|----------|--------|-------|--------|
| `ota_fread` | `esp_partition_read` | `xRingbufferReceive` | N/A |
| `ota_fwrite` | N/A | N/A | `esp_ota_write` |
| `ota_fseek` | Update offset | Update offset | Update offset |
| `ota_ftell` | Return offset | Return offset | Return offset |

## Private State

| Field | Type | Description |
|-------|------|-------------|
| `status_` | `OTAStatus` | Current state |
| `otaHandle_` | `esp_ota_handle_t` | ESP OTA handle |
| `updatePartition_` | `esp_partition_t*` | Target partition |
| `runningPartition_` | `esp_partition_t*` | Current partition |
| `expectedSize_` | `uint32_t` | Expected bytes |
| `expectedCRC_` | `uint32_t` | Expected CRC32 |
| `receivedBytes_` | `uint32_t` | Bytes received |
| `calculatedCRC_` | `uint32_t` | Running CRC32 |
| `ringBuffer_` | `RingbufHandle_t` | Delta chunk buffer |
| `deltaTask_` | `TaskHandle_t` | Background task |
| `isDelta_` | `bool` | Delta vs full mode |
| `deltaComplete_` | `volatile bool` | Task done flag |
| `deltaResult_` | `volatile OTAStatus` | Task result |

## Callbacks

```cpp
void setProgressCallback(OTAProgressCallback cb);
void setCompleteCallback(OTACompleteCallback cb);
```

Progress callback receives:
```cpp
struct OTAProgress {
  uint32_t bytesReceived;
  uint32_t totalBytes;
  uint8_t percentage;
};
```

## Usage Example

```cpp
ESP32OTAService otaService;
Controller w4rp(&canBus, &storage, &transport, &otaService);

void setup() {
  otaService.begin();
  
  otaService.setProgressCallback([](const OTAProgress &p) {
    Serial.printf("OTA: %d%%\n", p.percentage);
  });
  
  otaService.setCompleteCallback([](OTAStatus status) {
    if (status == OTAStatus::SUCCESS) {
      Serial.println("OTA success, rebooting...");
      delay(1000);
      ESP.restart();
    }
  });
  
  w4rp.begin();
}

void loop() {
  w4rp.loop();  // Calls otaService.loop() internally
}
```
