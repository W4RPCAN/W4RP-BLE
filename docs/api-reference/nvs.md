# NVS Storage

The library exposes simple key-value storage helpers using the ESP32 Preferences API. You can use these to store custom configuration alongside the module's rules.

## `nvsWrite(key, value)`
Writes a string value to persistent storage.

```cpp
w4rp.nvsWrite("calibration_val", "123.45");
```

## `nvsRead(key)`
Reads a string value. Returns an empty string if the key does not exist.

```cpp
String val = w4rp.nvsRead("calibration_val");
if (val.length() > 0) {
    // ...
}
```
