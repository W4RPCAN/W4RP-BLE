# Capabilities

Capabilities are the actions your module can perform.

Source: `src/core/Types.h`, `W4RP.h`

## Types

```cpp
// Handler callback
using ParamMap = std::map<String, String>;
using CapabilityHandler = std::function<void(const ParamMap &)>;

// Metadata for app
struct CapabilityMeta {
  String id;
  String label;
  String description;
  String category;
  std::vector<CapabilityParamMeta> params;
};

struct CapabilityParamMeta {
  String name;
  String type;         // "int", "float", "string", "bool"
  bool required = true;
  int min = 0;
  int max = 0;
  String description;
};
```

## Registration

### Simple (no metadata)

```cpp
void onRelay(const ParamMap &params) {
  // ...
}

w4rp.registerCapability("relay", onRelay);
```

### With Metadata

```cpp
CapabilityMeta meta = {
  .id = "relay",
  .label = "Relay Control",
  .description = "Toggle relay output",
  .category = "outputs",
  .params = {
    {
      .name = "state",
      .type = "int",
      .required = true,
      .min = 0,
      .max = 1,
      .description = "0=off, 1=on"
    }
  }
};

w4rp.registerCapability("relay", onRelay, meta);
```

## Handler Parameters

Parameters arrive as `p0`, `p1`, `p2`, etc. Always strings.

```cpp
void onDualOutput(const ParamMap &params) {
  // p0 = channel, p1 = value
  auto it0 = params.find("p0");
  auto it1 = params.find("p1");
  
  if (it0 != params.end() && it1 != params.end()) {
    int channel = it0->second.toInt();
    float value = it1->second.toFloat();
    setOutput(channel, value);
  }
}
```

## Categories

Used by app to group capabilities:

| Category | Examples |
|----------|----------|
| `outputs` | Relays, valves, motors |
| `display` | LEDs, screens |
| `audio` | Buzzers, speakers |
| `debug` | Logging, testing |

## Handler Rules

1. **Don't block** - No `delay()`, no long loops
2. **Don't call Controller** - Mutex is held
3. **Be fast** - Under 10ms target

### Async Pattern

```cpp
volatile bool valveRequested = false;
int requestedPosition = 0;

void onValve(const ParamMap &params) {
  requestedPosition = params.at("p0").toInt();
  valveRequested = true;  // Flag only
}

void loop() {
  w4rp.loop();
  
  if (valveRequested) {
    valveRequested = false;
    moveValve(requestedPosition);  // Slow operation here
  }
}
```

## Parameter Types

| Type | WBP Value | Conversion |
|------|-----------|------------|
| `int` | Raw int | `.toInt()` |
| `float` | value/100 | `.toFloat()` |
| `string` | String index | Direct |
| `bool` | 0/1 | `.toInt()` |

## Example: Exhaust Control

```cpp
CapabilityMeta exhaustMeta = {
  .id = "exhaust",
  .label = "Exhaust Valve",
  .description = "Control exhaust valve position",
  .category = "outputs",
  .params = {
    {
      .name = "mode",
      .type = "int",
      .required = true,
      .min = 0,
      .max = 3,
      .description = "0=quiet, 1=normal, 2=sport, 3=race"
    }
  }
};

void onExhaust(const ParamMap &params) {
  int mode = params.at("p0").toInt();
  
  switch (mode) {
    case 0: valveClose(); break;
    case 1: valveHalf(); break;
    case 2: valveOpen(); break;
    case 3: valveOpen(); enablePops(); break;
  }
}

void setup() {
  w4rp.registerCapability("exhaust", onExhaust, exhaustMeta);
  w4rp.begin();
}
```

## Querying Capabilities

```cpp
Engine &engine = w4rp.getEngine();
const auto &caps = engine.getCapabilities();

for (const auto &[id, meta] : caps) {
  Serial.printf("- %s: %s\n", id.c_str(), meta.label.c_str());
}
```
