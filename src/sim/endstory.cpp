// sim/endstory.cpp — the EndStory system: ticks the End, the dragons, the Deep Dark, the watcher and the rift
// (each in its own file), routes the game's events to them (itemUse, mobDeath, block changes, villageEnter) and
// holds the helpers they share. See sim/endstory.h.
#include "sim/endstory_int.h"
#include "sim/physics.h"
#include "sim/portals.h"
#include "sim/commands.h"
#include "gen/dimgens.h"
#include "gen/overworld.h"

namespace dq {
namespace es {

// ------------------------------------------------------------------ helpers
json& sub(json& j, const char* k, bool array) {
  if (!j.is_object()) j = json::object();
  json& v = j[k];
  if (array ? !v.is_array() : !v.is_object()) v = array ? json::array() : json::object();
  return v;
}
double num(const json& j, const char* k, double d) { if (!j.is_object()) return d; auto it = j.find(k); return it != j.end() && it->is_number() ? it->get<double>() : d; }
int jint(const json& j, const char* k, int d) { return (int)num(j, k, d); }
vec3 jvec(const json& j, vec3 d) { return j.is_array() && j.size() >= 3 && j[0].is_number() ? vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : d; }
float rnd() { return frand(); }

void say(Level& lv, u32 id, const std::string& text, const std::string& kind) { lv.fx->message(id, text, kind.empty() ? "info" : kind); }
std::vector<Player*> livePlayers(Level& lv, int realm) {
  std::vector<Player*> out;
  for (Player* p : levelPlayers(lv)) if (!p->dead && !p->removed && (realm < 0 || p->realm == realm)) out.push_back(p);
  return out;
}
void sayNear(Level& lv, int realm, vec3 at, float r, const std::string& text, const std::string& kind) {
  for (Player* p : livePlayers(lv, realm)) if (r <= 0 || std::hypot(p->pos.x - at.x, p->pos.z - at.z) < r) say(lv, p->id, text, kind);
}
void sayAll(Level& lv, const std::string& text, const std::string& kind) { for (Player* p : levelPlayers(lv)) say(lv, p->id, text, kind); }

Creature* spawnMob(Level& lv, int realm, const std::string& type, vec3 at) { return isCreatureType(type) ? spawnCreature(lv, realm, type, at) : nullptr; }

void teleport(Level& lv, Entity& e, vec3 to) {
  e.pos = e.prevPos = to; e.vel = vec3(0); e.fallDist = 0;
  if (auto* ps = portalSystem(lv)) ps->forget(e.id);
}
void particles(Level& lv, const std::string& tile, vec3 at, int n, float speed, float grav) {
  lv.fx->particles(tile, at, n, json{{"speed", speed}, {"grav", grav}});
}

bool lineOfSight(Level& lv, int realm, vec3 eye, vec3 to, float d) {
  World& w = lv.world(realm);
  vec3 dir = to - eye; float L = glm::length(dir); if (L < 1e-3f) return true;
  if (d <= 0) d = L;
  RayHit h = raycastBlocks(w, glm::dvec3(eye), dir / L, d - 0.6f, false);
  return !h.hit || isLeaves(h.id) || h.dist > d - 0.8f;
}

int standY(World& w, float x, float z, float py) {
  int fx = ifloor(x), fz = ifloor(z), y0 = ifloor(py + 0.01f);
  for (int dy : {0, -1, 1, -2, 2, -3, 3, -4, 4, -5, -6}) {
    int y = y0 + dy;
    if (y < 1 || y > WORLD_H - 3) continue;
    if (solid(w.getBlock(fx, y - 1, fz)) && !solid(w.getBlock(fx, y, fz)) && !solid(w.getBlock(fx, y + 1, fz)) && !liquid(w.getBlock(fx, y, fz))) return y;
  }
  return -1;
}

bool hurtPawn(Level& lv, Entity& who, float amount, Entity* by, const std::string& type, float knock, const std::string& element) {
  DamageSrc s; s.knock = knock; s.element = element;
  return damageEntity(lv, who, amount, by, type, s) > 0;
}

std::vector<Plan> plansNear(Level& lv, int dim, const std::string& key, int x, int z, int pad) {
  std::vector<Plan> out;
  if (!lv.gen) return out;
  DimGenerator* g = lv.gen->forDim(dim);
  const StructureIndex* idx = nullptr;
  if (auto* o = dynamic_cast<OverworldGen*>(g)) idx = &o->structures;
  else if (auto* d = dynamic_cast<DepthsGen*>(g)) idx = &d->structures;
  else if (auto* s = dynamic_cast<SkylandsGen*>(g)) idx = &s->structures;
  else if (auto* e = dynamic_cast<EndGen*>(g)) idx = &e->structures;
  if (!idx || !idx->active()) return out;
  int type = -1;
  const auto& T = structureTypes();
  for (size_t i = 0; i < T.size(); i++) if (T[i].key == key) { type = (int)i; break; }
  if (type < 0) return out;
  std::vector<const StructurePlan*> v;
  idx->near(x, z, pad, v, type);
  for (const StructurePlan* p : v) {
    Plan q; q.id = p->id; q.name = p->name; q.dim = dim; q.x0 = p->x0; q.z0 = p->z0; q.x1 = p->x1; q.z1 = p->z1;
    q.y = p->y + dimBase(dim); q.cx = p->cx; q.cz = p->cz; q.data = p->data;
    out.push_back(std::move(q));
  }
  return out;
}

const json& mobType(const std::string& key) {
  static const json none = json::object();
  const json& M = reg().mobs;
  if (M.is_object()) { auto it = M.find(key); if (it != M.end()) return *it; }
  return none;
}
std::string capLabel(const std::string& label) { std::string s = label; if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]); return s; }

// ------------------------------------------------------------------ block changes from every realm
class StoryWorldHook : public WorldSystem {
public:
  explicit StoryWorldHook(Level& l) : lv(l) {}
  const char* name() const override { return "endstory"; }
  void onBlockChanged(World& w, int x, int y, int z, BlockId o, BlockId n) override {
    if (lv.isRemote() || o == n) return;
    endBlockChanged(lv, w, x, y, z, o, n);
    dragonsBlockChanged(lv, w, x, y, z, o, n);
    deepBlockChanged(lv, w, x, y, z, o, n);
  }
  void onChunkUnloading(World& w, Chunk& c) override {
    if (auto* S = endStory(lv)) S->sculkCells.erase(((u64)(u32)c.cx << 32) | (u32)c.cz);
  }
  Level& lv;
};

// ------------------------------------------------------------------ the system
EndStory::EndStory(Level& l) : lv(l) {
  if (!lv.events) return;
  handlers.push_back(lv.events->on("itemUse", [this](const json& a) {
    if (lv.isRemote() || !a.is_array() || a.size() < 6) return true;
    Player* p = findPlayer(lv, a[0].get<std::string>());
    if (!p || p->dead) return true;
    std::string item = a[1].is_string() ? a[1].get<std::string>() : "";
    ivec3 hit(a[2].get<int>(), a[3].get<int>(), a[4].get<int>()); int face = a[5].get<int>();
    if (riftItemUse(lv, *this, *p, item, hit, face)) return false;
    if (deepItemUse(lv, *this, *p, item, hit, face)) return false;
    if (endItemUse(lv, *this, *p, item, hit, face)) return false;
    return true;
  }, -10));
  handlers.push_back(lv.events->on("mobDeath", [this](const json& a) {
    if (lv.isRemote() || !a.is_array() || a.empty() || !a[0].is_object()) return true;
    auto* c = dynamic_cast<Creature*>(lv.entity((u32)num(a[0], "id", 0)));
    if (!c) return true;
    Entity* killer = a.size() > 1 && a[1].is_string() ? findPlayer(lv, a[1].get<std::string>()) : nullptr;
    endOnDeath(lv, *this, *c, killer);
    dragonsOnDeath(lv, *this, *c, killer);
    deepOnDeath(lv, *this, *c, killer);
    riftOnDeath(lv, *this, *c, killer);
    return true;
  }));
  handlers.push_back(lv.events->on("villageEnter", [this](const json& a) {
    if (!a.is_array() || a.size() < 2) return true;
    if (Player* p = findPlayer(lv, a[0].get<std::string>())) riftVillageEnter(lv, *p, a[1].is_string() ? a[1].get<std::string>() : "", a.size() > 2 && a[2].is_string() ? a[2].get<std::string>() : "");
    return true;
  }));
}
EndStory::~EndStory() { if (lv.events) for (int h : handlers) lv.events->off(h); }

PlayerMem& EndStory::pm(const Player& p) { return pms[p.id]; }

EndStory* endStory(Level& lv) { return lv.system<EndStory>(); }

void EndStory::tick(Level& l, double dt) {
  now += dt;
  for (auto& [r, w] : l.allRealms()) if (!w->system<StoryWorldHook>()) w->addSystem(std::make_shared<StoryWorldHook>(l));
  if (!timers.empty()) {
    std::vector<Timer> due;
    for (size_t i = 0; i < timers.size();) { if (timers[i].at <= now) { due.push_back(std::move(timers[i])); timers.erase(timers.begin() + i); } else i++; }
    for (auto& t : due) t.fn(l);
  }
  if (l.isRemote()) return;
  endFightTick(l, *this, dt);
  dragonsTick(l, *this, dt);
  deepTick(l, *this, dt);
  hauntTick(l, *this, dt);
  riftTick(l, *this, dt);
  // forget the dead
  static double sweep = 0;
  if ((sweep -= dt) <= 0) {
    sweep = 5;
    for (auto it = mems.begin(); it != mems.end();) { Entity* e = l.entity(it->first); if (!e || e->removed) it = mems.erase(it); else ++it; }
    for (auto it = pms.begin(); it != pms.end();) { if (!l.entity(it->first)) it = pms.erase(it); else ++it; }
  }
}

} // namespace es

using namespace es;

json& riftRec(Level& lv) {
  json& R = lv.info.data["rift"];
  if (!R.is_object()) R = json{{"stage", 0}, {"frags", 0}};
  return R;
}
int riftStability(Level& lv) {
  const json& R = riftRec(lv);
  if (truthy(R, "sealed") || !jint(R, "stage")) return 100;
  if (jint(R, "stage") >= 3) return 5;
  return std::max(8, 100 - 26 * std::min(RIFT_NEED, jint(R, "frags")));
}
int riftTier(Level& lv) {
  const json& R = riftRec(lv);
  if (truthy(R, "sealed")) return 5;
  int st = jint(R, "stage");
  if (st >= 3) return 4;
  if (st >= 1) { int f = jint(R, "frags"); return f >= 3 ? 3 : f >= 1 ? 2 : 1; }
  const json& F = lv.info.data.contains("flags") ? lv.info.data["flags"] : json::object();
  return jint(F, "watcherSeen") ? 0 : -1;
}
json& endRec(Level& lv) {
  json& E = lv.info.data["end"];
  if (!E.is_object()) E = json{{"slain", 0}, {"gates", 0}, {"crystalsGone", json::array()}};
  if (!E.contains("crystalsGone") || !E["crystalsGone"].is_array()) E["crystalsGone"] = json::array();
  return E;
}

void registerEndStory() {
  static bool done = false;
  if (done) return;
  done = true;
  registerEndCreatures();
  registerDragons();
  registerDeepDark();
  registerHaunt();
  registerRift();
  registerLevelSystem([](Level& lv) { return std::make_shared<EndStory>(lv); });
}

} // namespace dq
