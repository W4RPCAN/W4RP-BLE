# Getting Started

## Installation

### Arduino IDE
1. Download the latest release `.zip` file.
2. Go to **Sketch** -> **Include Library** -> **Add .ZIP Library...**
3. Select the downloaded file.

### PlatformIO
Add the following to your `platformio.ini`:
```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
lib_deps =
    bblanchon/ArduinoJson@^6.0.0
    ; Add path to local library if not published
```

## Quick Start Code

Here is a minimal example to get your module running.

```cpp
#include <W4RPBLE.h>

W4RPBLE w4rp;

void setup() {
  Serial.begin(115200);

  // 1. Configure Hardware (Optional overrides)
  // w4rp.setPins(21, 20, 8); 

  // 2. Configure Module Identity
  w4rp.setBleName("My Module");
  w4rp.setModuleFirmware("1.0.0");
  
  // 3. Configure CAN Mode (Safe for vehicles)
  w4rp.setCanMode(W4RPBLE::CanMode::LISTEN_ONLY);
  
  // 4. Initialize
  w4rp.begin();
  
  // 5. Register Actions
  w4rp.registerCapability("toggle_led", [](const W4RPBLE::ParamMap &params) {
      // Handle action
      Serial.println("Action triggered!");
  });
}

void loop() {
  w4rp.loop();
}
```
