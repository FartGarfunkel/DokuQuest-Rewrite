// net/protocol.h — the DokuQuest network protocol (ENet/UDP), modelled on the original net/net.js messages.
// Full description: docs/net-protocol.md. Every packet is one byte of message type + a MessagePack body (a JSON
// object with short keys, like the original's JSON messages). Unknown types and unknown fields are ignored, so
// later versions can add both; PROTO only changes when an old peer could no longer understand a new one.
#pragma once
#include "world/world.h"

struct _ENetPacket;
struct _ENetPeer;
struct _ENetHost;

namespace dq::net {

constexpr int PROTO = 1;
constexpr int DEFAULT_PORT = 25580;      // like the original LAN server's first port
constexpr int DISCOVERY_PORT = 25581;    // LAN announcements (launcher/desktop/lan.js)
constexpr int CHANNELS = 2;              // 0: reliable, ordered (world, chat, control)  1: movement + entity snapshots
constexpr u32 LOCAL_ID_BASE = 1u << 30;  // client-side entity ids (the local player, things made here) start here
constexpr float ENT_RANGE = 96;          // NET_ENT_RANGE: how far from a player the server tells it about entities
constexpr double SEND_ME = 1.0 / 15, SEND_ENTS = 1.0 / 10, SEND_ENV = 1, SEND_CHUNKS = 0.2, SEND_SAVE = 8;   // NET_SEND

enum Msg : u8 {
  // client -> server
  HELLO = 1, ME = 2, SAVE = 3, BLK = 4, BE = 5, CHAT = 6, HIT = 7, ACT = 8, SPAWN = 9, PORTAL = 10, NEED = 11, BYE = 12,
  // server -> client (BLK and BE are also sent this way)
  REFUSE = 32, WELCOME = 33, CKDONE = 34, CK = 35, CS = 36, ES = 38, ENV = 39, FX = 40, MSG = 41, SELF = 42, PEER = 43,
  FOLLOW = 44, KICK = 45, FLUSH = 46,
};
const char* msgName(u8 t);

// Disconnect codes (enet_peer_disconnect data), like the original's WebSocket close codes
enum Bye : u32 { BYE_NORMAL = 0, BYE_KICKED = 4000, BYE_CLOSED = 4001, BYE_REFUSED = 4002 };

bool enetInit();                         // reference counted; safe to call many times
void enetDone();
_ENetPacket* makePacket(Msg t, const json& body, bool reliable = true);
bool parsePacket(const _ENetPacket* p, u8& t, json& body);
void send(_ENetPeer* peer, Msg t, const json& body, int channel = 0, bool reliable = true);
std::string peerAddress(const _ENetPeer* peer);

// "host", "host:port", "[v6]:port", "dq://host:port/..." -> host + port (DEFAULT_PORT when missing)
bool parseAddress(const std::string& text, std::string& host, int& port);

// Chunks: records are the save codec (palette of keys, so block ids never matter); checksums are the original's
// FNV hash over block ids + meta, in the SERVER's ids (`toServer` maps this side's ids, null = same ids).
u32 chunkHash(const Chunk& c, const std::vector<BlockId>* toServer = nullptr);
bool chunkSettled(const World& w, const Chunk& c);    // populated, with populated neighbours (its hash won't change)
bool replaceChunk(World& w, Chunk& c, const std::vector<u8>& record);   // a loaded chunk takes a record's contents

// Small json helpers for the short-key messages
inline json v3(vec3 v) { return json::array({v.x, v.y, v.z}); }
inline vec3 v3(const json& j, vec3 def = vec3(0)) { return j.is_array() && j.size() >= 3 ? vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : def; }
inline float r3(float v) { return std::round(v * 1000.f) / 1000.f; }

} // namespace dq::net
