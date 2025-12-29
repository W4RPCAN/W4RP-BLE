/**
 * @file OTA.ino
 * @brief Example: W4RP with OTA firmware updates
 *
 * Demonstrates ESP32OTAService for delta and full firmware updates.
 * Delta updates are preferred - they transfer only the diff.
 */

#include <W4RP.h>

using namespace W4RP;

TWAICanBus canBus(GPIO_NUM_21, GPIO_NUM_20, TWAI_TIMING_CONFIG_500KBITS());
NVSStorage storage;
BLETransport transport;
ESP32OTAService otaService;

Controller w4rp(&canBus, &storage, &transport, &otaService);

CapabilityMeta logMeta = {.id = "log",
                          .label = "Log Message",
                          .description = "Print to Serial",
                          .category = "debug"};

void onLog(const ParamMap &params) {
  auto it = params.find("p0");
  if (it != params.end()) {
    Serial.printf("[LOG] %s\n", it->second.c_str());
  }
}

void onOTAProgress(const OTAProgress &progress) {
  Serial.printf("[OTA] %d%% (%u/%u bytes)\n", progress.percentage,
                progress.bytesReceived, progress.totalBytes);
}

void onOTAComplete(OTAStatus status) {
  if (status == OTAStatus::SUCCESS) {
    Serial.println("[OTA] Success! Rebooting...");
    delay(1000);
    ESP.restart();
  } else {
    Serial.printf("[OTA] Failed with status %d\n", (int)status);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== W4RP with OTA ===");

  otaService.begin();
  otaService.setProgressCallback(onOTAProgress);
  otaService.setCompleteCallback(onOTAComplete);

  w4rp.setModuleInfo("W4RP_OTA", "1.0.0", "DEV-OTA");
  w4rp.setLedPin(8);
  w4rp.registerCapability("log", onLog, logMeta);
  w4rp.begin();

  Serial.println("OTA commands:");
  Serial.println("  OTA:DELTA:<size>:<crc>  (preferred)");
  Serial.println("  OTA:BEGIN:<size>:<crc>  (full firmware)");
}

void loop() { w4rp.loop(); }
