// sim/villagers.cpp — the villages and the people in them (game/villagers.js: who they are, standing with a village,
// bringing a village to life near players and letting it go when everyone has left), the VillagerSystem that runs
// it all, its events (itemUse for the quest board, gates and levers), the creature hooks and the chat commands.
#include "sim/villagers_internal.h"
#include "sim/skills.h"
#include "sim/rpg.h"
#include "sim/commands.h"
#include "sim/crafting.h"
#include "gen/overworld.h"
#include "gen/dimgens.h"
#include "gen/structures.h"
#include "gen/generator.h"
#include "world/dims.h"
#include "core/registry.h"
#include <regex>

namespace dq {

// ------------------------------------------------------------------ data
const json& VD() {
  static const json j = [] { auto r = readJson(dataDir() / "villagers.json"); if (!r) DQ_WARN("villagers: data/villagers.json missing"); return r ? *r : json::object(); }();
  return j;
}
std::string npcName(u32 seed) { const json& F = jarr(VD(), "first"); return F.empty() ? "Villager" : F[seed % F.size()].get<std::string>(); }
std::string jobTitle(const std::string& job) {
  const json& J = VD()["jobs"];
  if (J.contains(job) && J[job].contains("title")) return J[job]["title"];
  return job == "golem" ? "Iron Golem" : "Villager";
}
u32 npcSeedOf(const Village& v, int i) { return hash32((i32)v.seed, i, 77, 5) % 100000; }
NpcVoice npcVoice(u32 seed) {
  Mulberry32 r(seed ^ 0x51edu);
  NpcVoice o; o.pitch = 150 + (float)r() * 170; o.formant = 0.85f + (float)r() * 0.35f; o.triangle = r() < 0.35; o.seed = (int)(seed % 97);
  return o;
}
const json* npcOf(const Entity& e) { return e.data.is_object() && e.data.contains("npc") && e.data["npc"].is_object() ? &e.data["npc"] : nullptr; }
bool isVillager(const Entity& e) { return dynamic_cast<const Creature*>(&e) && npcOf(e); }
double npcPhase(const Level& lv) { return lv.dayFraction(); }
bool npcNight(const Level& lv) { double ph = npcPhase(lv); return ph > 0.53 && ph < 0.985; }
i64 npcDay(const Level& lv) { return lv.info.day; }
std::string itemName(const std::string& id) { const ItemDef* d = reg().item(id); return d ? d->name : id; }
int countOf(Player& p, const std::string& id) { Bags b{&p.inv, nullptr}; return id == "log" ? b.countTag("#log") : b.count(id); }
void removeOf(Player& p, const std::string& id, int n) { Bags b{&p.inv, nullptr}; if (id == "log") b.removeTag("#log", n); else b.remove(id, n); }
void vGive(Level& lv, Player& p, const ItemStack& s) { static_cast<void (*)(Level&, Player&, const ItemStack&)>(giveOrDrop)(lv, p, s); }
std::string dirWord(double ang) {
  static const char* D[8] = {"east", "south-east", "south", "south-west", "west", "north-west", "north", "north-east"};
  return D[(((int)std::floor(ang / (PI / 4) + 0.5) + 8) % 8 + 8) % 8];
}
int roundTo(double v, int step) { return (int)std::floor(v / step + 0.5) * step; }

// ------------------------------------------------------------------ villages
static int villageTypeIndex() {
  const auto& T = structureTypes();
  for (int i = 0; i < (int)T.size(); i++) if (T[i].key == "village") return i;
  return -1;
}
static const StructureIndex* owIndex(Level& lv) {
  if (!lv.gen) return nullptr;
  auto* o = dynamic_cast<const OverworldGen*>(lv.gen->forDim(DIM_OVERWORLD));
  return o ? &o->structures : nullptr;
}
static ivec3 jv3(const json& a, int dy) { return a.is_array() && a.size() >= 3 ? ivec3(a[0].get<int>(), a[1].get<int>() + dy, a[2].get<int>()) : ivec3(0); }

Village parseVillage(const json& d) {
  Village v; const int B = dimBase(DIM_OVERWORLD);
  v.id = d.value("id", std::string()); v.name = d.value("name", std::string("the village"));
  v.style = d.value("style", std::string("plains")); v.tier = d.value("tier", std::string("village"));
  v.x = d.value("x", 0); v.z = d.value("z", 0); v.y = d.value("y", 64) + B;
  v.P = d.value("P", 7); v.hw = d.value("hw", 1); v.fr = d.value("fr", 0); v.r = d.value("r", 24);
  v.guards = d.value("guards", 0); v.golems = d.value("golems", 0); v.seed = d.value("seed", 0u);
  std::smatch m; static const std::regex re("^[vt](-?\\d+)_(-?\\d+)$");
  if (std::regex_match(v.id, m, re)) { v.rx = std::stoi(m[1]); v.rz = std::stoi(m[2]); }
  v.board = jv3(d.value("board", json()), B); v.bell = jv3(d.value("bell", json()), B);
  for (auto& f : jarr(d, "folk")) {
    VFolk o; o.job = f.value("job", std::string("none"));
    if (f.contains("home") && f["home"].is_object()) { o.home = jv3(f["home"]["at"], B); o.homeF = f["home"].value("f", 0); }
    if (f.contains("work") && f["work"].is_object()) { o.hasWork = true; o.work = jv3(f["work"]["at"], B); o.workF = f["work"].value("face", 0); }
    v.folk.push_back(o);
  }
  for (auto& s : jarr(d, "seats")) {
    if (!s.is_array() || s.size() < 4) continue;
    VSeatSpot o; o.at = ivec3(s[0].get<int>(), s[1].get<int>() + B, s[2].get<int>()); o.f = s[3].get<int>() & 3;
    if (s.size() > 4 && s[4].is_array() && s[4].size() >= 2) { o.walk = true; o.ax = s[4][0]; o.az = s[4][1]; }
    if (s.size() > 5 && s[5].is_string()) o.held = s[5];
    v.seats.push_back(o);
  }
  for (auto& r : jarr(d, "roads")) {
    VRoad o; o.d = r.value("d", 0) & 3; o.L = r.value("L", 8); o.hw = r.value("hw", 1);
    for (auto& c : jarr(r, "cells")) if (c.is_array() && c.size() >= 3) o.cells.push_back(ivec3(c[0].get<int>(), c[2].get<int>() + B, c[1].get<int>()));   // [x, z, h]
    v.roads.push_back(o);
  }
  for (auto& g : jarr(d, "gates")) {
    VGate o; o.key = g.value("key", std::string()); o.name = g.value("name", std::string("Gate")); o.d = g.value("d", 0); o.stone = g.value("stone", false);
    for (auto& c : jarr(g, "cells")) o.cells.push_back(jv3(c, B));
    if (g.contains("center")) { auto& c = g["center"]; o.center = vec3(c[0].get<float>(), c[1].get<float>() + B, c[2].get<float>()); }
    o.h0 = g.value("h0", 0) + B; o.inX = g.value("inX", 0); o.inZ = g.value("inZ", 0); o.hw = g.value("hw", 1); o.span = g.value("span", 3);
    o.cx = g.value("cx", 0); o.cz = g.value("cz", 0);
    int k = 0; for (auto& p : jarr(g, "posts")) if (k < 2 && p.is_array() && p.size() >= 3) o.posts[k++] = vec3(p[0].get<float>(), p[1].get<float>() + B, p[2].get<float>());
    o.lever = jv3(g.value("lever", json()), B);
    v.gates.push_back(o);
  }
  for (auto& b : jarr(d, "blds")) {
    VBld o; o.type = b.value("type", std::string()); o.r = b.value("r", 0); o.ox = b.value("ox", 0); o.oz = b.value("oz", 0);
    o.W = b.value("W", 0); o.D = b.value("D", 0); o.H = b.value("H", 0); o.y = b.value("y", 0) + B;
    o.x0 = b.value("x0", 0); o.z0 = b.value("z0", 0); o.x1 = b.value("x1", 0); o.z1 = b.value("z1", 0);
    if (b.contains("job") && b["job"].is_string()) o.job = b["job"];
    if (b.contains("animals") && b["animals"].is_string()) o.animals = b["animals"];
    if (b.contains("mid")) { o.hasMid = true; o.mid = jv3(b["mid"], B); }
    if (b.contains("door")) o.door = jv3(b["door"], B);
    v.blds.push_back(o);
  }
  if (d.contains("wall") && d["wall"].is_object()) {
    auto& w = d["wall"]; v.hasWall = true; v.wallKind = w.value("kind", std::string("palisade"));
    v.wx0 = w.value("x0", 0); v.wz0 = w.value("z0", 0); v.wx1 = w.value("x1", 0); v.wz1 = w.value("z1", 0); v.wallH = w.value("h", 5);
  }
  return v;
}
Village* mutableVillage(const VillageRef& v) { return const_cast<Village*>(v.get()); }

static VillageRef fromPlan(VillagerSystem& S, const StructurePlan* P) {
  if (!P || !P->data.is_object() || P->data.value("kind", std::string()) != "village") return nullptr;
  auto it = S.parsed.find(P);
  if (it != S.parsed.end()) return it->second;
  json d = P->data; if (!d.contains("seed")) d["seed"] = P->seed;
  if (!d.contains("id")) d["id"] = P->id;
  auto v = std::make_shared<Village>(parseVillage(d));
  S.parsed[P] = v;
  return v;
}
void addVillage(Level& lv, const json& planData) {
  VillagerSystem* S = vsys(lv); if (!S) return;
  auto v = std::make_shared<Village>(parseVillage(planData));
  S->manual[v->id] = v;
}
std::vector<VillageRef> villagesNear(Level& lv, float x, float z, float pad) {
  std::vector<VillageRef> out;
  VillagerSystem* S = vsys(lv); if (!S) return out;
  for (auto& [id, v] : S->manual) if (std::fabs(x - v->x) <= v->r + pad && std::fabs(z - v->z) <= v->r + pad) out.push_back(v);
  const int k = villageTypeIndex();
  if (const StructureIndex* idx = owIndex(lv); idx && k >= 0) {
    std::vector<const StructurePlan*> plans;
    idx->near(ifloor(x), ifloor(z), (int)pad, plans, k);
    for (auto* P : plans) if (auto v = fromPlan(*S, P)) out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [&](const VillageRef& a, const VillageRef& b) { return std::hypot(a->x - x, a->z - z) < std::hypot(b->x - x, b->z - z); });
  return out;
}
VillageRef villageById(Level& lv, const std::string& id) {
  VillagerSystem* S = vsys(lv); if (!S) return nullptr;
  if (auto it = S->manual.find(id); it != S->manual.end()) return it->second;
  std::smatch m; static const std::regex re("^[vt](-?\\d+)_(-?\\d+)$");
  if (!std::regex_match(id, m, re)) return nullptr;
  const int k = villageTypeIndex(); const StructureIndex* idx = owIndex(lv);
  if (k < 0 || !idx) return nullptr;
  auto v = fromPlan(*S, idx->at(k, std::stoi(m[1]), std::stoi(m[2])));
  return v && v->id == id ? v : nullptr;
}
VillageRef villageAtPos(Level& lv, int realm, vec3 p, float pad) {
  if (realm != REALM_MAIN || dimOfY(ifloor(p.y)) != DIM_OVERWORLD) return nullptr;
  auto l = villagesNear(lv, p.x, p.z, pad);
  return l.empty() ? nullptr : l[0];
}
VillageRef villageNeighbour(Level& lv, const Village& v, float* dist) {
  VillagerSystem* S = vsys(lv); if (!S) return nullptr;
  VillageRef best; double bd = 1e9;
  auto consider = [&](const VillageRef& o) { if (!o || o->id == v.id) return; double d = std::hypot(o->x - v.x, o->z - v.z); if (d < bd) { bd = d; best = o; } };
  for (auto& [id, o] : S->manual) consider(o);
  const int k = villageTypeIndex(); const StructureIndex* idx = owIndex(lv);
  if (k >= 0 && idx) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) if (dx || dz) consider(fromPlan(*S, idx->at(k, v.rx + dx, v.rz + dz)));
  if (dist) *dist = (float)bd;
  return best;
}
json& villageRec(Level& lv, const std::string& vid) {
  json& m = lv.info.data["villages"]; if (!m.is_object()) m = json::object();
  json& r = m[vid]; if (!r.is_object()) r = json{{"dead", json::object()}};
  if (!r.contains("dead") || !r["dead"].is_object()) r["dead"] = json::object();
  return r;
}

// ------------------------------------------------------------------ standing
Standing standingOf(double rep) {
  const json& L = jarr(VD(), "standing");
  Standing s; int k = 0;
  for (int i = 0; i < (int)L.size(); i++) if (rep >= L[i][0].get<double>()) k = i;
  s.k = k; s.name = L.empty() ? "Stranger" : L[k][1].get<std::string>(); s.tier = k <= 2 ? 0 : std::min(3, k - 2);
  if (k + 1 < (int)L.size()) { s.hasNext = true; s.next = L[k + 1][0]; }
  return s;
}
json& npcStats(Player& p) {
  json& s = p.stats; if (!s.is_object()) s = json::object();
  if (!s.contains("vrep") || !s["vrep"].is_object()) s["vrep"] = json::object();
  if (!s.contains("quests") || !s["quests"].is_array()) s["quests"] = json::array();
  if (!s.contains("vstock") || !s["vstock"].is_object()) s["vstock"] = json::object();
  if (!s.contains("qdone") || !s["qdone"].is_object()) s["qdone"] = json::object();
  return s;
}
double repOf(Player& p, const std::string& vid) { json& S = npcStats(p); return S["vrep"].value(vid, 0.0); }
bool isHero(Player& p, const std::string& vid) { return standingOf(repOf(p, vid)).k >= (int)jarr(VD(), "standing").size() - 1; }

void npcRep(Level& lv, Player& p, const std::string& vid, double n, const std::string& why) {
  if (vid.empty() || !n) return;
  json& S = npcStats(p); const i64 day = npcDay(lv);
  if (why == "defend" || why == "trade") {          // a little at a time from these
    std::string k = why + ":" + vid; json& C = S["vcap"]; if (!C.is_object()) C = json::object();
    bool same = C.contains(k) && C[k].is_array() && C[k][0].get<i64>() == day;
    if (same && C[k][1].get<int>() >= (why == "trade" ? 6 : 4)) return;
    C[k] = same ? json::array({day, C[k][1].get<int>() + 1}) : json::array({day, 1});
  }
  Standing before = standingOf(S["vrep"].value(vid, 0.0));
  S["vrep"][vid] = S["vrep"].value(vid, 0.0) + n;
  Standing after = standingOf(S["vrep"][vid].get<double>());
  VillageRef v = villageById(lv, vid);
  std::string vn = v ? v->name : "The village";
  if (after.k > before.k) toast(lv, p, vn + " now counts you a " + after.name, after.k >= 5 ? "epic" : "good");
  else if (after.k < before.k) toast(lv, p, vn + " trusts you less (" + after.name + ")", "warn");
}

// ------------------------------------------------------------------ the rift's state (riftarc.js riftStability, riftlore.js riftTier)
// (riftStability / riftTier live in sim/endstory.cpp)

// ------------------------------------------------------------------ people
Player* playerByName(Level& lv, const std::string& name) { return findPlayer(lv, name); }
Player* playerById(Level& lv, u32 id) { return dynamic_cast<Player*>(lv.entity(id)); }
VillagerSystem* vsys(Level& lv) { return lv.system<VillagerSystem>(); }
void later(Level& lv, double secs, const Player& p, const std::string& text) {
  if (VillagerSystem* S = vsys(lv)) S->later.push_back({S->now + secs, {p.name, text}});
}
void openVillagerScreen(Level& lv, const Player& p, const std::string& id, const json& args) {
  if (lv.events) lv.events->emit("villagerScreen", json::array({p.name, id, args}));
}

Creature* npcSpawn(Level& lv, const Village& v, int i, const std::string& kind) {
  const VFolk* f = kind == "folk" && i < (int)v.folk.size() ? &v.folk[i] : nullptr;
  if (kind == "folk" && !f) return nullptr;
  std::string job = kind == "guard" ? "guard" : kind == "golem" ? "golem" : f->job;
  vec3 at;
  if (kind == "golem") at = vec3(v.x + 3.5f, v.y + 1, v.z + 3.5f);
  else if (kind == "guard") at = vec3(v.x + (i % 2 ? -3.5f : 3.5f), v.y + 1, v.z + (i < 2 ? 3.5f : -3.5f));
  else if (npcNight(lv)) at = vec3(f->home) + vec3(0.5f, 0, 0.5f);
  else if (f->hasWork) at = vec3(f->work) + vec3(0.5f, 0, 0.5f);
  else at = vec3(f->home) + vec3(0.5f, 0, 0.5f);
  const char* type = kind == "golem" ? "iron_golem" : kind == "guard" ? "village_guard" : "villager";
  Creature* e = spawnCreature(lv, REALM_MAIN, type, at);
  if (!e) return nullptr;
  int idx = kind == "guard" ? 100 + i : kind == "golem" ? 200 + i : i;
  u32 seed = kind == "golem" ? 0 : npcSeedOf(v, idx);
  e->data["npc"] = {{"v", v.id}, {"i", idx}, {"n", kind == "golem" ? std::string("Iron Golem") : npcName(seed)}, {"j", job}, {"s", seed}, {"p", 0}};
  if (kind == "guard") { e->heldItem = "iron_sword"; e->data["npcArmor"] = json::array({"iron_helmet", "iron_chestplate", "iron_leggings", "iron_boots"}); }
  else if (kind != "golem") { const json& J = VD()["jobs"]; e->heldItem = J.contains(job) && J[job].contains("held") && J[job]["held"].is_string() ? J[job]["held"].get<std::string>() : ""; }
  if (!e->heldItem.empty() && !reg().item(e->heldItem)) e->heldItem.clear();
  e->kept = false;
  return e;
}

void npcTickVillages(Level& lv, VillagerSystem& S, double dt) {
  if ((S.npcT -= (float)dt) > 0) return;
  S.npcT = 1.2f;
  std::vector<Player*> ps;
  for (Player* p : levelPlayers(lv)) if (!p->dead && p->realm == REALM_MAIN && dimOfY(ifloor(p->pos.y)) == DIM_OVERWORLD) ps.push_back(p);
  std::map<std::string, VillageRef> want;
  for (Player* p : ps) for (auto& v : villagesNear(lv, p->pos.x, p->pos.z, 72)) want[v->id] = v;
  for (auto it = S.active.begin(); it != S.active.end();) {
    if (want.count(it->first)) { ++it; continue; }
    const Village& v = *it->second.v;
    bool far = true; for (Player* p : ps) if (std::hypot(p->pos.x - v.x, p->pos.z - v.z) <= v.r + 110) far = false;
    if (far) { for (u32 id : it->second.ents) if (Entity* e = lv.entity(id)) e->removed = true; it = S.active.erase(it); }
    else ++it;
  }
  World& w = lv.world(REALM_MAIN);
  const i64 day = npcDay(lv);
  for (auto& [id, v] : want) {
    if (!w.isLoaded(v->x, v->y, v->z)) continue;   // only once the middle of the village is there to stand on
    auto& a = S.active[id]; a.v = v;
    json& rec = villageRec(lv, id); json& dead = rec["dead"];
    std::set<int> has;
    a.ents.erase(std::remove_if(a.ents.begin(), a.ents.end(), [&](u32 e) { Entity* x = lv.entity(e); return !x || x->removed; }), a.ents.end());
    for (u32 e : a.ents) if (const json* N = npcOf(*lv.entity(e))) has.insert(N->value("i", -1));
    auto deadRecently = [&](int i, int days) { std::string k = std::to_string(i); return dead.contains(k) && day - dead[k].get<i64>() < days; };
    for (int i = 0; i < (int)v->folk.size(); i++) {
      if (has.count(i) || deadRecently(i, 2)) continue;      // someone new moves in after a couple of days
      const ivec3 h = v->folk[i].home;
      if (!w.isLoaded(h.x, h.y, h.z)) continue;
      dead.erase(std::to_string(i));
      if (Creature* e = npcSpawn(lv, *v, i, "folk")) a.ents.push_back(e->id);
    }
    for (int i = 0; i < v->guards; i++) if (!has.count(100 + i) && !deadRecently(100 + i, 1)) { dead.erase(std::to_string(100 + i)); if (Creature* e = npcSpawn(lv, *v, i, "guard")) a.ents.push_back(e->id); }
    for (int i = 0; i < v->golems; i++) if (!has.count(200 + i) && !deadRecently(200 + i, 3)) { dead.erase(std::to_string(200 + i)); if (Creature* e = npcSpawn(lv, *v, i, "golem")) a.ents.push_back(e->id); }
    // the pens get their animals, once
    if (!rec.value("penned", 0)) {
      rec["penned"] = 1;
      for (auto& b : v->blds) if (b.type == "pen" && b.hasMid && creatureType(b.animals))
        for (int k = 0; k < 3; k++) if (Creature* m = spawnCreature(lv, REALM_MAIN, b.animals, vec3(b.mid.x + 0.5f + (k - 1) * 1.2f, b.mid.y, b.mid.z + 0.5f))) { m->kept = true; m->fed = true; }
    }
  }
}

std::vector<Creature*> spawnDevVillagers(Level& lv, Player& p, const std::vector<std::string>& jobs) {
  std::vector<Creature*> out;
  // a little village of one square in front of the player (so the brain has a home to go to)
  float fx = -std::sin(p.yaw), fz = -std::cos(p.yaw);
  int cx = ifloor(p.pos.x + fx * 4), cz = ifloor(p.pos.z + fz * 4), cy = ifloor(p.pos.y) - dimBase(DIM_OVERWORLD) - 1;
  json folk = json::array(); int guards = 0;
  for (size_t i = 0; i < jobs.size(); i++) {
    if (jobs[i] == "guard") { guards++; continue; }
    int k = (int)folk.size();
    float sx = (k - (float)(jobs.size() - 1) / 2) * 1.6f;
    int hx = ifloor(p.pos.x + fx * 4 + fz * sx), hz = ifloor(p.pos.z + fz * 4 - fx * sx);
    json spot = {{"at", json::array({hx, cy + 1, hz})}, {"face", 0}};
    folk.push_back({{"home", {{"at", json::array({hx, cy + 1, hz})}, {"f", 0}}}, {"job", jobs[i]}, {"work", spot}});
  }
  json d = {{"kind", "village"}, {"id", "v900000_900000"}, {"name", "Devhaven"}, {"style", "plains"}, {"tier", "village"}, {"x", cx}, {"z", cz}, {"y", cy},
            {"P", 7}, {"hw", 1}, {"r", 16}, {"board", json::array({cx, cy + 1, cz})}, {"bell", json::array({cx, cy + 1, cz})}, {"folk", folk}, {"guards", guards},
            {"golems", 0}, {"blds", json::array()}, {"roads", json::array({{{"d", 0}, {"L", 10}, {"hw", 1}}})}, {"seats", json::array()}, {"seed", 12345}};
  addVillage(lv, d);
  VillageRef v = villageById(lv, "v900000_900000");
  VillagerSystem* S = vsys(lv);
  if (!v || !S) return out;
  auto& a = S->active[v->id]; a.v = v;
  for (int i = 0; i < (int)v->folk.size(); i++) if (Creature* e = npcSpawn(lv, *v, i, "folk")) { a.ents.push_back(e->id); out.push_back(e); }
  for (int i = 0; i < guards; i++) if (Creature* e = npcSpawn(lv, *v, i, "guard")) {
    float sx = (i - (guards - 1) / 2.f) * 1.6f;
    e->pos = vec3(p.pos.x + fx * 6 + fz * sx, p.pos.y, p.pos.z + fz * 6 - fx * sx);
    a.ents.push_back(e->id); out.push_back(e);
  }
  return out;
}

// ------------------------------------------------------------------ the system
void VillagerSystem::tick(Level& lv, double dt) {
  if (lv.isRemote()) return;   // the host (or single player) runs the villages
  now += dt;
  if (!cleaned) {   // the villagers aren't saved: whoever a save brought back goes (they come again when someone is near)
    cleaned = true;
    for (auto& e : lv.entities()) if (isVillager(*e)) e->removed = true;
  }
  npcTickVillages(lv, *this, dt);
  gatesTick(lv, *this, dt);
  raidTick(lv, *this, dt);
  eliteTick(lv, *this, dt);
  lostTick(lv, *this, dt);
  homeTick(lv, *this, dt);
  for (Player* p : levelPlayers(lv)) playerLifeTick(lv, *this, *p, dt);
  for (size_t i = 0; i < later.size();) {
    if (later[i].first > now) { i++; continue; }
    if (Player* p = playerByName(lv, later[i].second.first)) toast(lv, *p, later[i].second.second, "");
    later.erase(later.begin() + i);
  }
  // minds of those who've gone
  static float sweep = 0;
  if ((sweep -= (float)dt) <= 0) {
    sweep = 5;
    for (auto it = minds.begin(); it != minds.end();) { Entity* e = lv.entity(it->first); if (!e || e->removed) it = minds.erase(it); else ++it; }
  }
}

// ---- the creature hooks: the brain runs in place of any other; hurt, death, use ----
static Player* attackerPlayer(Level& lv, const DamageSrc& src) { return dynamic_cast<Player*>(lv.entity(src.attacker)); }

static bool hookBeforeHurt(Level& lv, Creature& c, float& amount, const DamageSrc& src) {
  const json* N = npcOf(c);
  if (N || c.T->j.value("golem", false)) {
    // no friendly fire in a village
    if (auto* a = dynamic_cast<Creature*>(lv.entity(src.attacker))) if (const json* AN = npcOf(*a)) if (N && AN->value("v", "") == N->value("v", "")) return false;
    // hearth.js: the missing villager: nobody's to be hurt on the way home
    if (N && N->value("i", 0) == 300) return false;
  }
  // paths.js Beast Friend (a ranger's creatures take less, strike harder)
  amount *= pathPetFactor(lv, c, lv.entity(src.attacker));
  return true;
}
static void hookAfterHurt(Level& lv, Creature& c, float dealt, const DamageSrc& src) {
  if (lv.isRemote()) return;
  Player* who = attackerPlayer(lv, src);
  const json* N = npcOf(c);
  bool golem = c.T->j.value("golem", false);
  if (who && (N || golem)) {
    std::string vid = N ? N->value("v", std::string()) : std::string();
    if (vid.empty()) if (auto v = villageAtPos(lv, c.realm, c.pos, 16)) vid = v->id;
    VillagerSystem* S = vsys(lv);
    bool defender = c.type == "village_guard" || golem;
    if (S) {
      NpcMind& M = S->mind(c.id);
      if (!defender) { c.panicT = 4; c.fleeFrom = who->id; } else c.panicT = 0;
      M.holdT = 0;
    }
    if (!vid.empty()) npcOffend(lv, *who, vid, c, !c.alive());
  }
  // paths.js Cleaving Arc
  if (who && src.weapon && src.type != "cleave" && src.tool != "arrow") pathCleave(lv, *who, c, dealt);
}
static void hookDied(Level& lv, Creature& c, const DamageSrc& src) {
  if (lv.isRemote()) return;
  if (const json* N = npcOf(c)) {
    std::string vid = N->value("v", std::string()); int i = N->value("i", 0);
    if (i < 300) { json& rec = villageRec(lv, vid); rec["dead"][std::to_string(i)] = npcDay(lv); }
    for (Player* p : levelPlayers(lv)) if (std::hypot(p->pos.x - c.pos.x, p->pos.z - c.pos.z) < 40)
      toast(lv, *p, N->value("n", std::string("A villager")) + (c.type == "village_guard" ? " the guard" : "") + " has died", "warn");
  }
  if (c.data.contains("elite")) eliteSlain(lv, c);
  if (c.data.contains("raid")) { json& K = lv.info.data["raidKills"]; if (!K.is_object()) K = json::object(); std::string r = c.data.value("raid", ""); K[r] = K.value(r, 0) + 1; }
  // monsters slain: bounties (and the party's), the village's thanks for one killed near it, the Warlord's heal
  Player* a = attackerPlayer(lv, src);
  if (c.T->hostile && a) {
    questKill(lv, *a, c.type);
    for (Player* o : levelPlayers(lv)) if (o != a && o->realm == a->realm && std::hypot(o->pos.x - c.pos.x, o->pos.z - c.pos.z) < 64) questKill(lv, *o, c.type, true);
    if (auto v = villageAtPos(lv, c.realm, c.pos, 24)) npcRep(lv, *a, v->id, 1, "defend");
    if (hasTal(*a, "warlord")) a->hp = std::min(a->maxHp, a->hp + 2);
  }
}
static bool hookUse(Level& lv, Player& p, Creature& e, bool sneaking) {
  const json* N = npcOf(e);
  bool golem = e.T->j.value("golem", false);
  if (!e.alive()) return false;
  if (N && N->value("i", 0) == 300) {   // hearth.js: the missing: take them by the hand
    std::string said; lostTake(lv, e, p, &said);
    if (!said.empty()) toast(lv, p, said, "good");
    return true;
  }
  if (N && !golem && !sneaking && p.carrying.is_null()) {
    if (N->value("p", 0) & NP_SLEEP) { toast(lv, p, N->value("n", std::string("They")) + " is fast asleep"); return true; }
    p.swingT = 0.001f;
    openVillagerScreen(lv, p, "talk", {{"npc", e.id}});
    return true;
  }
  if (golem) { toast(lv, p, "The iron golem looks at you, and offers you a flower\xE2\x80\xA6 almost."); return true; }
  return false;
}
static bool hookNaturalSpawn(Level& lv, int realm, const std::string& type, vec3 at) {
  VillagerSystem* S = vsys(lv);
  if (!S || realm != REALM_MAIN || dimOfY(ifloor(at.y)) != DIM_OVERWORLD) return true;
  const CreatureType* T = creatureType(type);
  if (!T || !T->hostile || at.y <= dimBase(DIM_OVERWORLD) + SEA_LEVEL - 6) return true;
  for (auto& [id, a] : S->active) if (std::hypot(at.x - a.v->x, at.z - a.v->z) < a.v->r + 6) return false;
  return true;
}

// ---- itemUse: the quest board, the gates and their levers ----
static bool onItemUse(Level& lv, const json& a) {
  if (!a.is_array() || a.size() < 6 || !a[5].is_number() || a[5].get<int>() < 0) return true;
  Player* p = playerByName(lv, a[0].get<std::string>());
  if (!p) return true;
  ivec3 at(a[2].get<int>(), a[3].get<int>(), a[4].get<int>());
  World& w = lv.world(p->realm);
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  if (b.has("questBoard") && !p->sneaking) {
    if (!boardVillage(lv, p->realm, at)) { toast(lv, *p, "The notes are blank \xE2\x80\x94 this board belongs to no village"); return false; }
    p->swingT = 0.001f;
    openVillagerScreen(lv, *p, "board", {{"x", at.x}, {"y", at.y}, {"z", at.z}});
    return false;
  }
  if (b.key == "lever" && !(p->sneaking && reg().item(a[1].get<std::string>()) && !reg().item(a[1].get<std::string>())->block.empty())) {
    if (gateUse(lv, *p, at, true)) return true;   // the lever flips too
  }
  if (b.key == "gate_barrier" && gateUse(lv, *p, at, false)) { p->swingT = 0.001f; return false; }
  return true;
}

class VillagerSystemReg : public VillagerSystem {
public:
  explicit VillagerSystemReg(Level& lv) : lv(lv) {
    if (!lv.events) return;
    hUse = lv.events->on("itemUse", [this](const json& a) { return this->lv.isRemote() ? true : onItemUse(this->lv, a); }, 5);
  }
  ~VillagerSystemReg() override { if (lv.events && hUse) lv.events->off(hUse); }
  Level& lv; int hUse = 0;
};

// ---- chat commands (dev / tests) ----
static std::string cmdVillagers(CommandSource& src, const std::vector<std::string>& args) {
  if (!src.level || !src.player) return "Only a player can do that";
  std::vector<std::string> jobs;
  for (auto& a : args) { std::string s = a; size_t c; while ((c = s.find(',')) != std::string::npos) { jobs.push_back(s.substr(0, c)); s = s.substr(c + 1); } if (!s.empty()) jobs.push_back(s); }
  if (jobs.empty()) jobs = {"farmer", "librarian", "guard", "elder"};
  auto l = spawnDevVillagers(*src.level, *src.player, jobs);
  return "Spawned " + std::to_string(l.size()) + " villager(s)";
}
static std::string cmdRep(CommandSource& src, const std::vector<std::string>& args) {
  if (!src.level || !src.player) return "Only a player can do that";
  auto v = villageAtPos(*src.level, src.player->realm, src.player->pos, 64);
  if (!v) return "No village near";
  double n = args.empty() ? 10 : std::atof(args[0].c_str());
  npcRep(*src.level, *src.player, v->id, n);
  return v->name + ": " + standingOf(repOf(*src.player, v->id)).name + " (" + std::to_string((int)repOf(*src.player, v->id)) + ")";
}
static std::string cmdRaid(CommandSource& src, const std::vector<std::string>&) {
  if (!src.level || !src.player) return "Only a player can do that";
  auto v = villageAtPos(*src.level, src.player->realm, src.player->pos, 64);
  if (!v) return "No village near";
  return raidStart(*src.level, *v, 60, false) ? "Drums in the dark\xE2\x80\xA6" : "The raiders found nowhere to gather";
}

void registerVillagerUseHook();
void registerVillagers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level& lv) { return std::make_shared<VillagerSystemReg>(lv); });
  CreatureGlobalHooks& H = globalCreatureHooks();
  auto oldBrain = H.brain;
  H.brain = [oldBrain](Level& lv, Creature& c, Entity* p, float dist, float dt) -> OptMove {
    if (!c.ghost && (npcOf(c) || c.T->j.value("golem", false))) return npcBrain(lv, c, p, dist, dt);
    return oldBrain ? oldBrain(lv, c, p, dist, dt) : std::nullopt;
  };
  auto oldBefore = H.beforeHurt;
  H.beforeHurt = [oldBefore](Level& lv, Creature& c, float& amount, const DamageSrc& s) { return hookBeforeHurt(lv, c, amount, s) && (!oldBefore || oldBefore(lv, c, amount, s)); };
  auto oldAfter = H.afterHurt;
  H.afterHurt = [oldAfter](Level& lv, Creature& c, float d, const DamageSrc& s) { hookAfterHurt(lv, c, d, s); if (oldAfter) oldAfter(lv, c, d, s); };
  auto oldDied = H.died;
  H.died = [oldDied](Level& lv, Creature& c, const DamageSrc& s) { hookDied(lv, c, s); if (oldDied) oldDied(lv, c, s); };
  auto oldUse = H.use;
  H.use = [oldUse](Level& lv, Player& p, Creature& e, bool sn) { return hookUse(lv, p, e, sn) || (oldUse && oldUse(lv, p, e, sn)); };
  auto oldSpawn = H.naturalSpawn;
  H.naturalSpawn = [oldSpawn](Level& lv, int r, const std::string& t, vec3 at) { return hookNaturalSpawn(lv, r, t, at) && (!oldSpawn || oldSpawn(lv, r, t, at)); };
  registerVillagerUseHook();   // sim/villager_hooks.cpp
  registerChatCommand("villagers", "Bring villagers out in front of you (jobs: farmer,librarian,guard,elder,...)", cmdVillagers, true, "[jobs]");
  registerChatCommand("vrep", "Add standing with the village you're in", cmdRep, true, "[amount]");
  registerChatCommand("raid", "Start a raid on the village you're in", cmdRaid, true);
  // the content hooks: places on maps, waystones, discoveries
  ContentHooks& C = contentHooks();
  if (!C.villageNear) C.villageNear = [](Level& lv, vec3 pos, float radius) -> std::optional<VillageInfo> {
    auto v = villageAtPos(lv, REALM_MAIN, pos, radius);
    if (!v) return std::nullopt;
    VillageInfo o; o.id = v->id; o.name = v->name; o.x = v->x; o.y = v->y; o.z = v->z; o.r = v->r;
    for (auto& b : v->blds) o.blds.push_back(ivec4(b.x0, b.z0, b.x1, b.z1));
    return o;
  };
  if (!C.villageById) C.villageById = [](Level& lv, const std::string& id) -> std::optional<VillageInfo> {
    auto v = villageById(lv, id);
    if (!v) return std::nullopt;
    VillageInfo o; o.id = v->id; o.name = v->name; o.x = v->x; o.y = v->y; o.z = v->z; o.r = v->r;
    for (auto& b : v->blds) o.blds.push_back(ivec4(b.x0, b.z0, b.x1, b.z1));
    return o;
  };
  if (!C.placesNear) C.placesNear = [](Level& lv, int realm, vec3 pos, float pad) {
    std::vector<PlaceInfo> out;
    if (realm != REALM_MAIN || !lv.gen) return out;
    const int dim = dimOfY(ifloor(pos.y));
    const DimGenerator* g = lv.gen->forDim(dim);
    const StructureIndex* idx = nullptr;
    if (auto* o = dynamic_cast<const OverworldGen*>(g)) idx = &o->structures;
    else if (auto* d = dynamic_cast<const DepthsGen*>(g)) idx = &d->structures;
    else if (auto* s = dynamic_cast<const SkylandsGen*>(g)) idx = &s->structures;
    else if (auto* e = dynamic_cast<const EndGen*>(g)) idx = &e->structures;
    if (!idx) return out;
    std::vector<const StructurePlan*> plans;
    idx->near(ifloor(pos.x), ifloor(pos.z), (int)pad, plans);
    for (auto* P : plans) {
      if (P->data.is_object() && P->data.value("kind", std::string()) == "village") continue;   // villages are found by villages.js's rule
      PlaceInfo o; o.id = P->id; o.name = P->name; o.realm = realm; o.x0 = P->x0; o.z0 = P->z0; o.x1 = P->x1; o.z1 = P->z1; o.y = P->y + dimBase(dim);
      out.push_back(o);
    }
    return out;
  };
  if (!C.dungeonById) C.dungeonById = [](Level& lv, const std::string& id) -> std::optional<PlaceInfo> {
    if (!lv.gen) return std::nullopt;
    auto* o = dynamic_cast<const OverworldGen*>(lv.gen->forDim(DIM_OVERWORLD));
    std::smatch m; static const std::regex re("^d(-?\\d+)_(-?\\d+)$");
    if (!o || !std::regex_match(id, m, re)) return std::nullopt;
    const auto& T = structureTypes(); int k = -1;
    for (int i = 0; i < (int)T.size(); i++) if (T[i].key == "dungeon") k = i;
    if (k < 0) return std::nullopt;
    const StructurePlan* P = o->structures.at(k, std::stoi(m[1]), std::stoi(m[2]));
    if (!P || P->id != id) return std::nullopt;
    PlaceInfo r; r.id = P->id; r.name = P->name; r.x0 = P->x0; r.z0 = P->z0; r.x1 = P->x1; r.z1 = P->z1; r.y = P->y + dimBase(DIM_OVERWORLD);
    return r;
  };
}

} // namespace dq
