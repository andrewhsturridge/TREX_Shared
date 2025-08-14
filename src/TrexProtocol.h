#pragma once
#include <stdint.h>

#define TREX_PROTO_VERSION 1

// --- enums ---
enum class StationType : uint8_t { TREX=0, LOOT=1, DROP=2 };
enum class LightState  : uint8_t { GREEN=0, RED=1 };

enum class MsgType : uint8_t {
  HELLO=1, HEARTBEAT=2,
  STATE_TICK=10, GAME_OVER=11, SCORE_UPDATE=12, STATION_UPDATE=13,
  LOOT_HOLD_START=20, LOOT_HOLD_ACK=21, LOOT_TICK=22, LOOT_HOLD_STOP=23, HOLD_END=24,
  DROP_REQUEST=30, DROP_RESULT=31,
  CONFIG_UPDATE=40
};

#pragma pack(push,1)
struct MsgHeader {
  uint8_t  version;       // = TREX_PROTO_VERSION
  uint8_t  type;          // MsgType
  uint8_t  srcStationId;  // 0=T-Rex
  uint8_t  flags;         // reserved
  uint16_t payloadLen;    // bytes after header
  uint16_t seq;           // per-sender sequence
};

struct StateTickPayload {
  uint8_t  state;    // LightState
  uint32_t msLeft;
};

struct HelloPayload {
  uint8_t     stationType;   // StationType
  uint8_t     stationId;
  uint8_t     fwMajor, fwMinor;
  uint8_t     wifiChannel;   // for ESP-NOW alignment
  uint8_t     mac[6];
};
#pragma pack(pop)
