#include "TrexBuildConfig.h"
#if TREX_USE_UDP

#include "TrexTransport.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

static RxHandler g_onRx = nullptr;
static WiFiUDP   g_udp;
static const uint16_t UDP_PORT = 33333;

namespace Transport {

bool init(const TransportConfig& cfg, RxHandler onRx) {
  (void)cfg;
  g_onRx = onRx;

  // Let the sketch handle Wi-Fi connection/AP. We just bind the socket.
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA); // safe default; works for STA or after AP start in sketch
  }
  g_udp.begin(UDP_PORT);
  return true;
}

bool sendToServer(const uint8_t* data, uint16_t len) {
  if (!data || !len) return false;
  IPAddress bcast(255, 255, 255, 255);
  g_udp.beginPacket(bcast, UDP_PORT);
  size_t n = g_udp.write(data, len);
  g_udp.endPacket();
  return n == len;
}

bool broadcast(const uint8_t* data, uint16_t len) {
  // Same as sendToServer in UDP mode: broadcast to everyone
  return sendToServer(data, len);
}

void loop() {
  int pktLen = g_udp.parsePacket();
  if (pktLen <= 0) return;

  uint8_t buf[512];
  int n = g_udp.read(buf, (pktLen < (int)sizeof(buf)) ? pktLen : (int)sizeof(buf));
  if (n > 0 && g_onRx) {
    g_onRx(buf, static_cast<uint16_t>(n));
  }
}

} // namespace Transport

#endif // TREX_USE_UDP
