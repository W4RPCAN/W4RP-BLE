# Interfaces

Abstract base classes that define driver contracts.

Source: `src/interfaces/`

---

## CAN

Source: `src/interfaces/CAN.h`

```cpp
class CAN {
public:
  virtual ~CAN() = default;
  
  virtual bool begin() = 0;
  virtual bool receive(CanFrame &frame) = 0;
  virtual bool transmit(const CanFrame &frame) = 0;
  virtual void stop() = 0;
  virtual void resume() = 0;
  virtual bool isRunning() const = 0;
};
```

| Method | Parameters | Returns | Description |
|--------|------------|---------|-------------|
| `begin()` | - | `bool` | Initialize hardware |
| `receive()` | `CanFrame &frame` | `bool` | Non-blocking read |
| `transmit()` | `const CanFrame &frame` | `bool` | Queue frame |
| `stop()` | - | `void` | Stop bus (OTA safety) |
| `resume()` | - | `void` | Resume after stop |
| `isRunning()` | - | `bool` | Check bus active |

### CanFrame

```cpp
struct CanFrame {
  uint32_t id;       // CAN ID
  uint8_t data[8];   // Payload
  uint8_t dlc;       // Data length (0-8)
  bool extended;     // 29-bit ID
  bool rtr;          // Remote request
};
```

---

## Storage

Source: `src/interfaces/Storage.h`

```cpp
class Storage {
public:
  virtual ~Storage() = default;
  
  virtual bool begin() = 0;
  virtual bool writeBlob(const char *key, const uint8_t *data, size_t len) = 0;
  virtual size_t readBlob(const char *key, uint8_t *buffer, size_t maxLen) = 0;
  virtual bool writeString(const char *key, const String &value) = 0;
  virtual String readString(const char *key) = 0;
  virtual bool erase(const char *key) = 0;
  virtual bool commit() { return true; }
};
```

| Method | Parameters | Returns | Description |
|--------|------------|---------|-------------|
| `begin()` | - | `bool` | Initialize storage |
| `writeBlob()` | `key`, `data`, `len` | `bool` | Write binary |
| `readBlob()` | `key`, `buffer`, `maxLen` | `size_t` | Read binary (nullptr→size query) |
| `writeString()` | `key`, `value` | `bool` | Write string |
| `readString()` | `key` | `String` | Read string |
| `erase()` | `key` | `bool` | Delete key |
| `commit()` | - | `bool` | Flush (default: no-op) |

---

## Communication

Source: `src/interfaces/Communication.h`

```cpp
class Communication {
public:
  virtual ~Communication() = default;
  
  virtual bool begin(const char *deviceName) = 0;
  virtual bool isConnected() const = 0;
  virtual void send(const uint8_t *data, size_t len) = 0;
  virtual void send(const char *str) { send((const uint8_t *)str, strlen(str)); }
  virtual void sendStatus(const uint8_t *data, size_t len) = 0;
  virtual void onReceive(TransportRxCallback callback) = 0;
  virtual void onConnectionChange(TransportConnCallback callback) = 0;
  virtual void loop() = 0;
  virtual size_t getMTU() const { return 128; }
};
```

| Method | Parameters | Returns | Description |
|--------|------------|---------|-------------|
| `begin()` | `const char *deviceName` | `bool` | Start transport |
| `isConnected()` | - | `bool` | Check connection |
| `send()` | `data`, `len` or `str` | `void` | Send data |
| `sendStatus()` | `data`, `len` | `void` | Send on status channel |
| `onReceive()` | `TransportRxCallback` | `void` | Set receive callback |
| `onConnectionChange()` | `TransportConnCallback` | `void` | Set connection callback |
| `loop()` | - | `void` | Process events |
| `getMTU()` | - | `size_t` | Max transmission unit |

### Callbacks

```cpp
using TransportRxCallback = std::function<void(const uint8_t *data, size_t len)>;
using TransportConnCallback = std::function<void(bool connected)>;
```

---

## OTA

Source: `src/interfaces/OTA.h`

```cpp
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
```

| Method | Parameters | Returns | Description |
|--------|------------|---------|-------------|
| `begin()` | - | `bool` | Initialize OTA |
| `abort()` | - | `void` | Cancel update |
| `startFirmwareUpdate()` | `expectedSize`, `crc32` | `bool` | Begin full update |
| `writeFirmwareChunk()` | `data`, `len` | `bool` | Write firmware data |
| `finalizeFirmwareUpdate()` | - | `bool` | Validate and set boot |
| `startDeltaUpdate()` | `patchSize`, `sourceCRC` | `bool` | Begin delta update |
| `writeDeltaChunk()` | `data`, `len` | `bool` | Write patch data |
| `finalizeDeltaUpdate()` | - | `bool` | Apply patch |
| `getStatus()` | - | `OTAStatus` | Current status |
| `needsPause()` | - | `bool` | Controller should pause |
| `loop()` | - | `void` | Check background task |

### OTAStatus

```cpp
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
```

### OTAProgress

```cpp
struct OTAProgress {
  uint32_t bytesReceived;
  uint32_t totalBytes;
  uint8_t percentage;
};
```

### Callbacks

```cpp
using OTAProgressCallback = std::function<void(const OTAProgress &)>;
using OTACompleteCallback = std::function<void(OTAStatus)>;
```
