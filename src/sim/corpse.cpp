// sim/corpse.cpp — see corpse.h.
#include "sim/corpse.h"
#include "sim/gameplay.h"
#include "sim/physics.h"
// (sim/content.h clashes with sim/gameplay.h)
#include "sim/controller.h"
#include <random>

namespace dq {
int jsDimOf(int realm, int y);   // sim/content.cpp

namespace {

// every slot group a body keeps, by the name its record uses (the backpack first: given back first, it is worn again
// before anything that no longer fits its own slot goes into the bags)
struct SlotGroup { const char* key; std::vector<ItemStack> Player::* v; };
const SlotGroup GROUPS[] = {{"back", &Player::back}, {"inv", &Player::inv}, {"armor", &Player::armor}, {"off", &Player::offhand},
                            {"hook", &Player::hook}, {"charm", &Player::charm}, {"cape", &Player::cape}, {"quiver", &Player::quiver}, {"shield", &Player::shield}};
constexpr const char* COMPASS = "death_compass";

float voidY(int realm) { return realm == REALM_MAIN ? -8.f : (float)dimBase(DIM_OVERWORLD) - 8.f; }
json placeOf(int realm, vec3 p) { return {{"realm", realm}, {"dim", jsDimOf(realm, ifloor(p.y))}, {"x", ifloor(p.x)}, {"y", ifloor(p.y)}, {"z", ifloor(p.z)}}; }

// Player::grave (the compass) and stats["grave"] (the maps) follow the oldest body; an old world's grave block keeps
// its pointer until a body takes over
void pointCompass(Player& p) {
  json& B = deathBodies(p);
  if (!B.empty()) { json g = B[0]; g.erase("_m"); p.grave = g; }
  else if (p.grave.is_object() && p.grave.contains("cid")) p.grave = nullptr;
  if (p.grave.is_object()) p.stats["grave"] = p.grave; else p.stats.erase("grave");
}

class Corpse : public Entity {
public:
  Corpse() { type = "corpse"; w = 0.45f; h = 0.35f; persistent = true; hp = maxHp = 1; }
  void relocate(Level& lv) {
    // out of the world or in lava: to where its owner last stood safely, and if that fails too, to the world's spawn
    const json& s = data.value("safe", json());
    if (!data.value("moved", false) && s.is_array() && s.size() == 3) { pos = vec3(s[0].get<float>(), s[1].get<float>() + 0.5f, s[2].get<float>()); data["moved"] = true; }
    else {
      realm = REALM_MAIN;
      pos = lv.info.spawn.y >= 0 ? lv.info.spawn + vec3(0, 1, 0) : vec3(0.5f, dimBase(DIM_OVERWORLD) + 120, 0.5f);
      data["moved"] = 2;
    }
    vel = vec3(0); prevPos = pos;
  }
  void tick(Level& lv, double dtd) override {
    if (lv.isRemote()) return;   // a joined world: the host keeps the bodies
    const float dt = (float)dtd;
    if (pos.y < voidY(realm)) { relocate(lv); return; }
    World& wd = lv.world(realm);
    if (!wd.isLoaded(ifloor(pos.x), clampv(ifloor(pos.y), 0, WORLD_H - 1), ifloor(pos.z))) return;   // (nothing to lie on yet)
    const Contacts c = sampleContacts(wd, *this);
    if (c.lava) { relocate(lv); return; }
    inWater = c.water;
    vel.y -= 20 * dt;
    if (c.water) { vel.y = std::max(vel.y, -1.2f); }
    vel.x *= std::max(0.f, 1 - dt * 4); vel.z *= std::max(0.f, 1 - dt * 4);
    MoveResult r = moveBox(wd, pos, w, h, vel * dt, 0);
    if (r.hitY) vel.y = 0;
    if (r.hitX) vel.x = 0;
    if (r.hitZ) vel.z = 0;
    onGround = r.onGround;
    const std::string owner = data.value("of", std::string());
    const bool empty = data.value("empty", false);
    if (empty) {   // a body with nothing on it: it lies there a while after its owner is up again, then it's gone
      bool ownerDown = false;
      for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!e->removed && p->name == owner && p->dead) ownerDown = true;
      if (!ownerDown && age > 90) { removed = true; lv.fx->particles("poof", pos, 10); }
      return;
    }
    // walking into your own body takes your things back (using it does too: the use hook below)
    if (age > 2) for (auto& e : lv.entities()) {
      auto* p = dynamic_cast<Player*>(e.get());
      if (!p || e->removed || p->dead || p->name != owner || p->realm != realm) continue;
      if (std::fabs(p->pos.x - pos.x) < w + 0.35f && std::fabs(p->pos.z - pos.z) < w + 0.35f && p->pos.y > pos.y - 1.2f && p->pos.y < pos.y + 1.0f) { retrieveCorpse(lv, *p, *this); return; }
    }
  }
};

std::string newCid() {
  static std::mt19937 g{std::random_device{}()};
  char b[48]; snprintf(b, sizeof b, "%lld-%08x", (long long)unixMillis(), (unsigned)g());
  return b;
}

struct CorpseSystem : LevelSystem {
  float t = 0;
  const char* name() const override { return "corpses"; }
  void tick(Level& lv, double dt) override {
    t += (float)dt;
    if (t < 0.5f) return;
    for (auto& e : lv.entities()) if (!e->removed && !e->ghost && e->isPlayer()) syncDeathBodies(lv, static_cast<Player&>(*e), t);
    t = 0;
  }
};

} // namespace

bool isCorpse(const Entity& e) { return e.type == "corpse"; }

json& deathBodies(Player& p) {
  if (!p.stats.is_object()) p.stats = json::object();
  json& b = p.stats["bodies"];
  if (!b.is_array()) b = json::array();
  return b;
}

Entity* spawnCorpse(Level& lv, Player& p, const DamageSource& src, bool withItems) {
  World& w = lv.world(p.realm);
  vec3 at = p.pos;
  // fell out of the world or into lava: the body lies where they last stood safely (a guest's game hands its body to
  // the host where it fell, and the host's game moves it from there: data "safe")
  const bool bad = at.y < voidY(p.realm) || sampleContacts(w, p).lava;
  if (bad && p.hasLastSafe && !lv.isRemote()) at = p.lastSafe;
  auto e = std::make_unique<Corpse>();
  e->realm = p.realm; e->pos = e->prevPos = at; e->yaw = p.yaw;
  json items = json::array(), wear = json::array();
  for (auto& a : p.armor) wear.push_back(a.empty() ? json() : json(a.id));
  if (withItems)
    for (const SlotGroup& g : GROUPS) {
      std::vector<ItemStack>& v = p.*g.v;
      for (size_t i = 0; i < v.size(); i++) {
        if (v[i].empty() || v[i].id == COMPASS) continue;   // (the compass is never left in a body: it shows the way to them)
        items.push_back({{"g", g.key}, {"i", (int)i}, {"s", v[i].toJson()}});
        v[i].clear();
      }
    }
  vec3 kick(p.vel.x, 0, p.vel.z);
  if (Entity* a = src.attacker ? lv.entity(src.attacker) : nullptr) { vec3 d(p.pos.x - a->pos.x, 0, p.pos.z - a->pos.z); if (glm::length(d) > 1e-3f) kick += glm::normalize(d) * 2.5f; }
  const vec3 safe = p.hasLastSafe ? p.lastSafe : at;
  const std::string cid = newCid();
  e->data = {{"of", p.name}, {"skin", p.skin}, {"cid", cid}, {"items", items}, {"empty", items.empty()}, {"wear", wear},
             {"at", unixMillis()}, {"safe", {safe.x, safe.y, safe.z}}, {"rk", {std::round(kick.x * 100) / 100, 1.0, std::round(kick.z * 100) / 100}}};
  if (!p.cape.empty() && !p.cape[0].empty()) e->data["cape"] = p.cape[0].id;
  Entity* ent = lv.spawn(std::move(e));
  if (!items.empty()) { json b = placeOf(p.realm, at); b["cid"] = cid; deathBodies(p).push_back(b); }
  pointCompass(p);
  return ent;
}

bool retrieveCorpse(Level& lv, Player& p, Entity& c) {
  if (!isCorpse(c) || c.removed) return false;
  const std::string owner = c.data.value("of", std::string());
  if (owner != p.name) { toast(lv, p, "This is " + owner + "'s body"); return false; }
  const std::string cid = c.data.value("cid", std::string());
  json& B = deathBodies(p);
  for (size_t i = 0; i < B.size(); i++) if (B[i].value("cid", std::string()) == cid) { B.erase(B.begin() + i); break; }
  // the compass steps aside (it comes back after, if other bodies are still out there)
  int compasses = 0;
  for (const SlotGroup& g : GROUPS) for (auto& s : p.*g.v) if (!s.empty() && s.id == COMPASS) { compasses++; s.clear(); }
  std::vector<std::pair<const SlotGroup*, ItemStack>> rest;
  const json items = c.data.value("items", json::array());
  for (const SlotGroup& g : GROUPS)   // the backpack's slot first (GROUPS order), then the rest
    for (auto& it : items) {
      if (!it.is_object() || it.value("g", std::string()) != g.key) continue;
      ItemStack s = ItemStack::fromJson(it.value("s", json()));
      if (s.empty()) continue;
      std::vector<ItemStack>& v = p.*g.v;
      const int i = it.value("i", -1);
      if (i >= 0 && i < (int)v.size() && v[i].empty()) v[i] = s;   // its own slot
      else rest.push_back({&g, s});
    }
  for (auto& [g, s] : rest) {   // its slot is taken now: a free one (the bags, then a worn backpack), else at your feet
    int left = p.give(s);
    if (left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 0.5f, 0), false); }
  }
  if (!B.empty() && compasses > 0 && reg().item(COMPASS)) {
    int left = p.give(ItemStack(COMPASS, 1));
    if (left > 0) dropItem(lv, p.realm, ItemStack(COMPASS, 1), p.pos + vec3(0, 0.5f, 0), false);
  }
  pointCompass(p);
  c.removed = true;
  sfxUi(lv, p, "chest");
  toast(lv, p, B.empty() ? "You got your things back" : "You got your things back (another body is still out there)", "good");
  lv.fx->particles("motes", c.pos + vec3(0, 0.3f, 0), 12, {{"tile", "portal_mote_sky"}});
  if (lv.events) lv.events->emit("corpseRetrieved", json::array({p.name, cid}));
  return true;
}

void syncDeathBodies(Level& lv, Player& p, float dt) {
  json& B = deathBodies(p);
  if (B.empty()) { pointCompass(p); return; }
  std::unordered_map<std::string, Entity*> bodies;
  for (auto& e : lv.entities()) if (!e->removed && isCorpse(*e)) bodies[e->data.value("cid", std::string())] = e.get();
  for (size_t i = 0; i < B.size();) {
    json& b = B[i];
    auto it = bodies.find(b.value("cid", std::string()));
    if (it != bodies.end()) {   // it's there: follow it if it moved (fell, or was carried out of the lava)
      Entity& c = *it->second;
      if (std::abs(b.value("x", 0) - ifloor(c.pos.x)) + std::abs(b.value("y", 0) - ifloor(c.pos.y)) + std::abs(b.value("z", 0) - ifloor(c.pos.z)) > 0 || b.value("realm", 0) != c.realm) {
        json n = placeOf(c.realm, c.pos); n["cid"] = b["cid"]; b = n;
      }
      b.erase("_m"); i++; continue;
    }
    if (!lv.isRemote()) { B.erase(B.begin() + i); continue; }   // the host has every body: gone means taken back
    // a joined world only sees bodies near you: one you stand by that isn't there any more was taken back
    const bool near = b.value("realm", 0) == p.realm && std::hypot(b.value("x", 0) + 0.5f - p.pos.x, b.value("z", 0) + 0.5f - p.pos.z) < 6 &&
                      std::fabs(b.value("y", 0) - p.pos.y) < 6 && lv.world(p.realm).isLoaded(b.value("x", 0), clampv(b.value("y", 0), 0, WORLD_H - 1), b.value("z", 0));
    if (near) { b["_m"] = b.value("_m", 0.f) + dt; if (b.value("_m", 0.f) > 3) { B.erase(B.begin() + i); continue; } }
    else b.erase("_m");
    i++;
  }
  pointCompass(p);
}

void registerCorpses() {
  static bool done = false;
  if (done) return;
  done = true;
  registerEntityFactory("corpse", [](const std::string&) { return std::make_unique<Corpse>(); });
  hooks().useEntity.insert(hooks().useEntity.begin(), [](PlayerController& pc, Entity& e, ItemStack*, const ItemDef*) {
    if (!isCorpse(e)) return false;
    if (useOnHost(pc, "ent", nullptr, e.id)) return true;   // a joined world: the host gives it back (and the gear with our bags)
    pc.swingArm();
    retrieveCorpse(pc.lv, pc.p, e);
    return true;
  });
  registerLevelSystem([](Level&) { return std::make_shared<CorpseSystem>(); });
}

} // namespace dq
