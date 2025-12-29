/**
 * @file W4RP-BLE.ino
 * @brief Main W4RP example with dependency injection
 *
 * Demonstrates modular architecture with explicit driver instantiation.
 */

#include <W4RP.h>

using namespace W4RP;

#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_20
#define LED_PIN 8

TWAICanBus canBus(CAN_TX_PIN, CAN_RX_PIN, TWAI_TIMING_CONFIG_500KBITS(),
                  TWAI_MODE_LISTEN_ONLY);
NVSStorage storage;
BLETransport transport;

Controller w4rp(&canBus, &storage, &transport);

CapabilityMeta exhaustMeta = {
    .id = "exhaust_flap",
    .label = "Exhaust Valve Control",
    .description = "Controls the exhaust valve position",
    .category = "actuator",
    .params = {{.name = "position",
                .type = "int",
                .required = true,
                .min = 0,
                .max = 100,
                .description = "Valve position (0=closed, 100=open)"}}};

void onExhaustFlap(const ParamMap &params) {
  auto it = params.find("p0");
  if (it != params.end()) {
    int position = it->second.toInt();
    Serial.printf("[EXHAUST] Set position to %d%%\n", position);
    // TODO: Add your actuator control code here
  }
}

CapabilityMeta logMeta = {.id = "log",
                          .label = "Log Message",
                          .description = "Print message to Serial",
                          .category = "debug",
                          .params = {{.name = "msg",
                                      .type = "string",
                                      .required = true,
                                      .description = "Message to log"}}};

void onLog(const ParamMap &params) {
  auto it = params.find("p0");
  if (it != params.end()) {
    Serial.printf("[LOG] %s\n", it->second.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== W4RP Modular Example ===");

  w4rp.setModuleInfo("W4RP_STD_V1", "0.5.0", "AEJUFH1823123124", nullptr,
                     "Exhaust Valve");

  w4rp.setLedPin(LED_PIN);

  w4rp.registerCapability("exhaust_flap", onExhaustFlap, exhaustMeta);
  w4rp.registerCapability("log", onLog, logMeta);

  w4rp.begin();
}

void loop() { w4rp.loop(); }
