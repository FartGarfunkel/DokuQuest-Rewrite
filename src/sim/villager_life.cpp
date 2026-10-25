// sim/villager_life.cpp — village life beyond the people's day: the town gates (towns3.js: shut at dusk with the
// evening bell, opened for you at night by the watch, levers), raids and named foes (livingworld.js), the missing
// villager and a Hero's house (hearth.js), the welcome, the rift's word as you walk in (riftlore.js), and the
// moment a path opens to you (paths.js).
#include "sim/villagers_internal.h"
#include "sim/places.h"
#include "sim/skills.h"
#include "sim/rpg.h"
#include "gen/overworld.h"
#include "gen/structures.h"
#include "gen/generator.h"
#include "world/dims.h"
#include "core/registry.h"
#include <deque>

namespace dq {

static float rnd() { return frand(); }
static BlockId B(const char* k) { return reg().blockId(k); }
constexpr int GATE_H = 5;

// ------------------------------------------------------------------ the gates
const std::vector<VGate>& gatesOf(Level&, const Village& v) { return v.gates; }
static bool gateNight(Level& lv) { double ph = npcPhase(lv); return ph >= VD().value("gateShut", 0.535) && ph < VD().value("gateOpen", 0.985); }
bool gateShutAt(Level& lv, const VGate& G) {
  if (G.cells.empty()) return false;
  const ivec3 c = G.cells[std::min(G.cells.size() - 1, (G.cells.size() / GATE_H / 2) * GATE_H)];
  static const BlockId bar = B("gate_barrier");
  return bar && lv.world(REALM_MAIN).getBlock(c.x, c.y, c.z) == bar;
}
static bool gateBlocked(Level& lv, const VGate& G) {
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (auto& c : G.cells) { x0 = std::min(x0, (float)c.x); x1 = std::max(x1, (float)c.x); z0 = std::min(z0, (float)c.z); z1 = std::max(z1, (float)c.z); }
  x0 -= 0.4f; x1 += 1.4f; z0 -= 0.4f; z1 += 1.4f; float y0 = G.h0 + 0.5f, y1 = G.h0 + GATE_H + 1.f;
  for (auto& up : lv.entities()) {
    Entity& e = *up;
    if (e.removed || e.realm != REALM_MAIN) continue;
    if (auto* p = dynamic_cast<Player*>(&e)) { if (p->dead) continue; }
    else if (auto* c = dynamic_cast<Creature*>(&e)) { if (!c->alive()) continue; }
    if (e.pos.x > x0 && e.pos.x < x1 && e.pos.z > z0 && e.pos.z < z1 && e.pos.y > y0 - 1 && e.pos.y < y1) return true;
  }
  return false;
}
static bool gateSet(Level& lv, const VGate& G, bool open, bool sound) {
  World& w = lv.world(REALM_MAIN);
  static const BlockId bar = B("gate_barrier");
  if (!bar) return false;
  if (!open && gateBlocked(lv, G)) return false;
  bool any = false;
  for (auto& c : G.cells) {
    if (!w.isLoaded(c.x, c.y, c.z)) continue;
    BlockId id = w.getBlock(c.x, c.y, c.z);
    if (open && id == bar) { w.setBlock(c.x, c.y, c.z, AIR, 0); any = true; }
    else if (!open && (id == AIR || reg().tReplaceable[id])) { w.setBlock(c.x, c.y, c.z, bar, 0); any = true; }
  }
  if (any && sound) lv.fx->sound(G.stone ? "door_iron" : open ? "door_open" : "door_close", G.center + vec3(0, 2, 0), 32);
  return any;
}
static void gateDuskNews(Level& lv, VillagerSystem& S, const Village& v) {
  std::string key = v.id + ":" + std::to_string(npcDay(lv));
  if (S.lastDusk == key) return;
  S.lastDusk = key;
  for (Player* p : levelPlayers(lv)) if (std::hypot(p->pos.x - v.x, p->pos.z - v.z) < (v.r ? v.r : 60) + 40) toast(lv, *p, "The evening bell: the gates of " + v.name + " shut for the night");
  lv.fx->sound("bell", vec3(v.x + 0.5f, v.y + 6, v.z + 0.5f), 64);
}
void gatesTick(Level& lv, VillagerSystem& S, double dt) {
  if ((S.gateT -= (float)dt) > 0) return;
  S.gateT = 0.5f;
  const bool night = gateNight(lv);
  const bool turned = S.gateNightWas >= 0 && S.gateNightWas != (int)night;
  S.gateNightWas = night;
  World& w = lv.world(REALM_MAIN);
  std::vector<Player*> pawns; for (Player* p : levelPlayers(lv)) if (!p->dead && p->realm == REALM_MAIN) pawns.push_back(p);
  std::set<std::string> seenNow;
  for (Player* p : pawns) for (auto& v : villagesNear(lv, p->pos.x, p->pos.z, 48)) {
    if (!v->hasWall) continue;
    for (const VGate& G : v->gates) {
      const std::string& k = G.key;
      if (seenNow.count(k)) continue;
      seenNow.insert(k);
      if (!w.isLoaded(G.cx, G.h0, G.cz)) continue;
      // worked by hand: the town leaves it be for a while
      if (auto it = S.gateManual.find(k); it != S.gateManual.end()) { if (it->second - 0.5f > 0) { it->second -= 0.5f; continue; } S.gateManual.erase(it); }
      if (!S.gateSeen.count(k)) { S.gateSeen[k] = 1; if (!gateSet(lv, G, !night, false) && night) S.gatePending[k] = 1; continue; }
      if (turned) { if (gateSet(lv, G, !night, true) && night) gateDuskNews(lv, S, *v); else if (night) S.gatePending[k] = 1; continue; }
      if (S.gatePending.count(k) && night && !S.gateHeld.count(k)) { if (gateSet(lv, G, false, true)) S.gatePending.erase(k); continue; }
      if (!night) { if (gateShutAt(lv, G)) gateSet(lv, G, true, false); continue; }
      // the watch opens up for the party at night, and shuts it behind them
      std::vector<Player*> near;
      for (Player* q : pawns) if (std::hypot(q->pos.x - G.center.x, q->pos.z - G.center.z) < 4.5f + G.hw && std::fabs(q->pos.y - G.center.y) < 4) near.push_back(q);
      auto held = S.gateHeld.find(k);
      if (!near.empty()) {
        Creature* guard = nullptr;
        for (auto& up : lv.entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (const json* N = npcOf(*c))
          if (c->type == "village_guard" && c->alive() && !c->removed && N->value("v", "") == v->id && std::hypot(c->pos.x - G.center.x, c->pos.z - G.center.z) < 16) { guard = c; break; }
        if (held == S.gateHeld.end() && guard && gateShutAt(lv, G)) {
          if (gateSet(lv, G, true, true)) {
            S.gateHeld[k] = 4;
            for (Player* q : near) toast(lv, *q, npcOf(*guard)->value("n", std::string("A")) + " the guard opens the " + G.name + " for you");
            guard->yaw = std::atan2(-(near[0]->pos.x - guard->pos.x), -(near[0]->pos.z - guard->pos.z));
          }
        } else if (held != S.gateHeld.end()) held->second = 4;
      } else if (held != S.gateHeld.end()) {
        float t = held->second - 0.5f;
        if (t <= 0) { if (gateSet(lv, G, false, true)) S.gateHeld.erase(held); else held->second = 1; }
        else held->second = t;
      }
    }
  }
}
bool gateUse(Level& lv, Player& p, ivec3 at, bool lever) {
  VillagerSystem* S = vsys(lv);
  if (!S || p.realm != REALM_MAIN) return false;
  for (auto& v : villagesNear(lv, (float)at.x, (float)at.z, lever ? 4.f : 2.f)) for (const VGate& G : v->gates) {
    if (lever) {
      if (G.lever.x != at.x || G.lever.z != at.z || std::abs(G.lever.y - at.y) > 1) continue;
      bool shut = gateShutAt(lv, G);
      bool ok = gateSet(lv, G, shut, true);
      if (ok) { S->gateManual[G.key] = 90; S->gateHeld.erase(G.key); }
      if (!(ok || (!shut && gateShutAt(lv, G)))) toast(lv, p, "Something is in the way of the gate");
      return true;
    }
    bool in = false; for (auto& c : G.cells) if (c.x == at.x && c.z == at.z) in = true;
    if (!in || at.y <= G.h0 || at.y > G.h0 + GATE_H) continue;
    gateSet(lv, G, true, true);
    if (gateNight(lv)) S->gateHeld[G.key] = 8;
    S->gateManual.erase(G.key);
    return true;
  }
  return false;
}
VillageRef boardVillage(Level& lv, int realm, ivec3 at) { return villageAtPos(lv, realm, vec3(at) + vec3(0.5f), 8); }

// ------------------------------------------------------------------ raids (livingworld.js)
static json& raidRec(Level& lv, const std::string& vid) { json& m = lv.info.data["raids"]; if (!m.is_object()) m = json::object(); json& r = m[vid]; if (!r.is_object()) r = {{"last", -99}}; return r; }
bool raidStart(Level& lv, const Village& v, double st, bool sealed) {
  VillagerSystem* S = vsys(lv); if (!S) return false;
  World& w = lv.world(REALM_MAIN);
  int n = 4 + (int)std::floor(rnd() * 3) + (int)std::floor((100 - st) / 30);
  // they gather somewhere the world is actually there; failing that, nearer the square
  float a = rnd() * TAU, R = 46, cx = 0, cz = 0;
  for (int k = 0; k < 16; k++) {
    if (k == 8) R = 26;
    float ta = a + k * 0.785f; cx = v.x + std::cos(ta) * R; cz = v.z + std::sin(ta) * R;
    if (w.isLoaded(ifloor(cx), v.y, ifloor(cz)) && findSafeY(w, ifloor(cx), ifloor(cz), DIM_OVERWORLD) > 0) break;
  }
  std::vector<u32> mobs;
  for (int i = 0; i < n; i++) {
    float x = cx + (rnd() - 0.5f) * 8, z = cz + (rnd() - 0.5f) * 8;
    if (!w.isLoaded(ifloor(x), v.y, ifloor(z))) continue;
    int y = findSafeY(w, ifloor(x), ifloor(z), DIM_OVERWORLD);
    if (y < 1) continue;
    bool rift = !sealed && st < 60 && rnd() < (100 - st) / 160;
    static const char* RT[3] = {"rift_ghoul", "rift_skeleton", "rift_spider"};
    std::string type = rift ? RT[(int)std::floor(rnd() * 3)] : (rnd() < 0.15f && creatureType("witch") ? "witch" : "raider");
    if (!creatureType(type)) continue;
    if (Creature* m = spawnCreature(lv, REALM_MAIN, type, vec3(ifloor(x) + 0.5f, y, ifloor(z) + 0.5f))) { m->data["raid"] = v.id; m->huntT = 120; m->persistent = true; mobs.push_back(m->id); }
  }
  if (mobs.empty()) return false;
  lv.info.data["raidKills"][v.id] = 0;
  S->raid = {}; S->raid.on = true; S->raid.vid = v.id; S->raid.name = v.name; S->raid.mobs = mobs; S->raid.n = (int)mobs.size();
  lv.fx->sound("rift_boom", vec3(cx, v.y + 2, cz), 140);
  for (Player* p : levelPlayers(lv)) if (std::hypot(p->pos.x - v.x, p->pos.z - v.z) < 120) toast(lv, *p, "Drums in the dark! A raid is coming for " + v.name + "!", "boss");
  return true;
}
void raidTick(Level& lv, VillagerSystem& S, double dt) {
  auto& R = S.raid;
  if (R.on) {
    R.t += dt;
    VillageRef v = villageById(lv, R.vid);
    int left = 0;
    for (u32 id : R.mobs) {
      auto* m = dynamic_cast<Creature*>(lv.entity(id));
      if (!m || m->removed || !m->alive()) continue;
      left++;
      // they head for the village square (unless someone's close enough to fight)
      if (v && !nearestPawn(lv, m->realm, m->pos, 20)) {
        float d = std::hypot(v->x - m->pos.x, v->z - m->pos.z);
        if (d > 5) { float k = std::min(1.f, (float)dt * 4), sp = m->T->speed * 0.8f; m->vel.x += ((v->x - m->pos.x) / d * sp - m->vel.x) * k; m->vel.z += ((v->z - m->pos.z) / d * sp - m->vel.z) * k; m->yaw = std::atan2(-(v->x - m->pos.x), -(v->z - m->pos.z)); }
      }
    }
    if (left && R.t < 600) return;
    R.on = false;
    // won only when every raider was actually brought down
    int killed = lv.info.data.value("raidKills", json::object()).value(R.vid, 0);
    lv.info.data["raidKills"].erase(R.vid);
    if (left || killed < R.n) {
      for (u32 id : R.mobs) if (Entity* e = lv.entity(id)) e->removed = true;
      for (Player* p : levelPlayers(lv)) toast(lv, *p, "The raiders melt back into the dark around " + R.name);
      return;
    }
    for (Player* p : levelPlayers(lv)) {
      if (p->dead || !v || std::hypot(p->pos.x - v->x, p->pos.z - v->z) >= 90) continue;
      toast(lv, *p, "The raid on " + R.name + " is beaten! The village thanks you.", "epic");
      vGive(lv, *p, ItemStack("emerald", 4 + R.n));
      grantXp(lv, *p, 40 + R.n * 8, p->pos + vec3(0, 1, 0));
      npcRep(lv, *p, R.vid, 15, "raid");
      sfxUi(lv, *p, "levelup");
    }
    return;
  }
  if ((S.raidCheckT -= (float)dt) > 0) return;
  S.raidCheckT = 40;
  if (lv.info.rules.value("mobSpawning", true) == false) return;
  if (!(envDay(lv, DIM_OVERWORLD) < 0.25f)) return;   // only in the dark
  for (Player* p : levelPlayers(lv)) {
    if (p->dead) continue;
    VillageRef v = villageAtPos(lv, p->realm, p->pos, 64); if (!v) continue;
    json& rec = raidRec(lv, v->id); const i64 day = npcDay(lv);
    if (day - rec.value("last", (i64)-99) < 3) continue;
    const json Rf = lv.info.data.value("rift", json::object());
    bool sealed = Rf.is_object() && Rf.value("sealed", 0);
    double st = riftStability(lv);
    double chance = sealed ? 0.05 : 0.08 + (100 - st) / 100 * 0.18;
    if (rnd() > chance) continue;
    rec["last"] = day;
    raidStart(lv, *v, st, sealed);
    return;
  }
}

// ------------------------------------------------------------------ named foes (livingworld.js)
static std::deque<CreatureType> g_eliteTypes;   // each named foe's own numbers (the original cloned m.T)
static void eliteSpawn(Level& lv, const json& q, Player* who) {
  json& E = lv.info.data["elites"]; if (!E.is_object()) E = json::object();
  std::string id = q.value("id", ""), type = q.value("etype", "");
  const CreatureType* T0 = creatureType(type);
  if (E.value(id, std::string()) == "dead" || !T0) return;
  for (auto& up : lv.entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (c->alive() && !c->removed && c->data.value("elite", "") == id) return;
  World& w = lv.world(REALM_MAIN);
  int qx = q["at"][0], qz = q["at"][1];
  if (!w.isLoaded(qx, dimBase(DIM_OVERWORLD) + 64, qz)) return;
  int sx = qx, sz = qz, y = findSafeY(w, qx, qz, DIM_OVERWORLD);
  for (int k = 0; y < 1 && k < 24; k++) { float a = k * 0.9f, r = 3.f + k; sx = ifloor(qx + std::cos(a) * r); sz = ifloor(qz + std::sin(a) * r); if (w.isLoaded(sx, dimBase(DIM_OVERWORLD) + 64, sz)) y = findSafeY(w, sx, sz, DIM_OVERWORLD); }
  if (y < 1) return;
  Creature* m = spawnCreature(lv, REALM_MAIN, type, vec3(sx + 0.5f, y, sz + 0.5f));
  if (!m) return;
  const float k = 1 + 0.25f * q.value("elvl", 1);
  g_eliteTypes.push_back(*T0);
  CreatureType& T = g_eliteTypes.back();
  T.boss = true; T.label = q.value("ename", T.label); T.hp = std::round((T0->hp ? T0->hp : 20) * 5 * k);
  T.damage = std::round((T0->damage ? T0->damage : 3) * 1.7f * k * 2) / 2; T.speed = (T0->speed ? T0->speed : 2.5f) * 1.15f;
  T.knockResist = std::max(T0->knockResist, 0.5f); T.hasTint = true; T.tint = vec4(1, 0.25f, 0.15f, 0.25f); T.xp = (T0->xp ? T0->xp : 5) * 8;
  static const char* PRIZE[4] = {"steel_sword", "mithril_sword", "steel_chestplate", "golden_apple"};
  std::vector<CreatureType::Drop> extra = {{"emerald", 4, 10, 1}, {"gold_ingot", 1, 4, 0.8f}, {"diamond", 1, 2, 0.35f}, {"heart_crystal", 1, 1, 0.12f}, {PRIZE[(int)std::floor(rnd() * 4)], 1, 1, 0.5f}};
  for (auto& d : extra) if (reg().item(d.id)) T.drops.push_back(d);
  m->T = &T;
  m->maxHp = m->hp = T.hp;
  m->data["elite"] = id; m->persistent = true; m->huntT = 60;
  if (who) toast(lv, *who, q.value("ename", std::string("It")) + " is here!", "boss");
  lv.fx->sound("rift_boom", m->pos + vec3(0, 2, 0), 60);
}
void eliteSlain(Level& lv, Creature& m) {
  std::string id = m.data.value("elite", "");
  json& E = lv.info.data["elites"]; if (!E.is_object()) E = json::object();
  E[id] = "dead";
  for (Player* p : levelPlayers(lv)) for (auto& q : npcStats(*p)["quests"]) if (q.value("id", "") == id && !q.value("have", 0)) {
    q["have"] = 1; toast(lv, *p, q.value("ename", std::string("It")) + " is dead. Back to the board for your bounty.", "epic"); questHudChanged(lv, *p);
  }
}
void eliteTick(Level& lv, VillagerSystem& S, double dt) {
  if ((S.eliteT -= (float)dt) > 0) return;
  S.eliteT = 2;
  for (Player* p : levelPlayers(lv)) {
    if (p->realm != REALM_MAIN || dimOfY(ifloor(p->pos.y)) != DIM_OVERWORLD) continue;
    json L = npcStats(*p)["quests"];
    for (auto& q : L) if (q.value("kind", "") == "elite" && !q.value("have", 0) && q.contains("at") && std::hypot(p->pos.x - q["at"][0].get<float>(), p->pos.z - q["at"][1].get<float>()) <= 56) eliteSpawn(lv, q, p);
  }
}

// ------------------------------------------------------------------ the missing (hearth.js)
static void lostSpawn(Level& lv, const json& q, Player* who) {
  json& M = lv.info.data["lost"]; if (!M.is_object()) M = json::object();
  std::string id = q.value("id", "");
  if (M.contains(id)) return;
  for (auto& up : lv.entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (c->alive() && !c->removed && c->data.value("lostQ", "") == id) return;
  World& w = lv.world(REALM_MAIN);
  int qx = q["at"][0], qz = q["at"][1];
  if (!w.isLoaded(qx, dimBase(DIM_OVERWORLD) + 64, qz)) return;
  int sx = qx, sz = qz, y = findSafeY(w, qx, qz, DIM_OVERWORLD);
  for (int k = 0; y < 1 && k < 24; k++) { float a = k * 0.9f, r = 3.f + k; sx = ifloor(qx + std::cos(a) * r); sz = ifloor(qz + std::sin(a) * r); if (w.isLoaded(sx, dimBase(DIM_OVERWORLD) + 64, sz)) y = findSafeY(w, sx, sz, DIM_OVERWORLD); }
  if (y < 1) return;
  Creature* e = spawnCreature(lv, REALM_MAIN, "villager", vec3(sx + 0.5f, y, sz + 0.5f));
  if (!e) return;
  u32 seed = (u32)(q.value("ls", 0) % 100000);
  std::string nm = q.value("lname", npcName(seed)); if (nm.size() > 30) nm.resize(30);
  e->data["npc"] = {{"v", q.value("vid", "")}, {"i", 300}, {"n", nm}, {"j", "none"}, {"s", seed}, {"p", 0}};
  e->data["lostQ"] = id; e->persistent = true; e->kept = false;
  if (VillagerSystem* S = vsys(lv)) S->mind(e->id).lostQ = id;
  // and what's keeping them there
  bool night = envDay(lv, DIM_OVERWORLD) < 0.3f;
  int n = 2 + (night ? 2 : 0) + (int)std::floor(rnd() * 2);
  static const char* MT[4] = {"ghoul", "skeleton", "spider", "ghoul"};
  for (int i = 0; i < n; i++) {
    float a = rnd() * TAU, r = 5 + rnd() * 4, x = sx + std::cos(a) * r, z = sz + std::sin(a) * r;
    if (!w.isLoaded(ifloor(x), dimBase(DIM_OVERWORLD) + 64, ifloor(z))) continue;
    int my = findSafeY(w, ifloor(x), ifloor(z), DIM_OVERWORLD); if (my < 1) continue;
    if (!creatureType(MT[i % 4])) continue;
    if (Creature* m = spawnCreature(lv, REALM_MAIN, MT[i % 4], vec3(ifloor(x) + 0.5f, my, ifloor(z) + 0.5f))) m->huntT = 30;
  }
  if (who) toast(lv, *who, "Someone is calling for help nearby!", "warn");
}
static void lostHome(Level& lv, Creature& e, const Village& v) {
  std::string id = e.data.value("lostQ", "");
  lv.info.data["lost"][id] = "home";
  std::string name = npcOf(e) ? npcOf(e)->value("n", std::string("They")) : std::string("They");
  for (Player* p : levelPlayers(lv)) {
    for (auto& q : npcStats(*p)["quests"]) if (q.value("id", "") == id && !q.value("have", 0)) { q["have"] = 1; questHudChanged(lv, *p); }
    if (std::hypot(p->pos.x - e.pos.x, p->pos.z - e.pos.z) < 80) toast(lv, *p, name + " is home safe in " + v.name + "! Back to the board for your reward.", "epic");
    sfxUi(lv, *p, "levelup");
  }
  lv.fx->particles("mana_light", e.pos + vec3(0, 0.9f, 0), 24, json{{"vel", json::array({0, 1.2, 0})}, {"life", 0.8}, {"size", 0.06}, {"spread", 0.5}});
  e.removed = true;
}
void lostTick(Level& lv, VillagerSystem& S, double dt) {
  if ((S.lostT -= (float)dt) > 0) return;
  S.lostT = 1.5f;
  for (Player* p : levelPlayers(lv)) {
    if (p->realm != REALM_MAIN || dimOfY(ifloor(p->pos.y)) != DIM_OVERWORLD) continue;
    json L = npcStats(*p)["quests"];
    for (auto& q : L) if (q.value("kind", "") == "rescue" && !q.value("have", 0) && q.contains("at") && std::hypot(p->pos.x - q["at"][0].get<float>(), p->pos.z - q["at"][1].get<float>()) < 48) lostSpawn(lv, q, p);
  }
  // the ones on their way home
  for (auto& up : lv.entities()) {
    auto* e = dynamic_cast<Creature*>(up.get());
    if (!e || e->removed || !e->alive() || !e->data.contains("lostQ")) continue;
    S.mind(e->id).lostQ = e->data.value("lostQ", "");
    const json* N = npcOf(*e);
    VillageRef v = N ? villageById(lv, N->value("v", "")) : nullptr;
    if (v && std::hypot(v->x - e->pos.x, v->z - e->pos.z) < 14) lostHome(lv, *e, *v);
  }
}
bool lostTake(Level& lv, Creature& e, Player& p, std::string* said) {
  if (!e.data.contains("lostQ") || !npcOf(e) || !e.alive()) return false;
  VillagerSystem* S = vsys(lv); if (!S) return false;
  S->mind(e.id).follow = p.id;
  VillageRef v = villageById(lv, npcOf(e)->value("v", ""));
  if (said) *said = npcOf(e)->value("n", std::string("They")) + ": \"Oh, thank the stars! Take me home" + (v ? " to " + v->name : std::string()) + ", please. I'll stay close.\"";
  return true;
}

// ------------------------------------------------------------------ a house of your own (hearth.js)
static HouseBuilder g_builder;
void setHouseBuilder(HouseBuilder f) { g_builder = std::move(f); }
static std::pair<int, int> vRot(int r, int x, int z) { switch (r & 3) { case 0: return {x, z}; case 1: return {-z, x}; case 2: return {-x, -z}; default: return {z, -x}; } }
const json* homeOf(Player& p, const std::string& vid) { const json& H = p.stats.value("homes", json::object()); return H.is_object() && H.contains(vid) ? &p.stats["homes"][vid] : nullptr; }

struct Site { bool ok = false; int r = 0, ox = 0, oz = 0, y = 0; ivec4 bb{0}; };
static Site homeSite(Level& lv, const Village& v) {
  World& w = lv.world(REALM_MAIN);
  const Registry& R = reg();
  std::vector<ivec3> others; const json& HS = lv.info.data.value("homeSites", json::object());
  if (HS.is_object()) for (auto& [k, o] : HS.items()) others.push_back(ivec3(o[0].get<int>(), o[1].get<int>(), o[2].get<int>()));
  auto box = [&](int r, int ox, int oz) {
    ivec4 b(1 << 30, 1 << 30, -(1 << 30), -(1 << 30));
    for (auto [x, z] : {std::pair{-1, -2}, {7, -2}, {-1, 7}, {7, 7}}) { auto [a, c] = vRot(r, x, z); b.x = std::min(b.x, ox + a); b.y = std::min(b.y, oz + c); b.z = std::max(b.z, ox + a); b.w = std::max(b.w, oz + c); }
    return b;
  };
  for (int ring = 0; ring < 7; ring++) {
    const float dist = v.r + 4 + ring * 4.f;
    for (int k = 0; k < 20; k++) {
      float a = (k / 20.f) * TAU + ring * 0.4f;
      int cx = (int)std::floor(v.x + std::cos(a) * dist + 0.5f), cz = (int)std::floor(v.z + std::sin(a) * dist + 0.5f);
      int tx = v.x - cx, tz = v.z - cz, r = std::abs(tx) > std::abs(tz) ? (tx > 0 ? 1 : 3) : (tz < 0 ? 0 : 2);   // the door looks back toward the square
      auto [dx, dz] = vRot(r, 3, 3); int ox = cx - dx, oz = cz - dz;
      ivec4 bb = box(r, ox, oz);
      bool bad = false;
      for (auto& b : v.blds) if (b.x1 + 2 >= bb.x && b.x0 - 2 <= bb.z && b.z1 + 2 >= bb.y && b.z0 - 2 <= bb.w) bad = true;
      for (auto& o : others) if (o.x + 10 >= bb.x && o.x - 10 <= bb.z && o.z + 10 >= bb.y && o.z - 10 <= bb.w) bad = true;
      if (bad) continue;
      std::vector<int> hs; bool ok = true;
      for (int x = bb.x; x <= bb.z && ok; x++) for (int z = bb.y; z <= bb.w && ok; z++) {
        if (!w.isLoaded(x, v.y, z)) { ok = false; break; }
        int y = findSafeY(w, x, z, DIM_OVERWORLD); if (y < 1) { ok = false; break; }
        BlockId top = w.getBlock(x, y - 1, z);
        if (R.tLiquid[top] || R.tLeaves[top] || R.tWlog[top] || R.block(top).has("isLog")) { ok = false; break; }
        hs.push_back(y - 1);
      }
      if (!ok || hs.empty()) continue;
      std::sort(hs.begin(), hs.end());
      int Y = hs[hs.size() >> 1];
      if (hs.front() < Y - 3 || hs.back() > Y + 3) continue;
      // (0.9.0.33) never on anything someone built, a chest, a portal, or a place
      if (contentHooks().placesNear) { bool hit = false; for (auto& P : contentHooks().placesNear(lv, REALM_MAIN, vec3((bb.x + bb.z) / 2.f, Y, (bb.y + bb.w) / 2.f), 12)) if (P.x1 + 2 >= bb.x && P.x0 - 2 <= bb.z && P.z1 + 2 >= bb.y && P.z0 - 2 <= bb.w) hit = true; if (hit) continue; }
      bool built = false;
      for (int x = bb.x - 1; x <= bb.z + 1 && !built; x++) for (int z = bb.y - 1; z <= bb.w + 1 && !built; z++) for (int y = Y - 3; y <= Y + 14; y++) {
        BlockId id = w.getBlock(x, y, z);
        if (id && (R.tPortal[id] || w.blockEntity(x, y, z) || R.block(id).has("container") || R.tShape[id] == ShapeKind::Bed)) { built = true; break; }
      }
      if (built) continue;
      Site s; s.ok = true; s.r = r; s.ox = ox; s.oz = oz; s.y = Y; s.bb = bb;
      return s;
    }
  }
  return {};
}
// the default builders: a plain little house of the village's wood (the structures' house template replaces it)
static HeroHouse plainHouse(Level& lv, const Village& v, int r, int ox, int oz, int Y, const json& gifts) {
  World& w = lv.world(REALM_MAIN);
  const std::string wood = v.style == "savanna" ? "acacia" : v.style == "taiga" || v.style == "snowy" ? "spruce" : v.style == "desert" ? "" : "oak";
  BlockId planks = wood.empty() ? B("smooth_sandstone") : B((wood + "_planks").c_str()), log = wood.empty() ? B("sandstone") : B((wood + "_log").c_str());
  if (!planks) planks = B("oak_planks"); if (!log) log = planks;
  BlockId stone = v.style == "desert" ? B("sandstone") : B("cobblestone"), ground = v.style == "desert" ? B("sand") : B("grass");
  BlockId door = B((wood.empty() ? "oak" : wood + "_door").c_str()); if (!door) door = B("oak_door");
  BlockId slab = B((wood.empty() ? std::string("sandstone_slab") : wood + "_slab").c_str()); if (!slab) slab = planks;
  auto at = [&](int lx, int ly, int lz) { auto [a, c] = vRot(r, lx, lz); return ivec3(ox + a, Y + ly, oz + c); };
  auto set = [&](int lx, int ly, int lz, BlockId id, u8 m = 0) { ivec3 p = at(lx, ly, lz); w.setBlock(p.x, p.y, p.z, id, m); };
  auto inside = [](int lx, int lz) { return lx >= 0 && lx <= 6 && lz >= 0 && lz <= 5; };
  // the ground made ready: cleared above, a footing below, the yard levelled
  for (int lz = -2; lz <= 7; lz++) for (int lx = -1; lx <= 7; lx++) {
    ivec3 p = at(lx, 0, lz);
    for (int yy = Y + 1; yy <= Y + 13; yy++) if (w.getBlock(p.x, yy, p.z)) w.setBlock(p.x, yy, p.z, AIR);
    w.setBlock(p.x, Y, p.z, inside(lx, lz) ? stone : ground);
    for (int yy = Y - 1; yy > dimBase(DIM_OVERWORLD) + 1 && !reg().tSolid[w.getBlock(p.x, yy, p.z)]; yy--) w.setBlock(p.x, yy, p.z, inside(lx, lz) ? stone : B("dirt"));
  }
  for (int ly = 1; ly <= 4; ly++) for (int lz = 0; lz <= 5; lz++) for (int lx = 0; lx <= 6; lx++) {
    bool ex = lx == 0 || lx == 6, ez = lz == 0 || lz == 5;
    if (!ex && !ez) continue;
    set(lx, ly, lz, ex && ez ? log : (ly == 2 && (lx == 1 || lx == 5 || lz == 2 || lz == 3) && !(lz == 0 && lx == 3)) ? B("glass_pane") : planks);
  }
  for (int lz = -1; lz <= 6; lz++) for (int lx = -1; lx <= 7; lx++) set(lx, 5, lz, slab);
  // the door (local -z), facing the square
  u8 dm = (u8)((2 + r) & 3);
  set(3, 1, 0, door, dm); set(3, 2, 0, door, (u8)(dm | 16));
  // a bed (its foot toward +x), a chest of gifts, a torch
  BlockId bed = B("red_bed"); if (!bed) bed = B("bed");
  u8 bm = (u8)((1 + r) & 3);
  set(1, 1, 3, bed, bm); set(2, 1, 3, bed, (u8)(bm | 4));
  set(5, 1, 4, B("chest"), (u8)((2 + r + 2) & 3));
  ivec3 cp = at(5, 1, 4);
  json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
  int i = 0; for (auto& g : gifts) items[i++] = ItemStack(g[0].get<std::string>(), g[1].get<int>()).toJson();
  w.setBlockEntity(cp.x, cp.y, cp.z, json{{"type", "chest"}, {"items", items}});
  set(5, 3, 1, B("torch"), (u8)[](int d) { const int T[4] = {3, 2, 4, 1}; return T[d & 3]; }((0 + r) & 3));
  HeroHouse h; h.ok = true; h.bed = at(1, 1, 3); h.door = at(3, 1, -1); h.name = v.name;
  return h;
}
static HeroHouse homeGive(Level& lv, const Village& v, Player& who) {
  json& built = lv.info.data["homeBuilt"]; if (!built.is_object()) built = json::object();
  std::string key = v.id + ":" + who.name;
  if (built.contains(key)) {   // (0.9.0.33) one house each
    const json& b = built[key]; HeroHouse h; h.ok = true; h.name = b.value("name", v.name);
    h.bed = ivec3(b["bed"][0].get<int>(), b["bed"][1].get<int>(), b["bed"][2].get<int>()); h.door = ivec3(b["door"][0].get<int>(), b["door"][1].get<int>(), b["door"][2].get<int>());
    return h;
  }
  Site s = homeSite(lv, v);
  if (!s.ok) return {};
  json gifts = json::array();
  for (auto& [k, n] : std::vector<std::pair<const char*, int>>{{"emerald", 16}, {"bread", 8}, {"cooked_beef", 8}, {"torch", 24}, {"golden_apple", 1}, {"flower_pot", 2}, {"oak_sapling", 2}, {"wheat_seeds", 8}}) if (reg().item(k)) gifts.push_back(json::array({k, n}));
  HeroHouse h = g_builder ? g_builder(lv, v, s.r, s.ox, s.oz, s.y, gifts) : plainHouse(lv, v, s.r, s.ox, s.oz, s.y, gifts);
  if (!h.ok) return h;
  built[key] = {{"bed", json::array({h.bed.x, h.bed.y, h.bed.z})}, {"door", json::array({h.door.x, h.door.y, h.door.z})}, {"name", h.name}};
  lv.info.data["homeSites"][key] = json::array({s.ox, s.y, s.oz});
  sfxUi(lv, who, "levelup");
  return h;
}
bool homeAsk(Level& lv, Player& p, const Village& v, std::string& sayText) {
  if (const json* mine = homeOf(p, v.id)) {
    const json& d = (*mine)["door"];
    sayText = "It's yours for as long as " + v.name + " stands. You'll find it at the edge of the village, near x " + std::to_string(d[0].get<int>()) + ", z " + std::to_string(d[2].get<int>()) + ". The bed is made.";
    return true;
  }
  VillagerSystem* S = vsys(lv); if (!S) return false;
  for (auto& P : S->homePending) if (P.vid == v.id && P.player == p.name) { sayText = "The builders are on it. Give them a little while."; return true; }
  sayText = "Hero of " + v.name + "\xE2\x80\xA6 you've done more for us than we can repay. But we can give you a home. The builders will raise you a house at the edge of the village, right away.";
  HeroHouse h = homeGive(lv, v, p);
  if (h.ok) homeSettle(lv, p, v.id, h.bed, h.door, h.name);
  else { S->homePending.push_back({v.id, p.name, 0}); toast(lv, p, "The builders are looking for good ground. Your house will be ready soon."); }
  return true;
}
void homeTick(Level& lv, VillagerSystem& S, double dt) {
  if (S.homePending.empty() || (S.homeT -= (float)dt) > 0) return;
  S.homeT = 4;
  for (size_t i = 0; i < S.homePending.size();) {
    auto& P = S.homePending[i];
    VillageRef v = villageById(lv, P.vid); Player* p = playerByName(lv, P.player);
    if (!v || !p) { S.homePending.erase(S.homePending.begin() + i); continue; }
    HeroHouse h = homeGive(lv, *v, *p);
    if (!h.ok) {   // a few tries, then the builders give up until asked again (the search isn't free)
      if (++P.tries >= 6) { toast(lv, *p, "The builders of " + v->name + " couldn't find good open ground. Ask the elder again another time.", "warn"); S.homePending.erase(S.homePending.begin() + i); continue; }
      i++; continue;
    }
    homeSettle(lv, *p, v->id, h.bed, h.door, h.name);
    S.homePending.erase(S.homePending.begin() + i);
  }
}

// ------------------------------------------------------------------ each player, now and then
void playerLifeTick(Level& lv, VillagerSystem& S, Player& p, double dt) {
  float& t1 = S.playerT[p.name + "|q"]; float& t2 = S.playerT[p.name + "|r"]; float& t5 = S.playerT[p.name + "|p"]; float& tl = S.playerT[p.name + "|l"];
  const bool ow = p.realm == REALM_MAIN && dimOfY(ifloor(p.pos.y)) == DIM_OVERWORLD;
  // a welcome the first time you walk into a village
  if ((t1 -= (float)dt) <= 0) {
    t1 = 1;
    if (ow && !p.dead) if (VillageRef v = villageAtPos(lv, p.realm, p.pos, 0); v && std::hypot(p.pos.x - v->x, p.pos.z - v->z) < v->r * 0.75f) {
      json& S2 = npcStats(p); if (!S2["vseen"].is_object()) S2["vseen"] = json::object();
      if (!S2["vseen"].contains(v->id)) {
        S2["vseen"][v->id] = 1; toast(lv, p, "You have found " + v->name, "epic");
        if (!S2.value("vtip", 0)) { S2["vtip"] = 1; later(lv, 3, p, "Use a villager to talk, trade and find work. The board by the well has bounties."); }
      }
    }
  }
  // fairplay.js: bounties lapse
  if ((tl -= (float)dt) <= 0) { tl = 30; questLapse(lv, p); }
  // riftlore.js: when you first walk into the village after the story has moved on, someone calls out to you
  if ((t2 -= (float)dt) <= 0) {
    t2 = 2;
    const int tier = riftTier(lv);
    if (tier >= 1 && ow) if (VillageRef v = villageAtPos(lv, p.realm, p.pos, 4)) {
      json& done = p.stats["riftCalls"]; if (!done.is_object()) done = json::object();
      std::string key = v->id + ":" + std::to_string(tier);
      if (!done.contains(key)) {
        Creature* best = nullptr; float bd = 13;
        for (auto& up : lv.entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (const json* N = npcOf(*c))
          if (c->alive() && N->value("v", "") == v->id && N->value("j", "") != "golem") { float d = std::hypot(c->pos.x - p.pos.x, c->pos.z - p.pos.z); if (d < bd) { bd = d; best = c; } }
        if (best) {
          done[key] = 1;
          std::string line = pickStr(VD()["riftCall"][std::to_string(tier)], rnd());
          if (size_t i = line.find("{p}"); i != std::string::npos) line.replace(i, 3, p.name.empty() ? "friend" : p.name);
          toast(lv, p, npcOf(*best)->value("n", std::string()) + ": \xE2\x80\x9C" + line + "\xE2\x80\x9D", tier >= 5 ? "good" : tier >= 3 ? "warn" : "");
          npcSay(lv, *best, line);
        }
      }
    }
  }
  // paths.js: the moment a path opens to you; a Knight's hearts; new talent points
  if ((t5 -= (float)dt) <= 0) {
    t5 = 5;
    json& st = p.stats;
    if (!st.value("pathTold", 0) && combatLevel(p) >= VD().value("pathAt", 20) && !(st.contains("path") && st["path"].is_object())) {
      st["pathTold"] = 1;
      toast(lv, p, "You could walk a path now: Knight, Ranger or Mage. Any village guard, fletcher, librarian or elder can train you.", "epic");
    }
    int want = pathMaxHealthBonus(p), had = st.value("pathHp", 0);
    if (want != had) { p.maxHp += (float)(want - had); if (p.hp > p.maxHp) p.hp = p.maxHp; st["pathHp"] = want; }
    if (st.contains("path") && st["path"].is_object()) {
      int pts = pathPoints(p);
      if (st.value("pathPts", 0) < pts) { if (st.contains("pathPts") && pts > st["pathPts"].get<int>()) toast(lv, p, "A new talent point for your path (talk to a village trainer)", "good"); st["pathPts"] = pts; }
    }
  }
}

} // namespace dq
