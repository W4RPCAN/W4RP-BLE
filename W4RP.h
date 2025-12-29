/**
 * @file W4RP.h
 * @brief Unified W4RP library include
 *
 * This is the main entry point for the modular W4RP library.
 * Include this file and instantiate your preferred drivers.
 *
 * @example Basic usage:
 * @code
 * #include <W4RP.h>
 * using namespace W4RP;
 *
 * TWAICanBus canBus(GPIO_NUM_21, GPIO_NUM_20);
 * NVSStorage storage;
 * BLETransport transport;
 *
 * W4RPController controller(&canBus, &storage, &transport);
 *
 * void setup() {
 *   controller.setModuleInfo("W4RP_V1", "0.5.0", "DEV-001");
 *   controller.begin();
 *   controller.registerCapability("log", myHandler, myMeta);
 * }
 *
 * void loop() {
 *   controller.loop();
 * }
 * @endcode
 */

#pragma once

// Interfaces
#include "src/interfaces/CAN.h"
#include "src/interfaces/Communication.h"
#include "src/interfaces/OTA.h"
#include "src/interfaces/Storage.h"

// Core
#include "src/core/Engine.h"
#include "src/core/Protocol.h"
#include "src/core/Types.h"

// ESP32 Drivers (optional - user can provide their own)
#ifdef ESP32
#include "src/drivers/BLETransport.h"
#include "src/drivers/ESP32OTAService.h"
#include "src/drivers/NVSStorage.h"
#include "src/drivers/TWAICanBus.h"
#endif

namespace W4RP {

/**
 * @brief Main W4RP controller - orchestrates all components
 *
 * Thin wrapper that coordinates engine, transport, storage, and OTA.
 * Thread-safe when used from single core.
 */
class Controller {
public:
  /**
   * @brief Construct with injected dependencies
   * @param canBus CAN bus driver (required)
   * @param storage Storage driver (required)
   * @param transport Communication transport (required)
   * @param otaService OTA service (optional, can be nullptr)
   */
  Controller(CAN *canBus, Storage *storage, Communication *transport,
             OTA *otaService = nullptr);
  ~Controller();

  void setModuleInfo(const char *hw, const char *fw,
                     const char *serial = nullptr,
                     const char *moduleId = nullptr,
                     const char *bleName = nullptr);
  void setLedPin(int8_t pin);
  const char *getModuleId() const { return moduleId_.c_str(); }

  /**
   * @brief Initialize all components
   * Starts CAN, storage, transport, and loads rules from NVS
   */
  void begin();

  /**
   * @brief Main processing loop
   * Call from Arduino loop() - handles CAN, rules, BLE, debug, status
   */
  void loop();

  /**
   * @brief Register a capability handler
   * @warning Handlers are called with internal mutex held - don't call
   * controller methods inside!
   */
  void registerCapability(const String &id, CapabilityHandler handler);
  void registerCapability(const String &id, CapabilityHandler handler,
                          const CapabilityMeta &meta);

  bool isConnected() const;
  uint32_t getUptime() const { return millis(); }
  uint16_t getBootCount() const { return bootCount_; }
  uint8_t getRulesMode() const { return rulesMode_; }
  Engine &getEngine() { return engine_; }

private:
  CAN *canBus_;
  Storage *storage_;
  Communication *transport_;
  OTA *otaService_;
  Engine engine_;

  // Module info
  String moduleId_;
  String bleName_; // Custom BLE advertising name (empty = use moduleId_)
  String hwVersion_;
  String fwVersion_;
  String serialNumber_;
  int8_t ledPin_ = -1;

  // State
  uint16_t bootCount_ = 0;
  uint8_t rulesMode_ = 0; // 0=empty, 1=RAM, 2=NVS
  bool debugMode_ = false;

  // Stream state
  enum StreamType {
    NONE,
    RULESET_RAM,
    RULESET_NVS,
    DEBUG_WATCH,
    OTA_FULL,
    OTA_DELTA
  };
  StreamType streamType_ = NONE;
  std::vector<uint8_t> streamBuffer_;
  uint32_t streamExpectedLen_ = 0;
  uint32_t streamExpectedCRC_ = 0;

  uint32_t lastStatusMs_ = 0;
  uint32_t lastDebugTxMs_ = 0;

  /** @brief Parse and dispatch incoming command packet */
  void handleCommand(const uint8_t *data, size_t len);

  /** @brief Accumulate streamed binary data or forward to OTA */
  void handleStreamData(const uint8_t *data, size_t len);

  /** @brief Validate CRC, apply buffered data based on stream type */
  void finalizeStream();

  /**
   * @brief Serialize and send module profile as chunked WBP binary
   * Includes: moduleId, hw/fw version, serial, uptime, bootCount,
   * rulesMode, rulesCRC, signal/condition/action/rule counts, capabilities
   * Format: BEGIN → binary chunks → END:<len>:<crc>
   */
  void sendProfile();

  /**
   * @brief Send current ruleset binary to client
   * Format: BEGIN → binary chunks → END:<len>:<crc>
   * Returns ERR:NO_RULES if no ruleset loaded
   */
  void sendRules();

  /**
   * @brief Send status via status characteristic (every 5s when connected)
   * Format:
   * S:<rulesMode>:<signalCount>:<ruleCount>:<canIds>:<uptimeMs>:<bootCount>
   */
  void sendStatus();

  /** @brief Push dirty debug signal values to client (rate-limited) */
  void sendDebugUpdates();

  /** @brief Set LED based on connection state (call every loop, stateless) */
  void updateLed();

  /** @brief Load persisted ruleset from NVS on boot */
  void loadRulesFromNvs();

  /** @brief Persist current ruleset to NVS */
  void saveRulesToNvs();

  /** @brief Generate module ID from Bluetooth MAC address */
  String deriveModuleId();
};

} // namespace W4RP
