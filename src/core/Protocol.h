/**
 * @file Protocol.h
 * @brief CORE:Protocol - WBP parser and serializer
 * @version 1.0.0
 *
 * WBP (W4RP Binary Protocol) for rules and profile.
 */
#pragma once
#include "Types.h"
#include <vector>

namespace W4RP {

/**
 * @class Protocol
 * @brief WBP protocol utilities
 */
class Protocol {
public:
  /**
   * @brief Calculate CRC32 (IEEE 802.3)
   * @param data Data buffer
   * @param len Data length
   * @return CRC32 checksum
   */
  static uint32_t calculateCRC32(const uint8_t *data, size_t len);

  /**
   * @brief Parse WBP rules payload
   * @param data WBP binary
   * @param len Data length
   * @param outSignals Output signals
   * @param outConditions Output conditions
   * @param outActions Output actions
   * @param outRules Output rules
   * @return true if parsed successfully
   */
  static bool parseRules(const uint8_t *data, size_t len,
                         std::vector<RuntimeSignal> &outSignals,
                         std::vector<RuntimeCondition> &outConditions,
                         std::vector<RuntimeAction> &outActions,
                         std::vector<RuntimeRule> &outRules);

  /**
   * @brief Serialize module profile to WBP
   * @return Bytes written
   */
  static size_t serializeProfile(
      uint8_t *outBuffer, size_t maxLen, const char *moduleId,
      const char *hwVersion, const char *fwVersion, const char *serial,
      uint32_t uptimeMs, uint16_t bootCount, uint8_t rulesMode,
      uint32_t rulesCRC, uint8_t signalCount, uint8_t conditionCount,
      uint8_t actionCount, uint8_t ruleCount,
      const std::vector<std::pair<String, CapabilityMeta>> &capabilities);
};

#pragma pack(push, 1)

struct WBPRulesHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint16_t totalSize;
  uint8_t signalCount;
  uint8_t conditionCount;
  uint8_t actionCount;
  uint8_t ruleCount;
  uint16_t actionParamCount;
  uint16_t metaOffset;
  uint16_t stringTableOffset;
  uint16_t reserved;
  uint32_t crc32;
};

struct WBPMeta {
  uint8_t vehicleUUID[16];
  uint16_t authorStrIdx;
  uint16_t reserved1;
  uint64_t createdAt;
  uint64_t updatedAt;
  uint32_t reserved2;
};

struct WBPSignal {
  uint32_t canId;
  uint16_t startBit;
  uint8_t bitLength;
  uint8_t flags;
  float factor;
  float offset;
};

struct WBPCondition {
  uint8_t signalIdx;
  uint8_t operation;
  uint16_t reserved;
  float value1;
  float value2;
};

struct WBPAction {
  uint16_t capStrIdx;
  uint8_t paramCount;
  uint8_t paramStartIdx;
  uint32_t reserved;
};

struct WBPActionParam {
  uint8_t type;
  uint8_t reserved;
  uint16_t value;
};

struct WBPRule {
  uint16_t flowIdStrIdx; // String table index for flow ID (for diagram
                         // reconstruction)
  uint32_t conditionMask;
  uint8_t actionStartIdx;
  uint8_t actionCount;
  uint8_t debounceDs;
  uint8_t cooldownDs;
};

struct WBPProfileHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint16_t moduleIdStrIdx;
  uint16_t hwStrIdx;
  uint16_t fwStrIdx;
  uint16_t serialStrIdx;
  uint8_t capabilityCount;
  uint8_t rulesMode;
  uint32_t rulesCRC;
  uint8_t signalCount;
  uint8_t conditionCount;
  uint8_t actionCount;
  uint8_t ruleCount;
  uint32_t uptimeMs;
  uint16_t bootCount;
  uint16_t stringTableOffset;
};

struct WBPCapability {
  uint16_t idStrIdx;
  uint16_t labelStrIdx;
  uint16_t descStrIdx;
  uint16_t categoryStrIdx;
  uint8_t paramCount;
  uint8_t paramStartIdx;
  uint16_t reserved;
};

struct WBPCapParam {
  uint16_t nameStrIdx;
  uint16_t descStrIdx;
  uint8_t type;
  uint8_t required;
  uint16_t reserved;
  int16_t min;
  int16_t max;
};

#pragma pack(pop)

} // namespace W4RP
