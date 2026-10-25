// net/server_sync.cpp — what the server keeps each player's game in step with (net.js hostTick, flushBlocks,
// updateRange/flushChunks, sendEntities, flushEffects, env; netsync.js netSnap), plus the server's chat commands.
#include "net/net_internal.h"
#include "sim/commands.h"
#include "world/region.h"
#include <enet/enet.h>

namespace dq::net {

// ---- players' bags, in short ----
static std::string stacksSig(const std::vector<ItemStack>& v) {
  std::string s;
  for (auto& it : v) {
    if (it.empty()) { s += "-,"; continue; }
    s += it.id; s += '*'; s += std::to_string(it.n);
    if (it.d) { s += 'd'; s += std::to_string(it.d); }
    if (!it.tag.is_null()) { s += '+'; s += std::to_string(std::hash<std::string>()(it.tag.dump())); }
    s += ',';
  }
  return s;
}
std::string playerInvSig(const Player& p) {
  return stacksSig(p.inv) + "|" + stacksSig(p.armor) + "|" + stacksSig(p.offhand) + "|" + std::to_string(p.xp) + "|" + std::to_string(p.level);
}
static json stacksJson(const std::vector<ItemStack>& v) { json a = json::array(); for (auto& s : v) a.push_back(s.toJson()); return a; }
static void stacksFrom(const json& a, std::vector<ItemStack>& v) { if (!a.is_array()) return; for (size_t i = 0; i < v.size(); i++) v[i] = i < a.size() ? ItemStack::fromJson(a[i]) : ItemStack(); }
json playerInvJson(const Player& p) {
  return {{"inv", stacksJson(p.inv)}, {"ar", stacksJson(p.armor)}, {"of", stacksJson(p.offhand)}, {"sel", p.selected}, {"xp", p.xp}, {"lv", p.level}};
}
void applyPlayerInv(Player& p, const json& j) {
  if (j.contains("inv")) stacksFrom(j["inv"], p.inv);
  if (j.contains("ar")) stacksFrom(j["ar"], p.armor);
  if (j.contains("of")) stacksFrom(j["of"], p.offhand);
  p.selected = clampv(j.value("sel", p.selected), 0, 8);
  p.xp = j.value("xp", p.xp); p.level = j.value("lv", p.level);
}

// ---- what a player's game sees of an entity (netsync.js netSnap) ----
json entitySnapshot(const Entity& e) {
  json s = {{"i", e.id}, {"t", e.type}, {"x", r3(e.pos.x)}, {"y", r3(e.pos.y)}, {"z", r3(e.pos.z)}, {"r", r3(e.yaw)}, {"p", r3(e.pitch)},
            {"b", r3(e.bodyYaw)}, {"hp", std::round(e.hp * 10) / 10}, {"mh", e.maxHp}, {"w", e.w}, {"h", e.h}};
  int f = (e.onGround ? 1 : 0) | (e.inWater ? 2 : 0) | (e.inLava ? 4 : 0) | (e.hurtTime > 0 ? 8 : 0) | (e.noGravity ? 16 : 0);
  if (f) s["f"] = f;
  if (e.riding) s["rd"] = e.riding;
  if (auto* p = dynamic_cast<const Player*>(&e)) {
    s["n"] = p->name; s["sk"] = p->skin; s["md"] = p->mode;
    const ItemStack& h = p->inv[clampv(p->selected, 0, INV_MAIN - 1)];
    if (!h.empty()) s["hd"] = h.id;
    json ar = json::array(); bool anyAr = false;
    for (auto& a : p->armor) { ar.push_back(a.empty() ? json() : json(a.id)); anyAr |= !a.empty(); }
    if (anyAr) s["ar"] = ar;
    if (!p->offhand.empty() && !p->offhand[0].empty()) s["of"] = p->offhand[0].id;
    int pf = (p->sneaking ? 1 : 0) | (p->sprinting ? 2 : 0) | (p->flying ? 4 : 0) | (p->swimming ? 8 : 0) | (p->crawling ? 16 : 0) | (p->dead ? 32 : 0);
    if (pf) s["pf"] = pf;
  }
  return s;
}

// ---- changes, wherever they come from ----
void NetWatch::onBlockChanged(World& w, int x, int y, int z, BlockId, BlockId newId) {
  if (w.changeFlags & SetFlags::NoModify) return;   // world generation: every game makes that itself
  host.blockQ.push_back({w.realm, x, y, z, newId, w.getMeta(x, y, z)});
}
void NetWatch::onBlockEntityChanged(World& w, int x, int y, int z) {
  json* e = w.blockEntity(x, y, z);
  host.beQ[{w.realm, x, y, z}] = e ? *e : json();
}
void ServerHost::ensureWatchers() {
  if (!watch) watch = std::make_shared<NetWatch>(*this);
  for (auto& [r, w] : level.allRealms()) if (!watched.count(r)) { w->addSystem(watch); watched.insert(r); }
}

void ServerHost::applyBlocks(Peer& p, const json& m) {
  int realm = m.value("rl", p.pl->realm);
  if (!m.contains("l") || !m["l"].is_array()) return;
  World& w = level.world(realm);
  json fix = json::array();
  const auto& R = reg();
  for (auto& b : m["l"]) {
    if (!b.is_array() || b.size() < 5) continue;
    int x = b[0], y = b[1], z = b[2]; u32 id = b[3]; u8 meta = (u8)b[4].get<int>();
    if (id >= R.blocks.size() || !inWorldY(y)) continue;
    bool near = realm == p.pl->realm && glm::distance(vec3(x + 0.5f, y + 0.5f, z + 0.5f), p.pl->pos) <= 24;
    if (!w.isLoaded(x, y, z)) { if (near) pendingBlocks.push_back({realm, x, y, z, (BlockId)id, meta, t + 10, p.id}); continue; }
    BlockId old = w.getBlock(x, y, z);
    u8 oldMeta = w.getMeta(x, y, z);
    if (old == id && oldMeta == meta) continue;
    if (!near) { fix.push_back({x, y, z, old, oldMeta}); continue; }
    bool ok = true;
    if (level.events) {
      if (id == AIR && old != AIR) ok = level.events->emit("blockBreak", json::array({p.name, x, y, z, R.block(old).key}));
      else if (id != AIR && (old == AIR || R.tReplaceable[old] || R.tLiquid[old])) ok = level.events->emit("blockPlace", json::array({p.name, x, y, z, R.block((BlockId)id).key}));
    }
    if (!ok) { fix.push_back({x, y, z, old, oldMeta}); continue; }
    w.setBlock(x, y, z, (BlockId)id, meta, SetFlags::FromNet | SetFlags::Urgent);
    // everyone else near hears it (their own game already played it for them)
    bool broke = id == AIR;
    std::string mat = R.block(broke ? old : (BlockId)id).sound;
    json item = json::array({mat, broke ? "dig" : "place", x + 0.5f, y + 0.5f, z + 0.5f});
    for (auto& [qid, q] : peers) if (qid != p.id && q.ready && q.pl && glm::distance(q.pl->pos, vec3(x, y, z)) < 32) sendFxTo(q, "m", item);
    if (!opts.dedicated && innerFx) innerFx->material(mat, broke ? "dig" : "place", vec3(x + 0.5f, y + 0.5f, z + 0.5f));
  }
  if (!fix.empty() && p.enet) send(p.enet, BLK, {{"rl", realm}, {"l", fix}});
}

void ServerHost::applyBlockEntity(Peer& p, const json& m) {
  int realm = m.value("rl", p.pl->realm), x = m.value("x", 0), y = m.value("y", 0), z = m.value("z", 0);
  World* w = level.worldIfOpen(realm);
  if (!w || !w->isLoaded(x, y, z) || glm::distance(vec3(x, y, z), p.pl->pos) > 16) return;
  if (int q = m.value("q", 0)) beAck[{realm, x, y, z}][p.id] = q;   // taken in
  w->setBlockEntity(x, y, z, m.contains("e") ? m["e"] : json());
}

void ServerHost::flushBlocks() {
  if (blockQ.empty()) return;
  std::map<int, json> byRealm;
  for (auto& b : blockQ) byRealm[b.realm].push_back(json::array({b.x, b.y, b.z, b.id, b.meta}));
  blockQ.clear();
  for (auto& [r, l] : byRealm) toAll(BLK, {{"rl", r}, {"l", l}});
}

// ---- chunks ----
void ServerHost::updateRange(Peer& p, bool first) {
  Player& pl = *p.pl;
  int R = std::min(p.R, opts.viewDistance) + 1;
  int y = clampv(ifloor(pl.pos.y), 0, WORLD_H - 1), d = dimOfY(y), ly = y - dimBase(d);
  int pcx = ifloor(pl.pos.x) >> 4, pcz = ifloor(pl.pos.z) >> 4;
  std::vector<std::pair<int, int>> bands = {{d, R}};
  if (ly > DIM_H - 64 && d + 1 < DIM_COUNT) bands.push_back({d + 1, std::max(2, R / 2)});
  if (ly < 48 && d > 0) bands.push_back({d - 1, std::max(2, R / 2)});
  std::unordered_set<RCKey, RCKeyHash> now;
  for (auto& [bd, r] : bands)
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      int d2 = dx * dx + dz * dz;
      if (d2 > (r + 0.5f) * (r + 0.5f)) continue;
      RCKey k{pl.realm, bd, pcx + dx, pcz + dz};
      now.insert(k);
      if (!p.inRange.count(k)) p.ckQ.push_back({k, false, d2 + (bd == d ? 0 : 64)});
    }
  p.inRange = std::move(now);
  if (first) std::sort(p.ckQ.begin(), p.ckQ.end(), [](const CkQ& a, const CkQ& b) { return a.d < b.d; });
}

void ServerHost::flushChunks(Peer& p, int maxRecords) {
  bool bulk = maxRecords > 1000;
  p.chunkBytes = std::max(0.0, p.chunkBytes - SEND_CHUNKS * 1.5e6);   // ~1.5 MB/s of records per player at most
  std::vector<CkQ> keep;
  int n = 0;
  for (auto& q : p.ckQ) {
    if (!q.force && !p.inRange.count(q.k)) continue;   // gone out of range: their game makes or forgets it
    if (n >= maxRecords || (!bulk && p.chunkBytes > 1.5e6)) { keep.push_back(q); continue; }
    World* w = level.worldIfOpen(q.k.realm);
    if (!w) { if (q.force) keep.push_back(q); continue; }
    json head = {{"rl", q.k.realm}, {"d", q.k.dim}, {"cx", q.k.cx}, {"cz", q.k.cz}};
    Chunk* c = w->chunk(q.k.dim, q.k.cx, q.k.cz);
    std::vector<u8> rec;
    if (c && (c->modified || q.force)) rec = encodeChunk(*c);
    else if (!c && w->regionStore()) w->regionStore()->load({q.k.dim, q.k.cx, q.k.cz}, rec);
    if (!rec.empty()) {
      p.chunkBytes += rec.size();
      head["r"] = json::binary(std::move(rec));
      send(p.enet, CK, head);
      n++;
      continue;
    }
    if (!c) { if (q.force) keep.push_back(q); continue; }   // asked for, not loaded here yet: once it is
    // untouched: their game makes it from the seed too; a checksum says whether it came out the same
    if (chunkSettled(*w, *c)) { head["h"] = chunkHash(*c); send(p.enet, CS, head); }
    else keep.push_back(q);
  }
  p.ckQ = std::move(keep);
}

// ---- entities ----
void ServerHost::sendEntities(Peer& p, const std::unordered_map<u32, Snap>& snaps) {
  Player& pl = *p.pl;
  json list = json::array(), gone = json::array();
  std::unordered_set<u32> seen;
  for (auto& [id, sn] : snaps) {
    if (id == p.eid) continue;
    Entity* e = level.entity(id);
    if (!e || e->realm != pl.realm) continue;
    vec3 d = e->pos - pl.pos;
    if (std::abs(d.x) > ENT_RANGE || std::abs(d.z) > ENT_RANGE || std::abs(d.y) > 128) continue;
    seen.insert(id);
    SentEnt& was = p.sent[id];
    bool fresh = was.at == 0;
    if (!fresh && was.hash == sn.hash && was.dataHash == sn.dataHash && t - was.at < 2) continue;
    json s = sn.s;
    if (fresh || was.dataHash != sn.dataHash) s["d"] = e->data;
    was.hash = sn.hash; was.dataHash = sn.dataHash; was.at = t;
    list.push_back(std::move(s));
  }
  for (auto it = p.sent.begin(); it != p.sent.end();) {
    if (!seen.count(it->first)) { gone.push_back(it->first); it = p.sent.erase(it); } else ++it;
  }
  if (!list.empty() || !gone.empty()) send(p.enet, ES, {{"rl", pl.realm}, {"l", list}, {"g", gone}}, 1);
}

// ---- time & weather ----
void ServerHost::sendEnv() {
  json w = level.info.data.is_object() && level.info.data.contains("weather") ? level.info.data["weather"] : json();
  toAll(ENV, {{"time", level.info.time}, {"day", level.info.day}, {"w", w}, {"rules", level.info.rules}}, 0, 1);
}

// ---- sounds, puffs, messages ----
void ServerHost::queueFx(char kind, json item) {
  bool any = false; for (auto& [id, p] : peers) if (p.ready) { any = true; break; }
  if (!any) return;
  auto& q = kind == 's' ? fxS : kind == 'm' ? fxM : kind == 'v' ? fxV : fxP;
  if (q.size() < 60) q.push_back(std::move(item));
}
void ServerHost::sendFxTo(Peer& p, const char* key, json item) { if (p.fxOut.size() < 120) p.fxOut.push_back({key, std::move(item)}); }
void ServerHost::flushEffects() {
  auto near = [](const json& it, int at, vec3 pos, float r) { return glm::distance(vec3(it[at].get<float>(), it[at + 1].get<float>(), it[at + 2].get<float>()), pos) < r; };
  for (auto& [id, p] : peers) {
    if (!p.ready || !p.pl || !p.enet) { p.fxOut.clear(); continue; }
    json m = json::object();
    vec3 at = p.pl->pos;
    for (auto& s : fxS) if (near(s, 1, at, s[4].get<float>())) m["s"].push_back(s);
    for (auto& s : fxM) if (near(s, 2, at, 32)) m["m"].push_back(s);
    for (auto& s : fxV) if (near(s, 2, at, 40)) m["v"].push_back(s);
    for (auto& s : fxP) if (near(s, 1, at, 48)) m["p"].push_back(s);
    for (auto& [k, it] : p.fxOut) m[k].push_back(it);
    p.fxOut.clear();
    if (!m.empty()) send(p.enet, FX, m, 1, false);
  }
  fxS.clear(); fxM.clear(); fxV.clear(); fxP.clear();
}

void NetFx::sound(const std::string& name, vec3 pos, float range, float volume, float pitch) {
  if (inner) inner->sound(name, pos, range, volume, pitch);
  host.queueFx('s', json::array({name, r3(pos.x), r3(pos.y), r3(pos.z), range, volume, pitch}));
}
void NetFx::soundUi(u32 playerId, const std::string& name) {
  if (auto* p = host.peerByEntity(playerId)) host.sendFxTo(*p, "u", name);
  else if (inner) inner->soundUi(playerId, name);
}
void NetFx::material(const std::string& mat, const std::string& kind, vec3 pos) {
  if (inner) inner->material(mat, kind, pos);
  host.queueFx('m', json::array({mat, kind, r3(pos.x), r3(pos.y), r3(pos.z)}));
}
void NetFx::voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) {
  if (inner) inner->voice(creature, kind, pos, pitch);
  host.queueFx('v', json::array({creature, kind, r3(pos.x), r3(pos.y), r3(pos.z), pitch}));
}
void NetFx::particles(const std::string& kind, vec3 pos, int count, const json& extra) {
  if (inner) inner->particles(kind, pos, count, extra);
  host.queueFx('p', json::array({kind, r3(pos.x), r3(pos.y), r3(pos.z), count, extra}));
}
void NetFx::message(u32 playerId, const std::string& text, const std::string& kind) {
  if (playerId == 0) {
    if (inner) inner->message(0, text, kind);
    host.toAll(MSG, {{"text", text}, {"k", kind}});
    if (host.opts.dedicated) host.serverLog(text, kind);
    return;
  }
  if (auto* p = host.peerByEntity(playerId)) { if (p->enet) send(p->enet, MSG, {{"text", text}, {"k", kind}}); }
  else if (inner) inner->message(playerId, text, kind);
}
void NetFx::shake(u32 playerId, float strength) {
  if (auto* p = host.peerByEntity(playerId)) host.sendFxTo(*p, "k", strength);
  else if (inner) inner->shake(playerId, strength);
}

// ---- the server's chat commands ----
static ServerHost* hostOf(CommandSource& s) { return s.level && s.level->net ? dynamic_cast<ServerHost*>(s.level->net.get()) : nullptr; }
static std::string joinFrom(const std::vector<std::string>& a, size_t from) { std::string o; for (size_t i = from; i < a.size(); i++) o += (i > from ? " " : "") + a[i]; return o; }

void registerNetCommands() {
  registerChatCommand("list", "Who's playing", [](CommandSource& s, const std::vector<std::string>&) -> std::string {
    std::vector<std::string> names = s.level && s.level->net ? s.level->net->playerNames() : std::vector<std::string>{};
    if (names.empty() && s.player) names.push_back(s.player->name);
    std::string o; for (auto& n : names) o += (o.empty() ? "" : ", ") + n;
    return "Playing: " + (o.empty() ? std::string("nobody") : o);
  }, false);
  registerChatCommand("kick", "Remove a player from the world", [](CommandSource& s, const std::vector<std::string>& a) -> std::string {
    ServerHost* h = hostOf(s); if (!h) return "Nobody else is here";
    if (a.empty()) return "Usage: /kick <name> [reason]";
    return h->kick(a[0], joinFrom(a, 1)) ? "Kicked " + a[0] : "Nobody called " + a[0];
  }, true, "<name> [reason]");
  registerChatCommand("ban", "Keep a player out of this world", [](CommandSource& s, const std::vector<std::string>& a) -> std::string {
    ServerHost* h = hostOf(s); if (!h) return "Nobody else is here";
    if (a.empty()) return "Usage: /ban <name> [reason]";
    h->ban(a[0], joinFrom(a, 1)); return "Banned " + a[0];
  }, true, "<name> [reason]");
  registerChatCommand("banip", "Keep an address out of this world", [](CommandSource& s, const std::vector<std::string>& a) -> std::string {
    ServerHost* h = hostOf(s); if (!h) return "Nobody else is here";
    if (a.empty()) return "Usage: /banip <address>";
    h->banIp(a[0]); return "Banned " + a[0];
  }, true, "<address>");
  registerChatCommand("pardon", "Let a banned player or address back in", [](CommandSource& s, const std::vector<std::string>& a) -> std::string {
    ServerHost* h = hostOf(s); if (!h) return "Nobody else is here";
    if (a.empty()) return "Usage: /pardon <name|address>";
    return h->pardon(a[0]) ? a[0] + " may come back" : a[0] + " wasn't banned";
  }, true, "<name|address>");
  registerChatCommand("op", "Let a player use commands", [](CommandSource& s, const std::vector<std::string>& a) -> std::string {
    ServerHost* h = hostOf(s); if (!h) return "Nobody else is here";
    if (a.empty()) return "Usage: /op <name>";
    h->setOp(a[0], true); return a[0] + " is now an operator";
  }, true, "<name>");
  registerChatCommand("deop", "Stop a player using commands", [](CommandSource& s, const std::vector<std::string>& a) -> std::string {
    ServerHost* h = hostOf(s); if (!h) return "Nobody else is here";
    if (a.empty()) return "Usage: /deop <name>";
    h->setOp(a[0], false); return a[0] + " is no longer an operator";
  }, true, "<name>");
  auto tell = [](CommandSource& s, const std::vector<std::string>& a) -> std::string {
    if (a.size() < 2) return "Usage: /msg <name> <message>";
    std::string text = joinFrom(a, 1);
    Player* to = nullptr;
    if (s.level) for (auto& e : s.level->entities()) if (!e->removed && e->isPlayer() && toLower(static_cast<Player*>(e.get())->name) == toLower(a[0])) to = static_cast<Player*>(e.get());
    if (!to) return "Nobody called " + a[0];
    s.level->fx->message(to->id, "[" + s.name + " → you] " + text, "chat");
    return "[you → " + to->name + "] " + text;
  };
  registerChatCommand("msg", "Say something to one player", tell, false, "<name> <message>");
  registerChatCommand("tell", "Say something to one player", tell, false, "<name> <message>");
}

} // namespace dq::net
