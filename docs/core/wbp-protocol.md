# WBP Protocol

WBP (W4RP Binary Protocol) is the compact binary format for rules and profiles.

## Magic Numbers

| Magic | Type |
|-------|------|
| `0xC0DE5701` | Profile |
| `0xC0DE5702` | Rules |

## Version

Current version: `0x02`
Minimum supported: `0x02`

---

## Rules Payload

### WBPRulesHeader (24 bytes)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 4 | `magic` | uint32_t | `0xC0DE5702` |
| 4 | 1 | `version` | uint8_t | Protocol version |
| 5 | 1 | `flags` | uint8_t | Bit 0: HAS_META, Bit 1: PERSIST |
| 6 | 2 | `totalSize` | uint16_t | Total payload size |
| 8 | 1 | `signalCount` | uint8_t | Number of signals |
| 9 | 1 | `conditionCount` | uint8_t | Number of conditions |
| 10 | 1 | `actionCount` | uint8_t | Number of actions |
| 11 | 1 | `ruleCount` | uint8_t | Number of rules |
| 12 | 2 | `actionParamCount` | uint16_t | Total action parameters |
| 14 | 2 | `metaOffset` | uint16_t | Offset to WBPMeta (0 if none) |
| 16 | 2 | `stringTableOffset` | uint16_t | Offset to string table |
| 18 | 2 | `reserved` | uint16_t | Reserved |
| 20 | 4 | `crc32` | uint32_t | CRC32 of data after header |

### WBPMeta (40 bytes, optional)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 16 | `vehicleUUID` | uint8_t[16] | Vehicle UUID |
| 16 | 2 | `authorStrIdx` | uint16_t | Author string index |
| 18 | 2 | `reserved1` | uint16_t | Reserved |
| 20 | 8 | `createdAt` | uint64_t | Unix timestamp (ms) |
| 28 | 8 | `updatedAt` | uint64_t | Unix timestamp (ms) |
| 36 | 4 | `reserved2` | uint32_t | Reserved |

### WBPSignal (16 bytes each)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 4 | `canId` | uint32_t | CAN ID |
| 4 | 2 | `startBit` | uint16_t | Start bit position |
| 6 | 1 | `bitLength` | uint8_t | Bit length (1-64) |
| 7 | 1 | `flags` | uint8_t | Bit 0: bigEndian, Bit 1: signed |
| 8 | 4 | `factor` | float | Scale factor |
| 12 | 4 | `offset` | float | Offset value |

### WBPCondition (12 bytes each)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 1 | `signalIdx` | uint8_t | Signal index |
| 1 | 1 | `operation` | uint8_t | Operation enum |
| 2 | 2 | `reserved` | uint16_t | Reserved |
| 4 | 4 | `value1` | float | First comparison value |
| 8 | 4 | `value2` | float | Second value (WITHIN/OUTSIDE/HOLD) |

**Operation values:**

| Value | Name | Description |
|-------|------|-------------|
| 0 | EQ | signal == value1 |
| 1 | NE | signal != value1 |
| 2 | GT | signal > value1 |
| 3 | GE | signal >= value1 |
| 4 | LT | signal < value1 |
| 5 | LE | signal <= value1 |
| 6 | WITHIN | value1 <= signal <= value2 |
| 7 | OUTSIDE | signal < value1 OR signal > value2 |
| 8 | HOLD | signal active for value1 ms |

### WBPAction (8 bytes each)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 2 | `capStrIdx` | uint16_t | Capability ID string index |
| 2 | 1 | `paramCount` | uint8_t | Number of parameters |
| 3 | 1 | `paramStartIdx` | uint8_t | First param index |
| 4 | 4 | `reserved` | uint32_t | Reserved |

### WBPActionParam (4 bytes each)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 1 | `type` | uint8_t | Parameter type |
| 1 | 1 | `reserved` | uint8_t | Reserved |
| 2 | 2 | `value` | uint16_t | Value or string index |

**Type values:**

| Value | Name | Value interpretation |
|-------|------|----------------------|
| 0 | INT | Raw int16 |
| 1 | FLOAT | value / 100.0 |
| 2 | STRING | String table index |
| 3 | BOOL | 0 or 1 |

### WBPRule (8 bytes each)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 4 | `conditionMask` | uint32_t | Condition bitmap (AND logic) |
| 4 | 1 | `actionStartIdx` | uint8_t | First action index |
| 5 | 1 | `actionCount` | uint8_t | Number of actions |
| 6 | 1 | `debounceDs` | uint8_t | Debounce in deciseconds (×10ms) |
| 7 | 1 | `cooldownDs` | uint8_t | Cooldown in deciseconds (×10ms) |

### String Table

Null-terminated strings, consecutively packed. Indices are byte offsets from table start.

---

## Profile Payload

### WBPProfileHeader (36 bytes)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 4 | `magic` | uint32_t | `0xC0DE5701` |
| 4 | 1 | `version` | uint8_t | Protocol version |
| 5 | 1 | `flags` | uint8_t | Bit 0: has rules |
| 6 | 2 | `moduleIdStrIdx` | uint16_t | Module ID string index |
| 8 | 2 | `hwStrIdx` | uint16_t | Hardware version string index |
| 10 | 2 | `fwStrIdx` | uint16_t | Firmware version string index |
| 12 | 2 | `serialStrIdx` | uint16_t | Serial number string index |
| 14 | 1 | `capabilityCount` | uint8_t | Number of capabilities |
| 15 | 1 | `rulesMode` | uint8_t | 0=none, 1=RAM, 2=NVS |
| 16 | 4 | `rulesCRC` | uint32_t | Current rules CRC32 |
| 20 | 1 | `signalCount` | uint8_t | Loaded signals |
| 21 | 1 | `conditionCount` | uint8_t | Loaded conditions |
| 22 | 1 | `actionCount` | uint8_t | Loaded actions |
| 23 | 1 | `ruleCount` | uint8_t | Loaded rules |
| 24 | 4 | `uptimeMs` | uint32_t | Uptime in milliseconds |
| 28 | 2 | `bootCount` | uint16_t | Boot counter |
| 30 | 2 | `stringTableOffset` | uint16_t | Offset to string table |

### WBPCapability (12 bytes each)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 2 | `idStrIdx` | uint16_t | Capability ID string index |
| 2 | 2 | `labelStrIdx` | uint16_t | Label string index |
| 4 | 2 | `descStrIdx` | uint16_t | Description string index |
| 6 | 2 | `categoryStrIdx` | uint16_t | Category string index |
| 8 | 1 | `paramCount` | uint8_t | Number of parameters |
| 9 | 1 | `paramStartIdx` | uint8_t | First param index |
| 10 | 2 | `reserved` | uint16_t | Reserved |

### WBPCapParam (12 bytes each)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 2 | `nameStrIdx` | uint16_t | Parameter name string index |
| 2 | 2 | `descStrIdx` | uint16_t | Description string index |
| 4 | 1 | `type` | uint8_t | Parameter type |
| 5 | 1 | `required` | uint8_t | Required flag (0/1) |
| 6 | 2 | `reserved` | uint16_t | Reserved |
| 8 | 2 | `min` | int16_t | Minimum value |
| 10 | 2 | `max` | int16_t | Maximum value |

---

## Commands

Text commands sent over Communication interface:

| Command | Direction | Description |
|---------|-----------|-------------|
| `GET:PROFILE` | App → Module | Request WBP profile |
| `GET:RULES` | App → Module | Request current WBP ruleset |
| `SET:RULES:RAM:<len>:<crc>` | App → Module | Load rules to RAM only |
| `SET:RULES:NVS:<len>:<crc>` | App → Module | Load rules to NVS (persisted) |
| `DEBUG:START` | App → Module | Enable debug mode |
| `DEBUG:STOP` | App → Module | Disable debug mode |
| `DEBUG:WATCH:<len>:<crc>` | App → Module | Load debug signal definitions |
| `OTA:BEGIN:<size>:<crc>` | App → Module | Start full firmware update |
| `OTA:DELTA:<size>:<sourceCrc>` | App → Module | Start delta firmware update |
| `END` | App → Module | End binary stream |

### Responses

| Response | Description |
|----------|-------------|
| `OTA:READY` | OTA transfer can begin |
| `OTA:ERROR` | OTA start failed |
| `OTA:SUCCESS` | OTA completed |
| `RULES:OK` | Rules loaded successfully |
| `RULES:ERROR:<reason>` | Rules load failed |

### Binary Streams

Commands with `<len>:<crc>` expect binary data to follow:

1. App sends command (e.g., `SET:RULES:NVS:512:3847291`)
2. App sends `<len>` bytes of binary data
3. App sends `END`
4. Module validates CRC32 and processes

---

## CRC32

IEEE 802.3 polynomial. Calculated over everything after the 24-byte header.

```cpp
uint32_t Protocol::calculateCRC32(const uint8_t *data, size_t len) {
    return esp_crc32_le(0, data, len);
}
```

---

## Validation Order

1. Magic number
2. Version range
3. Total size vs buffer
4. CRC32
5. Count bounds
6. String table bounds
7. Signal index refs
8. Action param bounds
9. Capability existence (Engine)

Any failure rejects entire payload. No partial loading.
