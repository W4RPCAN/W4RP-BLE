/**
 * @file Engine.h
 * @brief CORE:Engine - Rule evaluation engine
 * @version 1.0.0
 *
 * Transport-agnostic rule engine. Processes CAN frames,
 * evaluates conditions, executes actions.
 */
#pragma once
#include "../interfaces/CAN.h"
#include "Types.h"
#include <map>
#include <vector>

namespace W4RP {

/**
 * @class Engine
 * @brief Rule evaluation engine
 */
class Engine {
public:
  Engine();
  ~Engine() = default;

  /**
   * @brief Load WBP binary ruleset
   * @param data WBP binary
   * @param len Data length
   * @return true if parsed successfully
   */
  bool loadRuleset(const uint8_t *data, size_t len);

  /**
   * @brief Get capability that caused load failure
   * @return Unknown capability ID or empty
   */
  String getUnknownCapability() const { return unknownCapability_; }

  /// @brief Clear all rules and signals
  void clearRuleset();

  /// @brief Get ruleset binary for persistence
  const std::vector<uint8_t> &getRulesetBinary() const {
    return rulesetBinary_;
  }

  /// @brief Get ruleset CRC32
  uint32_t getRulesetCRC() const { return rulesetCRC_; }

  /**
   * @brief Register capability handler
   * @param id Capability ID
   * @param handler Callback function
   */
  void registerCapability(const String &id, CapabilityHandler handler);

  /**
   * @brief Register capability with metadata
   * @param id Capability ID
   * @param handler Callback function
   * @param meta Capability metadata
   */
  void registerCapability(const String &id, CapabilityHandler handler,
                          const CapabilityMeta &meta);

  /// @brief Get registered capabilities
  const std::map<String, CapabilityMeta> &getCapabilities() const {
    return capabilityMeta_;
  }

  /**
   * @brief Process received CAN frame
   * @param frame CAN frame from bus
   */
  void processCanFrame(const CanFrame &frame);

  /// @brief Evaluate rules and execute triggered actions
  void evaluateRules();

  /**
   * @brief Load debug signal definitions
   * @param definitions Comma-separated signal specs
   * @return Number of signals parsed
   */
  size_t loadDebugSignals(const String &definitions);

  /// @brief Clear debug signals
  void clearDebugSignals();

  /**
   * @brief Get changed debug signal
   * @param outSignal Output signal
   * @return true if dirty signal found
   */
  bool popDirtyDebugSignal(RuntimeSignal &outSignal);

  /// @brief Check debug mode active
  bool isDebugMode() const { return debugMode_; }

  /// @brief Set debug mode
  void setDebugMode(bool enabled) { debugMode_ = enabled; }

  size_t getSignalCount() const { return signals_.size(); }
  size_t getConditionCount() const { return conditions_.size(); }
  size_t getActionCount() const { return actions_.size(); }
  size_t getRuleCount() const { return rules_.size(); }
  uint32_t getRulesTriggered() const { return rulesTriggered_; }

private:
  std::vector<RuntimeSignal> signals_;
  std::vector<RuntimeCondition> conditions_;
  std::vector<RuntimeAction> actions_;
  std::vector<RuntimeRule> rules_;
  std::vector<uint8_t> rulesetBinary_;
  uint32_t rulesetCRC_ = 0;

  std::map<uint32_t, std::vector<RuntimeSignal *>> signalMap_;
  std::map<String, CapabilityHandler> handlers_;
  std::map<String, CapabilityMeta> capabilityMeta_;

  bool debugMode_ = false;
  std::vector<RuntimeSignal> debugSignals_;
  std::map<uint32_t, std::vector<size_t>> debugSignalMap_;
  std::vector<bool> debugDirtyFlags_;
  std::vector<size_t> debugDirtyQueue_;
  size_t debugQueueHead_ = 0;

  uint32_t rulesTriggered_ = 0;
  String unknownCapability_;

  bool evaluateCondition(RuntimeCondition &cond, uint32_t nowMs);
  void executeAction(RuntimeAction &action);
  float decodeSignal(const RuntimeSignal &sig, const uint8_t *data);
};

} // namespace W4RP
