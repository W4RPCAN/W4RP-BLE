/**
 * @file Engine.cpp
 * @brief CORE:Engine - Rule evaluation engine implementation
 */

#include "Engine.h"
#include "Protocol.h"
#include <algorithm>
#include <cmath>

namespace W4RP {

static uint64_t extractBits(const uint8_t data[8], uint16_t start, uint8_t len,
                            bool bigEndian) {
  if (len == 0 || len > 64)
    return 0;

  uint64_t result = 0;

  if (!bigEndian) {
    for (uint8_t i = 0; i < len; i++) {
      uint16_t bitPos = start + i;
      uint8_t byteIdx = bitPos / 8;
      uint8_t bitIdx = bitPos % 8;
      if (byteIdx < 8) {
        uint8_t bit = (data[byteIdx] >> bitIdx) & 1;
        result |= ((uint64_t)bit << i);
      }
    }
  } else {
    for (uint8_t i = 0; i < len; i++) {
      int bitPos = start - i;
      if (bitPos < 0 || bitPos >= 64)
        continue;
      uint8_t byteIdx = bitPos / 8;
      uint8_t bitIdx = bitPos % 8;
      uint8_t bit = (data[byteIdx] >> bitIdx) & 1;
      result = (result << 1) | bit;
    }
  }

  return result;
}

Engine::Engine() {}

float Engine::decodeSignal(const RuntimeSignal &sig, const uint8_t *data) {
  uint64_t raw = extractBits(data, sig.startBit, sig.bitLength, sig.bigEndian);
  float val;

  if (sig.isSigned) {
    if (sig.bitLength > 0 && sig.bitLength < 64) {
      if (raw & (1ULL << (sig.bitLength - 1))) {
        raw |= (~0ULL << sig.bitLength);
      }
    }
    val = (float)(int64_t)raw;
  } else {
    val = (float)raw;
  }

  return val * sig.factor + sig.offset;
}

bool Engine::loadRuleset(const uint8_t *data, size_t len) {
  std::vector<RuntimeSignal> newSignals;
  std::vector<RuntimeCondition> newConditions;
  std::vector<RuntimeAction> newActions;
  std::vector<RuntimeRule> newRules;

  if (!Protocol::parseRules(data, len, newSignals, newConditions, newActions,
                            newRules)) {
    return false;
  }

  // Validate capabilities BEFORE committing (preserve existing rules on
  // failure)
  for (const RuntimeAction &action : newActions) {
    if (handlers_.find(action.capabilityId) == handlers_.end()) {
      unknownCapability_ = action.capabilityId;
      return false;
    }
  }
  unknownCapability_ = ""; // Clear on success

  // Swap atomically (only after validation passes)
  signals_ = std::move(newSignals);
  conditions_ = std::move(newConditions);
  actions_ = std::move(newActions);
  rules_ = std::move(newRules);

  // Build signal lookup map
  signalMap_.clear();
  for (RuntimeSignal &sig : signals_) {
    signalMap_[sig.canId].push_back(&sig);
  }

  // Store binary for persistence
  rulesetBinary_.assign(data, data + len);
  rulesetCRC_ = Protocol::calculateCRC32(data, len);

  return true;
}

void Engine::clearRuleset() {
  signals_.clear();
  conditions_.clear();
  actions_.clear();
  rules_.clear();
  signalMap_.clear();
  rulesetBinary_.clear();
  rulesetCRC_ = 0;
  rulesTriggered_ = 0;
}

void Engine::registerCapability(const String &id, CapabilityHandler handler) {
  handlers_[id] = handler;
}

void Engine::registerCapability(const String &id, CapabilityHandler handler,
                                const CapabilityMeta &meta) {
  handlers_[id] = handler;
  capabilityMeta_[id] = meta;
}

void Engine::processCanFrame(const CanFrame &frame) {
  uint32_t now = millis();

  // Update ruleset signals
  auto it = signalMap_.find(frame.id);
  if (it != signalMap_.end()) {
    for (RuntimeSignal *sig : it->second) {
      sig->lastValue = sig->value;
      sig->value = decodeSignal(*sig, frame.data);
      sig->lastUpdateMs = now;
      sig->everSet = true;
    }
  }

  // Update debug signals
  if (debugMode_) {
    auto dit = debugSignalMap_.find(frame.id);
    if (dit != debugSignalMap_.end()) {
      for (size_t idx : dit->second) {
        RuntimeSignal &sig = debugSignals_[idx];
        sig.lastValue = sig.value;
        sig.value = decodeSignal(sig, frame.data);
        sig.lastUpdateMs = now;
        sig.everSet = true;

        // Push to dirty queue if changed
        if (fabsf(sig.value - sig.lastDebugValue) > 0.01f) {
          if (!debugDirtyFlags_[idx] && debugDirtyQueue_.size() < 64) {
            debugDirtyFlags_[idx] = true;
            debugDirtyQueue_.push_back(idx);
          }
        }
      }
    }
  }
}

bool Engine::evaluateCondition(RuntimeCondition &cond, uint32_t nowMs) {
  if (cond.signalIdx >= signals_.size())
    return false;

  RuntimeSignal &sig = signals_[cond.signalIdx];
  if (!sig.everSet)
    return false;

  float val = sig.value;
  constexpr float EPSILON = 0.0001f;

  // Handle HOLD operation
  if (cond.operation == Operation::HOLD) {
    bool active = (fabsf(val) > EPSILON); // Fixed: use epsilon
    if (active) {
      if (!cond.holdActive) {
        cond.holdActive = true;
        cond.holdStartMs = nowMs;
      }
      return (nowMs - cond.holdStartMs) >= cond.holdMs;
    } else {
      cond.holdActive = false;
      cond.holdStartMs = 0;
      return false;
    }
  }

  // Standard operations
  switch (cond.operation) {
  case Operation::EQ:
    return (fabsf(val - cond.value1) < EPSILON); // Fixed: use epsilon
  case Operation::NE:
    return (fabsf(val - cond.value1) >= EPSILON); // Fixed: use epsilon
  case Operation::GT:
    return (val > cond.value1);
  case Operation::GE:
    return (val >= cond.value1);
  case Operation::LT:
    return (val < cond.value1);
  case Operation::LE:
    return (val <= cond.value1);
  case Operation::WITHIN:
    return (val >= cond.value1 && val <= cond.value2);
  case Operation::OUTSIDE:
    return (val < cond.value1 || val > cond.value2);
  default:
    return false;
  }
}

void Engine::executeAction(RuntimeAction &action) {
  auto it = handlers_.find(action.capabilityId);
  if (it == handlers_.end())
    return;

  // Convert params to map
  ParamMap params;
  for (size_t i = 0; i < action.params.size(); i++) {
    const RuntimeParam &p = action.params[i];
    char key[16];
    snprintf(key, sizeof(key), "p%d", (int)i);

    if (p.type == ParamType::STRING) {
      params[String(key)] = p.strVal;
    } else if (p.type == ParamType::FLOAT) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%.4f", p.floatVal);
      params[String(key)] = String(buf);
    } else {
      params[String(key)] = String(p.intVal);
    }
  }

  it->second(params);
}

void Engine::evaluateRules() {
  uint32_t nowMs = millis();

  for (RuntimeRule &rule : rules_) {
    // Evaluate all conditions in mask (AND logic)
    bool allMet = true;

    for (size_t c = 0; c < conditions_.size() && c < 32; c++) {
      if (rule.conditionMask & (1 << c)) {
        if (!evaluateCondition(conditions_[c], nowMs)) {
          allMet = false;
          break;
        }
      }
    }

    // Track state change for debounce
    if (allMet != rule.lastConditionState) {
      rule.lastConditionState = allMet;
      rule.lastConditionChangeMs = nowMs;
    }

    if (!allMet)
      continue;

    // Check debounce and cooldown
    bool debounced = (nowMs - rule.lastConditionChangeMs) >= rule.debounceMs;
    bool cooldownOk = (nowMs - rule.lastTriggerMs) >= rule.cooldownMs;

    if (!debounced || !cooldownOk)
      continue;

    // Execute actions
    for (size_t a = rule.actionStartIdx;
         a < rule.actionStartIdx + rule.actionCount && a < actions_.size();
         a++) {
      executeAction(actions_[a]);
    }

    rule.lastTriggerMs = nowMs;
    rulesTriggered_++;
  }
}

size_t Engine::loadDebugSignals(const String &definitions) {
  std::vector<RuntimeSignal> newSignals;
  std::map<uint32_t, std::vector<size_t>> newMap;

  int start = 0;
  while (start < (int)definitions.length()) {
    int comma = definitions.indexOf(',', start);
    if (comma < 0)
      comma = definitions.length();

    String def = definitions.substring(start, comma);
    def.trim();

    if (def.length() > 0) {
      // Parse: CanId:StartBit:BitLen:BE:Factor:Offset
      int p1 = def.indexOf(':');
      int p2 = def.indexOf(':', p1 + 1);
      int p3 = def.indexOf(':', p2 + 1);
      int p4 = def.indexOf(':', p3 + 1);
      int p5 = def.indexOf(':', p4 + 1);

      if (p1 > 0 && p2 > p1 && p3 > p2 && p4 > p3 && p5 > p4) {
        RuntimeSignal sig = {};
        sig.canId = def.substring(0, p1).toInt();
        sig.startBit = def.substring(p1 + 1, p2).toInt();
        sig.bitLength = def.substring(p2 + 1, p3).toInt();
        sig.bigEndian = def.substring(p3 + 1, p4).toInt() != 0;
        sig.factor = def.substring(p4 + 1, p5).toFloat();
        sig.offset = def.substring(p5 + 1).toFloat();
        sig.isSigned = false;
        sig.lastDebugValue = -999999.9f;

        size_t idx = newSignals.size();
        newSignals.push_back(sig);
        newMap[sig.canId].push_back(idx);
      }
    }
    start = comma + 1;
  }

  debugSignals_ = std::move(newSignals);
  debugSignalMap_ = std::move(newMap);
  debugDirtyFlags_.assign(debugSignals_.size(), false);
  debugDirtyQueue_.clear();
  debugQueueHead_ = 0;
  debugMode_ = true;

  return debugSignals_.size();
}

void Engine::clearDebugSignals() {
  debugSignals_.clear();
  debugSignalMap_.clear();
  debugDirtyFlags_.clear();
  debugDirtyQueue_.clear();
  debugQueueHead_ = 0;
  debugMode_ = false;
}

bool Engine::popDirtyDebugSignal(RuntimeSignal &outSignal) {
  if (debugQueueHead_ >= debugDirtyQueue_.size()) {
    // Queue exhausted - reset
    if (debugQueueHead_ > 0) {
      debugDirtyQueue_.clear();
      debugQueueHead_ = 0;
    }
    return false;
  }

  size_t idx = debugDirtyQueue_[debugQueueHead_++];
  if (idx < debugSignals_.size()) {
    debugDirtyFlags_[idx] = false;
    outSignal = debugSignals_[idx];
    debugSignals_[idx].lastDebugValue = debugSignals_[idx].value;
    return true;
  }

  return false;
}

} // namespace W4RP
