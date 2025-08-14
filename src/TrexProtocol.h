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

// --- Common UID type ---
struct TrexUid {
  uint8_t len;          // 4,7,10
  uint8_t bytes[10];    // zero-padded
};

// --- Server broadcasts ---
struct ScoreUpdatePayload {
  uint32_t teamScore;
};
struct StationUpdatePayload {
  uint8_t  stationId;
  uint16_t inventory;
  uint16_t capacity;
};
struct GameOverPayload {
  uint8_t reason;       // GameOverReason
};

// --- Loot hold flow ---
struct LootHoldStartPayload {
  uint32_t holdId;      // random per attempt (from loot station)
  TrexUid  uid;
  uint8_t  stationId;   // loot station id (1..5)
};
struct LootHoldAckPayload {
  uint32_t holdId;
  uint8_t  accepted;    // 1=yes, 0=no
  uint8_t  rateHz;      // usually 1
  uint8_t  maxCarry;
  uint8_t  carried;     // current carried for player
  uint16_t inventory;   // station inventory after ack
  uint16_t capacity;    // station capacity
  uint8_t  denyReason;  // HoldEndReason if accepted=0
};
struct LootTickPayload {
  uint32_t holdId;
  uint8_t  carried;     // after this tick
  uint16_t inventory;   // after this tick
};
struct LootHoldStopPayload {
  uint32_t holdId;
};
struct HoldEndPayload {
  uint32_t holdId;
  uint8_t  reason;      // HoldEndReason
};

// --- Drop flow ---
struct DropRequestPayload {
  TrexUid  uid;
  uint8_t  readerIndex; // 0..3 on the drop station
};
struct DropResultPayload {
  uint16_t dropped;     // moved from carried -> banked
  uint32_t teamScore;   // updated team
};
#pragma pack(pop)
