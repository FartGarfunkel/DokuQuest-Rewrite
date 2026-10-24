// net/server_host.cpp — the host side of net.js (Net.host / hostMsg / welcome / peerLeft) and the parts of
// game/server.js that aren't screens: the player limit, the welcome message, kick, plus ban and op lists.
#include "net/server_host.h"
#include "net/net_internal.h"
#include "sim/commands.h"
#include "gen/generator.h"
#include <enet/enet.h>

namespace dq::net {

static std::string lowerName(const std::string& s) { return toLower(s); }
static bool validPlayerName(const std::string& n) {
  if (n.empty() || n.size() > 24 || n[0] == ' ' || n.back() == ' ') return false;
  for (char c : n) if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != ' ') return false;
  return true;
}

ServerHost::ServerHost(Level& lv, HostOptions o) : opts(std::move(o)), level(lv) {
  for (auto& b : reg().blocks) palette.push_back(b.key);
  loadLists();
}
ServerHost::~ServerHost() {
  // the level may already be gone: stop() (while it's open) saves everyone and says goodbye; this only closes the port
  if (!host) return;
  for (auto& [id, p] : peers) if (p.enet) enet_peer_disconnect_now(p.enet, BYE_CLOSED);
  enet_host_destroy(host); host = nullptr;
  lanAnn.stop();
  enetDone();
}

bool ServerHost::start(std::string* err) {
  if (host) return true;
  if (!enetInit()) { if (err) *err = "The network could not start"; return false; }
  for (int p = opts.port; p < opts.port + 10 && !host; p++) {
    ENetAddress a; a.host = ENET_HOST_ANY; a.port = (enet_uint16)p;
    host = enet_host_create(&a, (size_t)std::max(1, opts.maxPlayers) + 4, CHANNELS, 0, 0);
    if (host) boundPort = p;
  }
  if (!host) {
    enetDone();
    if (err) *err = strFormat("No free port between %d and %d", opts.port, opts.port + 9);
    return false;
  }
  // sounds, puffs and messages of this world reach the players they're near
  innerFx = level.fx;
  fx = new NetFx(*this, innerFx);
  level.fx = fx;
  ensureWatchers();
  if (opts.lan) lanAnn.start();
  serverLog(strFormat("\"%s\" is open on port %d (%s)", opts.name.c_str(), boundPort, level.info.name.c_str()), "good");
  return true;
}

void ServerHost::stop(const std::string& why) {
  if (!host) return;
  flushSaves();
  saveAll();
  for (auto& [id, p] : peers) if (p.enet) {
    send(p.enet, KICK, {{"why", why}, {"closed", true}});
    enet_peer_disconnect_later(p.enet, BYE_CLOSED);
  }
  // give the goodbyes a moment to go out
  double t0 = nowSeconds();
  ENetEvent ev;
  while (nowSeconds() - t0 < 0.5 && enet_host_service(host, &ev, 20) >= 0) {
    if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    bool any = false; for (auto& [id, p] : peers) if (p.enet && p.enet->state != ENET_PEER_STATE_DISCONNECTED) any = true;
    if (!any) break;
  }
  for (auto& [id, p] : peers) { if (p.pl) p.pl->removed = true; if (p.enet) enet_peer_reset(p.enet); }
  peers.clear();
  enet_host_destroy(host); host = nullptr;
  lanAnn.stop();
  if (level.fx == fx) level.fx = innerFx;
  delete fx; fx = nullptr;
  for (auto& [r, w] : level.allRealms()) {
    auto& s = w->systems;
    s.erase(std::remove_if(s.begin(), s.end(), [&](auto& x) { return x.get() == watch.get(); }), s.end());
  }
  watched.clear();
  enetDone();
  serverLog("Server stopped", "");
}

void ServerHost::serverLog(const std::string& text, const std::string& kind) {
  DQ_LOG("[server] %s", text.c_str());
  if (onLog) onLog(text, kind);
  if (logging || !level.events) return;
  logging = true;
  level.events->emit("log", json::array({text, kind}));
  logging = false;
}

json ServerHost::lanInfo() const {
  int n = 0; for (auto& [id, p] : peers) if (p.ready) n++;
  if (!opts.dedicated) n++;
  return {{"world", opts.name}, {"host", opts.hostName}, {"players", n}, {"max", opts.maxPlayers}, {"port", boundPort},
          {"motd", opts.motd}, {"password", !opts.password.empty()}};
}

// ---- lists ----
void ServerHost::loadLists() {
  bans = readJson(level.folder / "bans.json").value_or(json::object());
  if (!bans.is_object()) bans = json::object();
  if (!bans.contains("names")) bans["names"] = json::array();
  if (!bans.contains("ips")) bans["ips"] = json::array();
  ops = readJson(level.folder / "ops.json").value_or(json::array());
  if (!ops.is_array()) ops = json::array();
}
void ServerHost::saveLists() { writeJson(level.folder / "bans.json", bans); writeJson(level.folder / "ops.json", ops); }
static bool inList(const json& a, const std::string& s) { for (auto& v : a) if (v.is_string() && toLower(v.get<std::string>()) == toLower(s)) return true; return false; }
static void removeFrom(json& a, const std::string& s) { json o = json::array(); for (auto& v : a) if (!(v.is_string() && toLower(v.get<std::string>()) == toLower(s))) o.push_back(v); a = o; }

bool ServerHost::isOp(const std::string& name) const { return inList(ops, name); }
void ServerHost::setOp(const std::string& name, bool on) {
  removeFrom(ops, name);
  if (on) ops.push_back(name);
  saveLists();
  tell(name, on ? "You are now an operator: commands are yours to use." : "You are no longer an operator.", "good");
  serverLog((on ? "Made " : "Took operator from ") + name, "");
}
bool ServerHost::ban(const std::string& name, const std::string& why) {
  if (!inList(bans["names"], name)) bans["names"].push_back(name);
  if (Peer* p = peerByName(name)) if (!inList(bans["ips"], p->addr) && p->addr != "127.0.0.1") bans["ips"].push_back(p->addr);
  saveLists();
  kick(name, why.empty() ? "You are banned from this server." : why);
  serverLog("Banned " + name, "warn");
  return true;
}
bool ServerHost::banIp(const std::string& ip) { if (!inList(bans["ips"], ip)) bans["ips"].push_back(ip); saveLists(); for (auto& [id, p] : peers) if (p.addr == ip) kick(p.name, "You are banned from this server."); return true; }
bool ServerHost::pardon(const std::string& s) {
  bool had = inList(bans["names"], s) || inList(bans["ips"], s);
  removeFrom(bans["names"], s); removeFrom(bans["ips"], s);
  saveLists();
  return had;
}

// ---- peers ----
ServerHost::Peer* ServerHost::peerOf(ENetPeer* e) {
  if (!e || !e->data) return nullptr;
  auto it = peers.find((u32)(uintptr_t)e->data);
  return it == peers.end() ? nullptr : &it->second;
}
ServerHost::Peer* ServerHost::peerByName(const std::string& name) {
  for (auto& [id, p] : peers) if (p.hello && !p.left && lowerName(p.name) == lowerName(name)) return &p;
  return nullptr;
}
ServerHost::Peer* ServerHost::peerByEntity(u32 eid) {
  if (!eid) return nullptr;
  for (auto& [id, p] : peers) if (p.eid == eid && !p.left) return &p;
  return nullptr;
}
Player* ServerHost::player(const std::string& name) const {
  for (auto& e : level.entities()) if (!e->removed && e->isPlayer()) { auto* p = static_cast<Player*>(e.get()); if (lowerName(p->name) == lowerName(name)) return p; }
  return nullptr;
}
std::vector<ServerHost::PlayerInfo> ServerHost::players() const {
  std::vector<PlayerInfo> out;
  for (auto& [id, p] : peers) if (p.ready && p.pl && !p.left) {
    PlayerInfo i; i.name = p.name; i.address = p.addr; i.entity = p.eid; i.realm = p.pl->realm; i.pos = p.pl->pos; i.hp = p.pl->hp;
    i.op = isOp(p.name); i.rttMs = p.enet ? p.enet->roundTripTime : 0;
    out.push_back(i);
  }
  return out;
}
std::vector<std::string> ServerHost::playerNames() const {
  std::vector<std::string> v;
  for (auto& e : level.entities()) if (!e->removed && e->isPlayer()) v.push_back(static_cast<Player*>(e.get())->name);
  return v;
}
void ServerHost::toAll(Msg t, const json& m, u32 except, int channel) {
  for (auto& [id, p] : peers) if (p.hello && !p.left && p.enet && id != except) send(p.enet, t, m, channel);
}

bool ServerHost::kick(const std::string& name, const std::string& why) {
  Peer* p = peerByName(name);
  if (!p) return false;
  std::string w = why.empty() ? "The host removed you from the world." : why;
  if (p->enet) { send(p->enet, KICK, {{"why", w}}); enet_peer_disconnect_later(p->enet, BYE_KICKED); }
  serverLog("Kicked " + p->name + (why.empty() ? "" : " (" + why + ")"), "warn");
  peerLeft(*p);
  return true;
}
void ServerHost::say(const std::string& text, const std::string& from) {
  level.fx->message(0, "[" + from + "] " + text.substr(0, 200), "chat");
}
bool ServerHost::tell(const std::string& name, const std::string& text, const std::string& kind) {
  Player* pl = player(name);
  if (!pl) return false;
  level.fx->message(pl->id, text, kind);
  return true;
}
bool ServerHost::teleport(const std::string& name, vec3 pos, int realm) {
  Player* pl = player(name);
  if (!pl) return false;
  pl->pos = pos; pl->vel = vec3(0); pl->fallDist = 0;
  if (realm >= 0) pl->realm = realm;
  if (!pl->ghost) pl->netPos = pos;
  return true;
}

vec3 ServerHost::spawnFor(const std::string& name) {
  if (!opts.dedicated)   // next to the host, like the original
    for (auto& e : level.entities()) if (!e->removed && e->isPlayer() && !e->ghost) return e->pos + vec3(1.2f, 0.2f, 0.6f);
  vec3 sp = level.info.spawn;
  if (sp.y < 0) {
    DimGenerator* g = level.gen ? level.gen->forDim(DIM_OVERWORLD) : nullptr;
    int sx = 0, sz = 0, h = g ? g->spawnHeight(0, 0) : 100;
    for (int r = 0; r < 400 && g; r += 16) {
      ColumnSample s; g->sample(r, r / 2, s);
      if (s.height > 0 && s.water < 0) { sx = r; sz = r / 2; h = s.height + 1; break; }
    }
    sp = vec3(sx + 0.5f, dimBase(DIM_OVERWORLD) + h + 1, sz + 0.5f);
    level.info.spawn = sp;
  }
  int top = level.world().surfaceY(ifloor(sp.x), ifloor(sp.z), dimOfY((int)sp.y));
  if (top > 0) sp.y = (float)top + 1.1f;
  return sp;
}

void ServerHost::refuse(Peer& p, const std::string& why) {
  send(p.enet, REFUSE, {{"why", why}});
  enet_peer_disconnect_later(p.enet, BYE_REFUSED);
  serverLog("Turned away " + (p.name.empty() ? p.addr : p.name) + ": " + why, "warn");
  p.left = true;
}

void ServerHost::welcome(Peer& p, const json& m) {
  std::string name = m.value("name", "");
  p.name = name;
  if (m.value("proto", 0) != PROTO) {
    refuse(p, "The host is playing version " + std::string(DQ_VERSION) + " and you have " + m.value("version", std::string("?")) +
              ". Update so you both have the same one.");
    return;
  }
  if (!validPlayerName(name)) { refuse(p, "Pick a name of letters, numbers, spaces, - or _ (24 at most) in Players & Skins."); return; }
  if (inList(bans["names"], name) || inList(bans["ips"], p.addr)) { refuse(p, "You are banned from this server."); return; }
  if (!opts.password.empty() && m.value("password", "") != opts.password) { refuse(p, m.value("password", "").empty() ? "This server needs a password." : "That password isn't right."); return; }
  int playing = 0; for (auto& [id, q] : peers) if (q.hello && !q.left) playing++;
  if (playing >= opts.maxPlayers) { refuse(p, strFormat("The server is full (%d players). Try again in a while.", opts.maxPlayers)); return; }
  // a name already playing here: the same person back from the same computer takes over; anyone else picks another
  if (Peer* other = peerByName(name)) {
    if (other->addr == p.addr) { if (other->enet) enet_peer_disconnect_now(other->enet, BYE_KICKED); peerLeft(*other, true); }
    else { refuse(p, "Someone called " + other->name + " is already playing here. Pick another name in Players & Skins."); return; }
  }
  for (auto& e : level.entities()) if (!e->removed && e->isPlayer() && !e->ghost && lowerName(static_cast<Player*>(e.get())->name) == lowerName(name)) {
    refuse(p, "The host is called " + name + " too. Pick another name in Players & Skins."); return;
  }
  p.hello = true; p.skin = m.value("skin", ""); p.R = clampv(m.value("R", 6), 2, 32);
  p.joinedAt = nowSeconds(); p.lastMeAt = p.joinedAt; p.allowJumpUntil = p.joinedAt + 5;
  // their body here: as they left it, or new at spawn
  auto pl = std::make_unique<Player>();
  pl->name = name; pl->skin = p.skin;
  json saved = level.loadPlayer(name);
  bool known = saved.is_object();
  if (known) { pl->load(saved); pl->name = name; pl->skin = p.skin.empty() ? pl->skin : p.skin; }
  else { pl->mode = level.info.mode; pl->pos = spawnFor(name); }
  pl->ghost = true; pl->netPos = pl->pos; pl->netYaw = pl->yaw; pl->id = 0;
  p.pl = static_cast<Player*>(level.spawn(std::move(pl)));
  p.eid = p.pl->id;
  p.aPos = p.pl->pos; p.aVel = p.pl->vel; p.aHp = p.pl->hp; p.aInv = playerInvSig(*p.pl); p.aMode = p.pl->mode; p.aXp = p.pl->xp;
  p.aLevel = p.pl->level; p.aRealm = p.pl->realm; p.aFly = p.pl->flying; p.aDead = p.pl->dead;
  json info = level.info.toJson();
  info.erase("format");
  json w = {{"proto", PROTO}, {"id", p.id}, {"eid", p.eid}, {"info", info}, {"pal", palette}, {"pl", p.pl->save()},
            {"name", opts.name}, {"host", opts.hostName}, {"vd", std::min(p.R, opts.viewDistance)}, {"op", isOp(name)},
            {"dedicated", opts.dedicated}, {"version", DQ_VERSION}};
  send(p.enet, WELCOME, w);
  // the changed chunks round where they'll start come straight away
  updateRange(p, true);
  size_t n = p.ckQ.size();
  flushChunks(p, 1 << 30);
  send(p.enet, CKDONE, {{"n", (int)n}});
  serverLog(name + " is joining from " + p.addr, "");
}

void ServerHost::peerLeft(Peer& p, bool quiet) {
  if (p.left && !p.pl) return;
  p.left = true;
  if (p.pl) {
    level.savePlayer(p.name, p.pl->save());
    p.pl->removed = true;
    p.pl = nullptr;
  }
  if (p.ready && !quiet) {
    toAll(PEER, {{"n", p.name}, {"j", 0}}, p.id);
    if (innerFx && !opts.dedicated) innerFx->message(0, p.name + " left the game", "");
    serverLog(p.name + " left", "");
    if (level.events) level.events->emit("leave", json::array({p.name}));
  }
  p.ready = false;
}

void ServerHost::flushSaves() {
  std::vector<u32> want;
  for (auto& [id, p] : peers) if (p.ready && p.enet) { send(p.enet, FLUSH, json::object()); want.push_back(id); p.savedAt = 0; }
  if (want.empty() || !host) return;
  double t0 = nowSeconds();
  ENetEvent ev;
  while (nowSeconds() - t0 < 1.5) {
    if (enet_host_service(host, &ev, 20) > 0) {
      if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
        Peer* p = peerOf(ev.peer); u8 t; json m;
        if (p && parsePacket(ev.packet, t, m) && (t == SAVE || t == BYE)) receive(*p, t, m);
        enet_packet_destroy(ev.packet);
      } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { if (Peer* p = peerOf(ev.peer)) { p->enet = nullptr; peerLeft(*p); } }
    }
    bool all = true; for (u32 id : want) { auto it = peers.find(id); if (it != peers.end() && !it->second.left && it->second.savedAt < t0) all = false; }
    if (all) break;
  }
}
void ServerHost::saveAll() { for (auto& [id, p] : peers) if (p.pl && !p.left) level.savePlayer(p.name, p.pl->save()); }

// ---- the loop ----
void ServerHost::update(Level& lv, double dt) {
  if (!host) return;
  t += dt;
  ensureWatchers();
  diffMirrors();   // first: what the server did to anyone since last time, before their own news overwrites it
  ENetEvent ev;
  int guard = 0;
  while (host && guard++ < 4096 && enet_host_service(host, &ev, 0) > 0) {
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        Peer p; p.id = nextPeerId++; p.enet = ev.peer; p.addr = peerAddress(ev.peer);
        ev.peer->data = (void*)(uintptr_t)p.id;
        enet_peer_timeout(ev.peer, 0, 10000, 30000);   // half a minute without a word: they've gone (NET_SILENT)
        peers[p.id] = p;
        serverLog("A connection from " + p.addr, "");
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE: {
        Peer* p = peerOf(ev.peer); u8 ty = 0; json m;
        if (p && !p->left && parsePacket(ev.packet, ty, m)) {
          try { receive(*p, ty, m); } catch (const std::exception& e) { DQ_WARN("net: message %s from %s: %s", msgName(ty), p->name.c_str(), e.what()); }
        }
        enet_packet_destroy(ev.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        if (Peer* p = peerOf(ev.peer)) { p->enet = nullptr; peerLeft(*p); }
        ev.peer->data = nullptr;
        break;
      }
      default: break;
    }
  }
  for (auto it = peers.begin(); it != peers.end();) {   // gone for good
    if (it->second.left && (!it->second.enet || it->second.enet->state == ENET_PEER_STATE_DISCONNECTED || it->second.enet->state == ENET_PEER_STATE_ZOMBIE)) {
      if (it->second.enet) it->second.enet->data = nullptr;
      it = peers.erase(it);
    } else ++it;
  }
  // changes to chunks that were still loading here when a player made them
  for (size_t i = 0; i < pendingBlocks.size();) {
    auto& b = pendingBlocks[i];
    World* w = level.worldIfOpen(b.realm);
    if (w && w->isLoaded(b.x, b.y, b.z)) { w->setBlock(b.x, b.y, b.z, b.id, b.meta, SetFlags::FromNet | SetFlags::Urgent); }
    else if (t < b.until) { i++; continue; }
    pendingBlocks[i] = pendingBlocks.back(); pendingBlocks.pop_back();
  }
  // the welcome message, a moment after arriving
  for (auto& [id, p] : peers) if (p.ready && p.motdAt > 0 && t >= p.motdAt) {
    p.motdAt = -1;
    if (!opts.motd.empty() && p.enet) send(p.enet, MSG, {{"text", "[Server] " + opts.motd}, {"k", "chat"}});
  }
  flushBlocks();
  for (auto& [k, e] : beQ) {
    auto& [r, x, y, z] = k;
    json a = json::object();
    if (auto it = beAck.find(k); it != beAck.end()) for (auto& [pid, q] : it->second) a[std::to_string(pid)] = q;
    toAll(BE, {{"rl", r}, {"x", x}, {"y", y}, {"z", z}, {"e", e}, {"a", a}});
  }
  beQ.clear();
  flushEffects();
  if ((tChunks += dt) >= SEND_CHUNKS) {
    tChunks = 0;
    for (auto& [id, p] : peers) if (p.ready && p.pl && p.enet) { updateRange(p, false); flushChunks(p, 10); }
  }
  if ((tEnts += dt) >= SEND_ENTS) {
    tEnts = 0;
    std::unordered_map<u32, Snap> snaps;
    bool any = false; for (auto& [id, p] : peers) if (p.ready) any = true;
    if (any) {
      for (auto& e : level.entities()) if (!e->removed && !e->netLocal) {
        Snap sn; sn.s = entitySnapshot(*e); sn.hash = std::hash<std::string>()(sn.s.dump());
        sn.dataHash = e->data.is_null() || e->data.empty() ? 0 : std::hash<std::string>()(e->data.dump());
        snaps.emplace(e->id, std::move(sn));
      }
      for (auto& [id, p] : peers) if (p.ready && p.pl && p.enet) sendEntities(p, snaps);
    }
  }
  if ((tEnv += dt) >= SEND_ENV) { tEnv = 0; sendEnv(); }
  if (!opts.dedicated) {   // open to LAN: the game streams the host's land; any other land a player is in is kept loaded here
    std::set<int> hostRealms;
    for (auto& e : level.entities()) if (!e->removed && e->isPlayer() && !e->ghost) hostRealms.insert(e->realm);
    std::map<int, std::vector<StreamAnchor>> away;
    for (auto& [id, p] : peers) if (p.hello && !p.left && p.pl && !hostRealms.count(p.pl->realm)) streamAnchors(p.pl->realm, away[p.pl->realm]);
    for (auto& [r, v] : away) level.world(r).stream(v, 2);
  }
  if ((tSave += dt) >= 30) { tSave = 0; saveAll(); }
  lanAnn.update(dt, lanInfo());
  enet_host_flush(host);
}

void ServerHost::streamAnchors(int realm, std::vector<StreamAnchor>& out) {
  for (auto& [id, p] : peers) if (p.hello && !p.left && p.pl && p.pl->realm == realm)
    out.push_back({p.pl->pos, std::min(p.R, opts.viewDistance) + 1, true});
}

void ServerHost::receive(Peer& p, u8 ty, json& m) {
  if (ty == HELLO) { if (!p.hello) welcome(p, m); return; }
  if (!p.hello || !p.pl) return;
  switch (ty) {
    case ME: applyMe(p, m); break;
    case SAVE: applySave(p, m); break;
    case BLK: applyBlocks(p, m); break;
    case BE: applyBlockEntity(p, m); break;
    case CHAT: hostChat(p, m.value("text", "")); break;
    case HIT: {
      Entity* e = level.entity(m.value("id", 0u));
      float a = m.value("a", 0.f);
      if (!e || e->removed || e == p.pl || a <= 0 || glm::distance(e->pos, p.pl->pos) > 12) break;
      if (e->isPlayer() && !opts.pvp) break;
      json src = m.value("s", json::object()); src["attacker"] = p.eid; src["attackerName"] = p.name;
      // gameplay code takes it (returns false = handled); otherwise a plain hit
      if (level.events && !level.events->emit("netHit", json::array({p.name, e->id, a, src}))) break;
      e->hp -= a; e->hurtTime = 0.5f;
      if (e->hp <= 0 && !e->isPlayer()) {
        e->removed = true;
        if (level.events) level.events->emit("mobDeath", json::array({json{{"type", e->type}, {"x", e->pos.x}, {"y", e->pos.y}, {"z", e->pos.z}, {"id", e->id}}, p.name}));
      }
      break;
    }
    case ACT: if (level.events) level.events->emit("netAction", json::array({p.name, m.value("n", ""), m.value("a", json())})); break;
    case SPAWN: {
      json e = m.value("e", json::object());
      std::string type = e.value("type", "");
      if (type.empty() || type == "player") break;
      auto ent = createEntity(type);
      ent->load(e); ent->id = 0; ent->realm = p.pl->realm;
      if (glm::distance(ent->pos, p.pl->pos) > 24) break;
      level.spawn(std::move(ent));
      break;
    }
    case PORTAL: {
      vec3 to = v3(m["to"], p.pl->pos);
      int realm = m.value("rl", p.pl->realm);
      p.allowJumpUntil = t + 5;
      portal(*p.pl, realm, to);
      break;
    }
    case NEED: {
      RCKey k{m.value("rl", 0), m.value("d", 0), m.value("cx", 0), m.value("cz", 0)};
      bool have = false; for (auto& q : p.ckQ) if (q.k == k) { q.force = true; have = true; }
      if (!have) p.ckQ.push_back({k, true, 0});
      break;
    }
    case BYE:
      if (m.contains("pl")) applySave(p, {{"pl", m["pl"]}, {"base", p.invSeq}});
      if (p.enet) enet_peer_disconnect_later(p.enet, BYE_NORMAL);
      peerLeft(p);
      break;
    default: break;   // newer clients may send more
  }
}

void ServerHost::applyMe(Peer& p, const json& m) {
  Player& pl = *p.pl;
  vec3 np(m.value("x", pl.pos.x), m.value("y", pl.pos.y), m.value("z", pl.pos.z));
  int realm = m.value("rl", pl.realm);
  bool dead = m.value("dead", 0) != 0;
  double now = t, since = std::max(0.05, now - p.lastMeAt);
  p.lastMeAt = now;
  if (!std::isfinite(np.x) || !std::isfinite(np.y) || !std::isfinite(np.z)) return;
  // light checks: no flying across the map (a respawn, a portal or a /tp the server sent are fine)
  float moved = glm::distance(np, pl.pos);
  auto flat = [](vec3 a, vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); };   // a respawn lands near spawn or the bed
  bool jumpOk = now < p.allowJumpUntil || p.aDead || realm != pl.realm || flat(np, level.info.spawn) < 8 ||
                (pl.spawnPoint.y >= 0 && flat(np, pl.spawnPoint) < 8);
  if (moved > 4 + 90 * since && !jumpOk) {
    send(p.enet, SELF, {{"tp", v3(pl.pos)}, {"rl", pl.realm}});
    p.allowJumpUntil = now + 2;
    serverLog(p.name + " moved too quickly!", "warn");
    return;
  }
  pl.pos = pl.netPos = np;
  pl.realm = realm;
  pl.vel = vec3(m.value("vx", 0.f), m.value("vy", 0.f), m.value("vz", 0.f));
  pl.yaw = pl.netYaw = m.value("r", pl.yaw); pl.pitch = m.value("p", pl.pitch); pl.bodyYaw = m.value("b", pl.yaw);
  int f = m.value("f", 0);
  pl.onGround = f & 1; pl.inWater = f & 2; pl.sneaking = f & 4; pl.sprinting = f & 8; pl.flying = f & 16; pl.swimming = f & 32; pl.crawling = f & 64;
  pl.hp = m.value("hp", pl.hp); pl.dead = dead;
  pl.selected = clampv(m.value("sel", pl.selected), 0, 8);
  pl.riding = m.value("rd", 0u);
  p.aPos = pl.pos; p.aVel = pl.vel; p.aHp = pl.hp; p.aRealm = pl.realm; p.aFly = pl.flying; p.aDead = dead;
  if (!p.ready) {
    p.ready = true; p.motdAt = t + 1.5;
    toAll(PEER, {{"n", p.name}, {"j", 1}}, p.id);
    if (innerFx && !opts.dedicated) innerFx->message(0, p.name + " joined the game", "good");
    serverLog(p.name + " joined the game", "good");
    if (level.events) level.events->emit("join", json::array({p.name}));
  }
}

void ServerHost::applySave(Peer& p, const json& m) {
  p.savedAt = nowSeconds();
  if (!p.pl || !m.contains("pl")) return;
  if (m.value("base", 0) < p.invSeq) return;   // made before our latest change to their bags reached them
  const json& j = m["pl"];
  applyPlayerInv(*p.pl, j);
  Player& pl = *p.pl;
  pl.air = j.value("air", pl.air);
  if (j.contains("stats") && j["stats"].is_object()) pl.stats = j["stats"];
  if (j.contains("spawnPoint")) pl.spawnPoint = v3(j["spawnPoint"], pl.spawnPoint);
  pl.spawnRealm = j.value("spawnRealm", pl.spawnRealm);
  p.aInv = playerInvSig(pl); p.aXp = pl.xp; p.aLevel = pl.level;
}

void ServerHost::hostChat(Peer& p, const std::string& text0) {
  std::string text = text0.substr(0, 200);
  while (!text.empty() && isspace((unsigned char)text.back())) text.pop_back();
  if (text.empty()) return;
  if (text[0] == '/') {
    CommandSource src; src.level = &level; src.player = p.pl; src.name = p.name; src.op = isOp(p.name);
    serverLog(p.name + " used " + text, "");
    std::string out = runCommand(level, src, text);
    if (!out.empty() && p.enet) send(p.enet, MSG, {{"text", out}, {"k", "cmd"}});
    return;
  }
  if (level.events && !level.events->emit("chat", json::array({p.name, text}))) return;
  level.fx->message(0, "<" + p.name + "> " + text, "chat");
}

// What the server's own systems did to a remote player's body since their game last told us about it.
void ServerHost::diffMirrors() {
  for (auto& [id, p] : peers) {
    if (!p.ready || !p.pl || p.left || !p.enet) continue;
    Player& pl = *p.pl;
    json s = json::object();
    if (glm::distance(pl.pos, p.aPos) > 0.75f || pl.realm != p.aRealm) {
      s["tp"] = v3(pl.pos); s["rl"] = pl.realm; p.allowJumpUntil = t + 3;
      p.aPos = pl.pos; p.aRealm = pl.realm; pl.netPos = pl.pos;
    }
    vec3 dv = pl.vel - p.aVel;
    if (std::hypot(dv.x, dv.z) > 1.0f) { s["kb"] = v3(dv); p.aVel = pl.vel; }
    if (pl.hp < p.aHp - 0.01f) s["hurt"] = p.aHp - pl.hp;
    else if (pl.hp > p.aHp + 0.01f) s["hp"] = pl.hp;
    p.aHp = pl.hp;
    std::string sig = playerInvSig(pl);
    if (sig != p.aInv) { s["inv"] = playerInvJson(pl); s["seq"] = ++p.invSeq; p.aInv = sig; p.aXp = pl.xp; p.aLevel = pl.level; }
    if (pl.mode != p.aMode) { s["md"] = pl.mode; p.aMode = pl.mode; }
    if (pl.xp != p.aXp || pl.level != p.aLevel) { s["xp"] = pl.xp; s["lv"] = pl.level; p.aXp = pl.xp; p.aLevel = pl.level; }
    if (pl.flying != p.aFly) { s["fl"] = pl.flying; p.aFly = pl.flying; }
    if (!s.empty()) send(p.enet, SELF, s);
  }
}

void ServerHost::portal(Player& who, int realm, vec3 to) {
  if (level.events) level.events->emit("portalEnter", json::array({who.name, realm, to.x, to.y, to.z}));
  bool everyone = !level.info.rules.is_object() || level.info.rules.value("portalsTakeEveryone", true);
  if (!everyone) return;
  // everyone in the same land goes too, a little apart (the original's followDim)
  int band = dimOfY((int)who.pos.y), i = 0;
  for (auto& e : level.entities()) {
    if (e->removed || !e->isPlayer() || e.get() == &who || e->realm != who.realm || dimOfY((int)e->pos.y) != band) continue;
    Player& o = *static_cast<Player*>(e.get());
    i++;
    o.pos = to + vec3(1.0f + i * 0.6f, 0, 0.5f); o.realm = realm; o.vel = vec3(0); o.fallDist = 0;
    if (!o.ghost) o.netPos = o.pos;
    if (Peer* p = peerByEntity(o.id)) p->allowJumpUntil = t + 5;
  }
}

} // namespace dq::net
