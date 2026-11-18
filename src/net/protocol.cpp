// net/protocol.cpp — packets, addresses and chunk helpers shared by the server and the client.
#include "net/protocol.h"
#include "world/light.h"
#include "sim/player.h"
#include "sim/attack_moves.h"
#include "sim/creature.h"
#include <enet/enet.h>
#ifdef _WIN32
#undef near   // windows.h (through enet) defines near and far as nothing
#undef far
#endif
#include <atomic>
#include <algorithm>
#include <mutex>

namespace dq::net {

const char* msgName(u8 t) {
  switch (t) {
    case HELLO: return "hello"; case ME: return "me"; case SAVE: return "save"; case BLK: return "blk"; case BE: return "be";
    case CHAT: return "chat"; case HIT: return "hit"; case ACT: return "act"; case SPAWN: return "spawn"; case PORTAL: return "portal";
    case NEED: return "need"; case BYE: return "bye"; case REFUSE: return "refuse"; case WELCOME: return "welcome"; case CKDONE: return "ckdone";
    case CK: return "ck"; case CS: return "cs"; case ES: return "es"; case ENV: return "env"; case FX: return "efx"; case MSG: return "msg";
    case SELF: return "self"; case PEER: return "peer"; case FOLLOW: return "dim"; case KICK: return "kick"; case FLUSH: return "flush";
    case SKINS: return "skins";
  }
  return "?";
}

std::function<void(const std::string&, const json&)>& netSkinHook() { static std::function<void(const std::string&, const json&)> h; return h; }
std::string netSkinRef(const json& payload) {
  const std::string d = payload.dump();
  u64 h = 1469598103934665603ull;   // FNV-1a: the same name for the same skin on every computer
  for (unsigned char c : d) { h ^= c; h *= 1099511628211ull; }
  char b[32]; std::snprintf(b, sizeof b, "net:%016llx", (unsigned long long)h);
  return b;
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
  if (!reliable && channel == CH_MOVE) p->flags |= ENET_PACKET_FLAG_UNSEQUENCED;   // (movement: an overtaken one still comes, its time says where it goes)
  if (enet_peer_send(peer, (enet_uint8)channel, p) != 0) enet_packet_destroy(p);
}
void steadyPeer(ENetPeer* peer) {
  // ENet's own throttle drops unreliable packets at the sender whenever the round trip comes out longer than usual — on a
  // jittery internet link that is most of the time, and the movement (all unreliable) was mostly thrown away before it
  // left. Never throttled down: the game paces what it sends itself (net/server_sync.cpp flushChunks).
  if (!peer) return;
  peer->packetThrottleDeceleration = 0;
  peer->packetThrottle = peer->packetThrottleLimit;
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
  c.airRgb = tmp.airRgb;
  c.blockEntities = std::move(tmp.blockEntities);
  if (std::any_of(tmp.climate.begin(), tmp.climate.end(), [](u8 v) { return v != 0; })) c.climate = tmp.climate;   // (an older host sends none)
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

void netAttack(dq::Player& p, const json& m) {
  const int id = m.value("am", 0);
  // no swing in this message: the one playing here plays to its end (Level::tick ends it). It is drawn later than its
  // owner made it, so their "done" comes while ours is still going: cutting it there clipped every swing's end.
  if (id <= 0 || id >= MV_COUNT) { p.atkHeardMv = 0; return; }
  const float t = m.value("at", 0.f);
  // The same swing heard again (its time going on from the last heard): it plays here on its own clock — pulled forward
  // only if ours fell behind (its start was lost), and not begun again once ours has finished (their later messages of
  // it come after ours ended: that had it flick on again). A different move, or the same one from its start again (its
  // time back below the last heard), is a new swing.
  const bool same = id == p.atkHeardMv && t >= p.atkHeardT - 0.05f;
  p.atkHeardMv = (u8)id; p.atkHeardT = t;
  if (same) { if (p.atkMove == id && t > p.atkT + 0.12f) p.atkT = t; return; }
  p.atkMove = (u8)id; p.atkT = t;
  p.atkYaw = m.value("ay", p.atkYaw);
  p.atkOff = m.value("ao", 0) != 0;
}

// what a player's body is doing, beyond walking (the guest's ME and the host's snapshots carry the same keys):
// sn arm swings begun (a count), mn [x,y,z,percent] the block being dug, bw the bow's draw, lc a ledge climb's progress,
// cy what's carried in the arms, xf flags (below)
enum : int { XF_SLEEP = 1, XF_GLIDE = 2, XF_SCOPE = 4, XF_HANG = 8, XF_SHIMMY = 16, XF_LADDER = 32, XF_WATER = 64, XF_EYEWATER = 128, XF_HOSTS = 256 };   // (HOSTS: played on the host itself, so its effects come as the host's)
void playerPoseInto(const dq::Player& p, json& s) {
  if (p.swingN) s["sn"] = p.swingN;
  if (p.mineP > 0) s["mn"] = json::array({p.mineAt.x, p.mineAt.y, p.mineAt.z, (int)std::lround(p.mineP * 100)});
  if (p.bowPull > 0) s["bw"] = r3(p.bowPull);
  const bool asleep = p.sleeping || (p.data.is_object() && p.data.value("sleeping", false));
  const int xf = (asleep ? XF_SLEEP : 0) | (p.gliding ? XF_GLIDE : 0) | (p.scoping ? XF_SCOPE : 0) | (p.hang ? XF_HANG : 0) |
                 (p.hang && p.hang->shimT > 0 ? XF_SHIMMY : 0) | (p.contacts.ladder && !p.onGround ? XF_LADDER : 0) |
                 (p.contacts.water ? XF_WATER : 0) | (p.contacts.eyeWater ? XF_EYEWATER : 0) | (!p.ghost ? XF_HOSTS : 0);
  if (xf) s["xf"] = xf;
  if (p.climb) s["lc"] = r3(std::min(1.f, p.climb->t / 0.55f));
  const json& c = p.carrying.is_object() ? p.carrying : p.netCarry;
  if (c.is_object()) {
    if (c.dump().size() < 600) s["cy"] = c;
    else { json k = json::object(); for (const char* f : {"kind", "key", "type", "meta", "id"}) if (c.contains(f)) k[f] = c[f]; s["cy"] = k; }   // (what it looks like, not all of it)
  }
}
void applyPlayerPose(dq::Player& p, const json& m, bool fromOwner) {
  p.swingN = (u8)m.value("sn", 0);
  if (m.contains("mn") && m["mn"].is_array() && m["mn"].size() >= 4) {
    const json& a = m["mn"];
    p.mineAt = ivec3(a[0].get<int>(), a[1].get<int>(), a[2].get<int>()); p.mineP = clampv(a[3].get<float>() / 100.f, 0.f, 1.f);
  } else p.mineP = 0;
  p.bowPull = clampv(m.value("bw", 0.f), 0.f, 1.f);
  const int xf = m.value("xf", 0);
  if (!fromOwner) { p.sleeping = xf & XF_SLEEP; p.fxSent = xf & XF_HOSTS; }   // (a guest's sleeping is the host's to say: it put their body in the bed)
  p.gliding = xf & XF_GLIDE; p.scoping = xf & XF_SCOPE;
  if (xf & XF_HANG) { if (!p.hang) p.hang.emplace(); p.hang->shimT = (xf & XF_SHIMMY) ? 1.f : 0.f; } else p.hang.reset();
  p.contacts.ladder = xf & XF_LADDER; p.contacts.water = xf & XF_WATER; p.contacts.eyeWater = xf & XF_EYEWATER;
  const float lc = m.value("lc", 0.f);
  if (lc > 0) { if (!p.climb) p.climb.emplace(); p.climb->t = lc * 0.55f; } else p.climb.reset();
  p.netCarry = m.contains("cy") && m["cy"].is_object() ? m["cy"] : json();
}

// One snapshot entry's state (not its place: sim/net_interp.cpp has that) put on a replica: a guest's of what the host
// sends (net/client.cpp), and the host's own drawing of a guest, from that guest's news at its moment (Player::netView).
// myEid / me: the guest's own player as the host knows it and here (a mount it rides), else 0 / null.
void applyReplicaState(Entity* e, const json& s, u32 myEid, Player* me) {
  float hp = s.value("hp", e->hp);
  if (hp < e->hp) e->hurtTime = 0.5f;
  e->hp = hp; e->maxHp = s.value("mh", e->maxHp); e->w = s.value("w", e->w); e->h = s.value("h", e->h);
  int f = s.value("f", 0);
  e->onGround = f & 1; e->inWater = f & 2; e->inLava = f & 4; e->noGravity = f & 16;
  if (f & 8) e->hurtTime = std::max(e->hurtTime, 0.3f);
  e->riding = s.value("rd", 0u);
  e->posture = s.value("po", 0.f); e->maxPosture = s.value("pm", e->maxPosture); e->staggerT = s.value("sg", 0.f); e->mobLevel = s.value("ml", 1); if (s.value("el", 0)) e->data["elite"] = true;   // (sim/combat_rpg.h)
  if (s.contains("d")) e->data = s["d"];
  if (auto* c = dynamic_cast<Creature*>(e)) if (s.contains("cs") && s["cs"].is_object()) {   // its look and state (Creature::snap)
    json cs = s["cs"]; cs["x"] = c->pos.x; cs["y"] = c->pos.y; cs["z"] = c->pos.z; cs["r"] = c->yaw; cs["h"] = e->hp;   // (the glide moves it)
    c->applySnap(cs);
    if (c->riddenBy && c->riddenBy == myEid && me) c->riddenBy = me->id;   // ridden by us (our player's id here)
  }
  if (auto* p = dynamic_cast<Player*>(e)) {
    p->name = s.value("n", p->name); p->skin = s.value("sk", p->skin); p->mode = s.value("md", p->mode);
    p->selected = 0;
    p->inv[0] = s.contains("hd") ? ItemStack(s["hd"].get<std::string>(), 1) : ItemStack();
    if (s.contains("hm")) p->data["hm"] = s["hm"]; else if (p->data.is_object()) p->data.erase("hm");   // (the look of what's in their hands: render/char_layer.cpp)
    for (int i = 0; i < INV_ARMOR; i++) p->armor[i] = s.contains("ar") && i < (int)s["ar"].size() && s["ar"][i].is_string() ? ItemStack(s["ar"][i].get<std::string>(), 1) : ItemStack();
    p->offhand[0] = s.contains("of") ? ItemStack(s["of"].get<std::string>(), 1) : ItemStack();
    if (s.contains("om")) p->data["om"] = s["om"]; else if (p->data.is_object()) p->data.erase("om");
    if (p->shield.empty()) p->shield.resize(1);
    p->shield[0] = s.contains("sh") ? ItemStack(s["sh"].get<std::string>(), 1) : ItemStack();
    if (s.contains("bk")) p->data["back"] = s["bk"]; else p->data.erase("back");          // (render/char_layer wornItem)
    if (s.contains("qv")) p->data["quiver"] = s["qv"]; else p->data.erase("quiver");
    if (const int f = s.value("qf", 0)) p->data["quiverFill"] = f; else p->data.erase("quiverFill");
    int pf = s.value("pf", 0);
    p->sneaking = pf & 1; p->sprinting = pf & 2; p->flying = pf & 4; p->swimming = pf & 8; p->crawling = pf & 16; p->dead = pf & 32;
    p->activity = (u8)(s.value("ac", 0) & 7); p->typeRate = clampv(s.value("tr", 0.f), 0.f, 40.f);
    p->stationKind = (i8)s.value("wk", -1); p->workTap = (u8)s.value("wt", (int)p->workTap);
    if (s.contains("wa") && s["wa"].is_array() && s["wa"].size() == 3) p->workAt = vec3(s["wa"][0].get<float>(), s["wa"][1].get<float>(), s["wa"][2].get<float>());
    if (!s.contains("bv")) { if (!p->bookPic.empty()) { p->bookPic.clear(); p->bookPicV++; } }   // (their book shut)
    else if (s.contains("bp") && s["bp"].is_string()) {
      std::string bp = s["bp"].get<std::string>();
      if (bp != p->bookPic) { p->bookPic = std::move(bp); p->bookPicV++; }
      p->bookLay = s.value("bl", std::string());
    }
    p->bookFlip = (u8)s.value("bf", (int)p->bookFlip); p->bookDir = (i8)(s.value("bd", 1) < 0 ? -1 : 1);
    p->eatT = clampv(s.value("et", 0.f), 0.f, 1.f); p->eatId = p->eatT > 0 ? s.value("ei", std::string()) : std::string();
    if (p != me) {   // their shield, roll and parry (sim/combat_rpg.h): ours are our own game's
      p->blockT = s.value("sb", 0.f);
      // a roll, a parry, a blow on the shield: each counts down here on its own (sim/combat_rpg.cpp); only a fresh one (well
      // above what is left of ours) starts it again — taking every message's value had a late one restart the roll mid-way
      // (a new one is their own value rising: how fast ours counts down never matters)
      auto fresh = [](float& mine, float& heard, float theirs) { const bool rose = theirs > heard + 0.1f; heard = theirs; if (rose) mine = theirs; return rose; };
      fresh(p->parryFlash, p->parryHeard, s.value("py", 0.f)); fresh(p->guardFlash, p->guardHeard, s.value("gf", 0.f));
      if (fresh(p->dodgeT, p->dodgeHeard, s.value("dg", 0.f))) p->dodgeYaw = s.value("dy", p->dodgeYaw);   // (render/char_layer.cpp plays the roll through from its start)
      netAttack(*p, s);
      applyPlayerPose(*p, s, false);   // swings, digging, bow, glide, spyglass, ledge, ladder, water, carrying, asleep
    }
  }
}

void hostViewStep(Player& p, double now, float dt) {
  if (!p.netView) return;
  while (!p.netViewQ.empty() && (p.netViewQ.front().first <= now + p.netOff - p.netDelay || p.netViewQ.size() > 40)) {
    applyReplicaState(p.netView.get(), p.netViewQ.front().second, 0, nullptr);
    p.netViewQ.pop_front();
  }
  advanceReplicaView(p, *p.netView, dt);
}

} // namespace dq::net
