/**
 * @file Protocol.cpp
 * @brief CORE:Protocol - WBP parser and serializer implementation
 */

#include "Protocol.h"
#include <cstring>
#include <esp_crc.h>

namespace W4RP {

uint32_t Protocol::calculateCRC32(const uint8_t *data, size_t len) {
  return esp_crc32_le(0, data, len);
}

static String readStringFromTable(const uint8_t *stringTable, uint16_t offset,
                                  size_t tableLen) {
  if (offset >= tableLen)
    return String();

  const char *ptr = reinterpret_cast<const char *>(stringTable + offset);
  size_t maxLen = tableLen - offset;
  size_t len = strnlen(ptr, maxLen);

  if (len == maxLen && (offset + len >= tableLen || ptr[len] != '\0')) {
    return String();
  }

  return String(ptr, len);
}

bool Protocol::parseRules(const uint8_t *data, size_t len,
                          std::vector<RuntimeSignal> &outSignals,
                          std::vector<RuntimeCondition> &outConditions,
                          std::vector<RuntimeAction> &outActions,
                          std::vector<RuntimeRule> &outRules) {
  // Validate minimum length
  if (len < sizeof(WBPRulesHeader)) {
    Serial.println("[WBP] Error: Data too short for header");
    return false;
  }

  const WBPRulesHeader *header = reinterpret_cast<const WBPRulesHeader *>(data);

  // Validate magic
  if (header->magic != WBP_MAGIC_RULES) {
    Serial.printf("[WBP] Error: Invalid magic 0x%08X\n", header->magic);
    return false;
  }

  // Validate version
  if (header->version < WBP_MIN_VERSION || header->version > WBP_VERSION) {
    Serial.printf("[WBP] Error: Unsupported version %d\n", header->version);
    return false;
  }

  // Validate total size
  if (header->totalSize > len) {
    Serial.printf("[WBP] Error: Declared size %d > buffer %d\n",
                  header->totalSize, len);
    return false;
  }

  if (header->totalSize < sizeof(WBPRulesHeader)) {
    Serial.println("[WBP] Error: Total size too small");
    return false;
  }

  // Validate CRC
  const uint8_t *crcData = data + sizeof(WBPRulesHeader);
  size_t crcLen = header->totalSize - sizeof(WBPRulesHeader);
  uint32_t calculatedCrc = calculateCRC32(crcData, crcLen);

  if (calculatedCrc != header->crc32) {
    Serial.printf("[WBP] Error: CRC mismatch 0x%08X != 0x%08X\n", calculatedCrc,
                  header->crc32);
    return false;
  }

  // Calculate offsets
  size_t offset = sizeof(WBPRulesHeader);

  if (header->flags & WBP_FLAG_HAS_META) {
    offset += sizeof(WBPMeta);
  }

  // Validate string table offset
  if (header->stringTableOffset < offset ||
      header->stringTableOffset >= header->totalSize) {
    Serial.println("[WBP] Error: Invalid string table offset");
    return false;
  }

  // Validate counts won't overflow
  size_t expectedSize =
      sizeof(WBPRulesHeader) +
      (header->flags & WBP_FLAG_HAS_META ? sizeof(WBPMeta) : 0) +
      header->signalCount * sizeof(WBPSignal) +
      header->conditionCount * sizeof(WBPCondition) +
      header->actionCount * sizeof(WBPAction) +
      header->actionParamCount * sizeof(WBPActionParam) +
      header->ruleCount * sizeof(WBPRule);

  if (expectedSize > len || header->stringTableOffset < expectedSize) {
    Serial.println("[WBP] Error: Counts exceed buffer");
    return false;
  }

  const uint8_t *stringTable = data + header->stringTableOffset;
  size_t stringTableLen = header->totalSize - header->stringTableOffset;

  // Parse Signals
  outSignals.clear();
  outSignals.reserve(header->signalCount);
  const WBPSignal *signals = reinterpret_cast<const WBPSignal *>(data + offset);

  for (int i = 0; i < header->signalCount; i++) {
    RuntimeSignal sig = {};
    sig.canId = signals[i].canId;
    sig.startBit = signals[i].startBit;
    sig.bitLength = signals[i].bitLength;
    sig.bigEndian = (signals[i].flags & 0x01) != 0;
    sig.isSigned = (signals[i].flags & 0x02) != 0;
    sig.factor = signals[i].factor;
    sig.offset = signals[i].offset;
    outSignals.push_back(sig);
  }
  offset += header->signalCount * sizeof(WBPSignal);

  // Parse Conditions
  outConditions.clear();
  outConditions.reserve(header->conditionCount);
  const WBPCondition *conditions =
      reinterpret_cast<const WBPCondition *>(data + offset);

  for (int i = 0; i < header->conditionCount; i++) {
    RuntimeCondition cond = {};
    cond.signalIdx = conditions[i].signalIdx;

    // Validate signal index
    if (cond.signalIdx >= header->signalCount) {
      Serial.printf("[WBP] Error: Condition %d references invalid signal %d\n",
                    i, cond.signalIdx);
      return false;
    }

    // Validate operation code
    uint8_t opCode = conditions[i].operation;
    if (opCode > static_cast<uint8_t>(Operation::HOLD)) {
      Serial.printf("[WBP] Error: Condition %d has invalid operation %d\n", i,
                    opCode);
      return false;
    }
    cond.operation = static_cast<Operation>(opCode);

    cond.value1 = conditions[i].value1;
    cond.value2 = conditions[i].value2;

    if (cond.operation == Operation::HOLD) {
      if (cond.value1 < 0.0f || cond.value1 > 86400000.0f) {
        Serial.println("[WBP] Invalid hold time");
        return false;
      }
      cond.holdMs = static_cast<uint32_t>(cond.value1);
    }

    outConditions.push_back(cond);
  }
  offset += header->conditionCount * sizeof(WBPCondition);

  // Parse Actions
  outActions.clear();
  outActions.reserve(header->actionCount);
  const WBPAction *actions = reinterpret_cast<const WBPAction *>(data + offset);
  offset += header->actionCount * sizeof(WBPAction);

  const WBPActionParam *actionParams =
      reinterpret_cast<const WBPActionParam *>(data + offset);
  offset += header->actionParamCount * sizeof(WBPActionParam);

  for (int i = 0; i < header->actionCount; i++) {
    RuntimeAction action = {};
    action.capabilityId =
        readStringFromTable(stringTable, actions[i].capStrIdx, stringTableLen);

    if (action.capabilityId.isEmpty()) {
      Serial.printf("[WBP] Error: Empty capability ID at action %d\n", i);
      return false;
    }

    // Bounds check for param start index
    uint8_t paramStart = actions[i].paramStartIdx;
    uint8_t paramCount = actions[i].paramCount;

    if (paramStart + paramCount > header->actionParamCount) {
      Serial.printf("[WBP] Error: Action %d param overflow (start=%d count=%d "
                    "total=%d)\n",
                    i, paramStart, paramCount, header->actionParamCount);
      return false;
    }

    for (int j = 0; j < paramCount; j++) {
      const WBPActionParam &ap = actionParams[paramStart + j];
      RuntimeParam param = {};

      // Validate param type
      if (ap.type > static_cast<uint8_t>(ParamType::BOOL)) {
        Serial.printf("[WBP] Error: Action %d param %d has invalid type %d\n",
                      i, j, ap.type);
        return false;
      }
      param.type = static_cast<ParamType>(ap.type);

      switch (param.type) {
      case ParamType::INT:
      case ParamType::BOOL:
        param.intVal = static_cast<int32_t>(ap.value);
        break;
      case ParamType::FLOAT:
        param.floatVal = static_cast<float>(ap.value) / 100.0f;
        break;
      case ParamType::STRING:
        param.strVal =
            readStringFromTable(stringTable, ap.value, stringTableLen);
        break;
      }

      action.params.push_back(param);
    }

    outActions.push_back(action);
  }

  // Parse Rules
  outRules.clear();
  outRules.reserve(header->ruleCount);
  const WBPRule *rules = reinterpret_cast<const WBPRule *>(data + offset);

  for (int i = 0; i < header->ruleCount; i++) {
    RuntimeRule rule = {};
    rule.conditionMask = rules[i].conditionMask;
    rule.actionStartIdx = rules[i].actionStartIdx;
    rule.actionCount = rules[i].actionCount;
    rule.debounceMs = static_cast<uint16_t>(rules[i].debounceDs) * 10;
    rule.cooldownMs = static_cast<uint16_t>(rules[i].cooldownDs) * 10;

    // Validate condition mask - ensure all referenced conditions exist
    for (size_t c = 0; c < 32; c++) {
      if ((rule.conditionMask & (1 << c)) && c >= header->conditionCount) {
        Serial.printf(
            "[WBP] Error: Rule %d references non-existent condition %d\n", i,
            (int)c);
        return false;
      }
    }

    // Validate action indices
    if (rule.actionStartIdx + rule.actionCount > header->actionCount) {
      Serial.printf("[WBP] Error: Rule %d action range [%d, %d) exceeds %d\n",
                    i, rule.actionStartIdx,
                    rule.actionStartIdx + rule.actionCount,
                    header->actionCount);
      return false;
    }

    outRules.push_back(rule);
  }

  Serial.printf(
      "[WBP] Parsed: %d signals, %d conditions, %d actions, %d rules\n",
      outSignals.size(), outConditions.size(), outActions.size(),
      outRules.size());

  return true;
}

class StringTableBuilder {
public:
  uint16_t add(const String &str) {
    auto it = indexMap_.find(str);
    if (it != indexMap_.end())
      return it->second;

    if (currentOffset_ + str.length() + 1 > 0xFFF0) {
      Serial.println("[WBP] String table overflow");
      return 0;
    }

    uint16_t offset = currentOffset_;
    strings_.push_back(str);
    indexMap_[str] = offset;
    currentOffset_ += str.length() + 1;
    return offset;
  }

  size_t size() const { return currentOffset_; }

  void write(uint8_t *dest) const {
    size_t pos = 0;
    for (const String &s : strings_) {
      memcpy(dest + pos, s.c_str(), s.length());
      pos += s.length();
      dest[pos++] = 0;
    }
  }

private:
  std::vector<String> strings_;
  std::map<String, uint16_t> indexMap_;
  uint16_t currentOffset_ = 0;
};

size_t Protocol::serializeProfile(
    uint8_t *outBuffer, size_t maxLen, const char *moduleId,
    const char *hwVersion, const char *fwVersion, const char *serial,
    uint32_t uptimeMs, uint16_t bootCount, uint8_t rulesMode, uint32_t rulesCRC,
    uint8_t signalCount, uint8_t conditionCount, uint8_t actionCount,
    uint8_t ruleCount,
    const std::vector<std::pair<String, CapabilityMeta>> &capabilities) {
  StringTableBuilder strTable;

  uint16_t moduleIdIdx = strTable.add(String(moduleId));
  uint16_t hwIdx = strTable.add(String(hwVersion));
  uint16_t fwIdx = strTable.add(String(fwVersion));
  uint16_t serialIdx = strTable.add(String(serial ? serial : ""));

  std::vector<WBPCapability> capEntries;
  std::vector<WBPCapParam> capParams;

  for (const auto &entry : capabilities) {
    const CapabilityMeta &meta = entry.second;
    WBPCapability cap = {};
    cap.idStrIdx = strTable.add(meta.id);
    cap.labelStrIdx = strTable.add(meta.label);
    cap.descStrIdx = strTable.add(meta.description);
    cap.categoryStrIdx = strTable.add(meta.category);
    cap.paramCount = meta.params.size();
    cap.paramStartIdx = capParams.size();

    for (const auto &p : meta.params) {
      WBPCapParam param = {};
      param.nameStrIdx = strTable.add(p.name);
      param.descStrIdx = strTable.add(p.description);

      if (p.type == "int")
        param.type = static_cast<uint8_t>(ParamType::INT);
      else if (p.type == "float")
        param.type = static_cast<uint8_t>(ParamType::FLOAT);
      else if (p.type == "string")
        param.type = static_cast<uint8_t>(ParamType::STRING);
      else if (p.type == "bool")
        param.type = static_cast<uint8_t>(ParamType::BOOL);
      else
        param.type = static_cast<uint8_t>(ParamType::INT);

      param.required = p.required ? 1 : 0;
      param.min = p.min;
      param.max = p.max;
      capParams.push_back(param);
    }

    capEntries.push_back(cap);
  }

  size_t headerSize = sizeof(WBPProfileHeader);
  size_t capsSize = capEntries.size() * sizeof(WBPCapability);
  size_t paramsSize = capParams.size() * sizeof(WBPCapParam);
  size_t stringSize = strTable.size();
  size_t totalSize = headerSize + capsSize + paramsSize + stringSize;

  if (totalSize > maxLen)
    return 0;

  WBPProfileHeader header = {};
  header.magic = WBP_MAGIC_PROFILE;
  header.version = WBP_VERSION;
  header.flags = (rulesCRC != 0) ? 0x01 : 0x00;
  header.moduleIdStrIdx = moduleIdIdx;
  header.hwStrIdx = hwIdx;
  header.fwStrIdx = fwIdx;
  header.serialStrIdx = serialIdx;
  header.capabilityCount = capEntries.size();
  header.rulesMode = rulesMode;
  header.rulesCRC = rulesCRC;
  header.signalCount = signalCount;
  header.conditionCount = conditionCount;
  header.actionCount = actionCount;
  header.ruleCount = ruleCount;
  header.uptimeMs = uptimeMs;
  header.bootCount = bootCount;
  header.stringTableOffset = headerSize + capsSize + paramsSize;

  size_t offset = 0;
  memcpy(outBuffer + offset, &header, sizeof(header));
  offset += sizeof(header);

  for (const auto &cap : capEntries) {
    memcpy(outBuffer + offset, &cap, sizeof(cap));
    offset += sizeof(cap);
  }

  for (const auto &param : capParams) {
    memcpy(outBuffer + offset, &param, sizeof(param));
    offset += sizeof(param);
  }

  strTable.write(outBuffer + offset);

  return totalSize;
}

} // namespace W4RP
