// sim/travel.cpp — waystones and the measure of a mount (game/travel.js).
#include "sim/travel.h"

namespace dq {

const char* const DIR_WORDS[8] = {"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"};
std::string dirWord(float dx, float dz) { float ang = std::atan2(dx, -dz); return DIR_WORDS[floormod((int)std::lround(ang / (PI / 4)), 8)]; }
int wayCost(float d) { return std::max(1, std::min(5, (int)std::ceil(d / 600))); }

namespace {
json& wayRec(Level& lv) { json& D = lv.info.data; if (!D.contains("waystones") || !D["waystones"].is_object()) D["waystones"] = json::object(); return D["waystones"]; }
json& waysOf(Player& p) { if (!p.stats.is_object()) p.stats = json::object(); if (!p.stats.contains("ways") || !p.stats["ways"].is_object()) p.stats["ways"] = json::object(); return p.stats["ways"]; }
BlockId waystoneId() { return reg().blockId("waystone"); }
std::string noA(const std::string& n) { return startsWith(n, "A ") ? n.substr(2) : n; }
void sparks(Level& lv, vec3 at, int n, float spread, float up) {
  for (int i = 0; i < n; i++) lv.fx->particles("mana_arcane", at + vec3(0, frand() * up, 0), 1, {{"vel", {frand2() * spread, frand() * 2, frand2() * spread}}, {"life", 0.8}, {"size", 0.08}});
}
struct Pending { std::string player; ivec3 at; };
std::vector<Pending>& pendingPlaced() { static std::vector<Pending> v; return v; }
float& wayT() { static float t = 0; return t; }
}

std::string wayIdAt(World& w, int x, int y, int z) {
  json* e = w.blockEntity(x, y, z);
  if (e && e->is_object() && e->contains("wayId") && (*e)["wayId"].is_string()) return (*e)["wayId"];
  return "p:" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
}
std::string wayNameAt(World& w, int x, int y, int z) {
  json* e = w.blockEntity(x, y, z);
  if (e && e->is_object() && e->contains("name") && (*e)["name"].is_string()) return (*e)["name"];
  return "A waystone";
}

std::string wayAttune(Level& lv, Player& p, int x, int y, int z) {
  World& w = lv.world(p.realm);
  std::string id = wayIdAt(w, x, y, z);
  json& W = waysOf(p);
  bool fresh = !W.contains(id);
  W[id] = {{"x", x}, {"y", y}, {"z", z}, {"dim", jsDimOf(p)}, {"name", wayNameAt(w, x, y, z)}};
  if (fresh) {
    toast(lv, p, "Attuned to the " + noA(W[id]["name"]), "good");
    sfxUi(lv, p, "enchant");
    sparks(lv, vec3(x + 0.5f, y + 0.5f, z + 0.5f), 24, 2, 1.5f);
  }
  return id;
}

std::vector<WayEntry> wayList(Level& lv, Player& p, const std::string& hereId) {
  std::vector<WayEntry> out;
  int dim = jsDimOf(p);
  for (auto& [id, r] : waysOf(p).items()) {
    if (id == hereId || r.value("dim", 0) != dim) continue;
    WayEntry e; e.id = id; e.name = r.value("name", std::string("A waystone")); e.pos = ivec3(r.value("x", 0), r.value("y", 0), r.value("z", 0));
    e.dist = std::hypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z); e.cost = wayCost(e.dist);
    e.dir = dirWord(e.pos.x - p.pos.x, e.pos.z - p.pos.z);
    e.can = creativeOf(p) || p.level >= e.cost;
    out.push_back(e);
  }
  std::sort(out.begin(), out.end(), [](const WayEntry& a, const WayEntry& b) { return a.dist < b.dist; });
  return out;
}

std::string wayGo(Level& lv, Player& p, const std::string& id) {
  json& W = waysOf(p);
  if (!W.contains(id)) return "That waystone is gone";
  json rec = W[id];
  auto fail = [&](const std::string& m) { toast(lv, p, m, "warn"); return m; };
  if (rec.value("dim", 0) != jsDimOf(p)) return fail("That waystone is in another layer of the world");
  ivec3 r(rec.value("x", 0), rec.value("y", 0), rec.value("z", 0));
  float d = std::hypot(r.x - p.pos.x, r.z - p.pos.z);
  int cost = wayCost(d);
  if (!creativeOf(p) && p.level < cost) return fail("You need " + std::to_string(cost) + " level" + (cost > 1 ? "s" : "") + " of experience to go that far");
  World& w = lv.world(p.realm);
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) w.requestChunk(dimOfY(r.y), (r.x >> 4) + dx, (r.z >> 4) + dz, 1000);
  if (w.isLoaded(r.x, r.y, r.z) && w.getBlock(r.x, r.y, r.z) != waystoneId()) { W.erase(id); return fail("That waystone is gone"); }
  // step out beside it
  std::optional<vec3> at;
  if (w.isLoaded(r.x, r.y, r.z)) {
    static const int D[10][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}, {0, 2}, {2, 0}};
    for (auto& o : D) {
      int x = r.x + o[0], z = r.z + o[1], y = findSafeY(w, x, z, dimOfY(r.y), r.y + 3);
      if (y > 0 && std::abs(y - r.y) <= 3) { at = vec3(x + 0.5f, (float)y, z + 0.5f); break; }
    }
  }
  if (!at) at = vec3(r.x + 0.5f, r.y + 1.f, r.z + 0.5f);
  if (!creativeOf(p)) p.level = std::max(0, p.level - cost);
  p.riding = 0;
  sparks(lv, p.pos, 30, 3, 1.8f);
  p.pos = *at; p.prevPos = p.pos; p.vel = vec3(0); p.fallDist = 0;
  sfxUi(lv, p, "spell_blink");
  sparks(lv, p.pos, 30, 3, 1.8f);
  toast(lv, p, "To the " + noA(rec.value("name", std::string("A waystone"))), "good");
  return "";
}

// a village's waystone goes in its square, on open ground clear of the buildings, the well and the rift
std::optional<ivec3> waySiteFor(Level& lv, const VillageInfo& v) {
  World& w = lv.world(REALM_MAIN);
  const json R = lv.info.data.value("rift", json());
  for (int r = 4; r <= 10; r++) for (int k = 0; k < 16; k++) {
    float a = k / 16.f * TAU + r;
    int x = ifloor(v.x + std::cos(a) * r), z = ifloor(v.z + std::sin(a) * r);
    if (!w.isLoaded(x, v.y, z) || !w.isLoaded(x + 1, v.y, z + 1)) return std::nullopt;
    bool inB = false;
    for (auto& b : v.blds) if (x >= b.x - 1 && x <= b.z + 1 && z >= b.y - 1 && z <= b.w + 1) { inB = true; break; }
    if (inB) continue;
    if (R.is_object() && R.contains("at") && R.value("village", "") == v.id && std::hypot(R["at"][0].get<float>() - x - 0.5f, R["at"][2].get<float>() - z - 0.5f) < 4) continue;
    int y = findSafeY(w, x, z, dimOfY(v.y));
    if (y < 1 || std::abs(y - (v.y + 1)) > 3) continue;
    BlockId below = w.getBlock(x, y - 1, z);
    if (reg().tLiquid[below] || !reg().tSolid[below]) continue;
    if (w.getBlock(x, y, z) || w.getBlock(x, y + 1, z)) continue;
    bool crowd = false;
    for (int dx = -1; dx <= 1 && !crowd; dx++) for (int dz = -1; dz <= 1; dz++) { BlockId id = w.getBlock(x + dx, y, z + dz); if (id && reg().tSolid[id]) { crowd = true; break; } }
    if (crowd) continue;
    return ivec3(x, y, z);
  }
  return std::nullopt;
}

bool travelUse(UseCtx& u) {
  if (!u.hasHit() || u.p.sneaking) return false;
  World& w = u.world();
  const BlockDef& b = reg().block(w.getBlock(u.hit.x, u.hit.y, u.hit.z));
  if (!b.has("waystone")) {
    // your own: named for you, and you're attuned the moment it's down (after gameplay places it)
    if (!u.p.held().empty() && u.p.held().id == "waystone") pendingPlaced().push_back({u.p.name, u.placeAt()});
    return false;
  }
  std::string id = wayAttune(u.lv, u.p, u.hit.x, u.hit.y, u.hit.z);
  openContentScreen(u.lv, u.p, "waystones", {{"here", id}});
  return true;
}

void travelTick(Level& lv, double dt) {
  // waystones you've just set down
  if (!pendingPlaced().empty()) {
    auto list = std::move(pendingPlaced()); pendingPlaced().clear();
    for (auto& pp : list) {
      Player* p = findPlayer(lv, pp.player); if (!p) continue;
      World& w = lv.world(p->realm);
      if (w.getBlock(pp.at.x, pp.at.y, pp.at.z) != waystoneId()) continue;
      json* e = w.blockEntity(pp.at.x, pp.at.y, pp.at.z, true);
      if (!e->is_object()) *e = json::object();
      if (!e->contains("name")) { (*e)["type"] = "waystone"; (*e)["name"] = p->name + "'s Waystone"; w.blockEntityChanged(pp.at.x, pp.at.y, pp.at.z); }
      wayAttune(lv, *p, pp.at.x, pp.at.y, pp.at.z);
    }
  }
  // every village gets one in its square
  if (lv.isRemote() || !contentHooks().villageNear || (wayT() -= (float)dt) > 0) return;
  wayT() = 3;
  json& W = wayRec(lv);
  for (Player* p : levelPlayers(lv)) {
    if (p->realm != REALM_MAIN || jsDimOf(*p) != JS_OVERWORLD) continue;
    auto v = contentHooks().villageNear(lv, p->pos, 48);
    if (!v || W.contains("v:" + v->id)) continue;
    auto s = waySiteFor(lv, *v);
    if (!s) continue;
    World& w = lv.world(REALM_MAIN);
    std::string name = "Waystone of " + v->name;
    w.setBlock(s->x, s->y, s->z, waystoneId(), 0);
    w.setBlockEntity(s->x, s->y, s->z, {{"type", "waystone"}, {"wayId", "v:" + v->id}, {"name", name}});
    W["v:" + v->id] = {{"x", s->x}, {"y", s->y}, {"z", s->z}, {"dim", 0}, {"name", name}};
  }
}

// ------------------------------------------------------------------ mounts
std::optional<vec2> mountBase(const std::string& t) {
  static const std::unordered_map<std::string, vec2> M = {
    {"horse", {10, 9.5f}}, {"donkey", {7.5f, 8.5f}}, {"llama", {6.5f, 8}}, {"camel", {7.5f, 8}}, {"deer", {9.5f, 10}}, {"goat", {6.5f, 11}},
    {"moa", {8.5f, 10}}, {"brown_bear", {6.5f, 8}}, {"polar_bear", {6.5f, 8}}, {"lion", {9.5f, 9.5f}}, {"lioness", {9.5f, 9.5f}}, {"tiger", {10, 9.5f}},
    {"panther", {10, 10}}, {"snow_leopard", {9, 10.5f}}, {"verdant_drake", {7, 9}}, {"sand_drake", {7, 9}}, {"frost_drake", {7, 9}}, {"bog_drake", {7, 9}},
    {"ember_drake", {7, 9}}, {"storm_drake", {7, 9}}, {"tide_drake", {7, 9}}, {"ender_drake", {7, 9}}};
  auto it = M.find(t);
  return it == M.end() ? std::nullopt : std::optional<vec2>(it->second);
}
static float r2(float v) { return std::round(v * 100) / 100; }
void hsApplyHp(Entity& m) {
  const json& hs = m.data["hs"];
  if (!hs.is_array() || hs.size() != 3) return;
  float base = reg().mobs.is_object() && reg().mobs.contains(m.type) ? reg().mobs[m.type].value("hp", 20.f) : 20.f;
  float mx = std::round(base * hs[2].get<float>()), f = m.maxHp > 0 ? m.hp / m.maxHp : 1;
  m.maxHp = mx; m.hp = std::max(1.f, std::round(mx * f));
}
json mountStats(Entity& m) {
  if (!mountBase(m.type)) return nullptr;
  if (!m.data["hs"].is_array()) {
    m.data["hs"] = json::array({r2(0.8f + frand() * 0.4f), r2(0.8f + frand() * 0.4f), r2(0.8f + frand() * 0.4f)});
    hsApplyHp(m);
  }
  return m.data["hs"];
}
std::string hsLine(const json& hs) {
  static const char* N[3] = {"Speed", "Jump", "Toughness"};
  std::string out;
  for (int i = 0; i < 3 && hs.is_array() && i < (int)hs.size(); i++) {
    int n = std::max(1, std::min(5, (int)std::lround((hs[i].get<float>() - 0.7f) / 0.13f)));
    if (i) out += " \xC2\xB7 ";
    out += N[i]; out += ' ';
    for (int k = 0; k < 5; k++) out += k < n ? "\xE2\x98\x85" : "\xE2\x98\x86";
  }
  return out;
}
json foalStats(const json& a, const json& b) {
  json out = json::array();
  for (int i = 0; i < 3; i++) {
    float va = a.is_array() && a.size() == 3 ? a[i].get<float>() : 1, vb = b.is_array() && b.size() == 3 ? b[i].get<float>() : va;
    out.push_back(r2(clampv((va + vb) / 2 + (frand() - 0.42f) * 0.14f, 0.7f, 1.5f)));
  }
  return out;
}

} // namespace dq
