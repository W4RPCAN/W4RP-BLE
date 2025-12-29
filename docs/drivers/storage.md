# Storage Driver

`NVSStorage` implements the `Storage` interface using ESP32's Non-Volatile Storage.

Source: `src/drivers/NVSStorage.h`, `src/drivers/NVSStorage.cpp`

## Constructor

```cpp
explicit NVSStorage(const char *ns = "w4rp");
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ns` | `const char*` | `"w4rp"` | NVS namespace |

## Interface Methods

| Method | Behavior |
|--------|----------|
| `begin()` | Calls `nvs_flash_init()`, opens namespace with `NVS_READWRITE` |
| `writeBlob(key, data, len)` | Writes with `nvs_set_blob()`, auto-commits |
| `readBlob(key, buffer, maxLen)` | Returns size if buffer is nullptr |
| `writeString(key, value)` | Writes with `nvs_set_str()`, auto-commits |
| `readString(key)` | Returns empty String if not found |
| `erase(key)` | Calls `nvs_erase_key()`, auto-commits |
| `commit()` | Calls `nvs_commit()` |

## NVS Flash Init

On `begin()`:
```cpp
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
    err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
  nvs_flash_erase();  // Erase and retry
  err = nvs_flash_init();
}
```

## Auto-Commit

Every write operation commits immediately:

```cpp
bool NVSStorage::writeBlob(...) {
  esp_err_t err = nvs_set_blob(handle_, key, data, len);
  if (err != ESP_OK) return false;
  return commit();  // Auto-commit
}
```

## Storage Keys Used by Controller

| Key | Type | Description |
|-----|------|-------------|
| `boot_count` | string | Boot counter |
| `rules_bin` | blob | Persisted WBP ruleset |

## Size Query

Pass nullptr to readBlob to get size:

```cpp
size_t size = storage.readBlob("rules_bin", nullptr, 0);
uint8_t *buf = new uint8_t[size];
storage.readBlob("rules_bin", buf, size);
```

## Error Handling

Returns false/empty on:
- NVS not initialized (`!opened_`)
- Key not found (`ESP_ERR_NVS_NOT_FOUND`)
- Any other ESP error

## Usage Example

```cpp
NVSStorage storage("my_app");

void setup() {
  storage.begin();
  
  // Write
  storage.writeString("version", "1.0.0");
  storage.writeBlob("data", bytes, len);
  
  // Read
  String ver = storage.readString("version");
  size_t size = storage.readBlob("data", nullptr, 0);
  
  // Delete
  storage.erase("old_key");
}
```
