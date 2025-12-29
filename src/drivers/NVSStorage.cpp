/**
 * @file NVSStorage.cpp
 * @brief ESP32 NVS (Non-Volatile Storage) implementation
 */

#include "NVSStorage.h"
#include <esp_log.h>

static const char *TAG = "NVSStorage";

namespace W4RP {

NVSStorage::NVSStorage(const char *ns) : namespace_(ns) {}

NVSStorage::~NVSStorage() {
  if (opened_) {
    nvs_close(handle_);
    opened_ = false;
  }
}

bool NVSStorage::begin() {
  if (opened_)
    return true;

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition truncated, erasing");
    nvs_flash_erase();
    err = nvs_flash_init();
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(err));
    return false;
  }

  err = nvs_open(namespace_, NVS_READWRITE, &handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace_,
             esp_err_to_name(err));
    return false;
  }

  opened_ = true;
  ESP_LOGI(TAG, "NVS opened namespace '%s'", namespace_);
  return true;
}

bool NVSStorage::writeBlob(const char *key, const uint8_t *data, size_t len) {
  if (!opened_)
    return false;

  esp_err_t err = nvs_set_blob(handle_, key, data, len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write blob '%s': %s", key, esp_err_to_name(err));
    return false;
  }

  return commit();
}

size_t NVSStorage::readBlob(const char *key, uint8_t *buffer, size_t maxLen) {
  if (!opened_)
    return 0;

  size_t requiredLen = 0;
  esp_err_t err = nvs_get_blob(handle_, key, nullptr, &requiredLen);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return 0;
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get blob size '%s': %s", key,
             esp_err_to_name(err));
    return 0;
  }

  if (buffer == nullptr) {
    return requiredLen;
  }

  size_t readLen = (requiredLen > maxLen) ? maxLen : requiredLen;
  err = nvs_get_blob(handle_, key, buffer, &readLen);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read blob '%s': %s", key, esp_err_to_name(err));
    return 0;
  }

  return readLen;
}

bool NVSStorage::writeString(const char *key, const String &value) {
  if (!opened_)
    return false;

  esp_err_t err = nvs_set_str(handle_, key, value.c_str());
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write string '%s': %s", key, esp_err_to_name(err));
    return false;
  }

  return commit();
}

String NVSStorage::readString(const char *key) {
  if (!opened_)
    return String();

  size_t requiredLen = 0;
  esp_err_t err = nvs_get_str(handle_, key, nullptr, &requiredLen);

  if (err == ESP_ERR_NVS_NOT_FOUND || requiredLen == 0) {
    return String();
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get string size '%s': %s", key,
             esp_err_to_name(err));
    return String();
  }

  char *buffer = new char[requiredLen];
  err = nvs_get_str(handle_, key, buffer, &requiredLen);

  if (err != ESP_OK) {
    delete[] buffer;
    return String();
  }

  String result(buffer);
  delete[] buffer;
  return result;
}

bool NVSStorage::erase(const char *key) {
  if (!opened_)
    return false;

  esp_err_t err = nvs_erase_key(handle_, key);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(TAG, "Failed to erase '%s': %s", key, esp_err_to_name(err));
    return false;
  }

  return commit();
}

bool NVSStorage::commit() {
  if (!opened_)
    return false;

  esp_err_t err = nvs_commit(handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

} // namespace W4RP
