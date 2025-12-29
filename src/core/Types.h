/**
 * @file Types.h
 * @brief CORE:Types - Runtime structures and callback types
 * @version 1.0.0
 *
 * Defines runtime structures decoded from WBP binary format.
 * Used by Engine for rule evaluation.
 */
#pragma once
#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>

namespace W4RP {

#define WBP_MAGIC_PROFILE 0xC0DE5701
#define WBP_MAGIC_RULES 0xC0DE5702
#define WBP_VERSION 0x02
#define WBP_MIN_VERSION 0x02
#define WBP_FLAG_HAS_META 0x01
#define WBP_FLAG_PERSIST 0x02

/**
 * @enum Operation
 * @brief Condition comparison operators
 */
enum class Operation : uint8_t {
  EQ = 0,
  NE = 1,
  GT = 2,
  GE = 3,
  LT = 4,
  LE = 5,
  WITHIN = 6,
  OUTSIDE = 7,
  HOLD = 8
};

/**
 * @enum ParamType
 * @brief Action parameter types
 */
enum class ParamType : uint8_t { INT = 0, FLOAT = 1, STRING = 2, BOOL = 3 };

/**
 * @struct RuntimeSignal
 * @brief CAN signal definition + runtime state
 */
struct RuntimeSignal {
  uint32_t canId;
  uint16_t startBit;
  uint8_t bitLength;
  bool bigEndian;
  bool isSigned;
  float factor;
  float offset;
  float value = 0.0f;
  float lastValue = 0.0f;
  float lastDebugValue = -999999.9f;
  uint32_t lastUpdateMs = 0;
  bool everSet = false;
};

/**
 * @struct RuntimeCondition
 * @brief Condition definition + hold state
 */
struct RuntimeCondition {
  uint8_t signalIdx;
  Operation operation;
  float value1;
  float value2;
  uint32_t holdMs = 0;
  uint32_t holdStartMs = 0;
  bool holdActive = false;
  bool lastResult = false;
};

/**
 * @struct RuntimeParam
 * @brief Action parameter value
 */
struct RuntimeParam {
  ParamType type;
  union {
    int32_t intVal;
    float floatVal;
  };
  String strVal;
};

/**
 * @struct RuntimeAction
 * @brief Action with capability ID and parameters
 */
struct RuntimeAction {
  String capabilityId;
  std::vector<RuntimeParam> params;
};

/**
 * @struct RuntimeRule
 * @brief Rule definition + runtime state
 */
struct RuntimeRule {
  uint32_t conditionMask;
  uint8_t actionStartIdx;
  uint8_t actionCount;
  uint16_t debounceMs;
  uint16_t cooldownMs;
  uint32_t lastTriggerMs = 0;
  uint32_t lastConditionChangeMs = 0;
  bool lastConditionState = false;
};

/**
 * @struct CapabilityParamMeta
 * @brief Parameter metadata for profile
 */
struct CapabilityParamMeta {
  String name;
  String type;
  bool required = true;
  int min = 0;
  int max = 0;
  String description;
};

/**
 * @struct CapabilityMeta
 * @brief Capability metadata for profile
 */
struct CapabilityMeta {
  String id;
  String label;
  String description;
  String category;
  std::vector<CapabilityParamMeta> params;
};

using ParamMap = std::map<String, String>;
using CapabilityHandler = std::function<void(const ParamMap &)>;

} // namespace W4RP
