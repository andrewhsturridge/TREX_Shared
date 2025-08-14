#pragma once
#include <stdint.h>
#include <functional>

struct TransportConfig {
  bool    maintenanceMode;   // true = prefer Wi-Fi/UDP; we’ll start with ESP-NOW
  uint8_t wifiChannel;       // ESPNOW channel (e.g. 6)
};

using RxHandler = std::function<void(const uint8_t* data, uint16_t len)>;

namespace Transport {
  bool init(const TransportConfig& cfg, RxHandler onRx);
  bool sendToServer(const uint8_t* data, uint16_t len);   // station → server
  bool broadcast(const uint8_t* data, uint16_t len);      // server → all (or general)
  void loop();                                            // pump background if needed
}
