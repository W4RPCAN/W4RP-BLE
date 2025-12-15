# Lifecycle Methods

## `begin()`
Initializes the BLE stack, CAN driver, and loads any persisted rules from NVS.
This method is blocking and takes about 500ms to complete.

```cpp
void setup() {
  // configuration...
  w4rp.begin();
}
```

## `loop()`
The main processing function. This must be called frequently in the Arduino `loop()`.
It handles:
- Reading CAN frames
- Evaluating rules
- Managing BLE connections
- Sending status updates

```cpp
void loop() {
  w4rp.loop();
}
```
