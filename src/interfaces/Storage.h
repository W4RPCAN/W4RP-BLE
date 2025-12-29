/**
 * @file Storage.h
 * @brief W4RP::Storage
 * @version 1.0.0
 *
 * W4RP::Controller - Persist rulesets and configuration
 * Keys: rules_bin, boot_count
 */

#pragma once

#include <Arduino.h>

namespace W4RP {

class Storage {
public:
  virtual ~Storage() = default;

  virtual bool begin() = 0;
  virtual bool writeBlob(const char *key, const uint8_t *data, size_t len) = 0;
  virtual size_t readBlob(const char *key, uint8_t *buffer, size_t maxLen) = 0;
  virtual bool writeString(const char *key, const String &value) = 0;
  virtual String readString(const char *key) = 0;
  virtual bool erase(const char *key) = 0;
  virtual bool commit() { return true; }
};

} // namespace W4RP