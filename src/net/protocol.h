// net/protocol.h — the DokuQuest network protocol (ENet/UDP), modelled on the original net/net.js messages.
// Full description: docs/net-protocol.md. Every packet is one byte of message type + a MessagePack body (a JSON
// object with short keys, like the original's JSON messages). Unknown types and unknown fields are ignored, so
// later versions can add both; PROTO only changes when an old peer could no longer understand a new one.
#pragma once
#include <functional>
#include "world/world.h"

struct _ENetPacket;
struct _ENetPeer;
struct _ENetHost;

namespace dq { class Player; class Entity; }
namespace dq::net {

constexpr int PROTO = 2;                 // 2: timed, unreliable movement on its own channel; more of the players' state
constexpr int DEFAULT_PORT = 25580;      // like the original LAN server's first port
constexpr int DISCOVERY_PORT = 25581;    // LAN announcements (launcher/desktop/lan.js)
// 0: reliable, ordered (world, chat, control)  1: reliable entity news (new ones, their data, gone), the time, effects
// 2: movement only, unreliable (the guest's ME, the entities' places and poses): never a reliable packet on it, so a lost
//    one holds nothing up (ENet holds a channel's unreliable packets behind its reliable ones)
constexpr int CHANNELS = 3;
constexpr int CH_MOVE = 2;
constexpr size_t MOVE_PACKET_MAX = 1100;   // an unreliable packet bigger than one datagram goes as reliable fragments: kept under
constexpr u32 LOCAL_ID_BASE = 1u << 30;  // client-side entity ids (the local player, things made here) start here
constexpr float ENT_RANGE = 96;          // NET_ENT_RANGE: how far from a player the server tells it about entities
// NET_SEND: players' places 20 a second (the guest's own and the host's news of them), other things' 10
constexpr double SEND_ME = 1.0 / 20, SEND_ENTS = 1.0 / 20, SEND_ENV = 1, SEND_CHUNKS = 0.2, SEND_SAVE = 8;

// The other side's clock against ours, from the times on its packets: the quickest packets say it best (a late one only
// says it was late), drifting down slowly in case the quickest was a fluke. Places it sends are put on our clock by it.
struct NetClock {
  double off = 0; bool known = false;
  void heard(double theirT, double ourNow) {
    const double o = theirT - ourNow;
    if (!known || o > off + 2 || o < off - 2) { off = o; known = true; }
    else if (o > off) off = o;
    else off += (o - off) * 0.002;
  }
  double toLocal(double theirT) const { return theirT - off; }
};

enum Msg : u8 {
  // client -> server
  HELLO = 1, ME = 2, SAVE = 3, BLK = 4, BE = 5, CHAT = 6, HIT = 7, ACT = 8, SPAWN = 9, PORTAL = 10, NEED = 11, BYE = 12,
  // server -> client (BLK and BE are also sent this way)
  REFUSE = 32, WELCOME = 33, CKDONE = 34, CK = 35, CS = 36, ES = 38, ENV = 39, FX = 40, MSG = 41, SELF = 42, PEER = 43,
  FOLLOW = 44, KICK = 45, FLUSH = 46, SKINS = 47,
  UIEV = 48,   // a screen the host's game opens for a guest (screenOpen / villagerScreen / contentScreen): the guest's UI opens it
};
const char* msgName(u8 t);

// Skins over the network: a player's skin reference names a file on THEIR computer ("me.png", "creator:Bob"), so what
// travels is the skin itself (a payload: {"png": base64, "slim"} or {"char": <the creator's character file>}). The host
// names each one "net:<hash>" and sends it to everyone; whoever receives one hands it to this hook (the game registers
// it with the skin loader, assets/char_creator.cpp registerNetSkin), and the players' "sk" carries the net name.
std::function<void(const std::string& ref, const json& payload)>& netSkinHook();
std::string netSkinRef(const json& payload);

// Disconnect codes (enet_peer_disconnect data), like the original's WebSocket close codes
enum Bye : u32 { BYE_NORMAL = 0, BYE_KICKED = 4000, BYE_CLOSED = 4001, BYE_REFUSED = 4002 };

bool enetInit();                         // reference counted; safe to call many times
void enetDone();
_ENetPacket* makePacket(Msg t, const json& body, bool reliable = true);
bool parsePacket(const _ENetPacket* p, u8& t, json& body);
void send(_ENetPeer* peer, Msg t, const json& body, int channel = 0, bool reliable = true);
std::string peerAddress(const _ENetPeer* peer);
void steadyPeer(_ENetPeer* peer);        // a connected peer's unreliable packets are never dropped by ENet's throttle

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
// another player's swing from a message ("am" move, "at" its time, "ay" its facing; none: no swing): a new or different
// move starts there; the same one keeps its own smooth clock (Level::tick plays it on) unless the message is well apart
void netAttack(dq::Player& p, const json& m);
// the rest of what a player's body is doing (swings, digging, the bow, gliding, the spyglass, a ledge, a ladder, the water,
// what's in their arms, asleep): written into a ME or a snapshot, and read back onto a replica (fromOwner: from their own
// game's ME, whose word on sleeping isn't taken)
void playerPoseInto(const dq::Player& p, json& s);
void applyPlayerPose(dq::Player& p, const json& m, bool fromOwner);
// one snapshot entry's state put on a replica (net/protocol.cpp): the guest's replicas and the host's drawing of its guests
void applyReplicaState(dq::Entity* e, const json& s, u32 myEid, dq::Player* me);
// hosting, every frame: a guest's queued doings put on their drawing copy (Player::netView) when their smoothed body (its
// view, sim/net_interp.cpp, updated first) reaches that moment
void hostViewStep(dq::Player& p, double now, float dt);

} // namespace dq::net
