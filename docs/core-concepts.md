# Core Concepts

W4RPBLE is designed to bridge the gap between low-level CAN bus signals and high-level automation rules controllable via Bluetooth Low Energy (BLE).

## Architecture

1.  **CAN Bus Layer**: Reads raw frames from the vehicle's bus (via the ESP32 TWAI driver).
2.  **Signal Decoding**: Extracts useful data (e.g., "RPM", "Oil Temp") from raw frames based on a defined schema.
3.  **Rules Engine**: Evaluates conditions (e.g., `RPM > 3000` AND `Pedal > 50%`).
4.  **Flow Execution**: Triggers actions when rules are met.
5.  **BLE Interface**: Allows a mobile app to update rules, monitor live attributes, and trigger manual overrides.

## Terminology

*   **Signal**: A specific value extracted from a CAN frame (e.g., Engine Speed).
*   **Rule/Node**: A logical condition (e.g., `Value > X`) or operation.
*   **Flow**: A collection of nodes that results in a trigger.
*   **Capability**: A distinct action the hardware can perform (e.g., "Open Exhaust Valve", "Blink LED").
