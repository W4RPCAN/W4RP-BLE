/**
 * @file NVSStorage.h
 * @brief DRIVERS:NVSStorage - ESP32 NVS storage
 * @version 1.0.0
 *
 * Implements Storage interface for persistent key-value storage.
 * Keys used: boot_count (string), rules_bin (blob)
 */
#pragma once
#include "../interfaces/Storage.h"
#include <nvs.h>
#include <nvs_flash.h>

namespace W4RP {

/**
 * @class NVSStorage
 * @brief ESP32 NVS storage driver
 */
class NVSStorage : public Storage {
public:
  /**
   * @brief Construct with namespace
   * @param ns NVS namespace (default: "w4rp")
   */
  explicit NVSStorage(const char *ns = "w4rp");
  ~NVSStorage();

  /**
   * @brief Initialize NVS, open namespace
   * @return true on success
   */
  bool begin() override;

  /**
   * @brief Write binary data + commit
   * @param key Storage key
   * @param data Data buffer
   * @param len Data length
   * @return true on success
   */
  bool writeBlob(const char *key, const uint8_t *data, size_t len) override;

  /**
   * @brief Read binary data
   * @param key Storage key
   * @param buffer Output buffer
   * @param maxLen Buffer capacity
   * @return Bytes read
   */
  size_t readBlob(const char *key, uint8_t *buffer, size_t maxLen) override;

  /**
   * @brief Write string + commit
   * @param key Storage key
   * @param value String value
   * @return true on success
   */
  bool writeString(const char *key, const String &value) override;

  /**
   * @brief Read string
   * @param key Storage key
   * @return String value or empty
   */
  String readString(const char *key) override;

  /**
   * @brief Delete key + commit
   * @param key Storage key
   * @return true if deleted
   */
  bool erase(const char *key) override;

  /**
   * @brief Flush to flash
   * @return true on success
   */
  bool commit() override;

private:
  const char *namespace_;
  nvs_handle_t handle_ = 0;
  bool opened_ = false;
};

} // namespace W4RP
