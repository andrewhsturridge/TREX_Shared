#include "TrexBuildConfig.h"
#if TREX_USE_ESPNOW

#include "TrexTransport.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <string.h>

static RxHandler g_onRx = nullptr;
static uint8_t   g_broadcastAddr[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#if ESP_IDF_VERSION_MAJOR >= 5
// ---- IDF v5.x callback signatures ----
static void onEspNowRecv(const esp_now_recv_info_t* info,
                         const uint8_t* data, int len) {
  (void)info; // we don't need src info right now
  if (g_onRx && data && len > 0) {
    g_onRx(data, static_cast<uint16_t>(len));
  }
}

static void onEspNowSend(const wifi_tx_info_t* info,
                         esp_now_send_status_t status) {
  (void)info; (void)status; // hook for optional debug
}
#else
// ---- IDF v4.x callback signatures ----
static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  if (g_onRx && data && len > 0) {
    g_onRx(data, static_cast<uint16_t>(len));
  }
}

static void onEspNowSend(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac; (void)status;
}
#endif

namespace Transport {

bool init(const TransportConfig& cfg, RxHandler onRx) {
  g_onRx = onRx;

  // ESPNOW requires STA mode and a fixed channel
  WiFi.mode(WIFI_STA);

  // Lock channel before esp_now_init()
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(cfg.wifiChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return false;

  // Register callbacks (IDF v4/v5 handled above)
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_register_send_cb(onEspNowSend);

  // Add a broadcast peer so we can send without knowing peers yet
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, g_broadcastAddr, 6);
  peer.channel = cfg.wifiChannel;
  peer.encrypt = false;
  #if ESP_IDF_VERSION_MAJOR >= 4
  peer.ifidx = WIFI_IF_STA;
  #endif

  esp_now_del_peer(g_broadcastAddr); // in case it already exists
  esp_now_add_peer(&peer);

  return true;
}

bool sendToServer(const uint8_t* data, uint16_t len) {
  if (!data || !len) return false;
  // Early phase: broadcast; the server filters by MsgType/source
  return esp_now_send(g_broadcastAddr, data, len) == ESP_OK;
}

bool broadcast(const uint8_t* data, uint16_t len) {
  if (!data || !len) return false;
  return esp_now_send(g_broadcastAddr, data, len) == ESP_OK;
}

void loop() {
  // ESPNOW is ISR-driven; nothing to pump here
}

} // namespace Transport

#endif // TREX_USE_ESPNOW
