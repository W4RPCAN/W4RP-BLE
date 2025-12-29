# CAN Driver

`TWAICanBus` implements the `CAN` interface using ESP32's native TWAI peripheral.

Source: `src/drivers/TWAICanBus.h`, `src/drivers/TWAICanBus.cpp`

## Constructor

```cpp
TWAICanBus(gpio_num_t txPin = GPIO_NUM_21,
           gpio_num_t rxPin = GPIO_NUM_20,
           twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS(),
           twai_mode_t mode = TWAI_MODE_LISTEN_ONLY);
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `txPin` | `gpio_num_t` | `GPIO_NUM_21` | CAN TX GPIO |
| `rxPin` | `gpio_num_t` | `GPIO_NUM_20` | CAN RX GPIO |
| `timing` | `twai_timing_config_t` | 500 kbps | Baud rate config |
| `mode` | `twai_mode_t` | `LISTEN_ONLY` | Operating mode |

## Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| `TWAI_MODE_LISTEN_ONLY` | Receive only, no ACK | Reading vehicle CAN |
| `TWAI_MODE_NORMAL` | Full participation | Custom CAN networks |
| `TWAI_MODE_NO_ACK` | Transmit without ACK | Testing, loopback |

## Baud Rates

| Rate | Macro |
|------|-------|
| 1 Mbps | `TWAI_TIMING_CONFIG_1MBITS()` |
| 500 kbps | `TWAI_TIMING_CONFIG_500KBITS()` |
| 250 kbps | `TWAI_TIMING_CONFIG_250KBITS()` |
| 125 kbps | `TWAI_TIMING_CONFIG_125KBITS()` |

## Interface Methods

| Method | Description |
|--------|-------------|
| `begin()` | Install and start TWAI driver |
| `receive(CanFrame&)` | Non-blocking read, returns true if frame |
| `transmit(const CanFrame&)` | Queue frame, 100ms timeout |
| `stop()` | Stop bus activity |
| `resume()` | Restart bus (calls begin if not installed) |
| `isRunning()` | Returns `running_` flag |

## Extended Methods

| Method | Description |
|--------|-------------|
| `begin(rxQueueLen, txQueueLen)` | Custom queue sizes (default: 64/16) |
| `isInstalled()` | Returns `installed_` flag |
| `getStatus()` | Returns `BusStatus` enum |
| `getErrorCount()` | Returns TX + RX error counters |
| `recover()` | Calls `twai_initiate_recovery()` |

## BusStatus Enum

```cpp
enum class BusStatus {
  NOT_INSTALLED,  // Driver not installed
  STOPPED,        // Installed but stopped
  RUNNING,        // Active
  RECOVERING,     // TWAI_STATE_RECOVERING
  BUS_OFF,        // TWAI_STATE_BUS_OFF
  ERROR           // twai_get_status_info failed
};
```

## Internal Constants

```cpp
constexpr uint32_t DEFAULT_RX_QUEUE_LEN = 64;
constexpr uint32_t DEFAULT_TX_QUEUE_LEN = 16;
constexpr TickType_t DEFAULT_TX_TIMEOUT_MS = 100;
```

## Driver Alerts

Enabled alerts:
- `TWAI_ALERT_TX_IDLE`
- `TWAI_ALERT_TX_SUCCESS`
- `TWAI_ALERT_TX_FAILED`
- `TWAI_ALERT_ERR_PASS`
- `TWAI_ALERT_BUS_OFF`
- `TWAI_ALERT_RX_QUEUE_FULL`

## Wiring

```
ESP32              CAN Transceiver       Vehicle
GPIO21 (TX) ────►  TX  ┌──────────┐
                       │SN65HVD230│ ────► CAN_H
GPIO20 (RX) ◄────  RX  │          │ ────► CAN_L
                       └──────────┘
3.3V ──────────────►  VCC
GND ───────────────►  GND
```

## Usage Example

```cpp
TWAICanBus canBus(GPIO_NUM_21, GPIO_NUM_20,
                  TWAI_TIMING_CONFIG_500KBITS(),
                  TWAI_MODE_LISTEN_ONLY);

void setup() {
  canBus.begin();
}

void loop() {
  CanFrame frame;
  while (canBus.receive(frame)) {
    // Process frame
  }
  
  if (canBus.getStatus() == BusStatus::BUS_OFF) {
    canBus.recover();
  }
}
```
