# Dependency Injection

W4RP uses constructor injection. You pick the drivers.

Source: `W4RP.h`

## Constructor

```cpp
Controller(CAN *canBus, Storage *storage, Communication *transport, OTA *otaService = nullptr);
```

| Parameter | Required | Description |
|-----------|----------|-------------|
| `canBus` | Yes | CAN interface |
| `storage` | Yes | Storage interface |
| `transport` | Yes | Communication interface |
| `otaService` | No | OTA interface (can be nullptr) |

## Usage

```cpp
// You create drivers
TWAICanBus canBus(GPIO_NUM_21, GPIO_NUM_20);
NVSStorage storage;
BLETransport transport;

// You inject them
Controller w4rp(&canBus, &storage, &transport);
```

Controller calls interface methods. It doesn't know the concrete types.

## Without OTA

```cpp
Controller w4rp(&canBus, &storage, &transport, nullptr);
// or just:
Controller w4rp(&canBus, &storage, &transport);
```

Controller checks nullptr before OTA calls:
```cpp
if (otaService_ && packet.startsWith("OTA:")) {
  // Handle OTA
}
```

## With OTA

```cpp
ESP32OTAService otaService;
Controller w4rp(&canBus, &storage, &transport, &otaService);
```

## Custom Drivers

Implement the interface:

```cpp
class MCP2515CanBus : public CAN {
public:
  MCP2515CanBus(uint8_t csPin) : csPin_(csPin) {}
  
  bool begin() override {
    return mcp.begin(csPin_) == CAN_OK;
  }
  
  bool receive(CanFrame &frame) override {
    if (mcp.checkReceive() != CAN_MSGAVAIL) return false;
    // Read and convert frame
    return true;
  }
  
  bool transmit(const CanFrame &frame) override {
    return mcp.sendMsgBuf(frame.id, 0, frame.dlc, frame.data) == CAN_OK;
  }
  
  void stop() override { /* ... */ }
  void resume() override { /* ... */ }
  bool isRunning() const override { return running_; }
  
private:
  uint8_t csPin_;
  MCP_CAN mcp;
  bool running_ = false;
};
```

Then use it:
```cpp
MCP2515CanBus canBus(10);  // CS on pin 10
Controller w4rp(&canBus, &storage, &transport);
```

## WiFi Instead of BLE

```cpp
class WiFiTransport : public Communication {
public:
  bool begin(const char *deviceName) override {
    WiFi.begin(ssid, password);
    server.begin();
    return true;
  }
  
  bool isConnected() const override {
    return client && client.connected();
  }
  
  void send(const uint8_t *data, size_t len) override {
    if (client) client.write(data, len);
  }
  
  void sendStatus(const uint8_t *data, size_t len) override {
    send(data, len);  // Same channel
  }
  
  void onReceive(TransportRxCallback cb) override {
    rxCallback_ = cb;
  }
  
  void onConnectionChange(TransportConnCallback cb) override {
    connCallback_ = cb;
  }
  
  void loop() override {
    if (!client) {
      client = server.available();
      if (client && connCallback_) connCallback_(true);
    }
    
    if (client && client.available() && rxCallback_) {
      uint8_t buf[256];
      int len = client.read(buf, sizeof(buf));
      rxCallback_(buf, len);
    }
  }
  
  size_t getMTU() const override { return 1024; }
};
```

## Testing

```cpp
class MockCAN : public CAN {
public:
  std::queue<CanFrame> frames;
  
  bool begin() override { return true; }
  
  bool receive(CanFrame &frame) override {
    if (frames.empty()) return false;
    frame = frames.front();
    frames.pop();
    return true;
  }
  
  bool transmit(const CanFrame &frame) override { return true; }
  void stop() override {}
  void resume() override {}
  bool isRunning() const override { return true; }
};

void test_signal_decoding() {
  MockCAN mockCan;
  MockStorage mockStorage;
  MockTransport mockTransport;
  
  Controller w4rp(&mockCan, &mockStorage, &mockTransport);
  
  // Inject test frame
  mockCan.frames.push({.id = 0x0C0, .data = {0, 0, 0x0B, 0xB8}});
  
  w4rp.loop();
  
  // Verify decoding
  // ...
}
```
