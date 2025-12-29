# Engine API

`W4RP::Engine` evaluates rules. Transport-agnostic - no BLE, no NVS.

Source: `src/core/Engine.h`

## Ruleset Management

### loadRuleset

```cpp
bool loadRuleset(const uint8_t *data, size_t len);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `const uint8_t*` | WBP binary |
| `len` | `size_t` | Data length |
| **Returns** | `bool` | true if parsed successfully |

Validates all capabilities exist BEFORE committing. On failure, existing rules are preserved.

### getUnknownCapability

```cpp
String getUnknownCapability() const;
```

Returns the capability ID that caused `loadRuleset()` to fail.

### clearRuleset

```cpp
void clearRuleset();
```

Clears signals, conditions, actions, rules. Resets triggered count.

### getRulesetBinary

```cpp
const std::vector<uint8_t> &getRulesetBinary() const;
```

Returns binary for persistence.

### getRulesetCRC

```cpp
uint32_t getRulesetCRC() const;
```

Returns CRC32 of current ruleset.

## Capability Registration

### registerCapability

```cpp
void registerCapability(const String &id, CapabilityHandler handler);
void registerCapability(const String &id, CapabilityHandler handler, const CapabilityMeta &meta);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `const String&` | Capability ID |
| `handler` | `CapabilityHandler` | `std::function<void(const ParamMap&)>` |
| `meta` | `const CapabilityMeta&` | Metadata |

### getCapabilities

```cpp
const std::map<String, CapabilityMeta> &getCapabilities() const;
```

Returns all registered capabilities with metadata.

## CAN Processing

### processCanFrame

```cpp
void processCanFrame(const CanFrame &frame);
```

Updates signal values from CAN frame. Called by Controller for each received frame.

### evaluateRules

```cpp
void evaluateRules();
```

Checks all conditions, executes triggered actions. Called by Controller each loop.

## Debug Mode

### loadDebugSignals

```cpp
size_t loadDebugSignals(const String &definitions);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `definitions` | `const String&` | Comma-separated signal specs |
| **Returns** | `size_t` | Number of signals parsed |

Format: `canId:startBit:bitLength:bigEndian:factor:offset`

Example: `"0x0C0:16:16:0:0.25:0,0x1A4:0:8:0:1:0"`

### clearDebugSignals

```cpp
void clearDebugSignals();
```

Clears debug signals and disables debug mode.

### popDirtyDebugSignal

```cpp
bool popDirtyDebugSignal(RuntimeSignal &outSignal);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `outSignal` | `RuntimeSignal&` | Output signal |
| **Returns** | `bool` | true if dirty signal found |

### isDebugMode / setDebugMode

```cpp
bool isDebugMode() const;
void setDebugMode(bool enabled);
```

## Status Queries

| Method | Return | Description |
|--------|--------|-------------|
| `getSignalCount()` | `size_t` | Number of signals |
| `getConditionCount()` | `size_t` | Number of conditions |
| `getActionCount()` | `size_t` | Number of actions |
| `getRuleCount()` | `size_t` | Number of rules |
| `getRulesTriggered()` | `uint32_t` | Total triggers since load |
| `getUnknownCapability()` | `String` | Failed capability ID |

## Private Methods

| Method | Description |
|--------|-------------|
| `evaluateCondition(RuntimeCondition&, uint32_t nowMs)` | Evaluate single condition |
| `executeAction(RuntimeAction&)` | Call capability handler |
| `decodeSignal(const RuntimeSignal&, const uint8_t*)` | Extract bits, apply factor/offset |
