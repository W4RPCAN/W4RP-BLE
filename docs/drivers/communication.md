# Communication Driver

`BLETransport` implements the `Communication` interface using ESP32 BLE.

Source: `src/drivers/BLETransport.h`, `src/drivers/BLETransport.cpp`

## BLE Service

```cpp
#define W4RP_SERVICE_UUID     "0000fff0-5734-5250-5734-525000000000"
#define W4RP_CHAR_RX_UUID     "0000fff1-5734-5250-5734-525000000001"
#define W4RP_CHAR_TX_UUID     "0000fff2-5734-5250-5734-525000000002"
#define W4RP_CHAR_STATUS_UUID "0000fff3-5734-5250-5734-525000000003"
```

| Characteristic | Properties | Direction |
|----------------|------------|-----------|
| RX (fff1) | WRITE, WRITE_NR | App → Module |
| TX (fff2) | NOTIFY, READ | Module → App |
| Status (fff3) | NOTIFY, READ | Module → App |

## Interface Methods

| Method | Behavior |
|--------|----------|
| `begin(deviceName)` | Init BLE, create service, start advertising |
| `isConnected()` | Returns `connected_` flag |
| `send(data, len)` | Notify on TX, chunked if needed |
| `sendStatus(data, len)` | Notify on Status |
| `onReceive(callback)` | Set RX callback |
| `onConnectionChange(callback)` | Set connection callback |
| `loop()` | Restart advertising after disconnect |
| `getMTU()` | Returns 128 |

## BLE Initialization

```cpp
bool BLETransport::begin(const char *deviceName) {
  BLEDevice::init(deviceName);
  BLEDevice::setMTU(247);
  
  server_ = BLEDevice::createServer();
  server_->setCallbacks(this);
  
  service_ = server_->createService(W4RP_SERVICE_UUID);
  
  rxChar_ = service_->createCharacteristic(
      W4RP_CHAR_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | 
      BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar_->setCallbacks(this);
  
  txChar_ = service_->createCharacteristic(
      W4RP_CHAR_TX_UUID,
      BLECharacteristic::PROPERTY_NOTIFY | 
      BLECharacteristic::PROPERTY_READ);
  txChar_->addDescriptor(new BLE2902());
  
  statusChar_ = service_->createCharacteristic(
      W4RP_CHAR_STATUS_UUID,
      BLECharacteristic::PROPERTY_NOTIFY | 
      BLECharacteristic::PROPERTY_READ);
  statusChar_->addDescriptor(new BLE2902());
  
  service_->start();
  startAdvertising();
  
  return true;
}
```

## Advertising Parameters

```cpp
void BLETransport::startAdvertising() {
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(W4RP_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);  // 7.5ms
  advertising->setMaxPreferred(0x12);  // 15ms
  advertising->start();
}
```

## Chunked Send

```cpp
void BLETransport::send(const uint8_t *data, size_t len) {
  size_t mtu = getMTU();  // 128
  size_t offset = 0;
  
  while (offset < len) {
    size_t chunkLen = min(mtu, len - offset);
    txChar_->setValue((uint8_t *)(data + offset), chunkLen);
    txChar_->notify();
    offset += chunkLen;
    
    if (offset < len) {
      delay(5);  // Prevent overflow
    }
  }
}
```

## Auto-Reconnect

```cpp
void BLETransport::loop() {
  if (!connected_ && lastDisconnectMs_ > 0) {
    if (millis() - lastDisconnectMs_ > 1000) {  // 1 second delay
      startAdvertising();
      lastDisconnectMs_ = 0;
    }
  }
}
```

## MTU

Returns 128 bytes (safe size, though ESP32 negotiates up to 247).

```cpp
size_t BLETransport::getMTU() const {
  return 128;
}
```

## Usage Example

```cpp
BLETransport transport;

void setup() {
  transport.begin("MyDevice");
  
  transport.onReceive([](const uint8_t *data, size_t len) {
    // Handle incoming data
  });
  
  transport.onConnectionChange([](bool connected) {
    Serial.printf("Connected: %d\n", connected);
  });
}

void loop() {
  transport.loop();
  
  if (transport.isConnected()) {
    transport.send("Hello", 5);
  }
}
```
