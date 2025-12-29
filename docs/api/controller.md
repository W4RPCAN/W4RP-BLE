# Controller API

`W4RP::Controller` is the main orchestrator. It connects the Engine to drivers and handles protocol.

Source: `W4RP.h`

## Constructor

```cpp
Controller(CAN *canBus, Storage *storage, Communication *transport, OTA *otaService = nullptr);
```

| Parameter | Required | Type | Description |
|-----------|----------|------|-------------|
| `canBus` | Yes | `CAN*` | CAN interface implementation |
| `storage` | Yes | `Storage*` | Storage interface implementation |
| `transport` | Yes | `Communication*` | Communication interface |
| `otaService` | No | `OTA*` | OTA interface (nullptr allowed) |

## Configuration

Call these **before** `begin()`.

### setModuleInfo

```cpp
void setModuleInfo(const char *hw, const char *fw,
                   const char *serial = nullptr,
                   const char *moduleId = nullptr,
                   const char *bleName = nullptr);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `hw` | `const char*` | Hardware identifier |
| `fw` | `const char*` | Firmware version |
| `serial` | `const char*` | Serial number (optional) |
| `moduleId` | `const char*` | Override auto-generated ID (optional) |
| `bleName` | `const char*` | BLE advertising name (defaults to moduleId) |

### setLedPin

```cpp
void setLedPin(int8_t pin);
```

GPIO for status LED. -1 disables.

## Lifecycle

### begin

```cpp
void begin();
```

Initializes:
1. `storage_->begin()`
2. `canBus_->begin()`
3. Loads boot_count from NVS, increments
4. Derives moduleId if not set
5. Loads rules from NVS
6. `transport_->begin(advertisingName)`
7. `otaService_->begin()` if available

### loop

```cpp
void loop();
```

Main processing:
1. Check OTA pause state
2. Read CAN frames, `engine_.processCanFrame()`
3. `engine_.evaluateRules()`
4. `transport_->loop()`
5. Send debug updates (if debug mode)
6. Send periodic status
7. Update LED

**Don't block.** No `delay()`.

## Capability Registration

### registerCapability

```cpp
void registerCapability(const String &id, CapabilityHandler handler);
void registerCapability(const String &id, CapabilityHandler handler, const CapabilityMeta &meta);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `const String&` | Capability ID (matches rules) |
| `handler` | `CapabilityHandler` | `std::function<void(const ParamMap&)>` |
| `meta` | `const CapabilityMeta&` | Metadata for profile |

**Warning:** Handlers are called with internal mutex - don't call Controller methods inside.

## Status Queries

| Method | Return | Description |
|--------|--------|-------------|
| `isConnected()` | `bool` | Transport connection status |
| `getUptime()` | `uint32_t` | `millis()` |
| `getBootCount()` | `uint16_t` | Boot counter from NVS |
| `getRulesMode()` | `uint8_t` | 0=empty, 1=RAM, 2=NVS |
| `getModuleId()` | `const char*` | Module identifier |
| `getEngine()` | `Engine&` | Reference to Engine |

## Internal State

| Field | Type | Description |
|-------|------|-------------|
| `rulesMode_` | `uint8_t` | 0=empty, 1=RAM, 2=NVS |
| `bootCount_` | `uint16_t` | Boot counter |
| `streamType_` | `enum` | NONE, RULESET_RAM, RULESET_NVS, DEBUG_WATCH, OTA_FULL, OTA_DELTA |
