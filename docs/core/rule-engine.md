# Rule Engine

The Engine is the brain. It processes CAN frames, evaluates conditions, and fires actions.

Source: `src/core/Engine.h`, `src/core/Types.h`

## Concepts

### Signals

A signal extracts a value from a CAN frame.

```cpp
struct RuntimeSignal {
  uint32_t canId;           // CAN ID to watch
  uint16_t startBit;        // Bit position (0-63)
  uint8_t bitLength;        // Bits to extract (1-64)
  bool bigEndian;           // Byte order
  bool isSigned;            // Signed interpretation
  float factor;             // Scale multiplier
  float offset;             // Offset to add
  
  // Runtime state
  float value = 0.0f;
  float lastValue = 0.0f;
  float lastDebugValue = -999999.9f;
  uint32_t lastUpdateMs = 0;
  bool everSet = false;
};
```

Physical value: `raw_bits * factor + offset`

### Conditions

A condition compares a signal to thresholds.

```cpp
struct RuntimeCondition {
  uint8_t signalIdx;        // Index into signals array
  Operation operation;       // Comparison type
  float value1;             // First threshold
  float value2;             // Second threshold (WITHIN/OUTSIDE/HOLD)
  
  // Runtime state
  uint32_t holdMs = 0;
  uint32_t holdStartMs = 0;
  bool holdActive = false;
  bool lastResult = false;
};
```

### Operations

```cpp
enum class Operation : uint8_t {
  EQ = 0,      // ==
  NE = 1,      // !=
  GT = 2,      // >
  GE = 3,      // >=
  LT = 4,      // <
  LE = 5,      // <=
  WITHIN = 6,  // value1 <= signal <= value2
  OUTSIDE = 7, // signal < value1 OR signal > value2
  HOLD = 8     // signal active for value1 ms
};
```

### Actions

An action calls a capability with parameters.

```cpp
struct RuntimeAction {
  String capabilityId;
  std::vector<RuntimeParam> params;
};

struct RuntimeParam {
  ParamType type;  // INT, FLOAT, STRING, BOOL
  union {
    int32_t intVal;
    float floatVal;
  };
  String strVal;
};
```

### Rules

A rule connects conditions to actions.

```cpp
struct RuntimeRule {
  uint32_t conditionMask;   // Bitmap of required conditions (AND)
  uint8_t actionStartIdx;    // First action index
  uint8_t actionCount;       // Number of actions
  uint16_t debounceMs;       // Must stay true for N ms
  uint16_t cooldownMs;       // Minimum time between triggers
  
  // Runtime state
  uint32_t lastTriggerMs = 0;
  uint32_t lastConditionChangeMs = 0;
  bool lastConditionState = false;
};
```

## Evaluation Loop

Every `Controller::loop()`:

```cpp
// 1. Read CAN frames
CanFrame frame;
while (canBus_->receive(frame)) {
  engine_.processCanFrame(frame);
}

// 2. Evaluate rules
engine_.evaluateRules();
```

### processCanFrame()

1. Look up signals by CAN ID
2. Extract bits using `decodeSignal()`
3. Update `value`, `lastValue`, `lastUpdateMs`, `everSet`
4. If debug mode: check dirty queue

### evaluateRules()

For each rule:
1. Check all conditions in `conditionMask` (AND logic)
2. Track state change for debounce
3. Check debounce and cooldown
4. Execute actions

## Condition Evaluation

```cpp
bool Engine::evaluateCondition(RuntimeCondition &cond, uint32_t nowMs) {
  if (cond.signalIdx >= signals_.size()) return false;
  
  RuntimeSignal &sig = signals_[cond.signalIdx];
  if (!sig.everSet) return false;  // Never received
  
  float val = sig.value;
  constexpr float EPSILON = 0.0001f;
  
  // HOLD operation
  if (cond.operation == Operation::HOLD) {
    bool active = (fabsf(val) > EPSILON);
    if (active) {
      if (!cond.holdActive) {
        cond.holdActive = true;
        cond.holdStartMs = nowMs;
      }
      return (nowMs - cond.holdStartMs) >= cond.holdMs;
    } else {
      cond.holdActive = false;
      return false;
    }
  }
  
  // Standard operations
  switch (cond.operation) {
    case Operation::EQ: return fabsf(val - cond.value1) < EPSILON;
    case Operation::NE: return fabsf(val - cond.value1) >= EPSILON;
    case Operation::GT: return val > cond.value1;
    case Operation::GE: return val >= cond.value1;
    case Operation::LT: return val < cond.value1;
    case Operation::LE: return val <= cond.value1;
    case Operation::WITHIN: return val >= cond.value1 && val <= cond.value2;
    case Operation::OUTSIDE: return val < cond.value1 || val > cond.value2;
    default: return false;
  }
}
```

## Signal Decoding

```cpp
float Engine::decodeSignal(const RuntimeSignal &sig, const uint8_t *data) {
  uint64_t raw = extractBits(data, sig.startBit, sig.bitLength, sig.bigEndian);
  float val;
  
  if (sig.isSigned && sig.bitLength < 64) {
    if (raw & (1ULL << (sig.bitLength - 1))) {
      raw |= (~0ULL << sig.bitLength);  // Sign extend
    }
    val = (float)(int64_t)raw;
  } else {
    val = (float)raw;
  }
  
  return val * sig.factor + sig.offset;
}
```

## Capability Validation

Rules are validated BEFORE committing:

```cpp
bool Engine::loadRuleset(const uint8_t *data, size_t len) {
  // Parse...
  
  // Validate all capabilities exist
  for (const RuntimeAction &action : newActions) {
    if (handlers_.find(action.capabilityId) == handlers_.end()) {
      unknownCapability_ = action.capabilityId;
      return false;  // Reject entire ruleset
    }
  }
  
  // Only now commit
  signals_ = std::move(newSignals);
  // ...
}
```

Existing rules are preserved on failure.

## Types Reference

```cpp
enum class ParamType : uint8_t {
  INT = 0,
  FLOAT = 1,
  STRING = 2,
  BOOL = 3
};

using ParamMap = std::map<String, String>;
using CapabilityHandler = std::function<void(const ParamMap &)>;
```
