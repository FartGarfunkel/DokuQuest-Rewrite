// net/protocol.cpp — packets, addresses and chunk helpers shared by the server and the client.
#include "net/protocol.h"
#include "world/light.h"
#include <enet/enet.h>
#ifdef _WIN32
#undef near   // windows.h (through enet) defines near and far as nothing
#undef far
#endif
#include <atomic>
#include <mutex>

namespace dq::net {

const char* msgName(u8 t) {
  switch (t) {
    case HELLO: return "hello"; case ME: return "me"; case SAVE: return "save"; case BLK: return "blk"; case BE: return "be";
    case CHAT: return "chat"; case HIT: return "hit"; case ACT: return "act"; case SPAWN: return "spawn"; case PORTAL: return "portal";
    case NEED: return "need"; case BYE: return "bye"; case REFUSE: return "refuse"; case WELCOME: return "welcome"; case CKDONE: return "ckdone";
    case CK: return "ck"; case CS: return "cs"; case ES: return "es"; case ENV: return "env"; case FX: return "efx"; case MSG: return "msg";
    case SELF: return "self"; case PEER: return "peer"; case FOLLOW: return "dim"; case KICK: return "kick"; case FLUSH: return "flush";
  }
  return "?";
}

static std::mutex g_initMx;
static int g_initRefs = 0;
bool enetInit() {
  std::lock_guard<std::mutex> lk(g_initMx);
  if (g_initRefs++ > 0) return true;
  if (enet_initialize() != 0) { g_initRefs = 0; DQ_ERR("net: ENet could not start"); return false; }
  return true;
}
void enetDone() {
  std::lock_guard<std::mutex> lk(g_initMx);
  if (g_initRefs > 0 && --g_initRefs == 0) enet_deinitialize();
}

ENetPacket* makePacket(Msg t, const json& body, bool reliable) {
  std::vector<u8> mp = json::to_msgpack(body);
  ENetPacket* p = enet_packet_create(nullptr, mp.size() + 1, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  p->data[0] = (u8)t;
  if (!mp.empty()) std::memcpy(p->data + 1, mp.data(), mp.size());
  return p;
}
bool parsePacket(const ENetPacket* p, u8& t, json& body) {
  if (!p || p->dataLength < 1) return false;
  t = p->data[0];
  if (p->dataLength == 1) { body = json::object(); return true; }
  body = json::from_msgpack(p->data + 1, p->data + p->dataLength, true, false);
  return !body.is_discarded() && body.is_object();
}
void send(ENetPeer* peer, Msg t, const json& body, int channel, bool reliable) {
  if (!peer) return;
  ENetPacket* p = makePacket(t, body, reliable);
  if (enet_peer_send(peer, (enet_uint8)channel, p) != 0) enet_packet_destroy(p);
}
std::string peerAddress(const ENetPeer* peer) {
  char buf[64] = "?";
  if (peer) enet_address_get_host_ip(&peer->address, buf, sizeof buf);
  return buf;
}

bool parseAddress(const std::string& text, std::string& host, int& port) {
  std::string s = text;
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  while (!s.empty() && isspace((unsigned char)s.front())) s.erase(0, 1);
  if (auto i = s.find("://"); i != std::string::npos) s = s.substr(i + 3);
  if (auto i = s.find('/'); i != std::string::npos) s = s.substr(0, i);
  if (s.empty()) return false;
  port = DEFAULT_PORT;
  if (s[0] == '[') {
    auto e = s.find(']'); if (e == std::string::npos) return false;
    host = s.substr(1, e - 1);
    if (e + 1 < s.size() && s[e + 1] == ':') port = std::atoi(s.c_str() + e + 2);
  } else {
    auto c = s.rfind(':');
    if (c != std::string::npos && s.find(':') == c) { host = s.substr(0, c); port = std::atoi(s.c_str() + c + 1); }
    else host = s;
  }
  return !host.empty() && port > 0 && port < 65536;
}

u32 chunkHash(const Chunk& c, const std::vector<BlockId>* toServer) {
  u32 h = 2166136261u;
  for (int y = 0; y < DIM_H; y++) {
    const Section* s = c.section(y >> 4);
    for (int i = 0; i < 256; i++) {
      u32 b = 0, m = 0;
      if (s) { int k = ((y & 15) << 8) | i; b = s->blocks[k]; m = s->meta[k]; if (toServer && b < toServer->size()) b = (*toServer)[b]; }
      h = (h ^ b) * 16777619u;
      if (m) h = (h ^ (m << 16)) * 16777619u;
    }
  }
  return h;
}

bool chunkSettled(const World& w, const Chunk& c) {
  auto pop = [](const Chunk* k) { return k && k->extra.is_object() && k->extra.value("populated", false); };
  if (!pop(&c)) return false;
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if ((dx || dz) && !pop(w.chunk(c.dim, c.cx + dx, c.cz + dz))) return false;
  return true;
}

bool replaceChunk(World& w, Chunk& c, const std::vector<u8>& record) {
  Chunk tmp(c.dim, c.cx, c.cz);
  if (!decodeChunk(tmp, record.data(), record.size())) return false;
  tmp.recomputeMaxY();
  initialChunkLight(tmp);
  tmp.compact();
  for (int i = 0; i < SECS; i++) c.secs[i] = std::move(tmp.secs[i]);
  c.airLight = tmp.airLight;
  c.blockEntities = std::move(tmp.blockEntities);
  c.climate = tmp.climate;
  std::memcpy(c.heightMap, tmp.heightMap, sizeof c.heightMap);
  c.maxY = tmp.maxY;
  json keepPop = c.extra.is_object() && c.extra.contains("populated") ? c.extra["populated"] : json();
  c.extra = tmp.extra.is_object() ? tmp.extra : json::object();
  if (!keepPop.is_null()) c.extra["populated"] = keepPop;
  c.modified = true; c.dirty = false; c.lightReady = false;
  for (int sy = 0; sy < SECS; sy++) w.markSectionDirty(c.dim, c.cx, sy, c.cz, true);
  for (auto& s : w.systems) s->onChunkLoaded(w, c);   // light engine & friends see it as new
  return true;
}

} // namespace dq::net
