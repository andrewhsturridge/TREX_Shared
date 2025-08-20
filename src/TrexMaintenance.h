// TrexMaintenance.h — Runtime Maintenance Mode for ESP32 (FeatherS3-friendly)
// - Long-press BOOT (GPIO0) at runtime to enter maintenance
// - Brings up Wi-Fi (STA with AP fallback), OTA, Telnet, mDNS, UDP beacons
// - Pause your game logic while Maint::active == true
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <TrexProtocol.h>   // for StationType

#ifndef MAINT_ENABLE_HTTP_FS
#define MAINT_ENABLE_HTTP_FS 1
#endif

#if MAINT_ENABLE_HTTP_FS
  #include <WebServer.h>
  #include <LittleFS.h>
  static WebServer *maintHttp = nullptr;
  static const char* kUploadPath = "/LootDrop.wav"; // change if you want a different filename

  inline void startHttpFs() {
    MDNS.addService("http","tcp",80);
    LittleFS.begin();
    maintHttp = new WebServer(80);

    maintHttp->on("/", HTTP_GET, [](){
      maintHttp->send(200,"text/html",
        "<h3>TREX FS Uploader</h3>"
        "<form method='POST' action='/upload' enctype='multipart/form-data'>"
        "<input type='file' name='f'><input type='submit' value='Upload'></form>"
        "<p>Target: " + String(kUploadPath) + "</p>");
    });

    maintHttp->on("/upload", HTTP_POST,
      [](){ maintHttp->send(200,"text/plain","OK. Reboot or re-open file to use new clip."); },
      [](){
        HTTPUpload &up = maintHttp->upload();
        static File f;
        switch (up.status) {
          case UPLOAD_FILE_START:
            if (LittleFS.exists(kUploadPath)) LittleFS.remove(kUploadPath);
            f = LittleFS.open(kUploadPath, "w");
            break;
          case UPLOAD_FILE_WRITE:
            if (f) f.write(up.buf, up.currentSize);
            break;
          case UPLOAD_FILE_END:
            if (f) f.close();
            break;
          default:
            break;
        }
      });

    maintHttp->begin();
    Serial.println("[Maint] HTTP FS uploader on / (port 80)");
  }

  inline void loopHttpFs() { if (maintHttp) maintHttp->handleClient(); }
#endif

namespace Maint {

// ── NEW: custom command hook so stations can add Telnet commands
using CmdHandler = bool(*)(const String& cmd, WiFiClient& out);
inline CmdHandler& CustomHandler() {  // single instance across the program
  static CmdHandler h = nullptr;
  return h;
}

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

// State
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

// mDNS with TXT records
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
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.begin(cfg_.ssid, cfg_.pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 8000) delay(50);
    staOK = (WiFi.status() == WL_CONNECTED);
  }

  if (!staOK && cfg_.apFallback) {
    String ssid = String(cfg_.host) + "-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.softAP(ssid.c_str(), cfg_.apPass, cfg_.apChannel, 0, 1);
    Serial.printf("[Maint] SoftAP: %s  pass:%s  ip:%s\n",
                  WiFi.softAPSSID().c_str(), cfg_.apPass, WiFi.softAPIP().toString().c_str());
  } else if (staOK) {
    Serial.printf("[Maint] STA ip: %s\n", WiFi.localIP().toString().c_str());
  }

  startMdns();

  ArduinoOTA.setHostname(cfg_.host);
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
  
  #if MAINT_ENABLE_HTTP_FS
    startHttpFs();
  #endif

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
    client.print(
      "\r\n[TREX] maintenance console\r\n"
      "Type 'help' for commands. HTTP FS upload served on port 80.\r\n"
    );
  }
  // Telnet commands
  if (client && client.connected() && client.available()) {
    String cmd = client.readStringUntil('\n'); cmd.trim(); cmd.toLowerCase();
    if (cmd == "help") {
      IPAddress ip = (WiFi.getMode()==WIFI_MODE_AP) ? WiFi.softAPIP() : WiFi.localIP();
      client.print(
        "Commands:\r\n"
        "  help            Show this help\r\n"
        "  ip              Show IP address\r\n"
        "  rssi            Show Wi-Fi RSSI (dBm)\r\n"
        "  free            Show free heap (bytes)\r\n"
        "  whoami          Show host / id / type\r\n"
        "  reboot          Reboot device\r\n"
      #if MAINT_ENABLE_HTTP_FS
        "  df              LittleFS usage\r\n"
        "  ls              List files in /\r\n"
        "  stat            Show uploaded clip size\r\n"
        "  rm /path        Delete file at /path\r\n"
        "  format          FORMAT LittleFS (ERASES ALL)\r\n"
      #endif
        "  --- server cmds available while in maintenance ---\r\n"
        "  status, set <key> <val>, pir on|off, cap/inv/fill/drain,\r\n"
        "  score +/-N, sprite <clip>, new, end, green, red\r\n"
      );
      #if MAINT_ENABLE_HTTP_FS
        client.printf("HTTP upload: http://%s.local/ (or http://%s/)\r\n",
                      cfg_.host, ((WiFi.getMode()==WIFI_MODE_AP)?WiFi.softAPIP().toString().c_str()
                                                            :WiFi.localIP().toString().c_str()));
        client.printf("Target file: %s\r\n", kUploadPath);
      #endif
    }
    else if (cmd=="ip")     client.printf("IP: %s\r\n",
                              (WiFi.getMode()==WIFI_MODE_AP)?WiFi.softAPIP().toString().c_str()
                                                            :WiFi.localIP().toString().c_str());
    else if (cmd=="rssi")   client.printf("RSSI: %d dBm\r\n", WiFi.RSSI());
    else if (cmd=="free")   client.printf("Heap: %u\r\n", (unsigned)ESP.getFreeHeap());
    else if (cmd=="whoami") client.printf("%s id=%u type=%s\r\n", cfg_.host, cfg_.stationId, typeStr(cfg_.stationType));
    else if (cmd=="reboot"){ client.print("Rebooting...\r\n"); delay(200); ESP.restart(); }
    #if MAINT_ENABLE_HTTP_FS
    else if (cmd=="stat") {
      File f = LittleFS.open(kUploadPath, "r");
      if (!f) client.print("missing\r\n");
      else { client.printf("%s size=%u\r\n", kUploadPath, (unsigned)f.size()); f.close(); }
    }
    else if (cmd=="df") {
      size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
      client.printf("LittleFS: used=%u / total=%u (free=%u)\r\n",
                    (unsigned)used, (unsigned)total, (unsigned)(total-used));
    }
    else if (cmd == "ls") {
      File root = LittleFS.open("/");
      if (!root) { client.print("LittleFS not mounted\r\n"); }
      else {
        File f = root.openNextFile();
        while (f) {
          client.printf("%s\t%u\r\n", f.name(), (unsigned)f.size());
          f = root.openNextFile();
        }
      }
    }
    else if (cmd.startsWith("rm ")) {
      String p = cmd.substring(3); p.trim();
      if (p.length()==0 || p[0] != '/') client.print("usage: rm /filename\r\n");
      else if (LittleFS.remove(p)) client.print("ok\r\n");
      else client.print("fail\r\n");
    }
    else if (cmd == "format") {
      client.print("Formatting...\r\n");
      LittleFS.format();
      client.print("Done. Rebooting.\r\n");
      delay(300);
      ESP.restart();
    }
    #endif
	else if (CustomHandler() && CustomHandler()(cmd, client)) {
      // handled by server-specific commands
    }
    else                    client.print("?\r\n");
  }

  // Periodic UDP beacon
  uint32_t now = millis();
  if (cfg_.enableBeacon && (now - lastBeacon) >= cfg_.beaconIntervalMs) {
    lastBeacon = now;
    sendBeaconOnce();
  }
  #if MAINT_ENABLE_HTTP_FS
    loopHttpFs();
  #endif
}

} // namespace Maint

// Convenience: mirror Serial.printf to telnet when active
#ifndef LOGF
  #define LOGF(...) do { Serial.printf(__VA_ARGS__); Maint::print(__VA_ARGS__); } while(0)
#endif
