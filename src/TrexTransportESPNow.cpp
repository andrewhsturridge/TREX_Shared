#include "TrexBuildConfig.h"
#if TREX_USE_ESPNOW

#include "TrexTransport.h"
#include "TrexProtocol.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <string.h>

static RxHandler g_onRx = nullptr;
static uint8_t   g_broadcastAddr[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static bool g_txFramed        = false;
static bool g_rxAcceptLegacy  = true;

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

#if ESP_IDF_VERSION_MAJOR >= 5
// ---- IDF v5.x callback signatures ----
static void onEspNowRecv(const esp_now_recv_info_t* info,
                         const uint8_t* data, int len) {
  (void)info; // src info optional
  deliverRx(data, len);
}

static void onEspNowSend(const wifi_tx_info_t* info,
                         esp_now_send_status_t status) {
  (void)info; (void)status; // hook for optional debug
}
#else
// ---- IDF v4.x callback signatures ----
static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  deliverRx(data, len);
}

static void onEspNowSend(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac; (void)status;
}
#endif

namespace Transport {

bool init(const TransportConfig& cfg, RxHandler onRx) {
  g_onRx          = onRx;
  g_txFramed      = cfg.txFramed;
  g_rxAcceptLegacy= cfg.rxAcceptLegacy;

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

static bool sendRaw(const uint8_t* dst, const uint8_t* data, uint16_t len) {
  if (!dst || !data || !len) return false;

  if (!g_txFramed) {
    return esp_now_send(dst, data, len) == ESP_OK;
  }

  // ESPNOW max payload is limited; keep a small fixed buffer to avoid heap use.
  // (Most TRex packets are well under this size.)
  constexpr size_t kMaxEspNowPayload = 250;
  constexpr size_t kWireHdrLen = 3;

  if ((size_t)len + kWireHdrLen > kMaxEspNowPayload) return false;

  uint8_t buf[kMaxEspNowPayload];
  buf[0] = (uint8_t)TREX_WIRE_MAGIC0;
  buf[1] = (uint8_t)TREX_WIRE_MAGIC1;
  buf[2] = (uint8_t)TREX_WIRE_VERSION;
  memcpy(buf + kWireHdrLen, data, len);

  return esp_now_send(dst, buf, (uint16_t)(len + kWireHdrLen)) == ESP_OK;
}

bool sendToServer(const uint8_t* data, uint16_t len) {
  // Early phase: broadcast; the server filters by MsgType/source.
  return sendRaw(g_broadcastAddr, data, len);
}

bool broadcast(const uint8_t* data, uint16_t len) {
  return sendRaw(g_broadcastAddr, data, len);
}

void loop() {
  // ESPNOW is ISR/task-driven; nothing to pump here
}

} // namespace Transport

#endif // TREX_USE_ESPNOW
