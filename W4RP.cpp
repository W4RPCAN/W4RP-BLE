/**
 * @file W4RP.cpp
 * @brief Main W4RP controller implementation
 *
 * Thin orchestrator that coordinates engine, transport, storage, and OTA.
 */

#include "W4RP.h"
#include <esp_mac.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "W4RP";

namespace W4RP {

Controller::Controller(CAN *canBus, Storage *storage, Communication *transport,
                       OTA *otaService)
    : canBus_(canBus), storage_(storage), transport_(transport),
      otaService_(otaService), engine_() {

  // Set transport callbacks
  transport_->onReceive([this](const uint8_t *data, size_t len) {
    if (streamType_ != NONE) {
      handleStreamData(data, len);
    } else {
      handleCommand(data, len);
    }
  });

  transport_->onConnectionChange([this](bool connected) {
    if (!connected) {
      // Reset stream state on disconnect
      streamType_ = NONE;
      streamBuffer_.clear();
      engine_.setDebugMode(false);
      engine_.clearDebugSignals();
    }
  });
}

Controller::~Controller() {
  // Interfaces are not owned, don't delete
}

void Controller::setModuleInfo(const char *hw, const char *fw,
                               const char *serial, const char *moduleId,
                               const char *bleName) {
  hwVersion_ = String(hw);
  fwVersion_ = String(fw);
  if (serial)
    serialNumber_ = String(serial);
  if (moduleId)
    moduleId_ = String(moduleId); // User-provided takes priority
  if (bleName)
    bleName_ = String(bleName); // Custom BLE advertising name
}

void Controller::setLedPin(int8_t pin) {
  ledPin_ = pin;
  if (ledPin_ >= 0) {
    pinMode(ledPin_, OUTPUT);
    digitalWrite(ledPin_, LOW);
  }
}

void Controller::begin() {
  Serial.printf("[%s] Starting...\n", TAG);

  // Initialize components
  storage_->begin();
  canBus_->begin();

  // Load boot count
  bootCount_ = storage_->readString("boot_count").toInt() + 1;
  storage_->writeString("boot_count", String(bootCount_));

  // Derive module ID only if not already set by user
  if (moduleId_.isEmpty()) {
    moduleId_ = deriveModuleId();
  }

  // Load rules from NVS
  loadRulesFromNvs();

  // Start transport (use bleName if set, otherwise moduleId)
  const char *advertisingName =
      bleName_.isEmpty() ? moduleId_.c_str() : bleName_.c_str();
  Serial.printf("[%s] BLE Name: '%s' (bleName_='%s')\n", TAG, advertisingName,
                bleName_.c_str());
  transport_->begin(advertisingName);

  // Initialize OTA if available
  if (otaService_) {
    otaService_->begin();
  }

  Serial.printf("[%s] Ready. ModuleID=%s Boots=%d\n", TAG, moduleId_.c_str(),
                bootCount_);
}

void Controller::loop() {
  if (otaService_ && otaService_->needsPause()) {
    otaService_->loop();
    updateLed();
    return;
  }

  CanFrame frame;
  while (canBus_->receive(frame)) {
    engine_.processCanFrame(frame);
  }

  engine_.evaluateRules();

  if (engine_.isDebugMode()) {
    sendDebugUpdates();
  }

  uint32_t now = millis();
  if (now - lastStatusMs_ >= 5000) {
    sendStatus();
    lastStatusMs_ = now;
  }

  transport_->loop();
  updateLed();

  if (otaService_) {
    otaService_->loop();
  }
}

void Controller::registerCapability(const String &id,
                                    CapabilityHandler handler) {
  engine_.registerCapability(id, handler);
}

void Controller::registerCapability(const String &id, CapabilityHandler handler,
                                    const CapabilityMeta &meta) {
  engine_.registerCapability(id, handler, meta);
}

bool Controller::isConnected() const { return transport_->isConnected(); }

void Controller::handleCommand(const uint8_t *data, size_t len) {
  String packet((char *)data, len);
  packet.trim();

  Serial.printf("[%s] CMD: %s\n", TAG, packet.c_str());

  // GET:PROFILE
  if (packet == "GET:PROFILE") {
    sendProfile();
    return;
  }

  // GET:RULES
  if (packet == "GET:RULES") {
    sendRules();
    return;
  }

  // DEBUG:START
  if (packet == "DEBUG:START") {
    engine_.setDebugMode(true);
    return;
  }

  // DEBUG:STOP
  if (packet == "DEBUG:STOP") {
    engine_.setDebugMode(false);
    engine_.clearDebugSignals();
    return;
  }

  // DEBUG:WATCH:<len>:<crc>
  if (packet.startsWith("DEBUG:WATCH:")) {
    int colon1 = packet.indexOf(':', 12);
    if (colon1 > 12) {
      streamExpectedLen_ = packet.substring(12, colon1).toInt();
      streamExpectedCRC_ =
          strtoul(packet.substring(colon1 + 1).c_str(), nullptr, 10);
      streamType_ = DEBUG_WATCH;
      streamBuffer_.clear();
      streamBuffer_.reserve(streamExpectedLen_);
    }
    return;
  }

  // SET:RULES:RAM:<len>:<crc>
  if (packet.startsWith("SET:RULES:RAM:")) {
    int colon1 = packet.indexOf(':', 14);
    if (colon1 > 14) {
      streamExpectedLen_ = packet.substring(14, colon1).toInt();
      streamExpectedCRC_ =
          strtoul(packet.substring(colon1 + 1).c_str(), nullptr, 10);
      streamType_ = RULESET_RAM;
      streamBuffer_.clear();
      streamBuffer_.reserve(streamExpectedLen_);
    }
    return;
  }

  // SET:RULES:NVS:<len>:<crc>
  if (packet.startsWith("SET:RULES:NVS:")) {
    int colon1 = packet.indexOf(':', 14);
    if (colon1 > 14) {
      streamExpectedLen_ = packet.substring(14, colon1).toInt();
      streamExpectedCRC_ =
          strtoul(packet.substring(colon1 + 1).c_str(), nullptr, 10);
      streamType_ = RULESET_NVS;
      streamBuffer_.clear();
      streamBuffer_.reserve(streamExpectedLen_);
    }
    return;
  }

  // OTA commands (if service available)
  if (otaService_ && packet.startsWith("OTA:")) {
    // OTA:BEGIN:<size>:<crc>
    if (packet.startsWith("OTA:BEGIN:")) {
      int colon1 = packet.indexOf(':', 10);
      if (colon1 > 10) {
        uint32_t size = packet.substring(10, colon1).toInt();
        uint32_t crc =
            strtoul(packet.substring(colon1 + 1).c_str(), nullptr, 16);

        if (otaService_->startFirmwareUpdate(size, crc)) {
          streamType_ = OTA_FULL;
          canBus_->stop();
          transport_->send("OTA:READY");
        } else {
          transport_->send("OTA:ERROR");
        }
      }
      return;
    }

    // OTA:DELTA:<size>:<sourceCrc>
    if (packet.startsWith("OTA:DELTA:")) {
      int colon1 = packet.indexOf(':', 10);
      if (colon1 > 10) {
        uint32_t size = packet.substring(10, colon1).toInt();
        uint32_t sourceCrc =
            strtoul(packet.substring(colon1 + 1).c_str(), nullptr, 16);

        if (otaService_->startDeltaUpdate(size, sourceCrc)) {
          streamType_ = OTA_DELTA;
          canBus_->stop();
          transport_->send("OTA:READY");
        } else {
          transport_->send("OTA:ERROR");
        }
      }
      return;
    }
  }
}

void Controller::handleStreamData(const uint8_t *data, size_t len) {
  String packet((char *)data, len);

  // Check for END marker
  if (packet == "END") {
    finalizeStream();
    return;
  }

  // OTA paths write directly to service
  if (streamType_ == OTA_FULL && otaService_) {
    otaService_->writeFirmwareChunk(data, len);
    return;
  }

  if (streamType_ == OTA_DELTA && otaService_) {
    otaService_->writeDeltaChunk(data, len);
    return;
  }

  // Buffer other streams
  streamBuffer_.insert(streamBuffer_.end(), data, data + len);
}

void Controller::finalizeStream() {
  Serial.printf("[%s] Stream END. Received %d bytes\n", TAG,
                streamBuffer_.size());

  // Handle OTA finalization
  if (streamType_ == OTA_FULL && otaService_) {
    if (otaService_->finalizeFirmwareUpdate()) {
      transport_->send("OTA:SUCCESS");
      delay(1000);
      esp_restart();
    } else {
      transport_->send("OTA:ERROR");
      canBus_->resume();
    }
    streamType_ = NONE;
    return;
  }

  if (streamType_ == OTA_DELTA && otaService_) {
    if (otaService_->finalizeDeltaUpdate()) {
      // Delta task handles completion
    } else {
      transport_->send("OTA:ERROR");
      canBus_->resume();
    }
    streamType_ = NONE;
    return;
  }

  // Verify length
  if (streamBuffer_.size() != streamExpectedLen_) {
    transport_->send("ERR:LEN_MISMATCH");
    streamType_ = NONE;
    streamBuffer_.clear();
    return;
  }

  // Verify CRC
  uint32_t calcCrc =
      Protocol::calculateCRC32(streamBuffer_.data(), streamBuffer_.size());
  if (calcCrc != streamExpectedCRC_) {
    transport_->send("ERR:CRC_FAIL");
    streamType_ = NONE;
    streamBuffer_.clear();
    return;
  }

  // Process based on stream type
  if (streamType_ == DEBUG_WATCH) {
    String defs((char *)streamBuffer_.data(), streamBuffer_.size());
    size_t count = engine_.loadDebugSignals(defs);
    Serial.printf("[%s] Loaded %d debug signals\n", TAG, count);
  } else if (streamType_ == RULESET_RAM || streamType_ == RULESET_NVS) {
    if (engine_.loadRuleset(streamBuffer_.data(), streamBuffer_.size())) {
      // All validations passed, accept ruleset
      rulesMode_ = (streamType_ == RULESET_NVS) ? 2 : 1;

      if (streamType_ == RULESET_NVS) {
        saveRulesToNvs();
      }

      Serial.printf("[%s] Loaded ruleset: %d signals, %d rules\n", TAG,
                    engine_.getSignalCount(), engine_.getRuleCount());
    } else {
      // Check if failure was due to unknown capability
      String unknownCap = engine_.getUnknownCapability();
      if (!unknownCap.isEmpty()) {
        char errMsg[64];
        snprintf(errMsg, sizeof(errMsg), "ERR:CAP_UNKNOWN:%s",
                 unknownCap.c_str());
        transport_->send(errMsg);
        Serial.printf("[%s] Rejected ruleset: unknown capability '%s'\n", TAG,
                      unknownCap.c_str());
      } else {
        transport_->send("ERR:RULES_INVALID");
      }
    }
  }

  streamType_ = NONE;
  streamBuffer_.clear();
}

void Controller::sendProfile() {
  uint8_t buffer[2048];

  // Build capability list
  std::vector<std::pair<String, CapabilityMeta>> caps;
  for (const auto &entry : engine_.getCapabilities()) {
    caps.push_back({entry.first, entry.second});
  }

  size_t len = Protocol::serializeProfile(
      buffer, sizeof(buffer), moduleId_.c_str(), hwVersion_.c_str(),
      fwVersion_.c_str(), serialNumber_.c_str(), millis(), bootCount_,
      rulesMode_, engine_.getRulesetCRC(), engine_.getSignalCount(),
      engine_.getConditionCount(), engine_.getActionCount(),
      engine_.getRuleCount(), caps);

  if (len == 0) {
    transport_->send("ERR:PROFILE_TOO_LARGE");
    return;
  }

  // Send chunked
  transport_->send("BEGIN");
  delay(10);

  size_t mtu = transport_->getMTU();
  for (size_t offset = 0; offset < len; offset += mtu) {
    size_t chunkLen = (len - offset > mtu) ? mtu : (len - offset);
    transport_->send(buffer + offset, chunkLen);
    delay(5);
  }

  char endMsg[64];
  snprintf(endMsg, sizeof(endMsg), "END:%d:%u", (int)len,
           Protocol::calculateCRC32(buffer, len));
  transport_->send(endMsg);
}

void Controller::sendRules() {
  const auto &rules = engine_.getRulesetBinary();

  if (rules.empty()) {
    transport_->send("ERR:NO_RULES");
    return;
  }

  transport_->send("BEGIN");
  delay(10);

  size_t mtu = transport_->getMTU();
  for (size_t offset = 0; offset < rules.size(); offset += mtu) {
    size_t chunkLen =
        (rules.size() - offset > mtu) ? mtu : (rules.size() - offset);
    transport_->send(rules.data() + offset, chunkLen);
    delay(5);
  }

  char endMsg[64];
  snprintf(endMsg, sizeof(endMsg), "END:%d:%u", (int)rules.size(),
           engine_.getRulesetCRC());
  transport_->send(endMsg);
}

void Controller::sendStatus() {
  if (!transport_->isConnected())
    return;

  char status[128];
  snprintf(status, sizeof(status), "S:%d:%d:%d:%d:%lu:%d", rulesMode_,
           (int)engine_.getSignalCount(), (int)engine_.getRuleCount(),
           (int)engine_.getSignalCount(), // Unique CAN IDs (simplified)
           millis(), bootCount_);

  transport_->sendStatus((uint8_t *)status, strlen(status));
}

void Controller::sendDebugUpdates() {
  uint32_t now = millis();
  if (now - lastDebugTxMs_ < 10)
    return; // Rate limit

  RuntimeSignal sig;
  if (engine_.popDirtyDebugSignal(sig)) {
    char msg[128];
    snprintf(msg, sizeof(msg), "D:S:%u:%u:%u:%d:%.4f:%.4f:%.2f", sig.canId,
             sig.startBit, sig.bitLength, sig.bigEndian ? 1 : 0, sig.factor,
             sig.offset, sig.value);

    transport_->send(msg);
    lastDebugTxMs_ = now;
  }
}

void Controller::updateLed() {
  if (ledPin_ < 0)
    return;

  bool connected = transport_->isConnected();
  uint32_t now = millis();

  if (connected) {
    digitalWrite(ledPin_, HIGH); // Solid when connected
  } else {
    // Blink when advertising
    digitalWrite(ledPin_, ((now / 500) % 2) ? HIGH : LOW);
  }
}

void Controller::loadRulesFromNvs() {
  size_t size = storage_->readBlob("rules_bin", nullptr, 0);
  if (size == 0) {
    rulesMode_ = 0;
    return;
  }

  std::vector<uint8_t> buffer(size);
  if (storage_->readBlob("rules_bin", buffer.data(), size) != size) {
    rulesMode_ = 0;
    return;
  }

  if (engine_.loadRuleset(buffer.data(), buffer.size())) {
    rulesMode_ = 2;
    Serial.printf("[%s] Loaded %d rules from NVS\n", TAG,
                  engine_.getRuleCount());
  } else {
    rulesMode_ = 0;
  }
}

void Controller::saveRulesToNvs() {
  const auto &data = engine_.getRulesetBinary();
  if (data.empty())
    return;

  if (storage_->writeBlob("rules_bin", data.data(), data.size())) {
    Serial.printf("[%s] Saved %d bytes to NVS\n", TAG, data.size());
  }
}

String Controller::deriveModuleId() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);

  char id[16];
  snprintf(id, sizeof(id), "W4RP-%02X%02X%02X", mac[3], mac[4], mac[5]);

  return String(id);
}

} // namespace W4RP
