# W4RPBLE

**BLE-to-CAN Rules Engine for ESP32**

A library for creating BLE-connected automotive modules that read CAN bus signals and execute rule-based automation flows.

## Supported Boards

- ESP32-C3 (current development)
- ESP32-S3 (production target)
- Any ESP32 with TWAI peripheral

## Features

- 📡 **BLE Connectivity** - Chunked data transfer with CRC verification
- 🚗 **CAN Bus Integration** - TWAI driver with configurable modes
- ⚡ **Rule Engine** - Condition → Action flow evaluation
- 💾 **NVS Persistence** - Rules survive power cycles
- 🔌 **Capability System** - Register custom actions

## Installation

### Arduino IDE
1. Download this repository as ZIP
2. Sketch → Include Library → Add .ZIP Library
3. Select the downloaded ZIP

### PlatformIO
```ini
lib_deps =
    bblanchon/ArduinoJson@^6.0.0
    ; Add this library path
```

## Quick Start

```cpp
#include <W4RPBLE.h>

W4RPBLE w4rp;

void setup() {
  Serial.begin(115200);

  // Configure before begin()
  w4rp.setBleName("My Module");
  w4rp.setModuleFirmware("1.0.0");
  w4rp.setCanMode(W4RPBLE::CanMode::LISTEN_ONLY);
  
  // Initialize
  w4rp.begin();
  
  // Register custom action
  w4rp.registerCapability("my_output", [](const W4RPBLE::ParamMap &params) {
    int value = params["amount"].toInt();
    // Do something with value
  });
}

void loop() {
  w4rp.loop();
}
```

## Wiring (ESP32-C3)

| Pin | Function | Default |
|-----|----------|---------|
| GPIO21 | CAN TX | Configurable via `setPins()` |
| GPIO20 | CAN RX | Configurable via `setPins()` |
| GPIO8 | Status LED | Configurable via `setPins()` |

## CAN Modes

| Mode | Description |
|------|-------------|
| `NORMAL` | Standard TX/RX with ACK |
| `LISTEN_ONLY` | RX only, no ACK (safe for vehicles) |
| `NO_ACK` | TX/RX without waiting for ACK |

## BLE Protocol

### Module Profile Request
Send: `GET:PROFILE`

Response:
```
BEGIN
<json chunks...>
END:<length>:<crc32>
```

### Rules Upload
```
SET:RULES:NVS:<length>:<crc32>
<json chunks...>
END
```

## Dependencies

- [ArduinoJson](https://arduinojson.org/) >= 6.0.0

## License

MIT License
