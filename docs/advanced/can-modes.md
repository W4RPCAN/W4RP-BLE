# CAN Bus Modes

The W4RPBLE library supports different CAN operating modes to ensure compatibility and safety when connected to vehicle networks.

## Setting the Mode
Use `setCanMode` **before** calling `begin()`.

```cpp
w4rp.setCanMode(W4RPBLE::CanMode::LISTEN_ONLY);
```

## Available Modes

| Mode | Enum Value | Description | Use Case |
|------|------------|-------------|----------|
| **Normal** | `W4RPBLE::CanMode::NORMAL` | Standard TX/RX. The controller will acknowledge (ACK) frames it receives. | Lab bench, or when you are the main controller. |
| **Listen Only** | `W4RPBLE::CanMode::LISTEN_ONLY` | Receives frames but **does not** ACK. The TX pin is disabled. | **Connecting to a real vehicle.** Prevents bus errors if baud rate mismatches. |
| **No ACK** | `W4RPBLE::CanMode::NO_ACK` | Sends and receives but does not require ACKs. | specific testing scenarios (Self-Test mode). |

> ⚠️ **Safety Warning:** When connecting to a vehicle OBD2 port or internal CAN wiring, ALWAYS use `LISTEN_ONLY` first. Sending data or ACKs on a bus with incorrect baud rate can trigger dashboard error lights.
