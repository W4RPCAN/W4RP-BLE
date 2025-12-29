# Quick Start

Your first W4RP module on ESP32.

Source: `W4RP.h`, `examples/`

## Install

1. Download this repository
2. Arduino IDE → Sketch → Include Library → Add .ZIP Library
3. Select the W4RP-BLE folder

Or clone into Arduino libraries:
```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/yourorg/W4RP-BLE.git
```

## Hardware

- ESP32 board (ESP32-C3 recommended)
- CAN transceiver (SN65HVD230)
- Vehicle CAN bus access

Wiring:
```
ESP32 GPIO21 → CAN TX
ESP32 GPIO20 → CAN RX
CAN H/L → Vehicle
```

## Minimal Code

```cpp
#include <W4RP.h>

using namespace W4RP;

// Create drivers
TWAICanBus canBus(GPIO_NUM_21, GPIO_NUM_20,
                  TWAI_TIMING_CONFIG_500KBITS(),
                  TWAI_MODE_LISTEN_ONLY);
NVSStorage storage;
BLETransport transport;

// Create controller
Controller w4rp(&canBus, &storage, &transport);

void setup() {
  Serial.begin(115200);
  
  w4rp.setModuleInfo("DEMO", "1.0.0");
  w4rp.begin();
}

void loop() {
  w4rp.loop();
}
```

## Adding a Capability

```cpp
#define RELAY_PIN 4

CapabilityMeta relayMeta = {
  .id = "relay",
  .label = "Relay Control",
  .description = "Toggle relay output",
  .category = "outputs"
};

void onRelay(const ParamMap &params) {
  auto it = params.find("p0");
  if (it != params.end()) {
    int state = it->second.toInt();
    digitalWrite(RELAY_PIN, state);
    Serial.printf("Relay: %d\n", state);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  
  w4rp.setModuleInfo("RELAY", "1.0.0");
  w4rp.setLedPin(8);
  w4rp.registerCapability("relay", onRelay, relayMeta);
  w4rp.begin();
}
```

## What Happens

1. **begin()** initializes:
   - `storage.begin()` - NVS flash
   - `canBus.begin()` - TWAI peripheral
   - Loads boot count
   - Loads rules from NVS
   - `transport.begin()` - BLE advertising

2. **loop()** processes:
   - CAN frames → signal updates
   - Rule evaluation → capability calls
   - BLE commands → protocol handling
   - Debug/status updates

## Key Types

```cpp
// Handler receives params as string map
using ParamMap = std::map<String, String>;
using CapabilityHandler = std::function<void(const ParamMap &)>;

// Metadata for app UI
struct CapabilityMeta {
  String id;
  String label;
  String description;
  String category;
  std::vector<CapabilityParamMeta> params;
};
```

## Don't Block

**Never use `delay()` in loop.** CAN buffer will overflow.

For slow operations:
```cpp
volatile bool workRequested = false;

void onCapability(const ParamMap &params) {
  workRequested = true;  // Set flag only
}

void loop() {
  w4rp.loop();
  
  if (workRequested) {
    workRequested = false;
    doSlowWork();  // Handle outside handler
  }
}
```

## Next

- [Architecture](architecture.md) - Layer structure
- [Capabilities](capabilities.md) - Advanced handlers
- [Rule Engine](../core/rule-engine.md) - Signals and conditions
