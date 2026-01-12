#pragma once
#include <stdint.h>
#include <functional>

struct TransportConfig {
  bool    maintenanceMode;   // true = prefer Wi-Fi/UDP; we’ll start with ESP-NOW
  uint8_t wifiChannel;       // ESPNOW channel (e.g. 6)

  // --- Multi-game safety / upgrade support ---
  // If true, prepend a small "wire header" (magic + wire version) to every packet we transmit.
  // This prevents other games on the same channel from accidentally parsing TRex packets.
  bool    txFramed = false;

  // If false, drop packets that do not have the wire header.
  // During rollout you can keep this true for backwards compatibility with older firmware.
  bool    rxAcceptLegacy = true;
};

using RxHandler = std::function<void(const uint8_t* data, uint16_t len)>;

namespace Transport {
  bool init(const TransportConfig& cfg, RxHandler onRx);
  bool sendToServer(const uint8_t* data, uint16_t len);   // station → server
  bool broadcast(const uint8_t* data, uint16_t len);      // server → all (or general)
  void loop();                                            // pump background if needed
}
