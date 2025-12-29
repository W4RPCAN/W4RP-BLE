# Architecture

W4RP is layered. Each layer has one responsibility.

Source: `W4RP.h`, `src/`

## Layer Diagram

```
┌─────────────────────────────────────────┐
│  YOUR SKETCH (.ino)                     │
│  - Instantiate drivers                  │
│  - Register capabilities                │
│  - Call w4rp.loop()                     │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│  CONTROLLER (W4RP.h)                    │
│  Orchestrates engine + drivers          │
│  Handles protocol commands              │
└─────────────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
┌───────────┐ ┌───────────┐ ┌───────────┐
│  ENGINE   │ │ PROTOCOL  │ │  TYPES    │
│ Rule eval │ │WBP binary │ │ Structs   │
└───────────┘ └───────────┘ └───────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│  INTERFACES                             │
│  Abstract contracts                     │
│  CAN | Storage | Communication | OTA    │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│  DRIVERS (ESP32)                        │
│  Concrete implementations               │
│  TWAICanBus | NVSStorage | BLETransport │
└─────────────────────────────────────────┘
```

## File Map

```
W4RP-BLE/
├── W4RP.h                     ← Controller class
├── W4RP.cpp                   ← Controller impl
├── src/
│   ├── core/
│   │   ├── Engine.h / .cpp    ← Rule evaluation
│   │   ├── Protocol.h / .cpp  ← WBP parser
│   │   └── Types.h            ← Shared types
│   ├── interfaces/
│   │   ├── CAN.h              ← CAN contract
│   │   ├── Storage.h          ← Storage contract
│   │   ├── Communication.h    ← Transport contract
│   │   └── OTA.h              ← OTA contract
│   └── drivers/
│       ├── TWAICanBus.h/.cpp  ← ESP32 CAN
│       ├── NVSStorage.h/.cpp  ← ESP32 NVS
│       ├── BLETransport.h/.cpp← ESP32 BLE
│       └── ESP32OTAService.*  ← ESP32 OTA
└── examples/
    └── OTA/                   ← With firmware updates
```

## Controller

`W4RP::Controller` coordinates everything:

```cpp
class Controller {
public:
  Controller(CAN*, Storage*, Communication*, OTA* = nullptr);
  
  void setModuleInfo(...);
  void setLedPin(int8_t);
  void registerCapability(...);
  
  void begin();  // Init all components
  void loop();   // Main processing
  
  bool isConnected() const;
  Engine &getEngine();
};
```

It doesn't know what TWAI or BLE is. It calls interface methods.

## Engine

`W4RP::Engine` evaluates rules:

```cpp
class Engine {
public:
  bool loadRuleset(const uint8_t*, size_t);
  void clearRuleset();
  
  void registerCapability(...);
  void processCanFrame(const CanFrame&);
  void evaluateRules();
  
  size_t getSignalCount();
  size_t getRuleCount();
};
```

Transport-agnostic. No BLE, no NVS.

## Interfaces

Abstract base classes:

| Interface | Methods |
|-----------|---------|
| `CAN` | begin, receive, transmit, stop, resume, isRunning |
| `Storage` | begin, writeBlob, readBlob, writeString, readString, erase |
| `Communication` | begin, send, sendStatus, onReceive, onConnectionChange, loop, getMTU |
| `OTA` | begin, abort, startFirmwareUpdate, writeFirmwareChunk, etc. |

## Drivers

ESP32-specific implementations:

| Driver | Interface | Hardware |
|--------|-----------|----------|
| `TWAICanBus` | CAN | TWAI peripheral |
| `NVSStorage` | Storage | NVS flash |
| `BLETransport` | Communication | ESP32 BLE |
| `ESP32OTAService` | OTA | Dual partitions |

## Why This Design

**Testability**: Mock drivers for unit tests.

```cpp
class MockCAN : public CAN {
  bool begin() override { return true; }
  bool receive(CanFrame &f) override { return false; }
  // ...
};
```

**Flexibility**: Swap implementations.

```cpp
// Use SPI CAN chip instead
MCP2515CanBus canBus(CS_PIN);
Controller w4rp(&canBus, &storage, &transport);
```

**Clarity**: Each component does one thing. Bugs are isolated.
