# Capabilities

Capabilities allow your device to expose "actions" that can be triggered by the rules engine.

## `registerCapability`

### Simple Registration
Use this for simple actions that don't need complex parameters.
```cpp
w4rp.registerCapability("toggle_led", [](const W4RPBLE::ParamMap &params) {
    digitalWrite(LED_BUILTIN, HIGH);
});
```

### Full Metadata Registration
Use this to tell the mobile app exactly what parameters your capability expects.

```cpp
W4RPBLE::CapabilityMeta meta;
meta.id = "exhaust_flap";
meta.label = "Exhaust Flap";
meta.category = "output";

W4RPBLE::CapabilityParamMeta param;
param.name = "amount";
param.type = "int";
param.min = 0;
param.max = 100;
meta.params.push_back(param);

w4rp.registerCapability(meta, onExhaustFlap);
```

## Types

### `ParamMap`
A standard map containing the parameters sent by the rule engine.
```cpp
using ParamMap = std::map<String, String>;
```

### `CapabilityHandler`
The function signature for your callbacks.
```cpp
using CapabilityHandler = std::function<void(const ParamMap &)>;
```
