# BLE Communication Protocol

This page describes the low-level protocol used between the W4RPBLE library and the mobile app.

## UUIDs
*   **Service**: `0000fff0-5734-5250-5734-525000000000`
*   **RX Char** (App -> Module): `0000fff1-...`
*   **TX Char** (Module -> App): `0000fff2-...`
*   **Status Char**: `0000fff3-...`

## Streaming Large Data
Because BLE packets are small (MTU ~20-512 bytes), large JSON payloads (like Profiles or Rule Sets) are streamed in chunks.

### Download: Module Profile
When the app requests `GET:PROFILE`:
1.  Module sends `BEGIN`.
2.  Module sends multiple notifications containing JSON chunks.
3.  Module sends `END:<total_bytes>:<crc32>`.
4.  App reconstructs the JSON and verifies CRC.

### Upload: Rule Set
When the app uploads a new configuration:
1.  App sends `SET:RULES:<MODE>:<len>:<crc32>`.
2.  App sends JSON chunks.
3.  App sends `END`.
4.  Module verifies CRC and applies rules.
