// TrexMaintenance.h — Runtime Maintenance Mode for ESP32 (FeatherS3-friendly)
// - Long-press BOOT (GPIO0) at runtime to enter maintenance
// - Brings up Wi-Fi (STA with AP fallback), OTA, Telnet, mDNS, UDP beacons
// - Pause your game logic while Maint::active == true
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <TrexProtocol.h>   // for StationType

namespace Maint {

struct Config {
  // Wi-Fi
  const char* ssid;                // STA SSID (nullptr/"" -> skip STA)
  const char* pass;                // STA password
  const char* host;                // mDNS hostname (also SoftAP SSID base)
  bool        apFallback     = true;
  uint8_t     apChannel      = 6;  // SoftAP channel
  const char* apPass         = "trexsetup"; // SoftAP pass (>=8 chars)
  // Trigger
  uint8_t     buttonPin      = 0;  // FeatherS3 BOOT/IO0
  uint16_t    holdMs         = 1500;
  // Labeling / discovery
  StationType stationType    = StationType::LOOT; // or DROP / TREX
  uint8_t     stationId      = 1;
  // UDP beacon
  bool        enableBeacon   = true;
  uint16_t    beaconPort     = 32458; // listen with: nc -ul 32458
  uint32_t    beaconIntervalMs = 5000;
};

// State (header-only: keep these 'static' so each TU gets its own copy)
static bool        active = false;
static WiFiServer  telnet(23);
static WiFiClient  client;
static Config      cfg_;
static WiFiUDP     beacon;
static uint32_t    lastBeacon = 0;

inline const char* typeStr(StationType t) {
  switch (t) {
    case StationType::LOOT: return "loot";
    case StationType::DROP: return "drop";
    case StationType::TREX: return "trex";
    default:                return "unk";
  }
}

// Telnet printf helper (safe no-op if no client)
inline void print(const char* fmt, ...) {
  if (!client || !client.connected()) return;
  char buf[256];
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) client.write((const uint8_t*)buf, (size_t)min(n,(int)sizeof(buf)));
}

// mDNS with TXT records (use String overloads to avoid ambiguous char* issues)
inline void startMdns() {
  if (!MDNS.begin(cfg_.host)) {
    Serial.println("[Maint] mDNS failed");
    return;
  }
  MDNS.addService("telnet","tcp",23);
  MDNS.addService("arduino","tcp",3232);
  MDNS.addService("trex","udp", cfg_.beaconPort);
  MDNS.addServiceTxt("trex","udp","type", String(typeStr(cfg_.stationType)));
  MDNS.addServiceTxt("trex","udp","id",   String(cfg_.stationId));
  MDNS.addServiceTxt("trex","udp","mode", String("maint"));
}

inline void sendBeaconOnce() {
  if (!cfg_.enableBeacon) return;
  IPAddress ip = (WiFi.getMode()==WIFI_MODE_AP) ? WiFi.softAPIP() : WiFi.localIP();
  int rssi = WiFi.RSSI();
  char msg[256];
  int n = snprintf(msg, sizeof(msg),
    "{\"host\":\"%s\",\"ip\":\"%u.%u.%u.%u\",\"type\":\"%s\",\"id\":%u,"
    "\"mode\":\"maint\",\"rssi\":%d}\n",
    cfg_.host, ip[0],ip[1],ip[2],ip[3], typeStr(cfg_.stationType), cfg_.stationId, rssi);
  if (n <= 0) return;
  beacon.beginPacket(IPAddress(255,255,255,255), cfg_.beaconPort);
  beacon.write((const uint8_t*)msg, (size_t)n);
  beacon.endPacket();
}

inline void begin(const Config& cfg) {
  cfg_   = cfg;
  active = true;

  WiFi.persistent(false);
  bool staOK = false;

  if (cfg_.ssid && cfg_.ssid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cfg_.host);
    WiFi.begin(cfg_.ssid, cfg_.pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 8000) delay(50);
    staOK = (WiFi.status() == WL_CONNECTED);
  }

  if (!staOK && cfg_.apFallback) {
    String ssid = String(cfg_.host) + "-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid.c_str(), cfg_.apPass, cfg_.apChannel, 0, 1);
    Serial.printf("[Maint] SoftAP: %s  pass:%s  ip:%s\n",
                  WiFi.softAPSSID().c_str(), cfg_.apPass, WiFi.softAPIP().toString().c_str());
  } else if (staOK) {
    Serial.printf("[Maint] STA ip: %s\n", WiFi.localIP().toString().c_str());
  }

  startMdns();

  ArduinoOTA.setHostname(cfg_.host);
  // Optional password:
  // ArduinoOTA.setPassword("trexOTA123");
  ArduinoOTA.onStart([](){ Serial.println("[OTA] start"); });
  ArduinoOTA.onEnd  ([](){ Serial.println("[OTA] end"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t){
    if (t) Serial.printf("[OTA] %u%%\n", (p*100)/t);
  });
  ArduinoOTA.onError([](ota_error_t e){ Serial.printf("[OTA] err %u\n", e); });
  ArduinoOTA.begin();

  telnet.begin();
  telnet.setNoDelay(true);

  if (cfg_.enableBeacon) {
    beacon.begin(cfg_.beaconPort);
    sendBeaconOnce(); // fire one immediately
  }

  Serial.printf("[Maint] Telnet: %s.local:23\n", cfg_.host);
}

// Runtime long-press entry (use this on FeatherS3; call from loop())
inline bool checkRuntimeEntry(const Config& cfg) {
  if (active) return true;
  pinMode(cfg.buttonPin, INPUT_PULLUP);
  static uint32_t pressStart = 0;
  int level = digitalRead(cfg.buttonPin);     // BOOT pressed => LOW
  if (level == LOW) {
    if (pressStart == 0) pressStart = millis();
    if ((millis() - pressStart) > cfg.holdMs) {
      Serial.println("[Maint] Long-press detected → entering maintenance");
      begin(cfg);
      return true;
    }
  } else {
    pressStart = 0;
  }
  return false;
}

inline void loop() {
  if (!active) return;
  ArduinoOTA.handle();

  // Accept telnet client
  if (telnet.hasClient()) {
    if (client && client.connected()) { telnet.available().stop(); }
    client = telnet.available();
    client.setNoDelay(true);
    client.print("\r\n[TREX] telnet ready. cmds: help, ip, rssi, free, whoami, reboot\r\n");
  }
  // Telnet commands
  if (client && client.connected() && client.available()) {
    String cmd = client.readStringUntil('\n'); cmd.trim(); cmd.toLowerCase();
    if (cmd=="help")        client.print("help, ip, rssi, free, whoami, reboot\r\n");
    else if (cmd=="ip")     client.printf("IP: %s\r\n",
                              (WiFi.getMode()==WIFI_MODE_AP)?WiFi.softAPIP().toString().c_str()
                                                            :WiFi.localIP().toString().c_str());
    else if (cmd=="rssi")   client.printf("RSSI: %d dBm\r\n", WiFi.RSSI());
    else if (cmd=="free")   client.printf("Heap: %u\r\n", (unsigned)ESP.getFreeHeap());
    else if (cmd=="whoami") client.printf("%s id=%u type=%s\r\n", cfg_.host, cfg_.stationId, typeStr(cfg_.stationType));
    else if (cmd=="reboot"){ client.print("Rebooting...\r\n"); delay(200); ESP.restart(); }
    else                    client.print("?\r\n");
  }

  // Periodic UDP beacon
  uint32_t now = millis();
  if (cfg_.enableBeacon && (now - lastBeacon) >= cfg_.beaconIntervalMs) {
    lastBeacon = now;
    sendBeaconOnce();
  }
}

} // namespace Maint

// Convenience: mirror Serial.printf to telnet when active
#ifndef LOGF
  #define LOGF(...) do { Serial.printf(__VA_ARGS__); Maint::print(__VA_ARGS__); } while(0)
#endif
