// sim/machines.cpp — game/mech.js (windmills, waterwheels, axles, millstone, sawmill) and game/machines.js
// (conveyor belts, the crusher, the harvester, hoppers feeding machines).
#include "sim/machines.h"
#include "sim/redstone.h"
#include "sim/vehicles.h"
#include "sim/player.h"

namespace dq {
using namespace rs;

namespace {
constexpr float MILL_TIME = 3.5f, CRUSH_TIME = 2.0f, BELT_SPEED = 1.7f;
constexpr int DIR4[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};   // 0 north (-z), 1 east, 2 south, 3 west
const int AXES[3][3] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}};
bool isUser(const std::string& k) { return k == "millstone" || k == "sawmill" || k == "crusher" || k == "harvester"; }
float loadOf(const std::string& k) { return k == "crusher" ? 2.f : 1.f; }
const json& tables() {
  static json t = [] { auto j = readJson(dataDir() / "machines.json"); return j ? *j : json::object(); }();
  return t;
}
} // namespace

const json* machineTable(const std::string& k) {
  const json& t = tables();
  const char* n = k == "millstone" ? "milling" : k == "sawmill" ? "sawing" : k == "crusher" ? "crushing" : nullptr;
  if (!n || !t.contains(n)) return nullptr;
  return &t[n];
}
float compostChance(const std::string& key) {
  const json& t = tables();
  if (!t.contains("compost")) return 0;
  auto it = t["compost"].find(key);
  return it == t["compost"].end() ? 0.f : it->get<float>();
}

// ---------------------------------------------------------------------------------------- nodes
void Mechanics::addNode(int x, int y, int z, BlockId id, int meta) {
  MechNode n; n.p = {x, y, z}; n.id = id; n.kind = reg().block(id).str("mech"); n.axis = std::min(2, meta & 3);
  u64 k = posKey(x, y, z);
  nodeMap[k] = n;
  if (Chunk* c = w.chunkAtBlock(x, y, z)) perChunk[c->key()].push_back(k);
  dirty = true;
}
void Mechanics::onChunkLoaded(World&, Chunk& c) {
  const Tables& t = T();
  for (int sy = 0; sy < SECS; sy++) {
    const Section* s = c.section(sy);
    if (!s) continue;
    for (int i = 0; i < 4096; i++) if (t.mech[s->blocks[i]]) addNode(c.worldX() + (i & 15), c.baseY() + sy * 16 + (i >> 8), c.worldZ() + ((i >> 4) & 15), s->blocks[i], s->meta[i]);
  }
}
void Mechanics::onChunkUnloading(World&, Chunk& c) {
  auto it = perChunk.find(c.key());
  if (it == perChunk.end()) return;
  for (u64 k : it->second) nodeMap.erase(k);
  perChunk.erase(it);
  dirty = true;
}
void Mechanics::onBlockChanged(World&, int x, int y, int z, BlockId oldId, BlockId newId) {
  const Tables& t = T();
  if (!t.mech[oldId] && !t.mech[newId]) return;
  u64 k = posKey(x, y, z);
  if (t.mech[newId]) {
    auto it = nodeMap.find(k);
    if (it != nodeMap.end()) { it->second.id = newId; it->second.kind = reg().block(newId).str("mech"); it->second.axis = std::min(2, w.getMeta(x, y, z) & 3); dirty = true; }
    else addNode(x, y, z, newId, w.getMeta(x, y, z));
  } else { nodeMap.erase(k); dirty = true; }
}

// Group the connected nodes (keeping each group's turning).
void Mechanics::rebuild() {
  std::vector<MechComp> old; old.swap(comps);
  std::unordered_map<u64, int> oldComp;
  for (auto& [k, n] : nodeMap) { if (n.comp >= 0) oldComp[k] = n.comp; n.comp = -1; }
  auto conn = [](const MechNode& n, int dx, int dy, int dz) {
    if (isUser(n.kind) || n.kind == "belt") return true;
    const int* a = AXES[n.axis];
    return std::abs(a[0]) == std::abs(dx) && std::abs(a[1]) == std::abs(dy) && std::abs(a[2]) == std::abs(dz);
  };
  static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (auto& [k0, n0] : nodeMap) {
    if (n0.comp >= 0) continue;
    int ci = (int)comps.size();
    comps.emplace_back();
    std::vector<u64> q{k0}; n0.comp = ci;
    while (!q.empty()) {
      u64 kc = q.back(); q.pop_back();
      MechNode& c = nodeMap[kc];
      MechComp& comp = comps[ci];
      comp.nodes.push_back(kc);
      if (c.kind == "windmill" || c.kind == "waterwheel") comp.sources.push_back(kc);
      if (isUser(c.kind)) comp.machines.push_back(kc);
      if (c.kind == "belt") comp.belts.push_back(kc);
      for (auto& d : D) {
        auto it = nodeMap.find(posKey(c.p.x + d[0], c.p.y + d[1], c.p.z + d[2]));
        if (it == nodeMap.end() || it->second.comp >= 0) continue;
        MechNode& o = it->second;
        // two machines never pass power to each other directly
        if (isUser(c.kind) && isUser(o.kind)) continue;
        if (conn(c, d[0], d[1], d[2]) && conn(o, -d[0], -d[1], -d[2])) { o.comp = ci; q.push_back(it->first); }
      }
    }
    // (engine change) the group keeps its speed and angle across rebuilds; the original started them from 0
    auto oc = oldComp.find(k0);
    if (oc != oldComp.end() && oc->second < (int)old.size()) { comps[ci].speed = old[oc->second].speed; comps[ci].angle = old[oc->second].angle; }
  }
  dirty = false;
}
// How hard a windmill or waterwheel is pushing right now (0..1).
Mechanics::Source Mechanics::sourcePower(const MechNode& n) {
  const Registry& R = reg();
  const int* a = AXES[n.axis];
  // the two directions across the wheel's face
  ivec3 u = a[0] ? ivec3(0, 1, 0) : a[2] ? ivec3(0, 1, 0) : ivec3(1, 0, 0), v = a[0] ? ivec3(0, 0, 1) : a[2] ? ivec3(1, 0, 0) : ivec3(0, 0, 1);
  if (n.kind == "windmill") {
    bool blocked = false;
    for (int i = -4; i <= 4 && !blocked; i++) for (int j = -4; j <= 4; j++) {
      if (!i && !j) continue;
      if (i * i + j * j > 17) continue;
      ivec3 b = n.p + u * i + v * j;
      BlockId id = w.getBlock(b.x, b.y, b.z);
      if (R.tSolid[id] && !R.block(id).has("mech")) { blocked = true; break; }
    }
    float wind = windOf(lv).strength;
    // (the original's y 52..86 in a 128-tall world: 4..38 blocks above its sea level 48)
    float height = clampv(((n.p.y & 255) - (SEA_LEVEL + 4)) / 34.f, 0.15f, 1.f);
    return {blocked || w.realm == REALM_POCKET ? 0.f : wind * height * 1.6f, blocked};
  }
  // waterwheel: flowing (not still) water through the wheel
  int flow = 0;
  for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) {
    ivec3 b = n.p + u * i + v * j;
    if (R.tLiquid[w.getBlock(b.x, b.y, b.z)] == 1 && (w.getMeta(b.x, b.y, b.z) & 15) != 0) flow++;
  }
  return {std::min(1.2f, flow * 0.4f), false};
}
float Mechanics::speedAt(int x, int y, int z) const {
  auto it = nodeMap.find(posKey(x, y, z));
  return it != nodeMap.end() && it->second.comp >= 0 && it->second.comp < (int)comps.size() ? comps[it->second.comp].speed : 0.f;
}
void Mechanics::update(double dtd) {
  float dt = (float)dtd;
  t += dt;
  if (dirty || t > 2) { if (t > 2) t = 0; rebuild(); }
  bool remote = lv.isRemote();
  for (auto& c : comps) {
    float power = 0;
    for (u64 k : c.sources) { MechNode& s = nodeMap[k]; Source r = sourcePower(s); s.power = r.power; s.blocked = r.blocked; power += r.power; }
    float load = 0;
    for (u64 k : c.machines) load += loadOf(nodeMap[k].kind);
    load += c.belts.size() * 0.05f;
    float want = !c.sources.empty() ? std::min(1.5f, power / std::max(1.f, load * 0.6f)) : 0.f;
    c.speed += (want - c.speed) * std::min(1.f, dt * 0.8f);
    c.angle = std::fmod(c.angle + c.speed * dt * 2.4f, PI * 2000);
    if (remote) continue;
    for (u64 k : c.machines) work(nodeMap[k], dt * c.speed);
    if (c.speed > 0.15f && !c.nodes.empty()) {
      creakT -= dt;
      if (creakT < 0) { creakT = 2.5f + rnd() * 3; lv.fx->sound("creak", vec3(nodeMap[c.nodes[0]].p) + vec3(0.5f), 20); }
    }
  }
  beltTick(dtd);
}
json* Mechanics::machineEntity(const MechNode& m) {
  json* e = w.blockEntity(m.p.x, m.p.y, m.p.z, true);
  if (!e) return nullptr;
  if (!e->contains("type")) (*e)["type"] = isUser(m.kind) && m.kind != "harvester" ? "machine" : "mech";
  if (!(*e)["items"].is_array()) (*e)["items"] = json::array({nullptr, nullptr});
  while ((*e)["items"].size() < 2) (*e)["items"].push_back(nullptr);
  return e;
}
void Mechanics::work(MechNode& m, float amount) {
  if (m.kind == "crusher") { crusherWork(m, amount); return; }
  if (m.kind == "harvester") { harvesterWork(m, amount); return; }
  const json* table = machineTable(m.kind);
  json* e = table ? machineEntity(m) : nullptr;
  if (!e) return;
  json& items = (*e)["items"];
  ItemStack inp = ItemStack::fromJson(items[0]), out = ItemStack::fromJson(items[1]);
  auto r = inp.empty() ? table->end() : table->find(inp.id);
  if (r == table->end()) { (*e)["progress"] = 0; return; }
  std::string ok = (*r)[0]; int rn = (*r)[1];
  if (!out.empty() && (out.id != ok || out.n + rn > ItemStack(ok, 1).maxStack())) { (*e)["progress"] = 0; return; }
  if (amount <= 0) return;
  float prog = e->value("progress", 0.f) + amount / MILL_TIME;
  if (rnd() < amount * 3) lv.fx->particles("burst", vec3(m.p) + vec3(0.5f, 1.05f, 0.5f), 1, json{{"item", inp.id}, {"speed", 0.4}});
  if (prog >= 1) {
    prog = 0;
    inp.n--; items[0] = inp.empty() ? json(nullptr) : inp.toJson();
    if (!out.empty()) out.n += rn; else out = ItemStack(ok, rn);
    items[1] = out.toJson();
    lv.fx->sound(m.kind == "millstone" ? "grind" : "saw", vec3(m.p) + vec3(0.5f), 20);
    (*e)["progress"] = prog;
    w.blockEntityChanged(m.p.x, m.p.y, m.p.z);
    return;
  }
  (*e)["progress"] = prog;
}
void Mechanics::crusherWork(MechNode& m, float amount) {
  const json* table = machineTable("crusher");
  json* e = table ? machineEntity(m) : nullptr;
  if (!e) return;
  json& items = (*e)["items"];
  ItemStack inp = ItemStack::fromJson(items[0]), out = ItemStack::fromJson(items[1]);
  auto r = inp.empty() ? table->end() : table->find(inp.id);
  if (r == table->end()) { (*e)["progress"] = 0; return; }
  std::string ok = (*r)[0]; int rn = (*r)[1];
  if (!out.empty() && (out.id != ok || out.n + rn + 1 > ItemStack(ok, 1).maxStack())) { (*e)["progress"] = 0; return; }
  if (amount <= 0) return;
  float prog = e->value("progress", 0.f) + amount / CRUSH_TIME;
  if (rnd() < amount * 4) lv.fx->particles("burst", vec3(m.p) + vec3(0.5f, 1.05f, 0.5f), 1, json{{"item", inp.id}, {"speed", 0.5}});
  if (prog < 1) { (*e)["progress"] = prog; return; }
  (*e)["progress"] = 0;
  // whoever built it: their Smithing makes a crusher better (the skills port reads "skillGain")
  bool ore = endsWith(inp.id, "_ore") || startsWith(inp.id, "raw_");
  int slv = e->value("smithLv", 1);
  int n = rn + (ore && rnd() < slv / 99.f * 0.5f ? 1 : 0);
  inp.n--; items[0] = inp.empty() ? json(nullptr) : inp.toJson();
  if (!out.empty()) out.n += n; else out = ItemStack(ok, n);
  items[1] = out.toJson();
  std::string owner = e->value("owner", "");
  if (ore && !owner.empty() && lv.events) lv.events->emit("skillGain", json::array({owner, "smithing", 8}));
  lv.fx->sound("grind", vec3(m.p) + vec3(0.5f), 20);
  w.blockEntityChanged(m.p.x, m.p.y, m.p.z);
}
void Mechanics::harvesterWork(MechNode& m, float amount) {
  if (amount <= 0) return;
  json* e = w.blockEntity(m.p.x, m.p.y, m.p.z, true);
  if (!e) return;
  float tt = e->value("t", 0.f) + amount;
  if (tt < 4) { (*e)["t"] = tt; return; }
  (*e)["t"] = 0;
  const Registry& R = reg();
  Redstone* rsys = redstoneOf(lv, w.realm);
  std::vector<Container> store;
  static const int SIDES[6][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
  if (rsys) for (auto& d : SIDES) if (Container c = rsys->inv(m.p.x + d[0], m.p.y + d[1], m.p.z + d[2])) store.push_back(c);
  auto put = [&](ItemStack& s) {
    for (auto& c : store) {
      while (s.n > 0 && rsys->insertOne(c, 1, s)) s.n--;
      rsys->changedInv(c);
      if (s.n <= 0) return true;
    }
    return false;
  };
  static std::unordered_map<BlockId, std::string> SEED_OF;
  auto seedFor = [&](const BlockDef& b) -> std::string {
    auto it = SEED_OF.find(b.id);
    if (it != SEED_OF.end()) return it->second;
    std::string s;
    if (b.props.contains("crop") && b.props["crop"].contains("seed") && R.item(b.props["crop"]["seed"].get<std::string>())) s = b.props["crop"]["seed"];
    if (s.empty()) for (auto& d : R.items) if (d.str("plants") == b.key || (d.block == b.key && !d.has("food"))) { s = d.key; break; }
    if (s.empty()) for (auto& d : R.items) if (d.block == b.key) { s = d.key; break; }
    return SEED_OF[b.id] = s;
  };
  int done = 0;
  std::string owner = e->value("owner", "");
  for (int dy = -1; dy <= 1 && done < 8; dy++) for (int dz = -4; dz <= 4 && done < 8; dz++) for (int dx = -4; dx <= 4 && done < 8; dx++) {
    int x = m.p.x + dx, y = m.p.y + dy, z = m.p.z + dz;
    BlockId id = w.getBlock(x, y, z);
    const BlockDef& b = R.block(id);
    bool crop = b.props.contains("crop") && b.props["crop"].is_object();
    int meta = w.getMeta(x, y, z);
    bool ripe = crop && (meta & 15) >= b.props["crop"].value("stages", 8) - 1;
    std::string harvest = b.str("harvest");
    if (!ripe && harvest.empty()) continue;
    done++;
    std::vector<ItemStack> got;
    if (!harvest.empty()) { got.push_back(ItemStack(harvest, 2 + (rnd() < 0.5f ? 1 : 0))); w.setBlock(x, y, z, T().berryEmpty, 0); }
    else {
      // (game.js dropBlockItems) what a ripe crop gives
      const json& cr = b.props["crop"];
      std::string drop = cr.value("drop", ""), seed = cr.value("seed", "");
      if (b.key == "wheat_crop") { got.push_back(ItemStack("wheat_seeds", 1 + (int)(rnd() * 3))); got.push_back(ItemStack("wheat", 1)); }
      else if (b.key == "beetroot_crop") { got.push_back(ItemStack("beetroot_seeds", 1 + (int)(rnd() * 3))); got.push_back(ItemStack("beetroot", 1)); }
      else if (!seed.empty()) { got.push_back(ItemStack(seed, 1 + (int)(rnd() * 2))); got.push_back(ItemStack(drop, 1 + (int)(rnd() * 3))); }
      else got.push_back(ItemStack(drop, 2 + (int)(rnd() * 3)));
      w.setBlock(x, y, z, AIR, 0);
      // the seed goes back in
      std::string sd = seedFor(b);
      for (auto& s : got) if (!sd.empty() && s.id == sd && s.n > 0) {
        if (R.tSolid[w.getBlock(x, y - 1, z)]) { s.n--; w.setBlock(x, y, z, id, 0); }
        break;
      }
      if (!owner.empty() && lv.events) lv.events->emit("skillGain", json::array({owner, "farming", 3}));
    }
    for (auto& s : got) if (s.n > 0 && !put(s)) dropStack(lv, w.realm, s, vec3(m.p) + vec3(0.5f, 1.2f, 0.5f), vec3((rnd() - 0.5f) * 2, 2, (rnd() - 0.5f) * 2));
    lv.fx->particles("burst", vec3(x + 0.5f, y + 0.4f, z + 0.5f), 4, json{{"block", b.key}, {"speed", 0.4}});
  }
  if (done) lv.fx->material("grass", "dig", vec3(m.p) + vec3(0.5f));
}

// ---------------------------------------------------------------------------------------- belts
void Mechanics::beltTick(double dtd) {
  float dt = (float)dtd;
  bool remote = lv.isRemote();
  struct At { int dir; float s; };
  std::unordered_map<u64, At> at;
  const Registry& R = reg();
  for (auto& c : comps) {
    if (c.speed <= 0.05f) continue;
    for (u64 k : c.belts) { MechNode& b = nodeMap[k]; at[k] = {w.getMeta(b.p.x, b.p.y, b.p.z) & 3, std::min(1.2f, c.speed)}; }
    // machines push what they've made onto a belt leading away from them
    if (remote) continue;
    for (u64 k : c.machines) {
      MechNode& m = nodeMap[k];
      json* e = w.blockEntity(m.p.x, m.p.y, m.p.z);
      if (!e || !(*e)["items"].is_array() || (*e)["items"].size() < 2 || (*e)["items"][1].is_null()) continue;
      float& tt = ejectT[k];
      tt += dt * c.speed;
      if (tt < 0.6f) continue;
      tt = 0;
      for (int d = 0; d < 4; d++) {
        int bx = m.p.x + DIR4[d][0], bz = m.p.z + DIR4[d][1];
        BlockId bid = w.getBlock(bx, m.p.y, bz);
        if (R.block(bid).str("mech") != "belt" || (w.getMeta(bx, m.p.y, bz) & 3) != d) continue;
        ItemStack s = ItemStack::fromJson((*e)["items"][1]), one = s; one.n = 1;
        s.n--; (*e)["items"][1] = s.empty() ? json(nullptr) : s.toJson();
        dropStack(lv, w.realm, one, vec3(bx + 0.5f, m.p.y + 0.2f, bz + 0.5f), vec3(0), 1);
        w.blockEntityChanged(m.p.x, m.p.y, m.p.z);
        break;
      }
    }
  }
  if (at.empty()) return;
  Redstone* rsys = redstoneOf(lv, w.realm);
  auto carry = [&](Entity& e, bool isItem) {
    int by = ifloor(e.pos.y + 0.02f), bx = ifloor(e.pos.x), bz = ifloor(e.pos.z);
    auto it = at.find(posKey(bx, by, bz));
    if (it == at.end()) return;
    if (e.pos.y - by > 0.35f) return;   // only what's on the belt
    int dx = DIR4[it->second.dir][0], dz = DIR4[it->second.dir][1];
    float v = BELT_SPEED * it->second.s * dt;
    // into whatever's at the far end
    if (isItem && !remote && rsys) {
      float ahead = (e.pos.x - (bx + 0.5f)) * dx + (e.pos.z - (bz + 0.5f)) * dz;
      int nx = bx + dx, nz = bz + dz;
      if (ahead > 0.3f && R.block(w.getBlock(nx, by, nz)).str("mech") != "belt") {
        if (Container c = rsys->inv(nx, by, nz)) {
          int from = it->second.dir == 0 ? 3 : it->second.dir == 1 ? 4 : it->second.dir == 2 ? 2 : 5;   // the side it arrives at
          ItemStack s = ItemStack::fromJson(e.data["stack"]);
          bool moved = false;
          while (s.n > 0 && rsys->insertOne(c, from, s)) { s.n--; moved = true; }
          if (moved) { rsys->changedInv(c); e.data["stack"] = s.toJson(); }
          if (s.n <= 0) { e.removed = true; return; }
        }
      }
    }
    // along the belt, and drawn to its middle line
    float cx = bx + 0.5f, cz = bz + 0.5f, side = dx ? (cz - e.pos.z) : (cx - e.pos.x), pull = clampv(side * 0.5f, -v, v);
    float ox = dx * v + (dx ? 0 : pull), oz = dz * v + (dz ? 0 : pull), ew = e.isPlayer() ? 0.3f : (e.w > 0 ? e.w : 0.3f), eh = e.isPlayer() ? 1.8f : (e.h > 0 ? e.h : 0.5f);
    if (boxFree(w, vec3(e.pos.x + ox - ew, e.pos.y + 0.01f, e.pos.z + oz - ew), vec3(e.pos.x + ox + ew, e.pos.y + eh, e.pos.z + oz + ew))) { e.pos.x += ox; e.pos.z += oz; }
    if (isItem) { e.vel.x *= 0.5f; e.vel.z *= 0.5f; }
  };
  for (auto& ep : lv.entities()) {
    Entity& e = *ep;
    if (e.removed || e.ghost || e.realm != w.realm) continue;
    if (e.isPlayer()) {
      Player& p = static_cast<Player&>(e);
      if (!p.dead && p.onGround && !p.riding && !p.flying) carry(p, false);
      continue;
    }
    if (remote) continue;
    if (e.type == "item") carry(e, true);
    else if (R.mobs.is_object() && R.mobs.contains(e.type) && e.hp > 0 && !e.noGravity && !R.mobs[e.type].value("boss", false)) carry(e, false);
  }
}

// ---------------------------------------------------------------------------------------- the level side
Mechanics* MechLevel::of(int realm) {
  auto it = per.find(realm);
  if (it != per.end()) return it->second.get();
  World* w = lv.worldIfOpen(realm);
  if (!w) return nullptr;
  auto m = std::make_shared<Mechanics>(lv, *w);
  w->addSystem(m);
  for (auto& [k, c] : w->chunks()) m->onChunkLoaded(*w, *c);
  per[realm] = m;
  return m.get();
}
void MechLevel::tick(Level& l, double dt) {
  for (auto& [realm, w] : l.allRealms()) if (Mechanics* m = of(realm)) m->update(dt);
}
Mechanics* mechanicsOf(Level& lv, int realm) { auto* s = lv.system<MechLevel>(); return s ? s->of(realm) : nullptr; }

void mechPlaced(Level& lv, Player* p, World& w, int x, int y, int z, BlockId id, int face) {
  const BlockDef& d = reg().block(id);
  std::string k = d.str("mech");
  int nx = face == 0 ? 1 : face == 1 ? -1 : 0, nz = face == 4 ? 1 : face == 5 ? -1 : 0;
  int look = p ? dirOf(-std::sin(p->yaw), -std::cos(p->yaw)) : 0;
  if (k == "belt") { w.setBlock(x, y, z, id, look); w.setBlockEntity(x, y, z, json{{"type", "mech"}}); return; }
  if (k == "harvester") { w.setBlockEntity(x, y, z, json{{"type", "mech"}, {"owner", p ? p->name : ""}}); return; }
  if (k == "axle" || k == "windmill" || k == "waterwheel") {
    int axis = 0;
    if (k == "axle") axis = nx ? 1 : nz ? 2 : 0;
    else axis = nx ? 1 : nz ? 2 : (look & 1 ? 1 : 2);
    w.setBlock(x, y, z, id, axis);
    w.setBlockEntity(x, y, z, json{{"type", "mech"}});
  } else if (d.has("machine")) {
    json e{{"type", "machine"}, {"items", json::array({nullptr, nullptr})}, {"progress", 0}};
    if (k == "crusher" && p) { e["owner"] = p->name; e["smithLv"] = 1; }
    w.setBlockEntity(x, y, z, e);
  }
  if (k == "windmill" && p) {
    Mechanics* m = mechanicsOf(lv, w.realm);
    MechNode n; n.p = {x, y, z}; n.id = id; n.kind = k; n.axis = std::min(2, w.getMeta(x, y, z) & 3);
    Mechanics::Source s = m ? m->sourcePower(n) : Mechanics::Source{0, false};
    vehToast(lv, *p, s.blocked ? "The sails need open air all around \xE2\x80\x94 4 blocks every way" : (y & 255) < SEA_LEVEL + 14 ? "A windmill turns best high up" : "The sails catch the wind", s.blocked ? "warn" : "good");
  }
}

} // namespace dq
