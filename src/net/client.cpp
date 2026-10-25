// net/client.cpp — the guest side of net.js: join (hello / welcome / ckdone), guestMsg, guestTick, applyEntities,
// chunkLoaded (checksums and stale chunks), plus what netsync.js does on a guest (block changes reported, things
// made here handed to the server).
#include "net/client.h"
#include "net/server_host.h"
#include "world/region.h"
#include <enet/enet.h>
#include <thread>

namespace dq::net {

u64 packChunkKey(int realm, int dim, int cx, int cz) {
  return ((u64)(realm & 15) << 60) | ((u64)(dim & 15) << 56) | ((u64)((u32)cx & 0xFFFFFFF) << 28) | (u64)((u32)cz & 0xFFFFFFF);
}
static void unpackChunkKey(u64 k, int& realm, int& dim, int& cx, int& cz) {
  realm = (int)(k >> 60); dim = (int)((k >> 56) & 15);
  cx = (int)((k >> 28) & 0xFFFFFFF); if (cx & 0x8000000) cx |= ~0xFFFFFFF;
  cz = (int)(k & 0xFFFFFFF); if (cz & 0x8000000) cz |= ~0xFFFFFFF;
}

// Block changes made by this game (the player, its own systems) go to the server; chunks arriving are checked.
class ClientWatch : public WorldSystem {
public:
  explicit ClientWatch(NetClient& c) : cl(c) {}
  const char* name() const override { return "net-client"; }
  void onBlockChanged(World& w, int x, int y, int z, BlockId, BlockId newId) override {
    if (cl.applying || (w.changeFlags & (SetFlags::NoModify | SetFlags::FromNet))) return;
    std::string r = std::to_string(w.realm);
    cl.blkOut[r].push_back(json::array({x, y, z, cl.toServer(newId), w.getMeta(x, y, z)}));
    cl.stats.blocksOut++;
  }
  void onBlockEntityChanged(World& w, int x, int y, int z) override {
    if (cl.applyingBE) return;
    json* e = w.blockEntity(x, y, z);
    int q = ++cl.beSeq;   // numbered: until the server's copy includes this change, its older news of it is ignored
    cl.bePend[strFormat("%d,%d,%d,%d", w.realm, x, y, z)] = q;
    cl.sendTo(BE, {{"rl", w.realm}, {"x", x}, {"y", y}, {"z", z}, {"e", e ? *e : json()}, {"q", q}});
  }
  void onChunkLoaded(World& w, Chunk& c) override {
    if (cl.applying) return;
    u64 k = packChunkKey(w.realm, c.dim, c.cx, c.cz);
    if (cl.stale.erase(k)) { cl.sendTo(NEED, {{"rl", w.realm}, {"d", c.dim}, {"cx", c.cx}, {"cz", c.cz}}); cl.stats.needs++; return; }
    if (cl.fresh.erase(k)) cl.replaceQ.push_back(k);
  }
  NetClient& cl;
};

NetClient::NetClient() {}
NetClient::~NetClient() {
  // the level (and our player) may already be gone: only the connection is closed here (close() says goodbye properly)
  if (host) {
    if (peer) { if (st == State::Ready) send(peer, BYE, lastSave.is_null() ? json::object() : json{{"pl", lastSave}}); enet_host_flush(host); enet_peer_disconnect_now(peer, BYE_NORMAL); }
    enet_host_destroy(host); host = nullptr; peer = nullptr;
    enetDone();
  }
}

void NetClient::sendTo(Msg ty, const json& m, int channel, bool reliable) {
  if (!peer || (st != State::Ready && st != State::Joining)) return;
  send(peer, ty, m, channel, reliable);
  stats.msgsOut++;
}

bool NetClient::begin(const JoinOptions& o) {
  opts = o;
  std::string h; int port;
  if (!parseAddress(o.address, h, port)) { err = "That address doesn't look right"; st = State::Failed; return false; }
  if (!enetInit()) { err = "The network could not start"; st = State::Failed; return false; }
  host = enet_host_create(nullptr, 1, CHANNELS, 0, 0);
  if (!host) { enetDone(); err = "The network could not start"; st = State::Failed; return false; }
  ENetAddress a;
  if (enet_address_set_host(&a, h.c_str()) != 0) { err = "Could not find " + h; st = State::Failed; return false; }
  a.port = (enet_uint16)port;
  peer = enet_host_connect(host, &a, CHANNELS, 0);
  if (!peer) { err = "Could not connect to that address"; st = State::Failed; return false; }
  enet_peer_timeout(peer, 0, 10000, 30000);
  st = State::Connecting; startedAt = nowSeconds();
  DQ_LOG("net: connecting to %s:%d", h.c_str(), port);
  return true;
}

float NetClient::progress() const {
  switch (st) {
    case State::Connecting: return 0.05f;
    case State::Joining: return expectRecords < 0 ? 0.15f : 0.2f + 0.8f * (expectRecords ? std::min(1.f, (float)gotRecords / expectRecords) : 1.f);
    case State::Ready: return 1;
    default: return 0;
  }
}

NetClient::State NetClient::poll() {
  if (!host || st == State::Ready || st == State::Failed || st == State::Closed) return st;
  ENetEvent ev;
  while (host && enet_host_service(host, &ev, 0) > 0) {
    if (ev.type == ENET_EVENT_TYPE_CONNECT) {
      st = State::Joining;
      send(peer, HELLO, {{"proto", PROTO}, {"version", DQ_VERSION}, {"name", opts.name}, {"skin", opts.skin}, {"password", opts.password}, {"R", opts.viewDistance}});
    } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
      u8 ty; json m;
      bool ok = parsePacket(ev.packet, ty, m);
      enet_packet_destroy(ev.packet);
      if (!ok) continue;
      stats.msgsIn++;
      if (ty == REFUSE) { err = m.value("why", "The host said no"); st = State::Failed; break; }
      if (ty == WELCOME) onWelcome(m);
      else if (ty == CK) { if (m.contains("r") && m["r"].is_binary()) { auto& b = m["r"].get_binary(); storeRecord(m.value("rl", 0), m.value("d", 0), m.value("cx", 0), m.value("cz", 0), std::vector<u8>(b.begin(), b.end()), true); gotRecords++; } }
      else if (ty == CS) { sums[packChunkKey(m.value("rl", 0), m.value("d", 0), m.value("cx", 0), m.value("cz", 0))] = m.value("h", 0u); stats.sumsIn++; }
      else if (ty == CKDONE) { expectRecords = m.value("n", 0); if (!welcomeMsg.is_null()) { st = State::Ready; break; } }
      // anything else before we're in (block changes, entities) is caught up with by the regular updates
    } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
      if (err.empty()) err = st == State::Connecting ? "No answer from that address" : "The host closed the connection";
      st = State::Failed; peer = nullptr; break;
    }
  }
  if ((st == State::Connecting || st == State::Joining) && nowSeconds() - startedAt > opts.timeout) {
    err = st == State::Connecting ? "No answer from that address" : "The host did not answer";
    st = State::Failed;
  }
  if (st == State::Failed) { if (peer) enet_peer_reset(peer); peer = nullptr; DQ_WARN("net: join failed: %s", err.c_str()); }
  if (st == State::Ready) DQ_LOG("net: joined '%s' (%d changed chunks)", serverName.c_str(), gotRecords);
  return st;
}

bool NetClient::connect(const JoinOptions& o) {
  if (!begin(o)) return false;
  while (st == State::Connecting || st == State::Joining) {
    poll();
    if (st == State::Connecting || st == State::Joining) enet_host_service(host, nullptr, 0), std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return st == State::Ready;
}

void NetClient::onWelcome(json& m) {
  welcomeMsg = m;
  myId = m.value("id", 0);
  serverName = m.value("name", "a world"); hostName = m.value("host", "someone");
  op = m.value("op", false); dedicated = m.value("dedicated", true); viewDistance = m.value("vd", opts.viewDistance);
  // block ids: the server's palette, matched to ours by key
  auto& R = reg();
  std::vector<std::string> missing;
  s2l.clear(); l2s.assign(R.blocks.size(), 0);
  if (m.contains("pal")) for (auto& k : m["pal"]) {
    std::string key = k.is_string() ? k.get<std::string>() : "";
    BlockId lid = key.empty() ? AIR : R.blockId(key);
    if (!lid && key != "air" && !key.empty() && missing.size() < 8) missing.push_back(key);
    if (lid && lid < l2s.size() && !l2s[lid]) l2s[lid] = (BlockId)s2l.size();
    s2l.push_back(lid);
  }
  if (!missing.empty()) DQ_WARN("net: the server has blocks this game doesn't (%s...) — they show as air", missing[0].c_str());
  if (expectRecords >= 0) st = State::Ready;
}

LevelInfo NetClient::levelInfo() const {
  LevelInfo li;
  if (welcomeMsg.contains("info")) li.fromJson(welcomeMsg["info"]);
  li.name = welcomeMsg.value("name", li.name);
  li.remote = true;
  return li;
}

void NetClient::storeRecord(int realm, int dim, int cx, int cz, std::vector<u8>&& rec, bool isEarly) {
  stats.records++;
  if (!lv) { early.push_back({realm, dim, cx, cz, std::move(rec)}); return; }
  World& w = lv->world(realm);
  u64 k = packChunkKey(realm, dim, cx, cz);
  stale.erase(k);
  if (Chunk* c = w.chunk(dim, cx, cz)) {
    applying = true; replaceChunk(w, *c, rec); applying = false;
    if (w.regionStore()) w.regionStore()->store({dim, cx, cz}, std::move(rec));
    return;
  }
  if (w.regionStore()) w.regionStore()->store({dim, cx, cz}, std::move(rec));
  if (!isEarly && w.pendingGeneration() > 0) fresh.insert(k);   // it may be generating right now: put it right when it arrives
}

void NetClient::attach(Level& level) {
  lv = &level;
  // the local player and anything made here get ids the server never uses
  auto marker = std::make_unique<Entity>();
  marker->type = "net_marker"; marker->id = LOCAL_ID_BASE; marker->removed = true; marker->netLocal = true;
  level.spawn(std::move(marker));
  watch = std::make_shared<ClientWatch>(*this);
  for (auto& e : early) storeRecord(e.realm, e.dim, e.cx, e.cz, std::move(e.rec), true);
  early.clear();
  for (auto& [r, w] : level.allRealms()) if (!watched.count(r)) { w->addSystem(watch); watched.insert(r); }
}

void NetClient::findMe() {
  if (me && !me->removed) return;
  me = nullptr;
  for (auto& e : lv->entities()) if (!e->removed && !e->ghost && e->isPlayer() && e->id >= LOCAL_ID_BASE) { me = dynamic_cast<Player*>(e.get()); if (me) break; }
}

void NetClient::update(Level& level, double dt) {
  if (!lv) attach(level);
  if (!host || st != State::Ready) return;
  t += dt;
  findMe();
  for (auto& [r, w] : level.allRealms()) if (!watched.count(r)) { w->addSystem(watch); watched.insert(r); }
  ENetEvent ev;
  int guard = 0;
  while (host && guard++ < 8192 && enet_host_service(host, &ev, 0) > 0) {
    if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
      u8 ty; json m;
      bool ok = parsePacket(ev.packet, ty, m);
      enet_packet_destroy(ev.packet);
      if (!ok) continue;
      stats.msgsIn++;
      try { handle(ty, m); } catch (const std::exception& e) { DQ_WARN("net: message %s: %s", msgName(ty), e.what()); }
    } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { disconnected(ev.data); return; }
  }
  if (st != State::Ready) return;
  // records that came while their chunk was being generated here
  for (u64 k : replaceQ) {
    int r, d, cx, cz; unpackChunkKey(k, r, d, cx, cz);
    World* w = level.worldIfOpen(r); Chunk* c = w ? w->chunk(d, cx, cz) : nullptr;
    std::vector<u8> rec;
    if (c && w->regionStore() && w->regionStore()->load({d, cx, cz}, rec)) { applying = true; replaceChunk(*w, *c, rec); applying = false; }
  }
  replaceQ.clear();
  // the server's checksums for untouched chunks: compare once ours has settled
  if ((tSums += dt) >= 0.5 && !sums.empty()) {
    tSums = 0;
    for (auto it = sums.begin(); it != sums.end();) {
      int r, d, cx, cz; unpackChunkKey(it->first, r, d, cx, cz);
      World* w = level.worldIfOpen(r); Chunk* c = w ? w->chunk(d, cx, cz) : nullptr;
      if (c && chunkSettled(*w, *c)) {
        if (!c->modified && chunkHash(*c, &l2s) != it->second) { sendTo(NEED, {{"rl", r}, {"d", d}, {"cx", cx}, {"cz", cz}}); stats.needs++; }
        stats.sums++;
        it = sums.erase(it);
      } else ++it;
    }
    if (sums.size() > 20000) sums.clear();
  }
  if (me) {
    // a new land: the old one's ghosts are gone (the server sends this land's)
    if (me->realm != lastRealm) {
      for (auto& e : level.entities()) if (e->ghost && e->realm != me->realm) e->removed = true;
      lastRealm = me->realm;
    }
    // things made here (arrows, dropped items, boats...): the server makes the real ones
    for (auto& e : level.entities()) {
      if (e->removed || e->ghost || e->netLocal || e.get() == me || e->isPlayer() || e->id <= LOCAL_ID_BASE) continue;
      json s = e->save(); s["type"] = e->type;
      sendTo(SPAWN, {{"e", s}});
      e->removed = true;
    }
    if ((tMe += dt) >= SEND_ME) { tMe = 0; sendMe(); }
    if ((tInv += dt) >= 0.5) {
      tInv = 0;
      std::string sig = playerInvSig(*me);
      if (!invSig.empty() && sig != invSig && saveDue == 0) saveDue = t + 0.7;   // what you carry changed: soon, not in 8 s
      invSig = sig;
    }
    if ((tSave += dt) >= SEND_SAVE || (saveDue > 0 && t >= saveDue)) sendSave();
  }
  if (!blkOut.empty()) {
    for (auto& [r, l] : blkOut.items()) sendTo(BLK, {{"rl", std::stoi(r)}, {"l", l}});
    blkOut = json::object();
  }
  if (host) enet_host_flush(host);
}

void NetClient::sendMe() {
  Player& p = *me;
  int f = (p.onGround ? 1 : 0) | (p.inWater ? 2 : 0) | (p.sneaking ? 4 : 0) | (p.sprinting ? 8 : 0) | (p.flying ? 16 : 0) | (p.swimming ? 32 : 0) | (p.crawling ? 64 : 0);
  json m = {{"x", r3(p.pos.x)}, {"y", r3(p.pos.y)}, {"z", r3(p.pos.z)}, {"vx", r3(p.vel.x)}, {"vy", r3(p.vel.y)}, {"vz", r3(p.vel.z)},
            {"r", r3(p.yaw)}, {"p", r3(p.pitch)}, {"b", r3(p.bodyYaw)}, {"rl", p.realm}, {"f", f}, {"hp", std::round(p.hp * 10) / 10},
            {"dead", p.dead ? 1 : 0}, {"sel", p.selected}};
  if (p.riding) m["rd"] = p.riding;
  sendTo(ME, m, 1, false);
}

void NetClient::sendSave() {
  if (!me) return;
  tSave = 0; saveDue = 0;
  json pl = playerInvJson(*me);
  pl["air"] = me->air; pl["stats"] = me->stats; pl["spawnPoint"] = v3(me->spawnPoint); pl["spawnRealm"] = me->spawnRealm;
  lastSave = pl;
  invSig = playerInvSig(*me);
  sendTo(SAVE, {{"base", invSeq}, {"pl", pl}});
}

void NetClient::handle(u8 ty, json& m) {
  Level& L = *lv;
  switch (ty) {
    case CK:
      if (m.contains("r") && m["r"].is_binary()) { auto& b = m["r"].get_binary(); storeRecord(m.value("rl", 0), m.value("d", 0), m.value("cx", 0), m.value("cz", 0), std::vector<u8>(b.begin(), b.end()), false); }
      break;
    case CS: {
      int r = m.value("rl", 0), d = m.value("d", 0), cx = m.value("cx", 0), cz = m.value("cz", 0);
      sums[packChunkKey(r, d, cx, cz)] = m.value("h", 0u);
      stats.sumsIn++;
      break;
    }
    case BLK: {
      int r = m.value("rl", 0);
      World* w = L.worldIfOpen(r);
      for (auto& b : m["l"]) {
        if (!b.is_array() || b.size() < 5) continue;
        int x = b[0], y = b[1], z = b[2];
        stats.blocksIn++;
        if (w && w->isLoaded(x, y, z)) { applying = true; w->setBlock(x, y, z, toLocal(b[3].get<u32>()), (u8)b[4].get<int>(), SetFlags::FromNet | SetFlags::Urgent); applying = false; }
        else if (inWorldY(y)) stale.insert(packChunkKey(r, dimOfY(y), x >> 4, z >> 4));   // fetched fresh when it loads
      }
      break;
    }
    case BE: {
      int r = m.value("rl", 0), x = m.value("x", 0), y = m.value("y", 0), z = m.value("z", 0);
      World* w = L.worldIfOpen(r);
      if (!w || !w->isLoaded(x, y, z)) { if (inWorldY(y)) stale.insert(packChunkKey(r, dimOfY(y), x >> 4, z >> 4)); break; }
      std::string k = strFormat("%d,%d,%d,%d", r, x, y, z);
      if (auto it = bePend.find(k); it != bePend.end()) {   // a change of ours the server hasn't taken in yet
        int got = m.contains("a") && m["a"].is_object() ? m["a"].value(std::to_string(myId), 0) : 0;
        if (got < it->second) break;
        bePend.erase(it);
      }
      applyingBE = true; w->setBlockEntity(x, y, z, m.value("e", json())); applyingBE = false;
      break;
    }
    case ES: applyEntities(m); break;
    case SELF: applySelf(m); break;
    case ENV: applyEnv(m); break;
    case FX: applyFx(m); break;
    case MSG: L.fx->message(0, m.value("text", ""), m.value("k", "")); break;
    case PEER: {
      std::string n = m.value("n", "Someone");
      L.fx->message(0, n + (m.value("j", 0) ? " joined the game" : " left the game"), m.value("j", 0) ? "good" : "");
      break;
    }
    case FLUSH: sendSave(); break;
    case KICK:   // let go: don't wait for the connection to wind down
      lostReason = m.value("why", "The host removed you from the world.");
      if (peer) { enet_peer_disconnect_now(peer, BYE_NORMAL); peer = nullptr; }
      st = State::Closed;
      DQ_LOG("net: %s", lostReason.c_str());
      break;
    default: break;   // newer servers may send more
  }
}

void NetClient::applyEntities(const json& m) {
  Level& L = *lv;
  int realm = m.value("rl", 0);
  if (me && realm != me->realm) return;   // on the way through a portal: that land's news isn't for here
  for (auto& s : m["l"]) {
    u32 id = s.value("i", 0u);
    if (!id || id >= LOCAL_ID_BASE) continue;
    Entity* e = L.entity(id);
    if (e && !e->ghost) continue;
    vec3 pos(s.value("x", 0.f), s.value("y", 0.f), s.value("z", 0.f));
    float yaw = s.value("r", 0.f);
    std::string type = s.value("t", "");
    if (e && e->removed) { e->removed = false; e->pos = e->netPos = pos; }   // gone and back (out of range and in again)
    if (!e) {
      std::unique_ptr<Entity> ne = type == "player" ? std::make_unique<Player>() : createEntity(type);
      ne->id = id; ne->type = type; ne->ghost = true; ne->realm = realm;
      ne->pos = ne->netPos = ne->prevPos = pos; ne->yaw = ne->netYaw = ne->prevYaw = yaw;
      e = L.spawn(std::move(ne));
    } else { e->netPos = pos; e->netYaw = yaw; }
    e->pitch = s.value("p", e->pitch); e->bodyYaw = s.value("b", e->bodyYaw);
    float hp = s.value("hp", e->hp);
    if (hp < e->hp) e->hurtTime = 0.5f;
    e->hp = hp; e->maxHp = s.value("mh", e->maxHp); e->w = s.value("w", e->w); e->h = s.value("h", e->h);
    int f = s.value("f", 0);
    e->onGround = f & 1; e->inWater = f & 2; e->inLava = f & 4; e->noGravity = f & 16;
    if (f & 8) e->hurtTime = std::max(e->hurtTime, 0.3f);
    e->riding = s.value("rd", 0u);
    if (s.contains("d")) e->data = s["d"];
    if (auto* p = dynamic_cast<Player*>(e)) {
      p->name = s.value("n", p->name); p->skin = s.value("sk", p->skin); p->mode = s.value("md", p->mode);
      p->selected = 0;
      p->inv[0] = s.contains("hd") ? ItemStack(s["hd"].get<std::string>(), 1) : ItemStack();
      for (int i = 0; i < INV_ARMOR; i++) p->armor[i] = s.contains("ar") && i < (int)s["ar"].size() && s["ar"][i].is_string() ? ItemStack(s["ar"][i].get<std::string>(), 1) : ItemStack();
      p->offhand[0] = s.contains("of") ? ItemStack(s["of"].get<std::string>(), 1) : ItemStack();
      int pf = s.value("pf", 0);
      p->sneaking = pf & 1; p->sprinting = pf & 2; p->flying = pf & 4; p->swimming = pf & 8; p->crawling = pf & 16; p->dead = pf & 32;
    }
  }
  if (m.contains("g")) for (auto& id : m["g"]) if (Entity* e = L.entity(id.get<u32>())) if (e->ghost) e->removed = true;
}

void NetClient::applySelf(const json& m) {
  findMe();
  if (!me) return;
  Player& p = *me;
  if (m.contains("tp")) { p.pos = p.prevPos = v3(m["tp"], p.pos); p.vel = vec3(0); p.fallDist = 0; p.realm = m.value("rl", p.realm); }
  if (m.contains("kb")) p.vel += v3(m["kb"]);
  if (m.contains("hurt")) {
    float a = m["hurt"].get<float>();
    p.hp -= a; p.hurtTime = 0.5f;
    if (lv->events) lv->events->emit("netHurt", json::array({a}));
  }
  if (m.contains("hp")) p.hp = m["hp"].get<float>();
  if (m.contains("inv")) { applyPlayerInv(p, m["inv"]); invSeq = m.value("seq", invSeq); invSig = playerInvSig(p); }
  if (m.contains("md")) p.mode = m["md"].get<std::string>();
  if (m.contains("fl")) p.flying = m["fl"].get<bool>();
  if (m.contains("xp")) { p.xp = m.value("xp", p.xp); p.level = m.value("lv", p.level); }
}

void NetClient::applyEnv(const json& m) {
  LevelInfo& I = lv->info;
  // our clock runs on between updates; only a real jump (a night slept through) snaps it
  double time = m.value("time", I.time), d = std::abs(time - I.time);
  if (d > 40 && d < DAY_LEN - 40) I.time = time;
  else I.time += (std::fmod(time - I.time + DAY_LEN * 1.5, DAY_LEN) - DAY_LEN / 2) * 0.5;
  if (I.time < 0) I.time += DAY_LEN;
  if (I.time >= DAY_LEN) I.time -= DAY_LEN;
  I.day = m.value("day", I.day);
  if (m.contains("w") && !m["w"].is_null()) { if (!I.data.is_object()) I.data = json::object(); I.data["weather"] = m["w"]; }
  if (m.contains("rules") && m["rules"].is_object()) I.rules = m["rules"];
}

void NetClient::applyFx(const json& m) {
  LevelFx& fx = *lv->fx;
  auto P = [](const json& a, int i) { return vec3(a[i].get<float>(), a[i + 1].get<float>(), a[i + 2].get<float>()); };
  if (m.contains("s")) for (auto& s : m["s"]) fx.sound(s[0], P(s, 1), s[4], s.size() > 5 ? s[5].get<float>() : 1.f, s.size() > 6 ? s[6].get<float>() : 1.f);
  if (m.contains("m")) for (auto& s : m["m"]) fx.material(s[0], s[1], P(s, 2));
  if (m.contains("v")) for (auto& s : m["v"]) fx.voice(s[0], s[1], P(s, 2), s.size() > 5 ? s[5].get<float>() : 1.f);
  if (m.contains("p")) for (auto& s : m["p"]) fx.particles(s[0], P(s, 1), s[4], s.size() > 5 ? s[5] : json());
  u32 myEnt = me ? me->id : 0;
  if (m.contains("u")) for (auto& s : m["u"]) fx.soundUi(myEnt, s);
  if (m.contains("k")) for (auto& s : m["k"]) fx.shake(myEnt, s);
}

bool NetClient::chat(Player&, const std::string& text) { sendTo(CHAT, {{"text", text.substr(0, 200)}}); return true; }
void NetClient::hitEntity(Player&, u32 id, float amount, const json& src) { sendTo(HIT, {{"id", id}, {"a", amount}, {"s", src}}); }
void NetClient::action(Player&, const std::string& name, const json& args) { sendTo(ACT, {{"n", name}, {"a", args}}); }
void NetClient::portal(Player& who, int realm, vec3 to) { sendTo(PORTAL, {{"rl", realm}, {"to", v3(to)}}); }
std::vector<std::string> NetClient::playerNames() const {
  std::vector<std::string> v;
  if (lv) for (auto& e : lv->entities()) if (!e->removed && e->isPlayer()) if (auto* p = dynamic_cast<Player*>(e.get())) v.push_back(p->name);
  return v;
}

void NetClient::disconnected(u32 code) {
  peer = nullptr;
  if (lostReason.empty())
    lostReason = code == BYE_KICKED ? "The host removed you from the world." : code == BYE_CLOSED ? "The host closed the world." : "Lost the connection to the host.";
  st = State::Closed;
  DQ_LOG("net: %s", lostReason.c_str());
}

void NetClient::close(const std::string& why) {
  if (host) {
    if (peer && st == State::Ready) {
      if (me && lv) { try { sendSave(); } catch (...) {} }
      send(peer, BYE, lastSave.is_null() ? json::object() : json{{"pl", lastSave}});
      enet_host_flush(host);
      enet_peer_disconnect_later(peer, BYE_NORMAL);
      double t0 = nowSeconds(); ENetEvent ev;
      while (nowSeconds() - t0 < 0.3 && enet_host_service(host, &ev, 20) >= 0) {
        if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT) break;
      }
    }
    if (peer) enet_peer_reset(peer);
    enet_host_destroy(host);
    host = nullptr; peer = nullptr;
    enetDone();
  }
  if (lv && watch) for (auto& [r, w] : lv->allRealms()) {
    auto& s = w->systems;
    s.erase(std::remove_if(s.begin(), s.end(), [&](auto& x) { return x.get() == watch.get(); }), s.end());
  }
  if (st != State::Failed) st = State::Closed;
  if (!why.empty() && lostReason.empty()) lostReason = why;
}

} // namespace dq::net
