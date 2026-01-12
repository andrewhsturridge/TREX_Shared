#include "TrexBuildConfig.h"
#if TREX_USE_UDP

#include "TrexTransport.h"
#include "TrexProtocol.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>

static RxHandler g_onRx = nullptr;
static WiFiUDP   g_udp;
static const uint16_t UDP_PORT = 33333;

static bool g_txFramed       = false;
static bool g_rxAcceptLegacy = true;

static inline bool isFramedPacket(const uint8_t* data, int len) {
  return data && len >= 3 &&
         data[0] == (uint8_t)TREX_WIRE_MAGIC0 &&
         data[1] == (uint8_t)TREX_WIRE_MAGIC1 &&
         data[2] == (uint8_t)TREX_WIRE_VERSION;
}

static inline void deliverRx(const uint8_t* data, int len) {
  if (!g_onRx || !data || len <= 0) return;

  if (isFramedPacket(data, len)) {
    const uint8_t* payload = data + 3;
    const int      payLen  = len - 3;
    if (payLen > 0) g_onRx(payload, (uint16_t)payLen);
    return;
  }
  if (g_rxAcceptLegacy) {
    g_onRx(data, (uint16_t)len);
  }
}

namespace Transport {

bool init(const TransportConfig& cfg, RxHandler onRx) {
  (void)cfg;
  g_onRx           = onRx;
  g_txFramed       = cfg.txFramed;
  g_rxAcceptLegacy = cfg.rxAcceptLegacy;

  // Let the sketch handle Wi-Fi connection/AP. We just bind the socket.
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA); // safe default; works for STA or after AP start in sketch
  }
  g_udp.begin(UDP_PORT);
  return true;
}

static bool sendRaw(const uint8_t* data, uint16_t len) {
  if (!data || !len) return false;

  IPAddress bcast(255, 255, 255, 255);
  g_udp.beginPacket(bcast, UDP_PORT);

  if (!g_txFramed) {
    size_t n = g_udp.write(data, len);
    g_udp.endPacket();
    return n == len;
  }

  uint8_t hdr[3] = {(uint8_t)TREX_WIRE_MAGIC0, (uint8_t)TREX_WIRE_MAGIC1, (uint8_t)TREX_WIRE_VERSION};
  size_t n0 = g_udp.write(hdr, sizeof(hdr));
  size_t n1 = g_udp.write(data, len);
  g_udp.endPacket();
  return (n0 == sizeof(hdr)) && (n1 == len);
}

bool sendToServer(const uint8_t* data, uint16_t len) {
  return sendRaw(data, len);
}

bool broadcast(const uint8_t* data, uint16_t len) {
  // Same as sendToServer in UDP mode: broadcast to everyone
  return sendRaw(data, len);
}

void loop() {
  int pktLen = g_udp.parsePacket();
  if (pktLen <= 0) return;

  uint8_t buf[512];
  int n = g_udp.read(buf, (pktLen < (int)sizeof(buf)) ? pktLen : (int)sizeof(buf));
  if (n > 0) {
    deliverRx(buf, n);
  }
}

} // namespace Transport

#endif // TREX_USE_UDP
