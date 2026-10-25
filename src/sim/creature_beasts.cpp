// sim/creature_beasts.cpp — the animals: game/bestiary.js (archetypes: passive, neutral, predator, flyer,
// swimmer, amphibian), the farm animals (game/blocks2.js farmAI / breeding / eggs / wool), game/husbandry.js
// (following food, babies trailing their elders, bees working flowers), game/aquatic.js (stranded flopping),
// game/pets.js (taming, follow/stay, defending, riding with saddles) and monsters.js's stalking big cats.
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/mob_combat.h"
#include "sim/physics.h"
#include "sim/player.h"
#include "core/registry.h"

namespace dq {

static const std::unordered_map<std::string, std::vector<std::string>>& predators() {
  static std::unordered_map<std::string, std::vector<std::string>> m;
  static bool built = false;
  if (!built) {
    built = true;
    for (auto& [k, v] : reg().mobs.items()) if (const CreatureType* t = creatureType(k)) for (auto& q : t->prey) m[q].push_back(k);
  }
  return m;
}
static bool solidAt(World& w, float x, float y, float z) { return reg().tSolid[w.getBlock(ifloor(x), ifloor(y), ifloor(z))]; }
static bool wetAt(World& w, float x, float y, float z) { return reg().tLiquid[w.getBlock(ifloor(x), ifloor(y), ifloor(z))] == 1; }
static bool isAlive(Entity* e) { if (!e || e->removed) return false; if (auto* c = dynamic_cast<Creature*>(e)) return c->alive(); if (auto* p = dynamic_cast<Player*>(e)) return !p->dead; return true; }

// ---- what each creature can be tamed with, and the chance each offer works (pets.js) ----
struct TameFood { std::vector<std::string> foods; float chance; };
static const std::unordered_map<std::string, TameFood>& tameTable() {
  static const std::vector<std::string> meat = {"raw_meat", "venison", "porkchop", "beef", "mutton"}, horse = {"apple", "golden_apple", "sugar", "wheat", "hay_bale"},
                                       seeds = {"wheat_seeds", "melon_seeds", "pumpkin_seeds", "beetroot_seeds"}, fish = {"cod", "salmon"};
  static const std::unordered_map<std::string, TameFood> m = {
    {"wolf", {{"bone"}, 0.34f}}, {"fox", {{"sweet_berries", "glow_berries"}, 0.25f}}, {"snow_fox", {{"sweet_berries", "glow_berries"}, 0.25f}},
    {"cat", {fish, 0.34f}}, {"ocelot", {fish, 0.25f}}, {"parrot", {seeds, 0.34f}}, {"horse", {horse, 0.25f}}, {"donkey", {horse, 0.3f}},
    {"llama", {{"wheat", "hay_bale", "apple"}, 0.3f}}, {"camel", {{"cactus", "apple"}, 0.3f}}, {"moa", {{"wheat_seeds", "beetroot_seeds"}, 0.3f}},
    {"pig", {{"carrot", "potato", "beetroot"}, 0.34f}}, {"boar", {{"carrot", "potato", "apple"}, 0.25f}}, {"cow", {{"wheat"}, 0.34f}}, {"sheep", {{"wheat"}, 0.34f}},
    {"goat", {{"wheat"}, 0.3f}}, {"deer", {{"apple", "carrot"}, 0.25f}}, {"chicken", {{"wheat_seeds"}, 0.34f}}, {"turkey", {{"wheat_seeds"}, 0.34f}}, {"duck", {{"wheat_seeds", "bread"}, 0.34f}},
    {"rabbit", {{"carrot", "dandelion"}, 0.34f}}, {"mouse", {{"wheat_seeds", "wheat"}, 0.4f}}, {"raccoon", {{"sweet_berries", "apple", "bread"}, 0.3f}},
    {"brown_bear", {{"honey_bottle", "salmon", "cod", "sweet_berries"}, 0.2f}}, {"polar_bear", {fish, 0.2f}}, {"panda", {{"bamboo"}, 0.3f}},
    {"frog", {{"slime_ball"}, 0.34f}}, {"turtle", {{"seagrass"}, 0.34f}}, {"axolotl", {fish, 0.34f}}, {"armadillo", {{"spider_eye", "string"}, 0.3f}},
    {"aerbunny", {{"carrot"}, 0.4f}}, {"dolphin", {fish, 0.3f}}, {"lion", {meat, 0.15f}}, {"lioness", {meat, 0.18f}}, {"tiger", {meat, 0.15f}},
    {"panther", {{"raw_meat", "venison", "porkchop", "chicken"}, 0.15f}}, {"snow_leopard", {{"raw_meat", "venison", "mutton", "rabbit"}, 0.2f}},
    {"crocodile", {{"raw_meat", "cod", "salmon"}, 0.1f}}, {"slime", {{"slime_ball"}, 0.4f}}, {"magma_slime", {{"magma_cream"}, 0.4f}},
  };
  return m;
}
// creatures you can ride once tamed and saddled: top speed (blocks a second) and jump
static bool mountStats(const std::string& t, float& speed, float& jump) {
  static const std::unordered_map<std::string, vec2> M = {
    {"horse", {10, 9.5f}}, {"donkey", {7.5f, 8.5f}}, {"llama", {6.5f, 8}}, {"camel", {7.5f, 8}}, {"deer", {9.5f, 10}}, {"goat", {6.5f, 11}}, {"moa", {8.5f, 10}},
    {"brown_bear", {6.5f, 8}}, {"polar_bear", {6.5f, 8}}, {"lion", {9.5f, 9.5f}}, {"lioness", {9.5f, 9.5f}}, {"tiger", {10, 9.5f}}, {"panther", {10, 10}}, {"snow_leopard", {9, 10.5f}}};
  auto it = M.find(t);
  if (it == M.end()) { speed = 7; jump = 8; return false; }
  speed = it->second.x; jump = it->second.y; return true;
}
static bool tameFood(const std::string& type, const std::string& item) {
  auto it = tameTable().find(type);
  if (it == tameTable().end() || !reg().item(item)) return false;
  return std::find(it->second.foods.begin(), it->second.foods.end(), item) != it->second.foods.end();
}

// ------------------------------------------------------------------ shared moves
float Creature::goToward(Level& lv, vec3 t, float dt) {
  float d = std::hypot(t.x - pos.x, t.z - pos.z);
  std::optional<float> nd; if (d > 3) nd = navDir(lv, t, dt);
  return nd ? *nd : heading(pos.x, pos.z, t.x, t.z);
}
float Creature::fleeDir(Entity* from) {
  float away = from ? heading(from->pos.x, from->pos.z, pos.x, pos.z) : yaw;
  if (hitWall) { if (!fleeTurn) fleeTurn = frand() < 0.5f ? -1 : 1; }
  else if (fleeTurn && frand() < 0.02f) fleeTurn = 0;
  return away + fleeTurn * 0.9f + std::sin(age * 1.7f) * 0.18f;
}
bool Creature::bite(Level& lv, Entity* target, float dmg) {
  if (!target || attackCd > 0) return false;
  float d = std::hypot(target->pos.x - pos.x, target->pos.z - pos.z), dy = std::fabs(target->pos.y - pos.y);
  if (d > 1.1f + w + (target->w > 0 ? target->w : 0.3f) || dy > 1.8f) return false;
  attackCd = 1.0f + frand() * 0.3f; swingT = 0.3f;
  if (T->bear) rearT = 0.6f;
  DamageSrc s; s.knock = T->bear ? 7 : 4;
  damageEntity(lv, *target, dmg, this, "mob", s);
  return true;
}
void Creature::provoke(Level& lv, Entity* who) {
  if (!who || (T->ai == "passive" && !T->angersWhenHurt)) return;
  angryT = 20 + frand() * 10; foe = who->id; panicT = 0; sitting = sleeping = false;
  if (T->pack) for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get()))
    if (c != this && c->type == type && c->alive() && !(c->angryT > 0) && std::hypot(c->pos.x - pos.x, c->pos.z - pos.z) < 16) { c->angryT = 20; c->foe = who->id; c->panicT = 0; }
}

// ------------------------------------------------------------------ bestiary.js
static void scanAround(Level& lv, Creature& m) {
  const auto& T = *m.T;
  m.nearPred = m.nearPrey = m.herdMate = 0; m.herdCount = 0; m.leader = T.school ? 0 : m.leader;
  float dPred = 9, dPrey = 16, dHerd = 40;
  auto pit = predators().find(m.type);
  const std::vector<std::string>* preds = pit == predators().end() ? nullptr : &pit->second;
  Creature* lead = nullptr;
  for (auto& e : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(e.get());
    if (!c || c == &m || !c->alive() || c->removed || c->realm != m.realm) continue;
    float d = std::fabs(c->pos.x - m.pos.x) + std::fabs(c->pos.z - m.pos.z);
    if (d > 40) continue;
    if (preds && std::find(preds->begin(), preds->end(), c->type) != preds->end() && c->owner.empty() && d < dPred) { dPred = d; m.nearPred = c->id; }
    if (!T.prey.empty() && T.hunts(c->type) && !c->fed && c->owner.empty() && d < dPrey && (c->type != "skeleton" || m.type == "wolf")) { dPrey = d; m.nearPrey = c->id; }
    if (c->type == m.type) {
      m.herdCount++;
      if (d < dHerd) { dHerd = d; m.herdMate = c->id; }
      if (T.school && c->id < m.id && (!lead || c->id < lead->id) && d < 10) lead = c;
    }
  }
  if (T.school) m.leader = lead ? lead->id : 0;
}
static void beastVoice(Level& lv, Creature& m, float dt, float dist) {
  m.soundT -= dt;
  if (m.soundT > 0) return;
  const auto& T = *m.T;
  m.soundT = (T.ai == "swimmer" ? 14 : T.ambient ? 4 : 7) + frand() * 10;
  if (T.sound.empty() || dist > 20) return;
  std::string kind = "say";
  if (m.angryT > 0 && (T.sound == "wolf" || T.sound == "bear" || T.sound == "cat" || T.sound == "bee")) kind = "angry";
  else if (T.sound == "wolf" && envDay(lv, dimAt(m)) < 0.35f && frand() < 0.3f) kind = "howl";
  else if (T.sound == "cat" && m.sitting && frand() < 0.5f) kind = "purr";
  if (T.sound == "fish" && m.inWater) return;
  if (T.sound == "frog") m.croakT = 0.6f;
  if (T.displays && frand() < 0.5f) m.displayT = 2.5f;
  lv.fx->voice(T.sound, kind, m.pos + vec3(0, m.h * 0.7f, 0));
}

static Move fight(Level& lv, Creature& m, Entity& foe, float dt) {
  const auto& T = *m.T;
  float d = std::hypot(foe.pos.x - m.pos.x, foe.pos.z - m.pos.z), dir = m.goToward(lv, foe.pos, dt);
  if (T.spit) {   // llamas spit from a distance
    m.spitT -= dt;
    if (m.spitT <= 0 && d < 10) {
      m.spitT = 2 + frand();
      vec3 e = m.pos + vec3(-std::sin(m.yaw) * 0.6f, m.h * 0.9f, -std::cos(m.yaw) * 0.6f);
      vec3 t = foe.pos + vec3(0, 1.2f, 0) - e; float l = glm::length(t); if (l < 1e-3f) l = 1;
      spawnProjectile(lv, m.realm, e, vec3(t.x / l * 12, t.y / l * 12 + l * 0.35f, t.z / l * 12), "spit", &m);
      lv.fx->sound("spit", e);
    }
    return {d < 5 ? -T.speed * 0.4f : d > 8 ? T.speed : 0, dir};
  }
  if (T.rams) {   // goats back off, lower their heads and charge
    m.ramT -= dt;
    if (m.ramming) {
      if (m.bite(lv, &foe, T.damage > 0 ? T.damage : 2)) {
        m.ramming = false; m.ramT = 2.5f; knockback(foe, vec3(-std::sin(m.yaw) * 9, 4, -std::cos(m.yaw) * 9));
        if (m.ramOnce) { m.ramOnce = false; m.angryT = 0; }
      } else if (m.hitWall) {
        m.ramming = false; m.ramT = 2;
        if (frand() < 0.15f) dropItem(lv, m.realm, ItemStack("goat_horn", 1), m.pos + vec3(0, 1, 0), true);
        lv.fx->sound("thud", m.pos);
      }
      return {6.5f, m.yaw};
    }
    if (m.ramT <= 0 && d > 3 && d < 9) { m.ramming = true; m.yaw = dir; lv.fx->voice("goat", "say", m.pos + vec3(0, 1, 0)); return {6.5f, dir}; }
    return {d < 4 ? -T.speed : T.speed * 0.5f, dir};
  }
  m.bite(lv, &foe, (T.damage > 0 ? T.damage : 2) + (m.baby ? -1 : 0));
  return {d < 0.8f ? 0 : (T.run > 0 ? T.run : T.speed * 2) * 0.85f, dir};
}

static Move landAI(Level& lv, Creature& m, Entity* p, float dist, float toPlayer, float dt) {
  const auto& T = *m.T;
  float day = envDay(lv, dimAt(m));
  if (T.curls) {   // armadillos curl up when something big comes close
    bool scared = (p && dist < 3.5f && !pawnSneaking(p)) || m.hurtTime > 0 || m.nearPred;
    if (scared) m.curlT = 4;
    m.curlT -= dt; m.curled = m.curlT > 0;
    if (m.curled) return {0, m.yaw};
  }
  Entity* foe = m.angryT > 0 ? m.ent(lv, m.foe) : nullptr;
  if (m.angryT > 0 && (!isAlive(foe) || std::hypot(foe->pos.x - m.pos.x, foe->pos.z - m.pos.z) > 28)) { m.angryT = 0; foe = nullptr; }
  if (m.angryT > 0 && foe) return fight(lv, m, *foe, dt);
  // bears guard their cubs
  if (T.protective && p && dist < 7 && !m.baby && !pawnCreative(p)) {
    auto* hm = dynamic_cast<Creature*>(m.ent(lv, m.herdMate));
    if (hm && hm->baby && std::hypot(p->pos.x - hm->pos.x, p->pos.z - hm->pos.z) < 6) m.provoke(lv, p);
  }
  if (Entity* pr = m.ent(lv, m.nearPred)) if (m.panicT <= 0 && std::hypot(pr->pos.x - m.pos.x, pr->pos.z - m.pos.z) < 7) { m.panicT = 2.5f; m.fleeFrom = pr->id; }
  bool spooked = T.skittish > 0 && p && dist < T.skittish * (pawnSneaking(p) ? 0.45f : 1) && !m.fed && m.owner.empty();
  if (spooked && m.panicT <= 0) { m.panicT = 2 + frand() * 2; m.fleeFrom = p->id; }
  if (m.panicT > 0) { m.panicT -= dt; m.sitting = m.sleeping = false; Entity* f = m.ent(lv, m.fleeFrom); return {T.run > 0 ? T.run : T.speed * 2.2f, m.fleeDir(f ? f : p)}; }
  if (T.farm) if (OptMove f = m.farmAI(lv, p, dist, toPlayer, dt)) { m.sitting = m.sleeping = false; return *f; }
  if (T.ai == "predator") {   // hungry hunters go after prey
    if (m.hunger < 0) m.hunger = frand() * 90;
    m.hunger += dt;
    auto* pr = dynamic_cast<Creature*>(m.ent(lv, m.nearPrey));
    if (m.hunger > 110 && pr && pr->alive() && !m.baby) {
      m.sitting = m.sleeping = false;
      if (m.bite(lv, pr, T.damage > 0 ? T.damage : 2) && !pr->alive()) { m.hunger = 0; m.grazeT = m.grazeMax = 2.5f; }
      if (!pr->alive()) m.hunger = 0;
      return {T.run > 0 ? T.run : T.speed * 2, m.goToward(lv, pr->pos, dt)};
    }
  }
  if (T.rams && p && !m.baby && dist > 4 && dist < 9 && frand() < dt * 0.01f && !pawnCreative(p) && !pawnSneaking(p)) { m.provoke(lv, p); m.angryT = 4; m.ramOnce = true; }
  bool restful = T.nocturnal ? day > 0.6f : day < 0.3f;
  if ((T.ai == "predator" || T.sound == "raccoon") && !m.wanderOn) {
    if (!m.sitting && !m.sleeping && frand() < dt * 0.05f) { if (restful) m.sleeping = true; else if (T.sound == "cat" || T.sound == "fox" || T.sound == "wolf") m.sitting = true; }
  } else m.sitting = m.sleeping = false;
  if ((m.sitting || m.sleeping) && frand() < dt * 0.04f) m.sitting = m.sleeping = false;
  if (m.sitting || m.sleeping) return {0, m.yaw};
  if (T.herd) if (Entity* hm = m.ent(lv, m.herdMate)) if (std::hypot(hm->pos.x - m.pos.x, hm->pos.z - m.pos.z) > 10) return {T.speed * 0.8f, m.goToward(lv, hm->pos, dt)};
  if (m.baby) if (auto* hm = dynamic_cast<Creature*>(m.ent(lv, m.herdMate))) if (!hm->baby && std::hypot(hm->pos.x - m.pos.x, hm->pos.z - m.pos.z) > 4) return {T.speed * 1.2f, m.goToward(lv, hm->pos, dt)};
  m.wander(dt);
  return m.wanderMove();
}

// ---- husbandry.js bees: out by day to the flowers, home with pollen to make honey ----
static bool isFlower(BlockId id) {
  static std::vector<u8> t;
  if (t.size() != reg().blocks.size()) {
    t.assign(reg().blocks.size(), 0);
    static const char* W[] = {"flower", "poppy", "dandelion", "tulip", "orchid", "allium", "azure", "daisy", "cornflower", "lily_of", "rose", "peony", "lilac", "sunflower", "bluet", "blossom", "lavender", "clover", "aster", "foxglove", "primrose", "heather", "petals"};
    for (auto& b : reg().blocks) {
      bool f = false; for (auto* w : W) if (b.key.find(w) != std::string::npos) f = true;
      if (f && (b.render == Render::Cross || b.key.find("petals") != std::string::npos)) t[b.id] = 1;
    }
  }
  return id < t.size() && t[id];
}
bool beeFlower(World& w, vec3 at, float R, int tries, ivec3& out) {
  for (int i = 0; i < tries; i++) {
    int fx = ifloor(at.x + (frand() - 0.5f) * 2 * R), fz = ifloor(at.z + (frand() - 0.5f) * 2 * R);
    for (int fy = ifloor(at.y) + 4; fy > ifloor(at.y) - 8; fy--) { BlockId id = w.getBlock(fx, fy, fz); if (isFlower(id)) { out = {fx, fy, fz}; return true; } if (reg().tSolid[id]) break; }
  }
  return false;
}
vec3 hiveFront(World& w, ivec3 h) {
  static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};   // DIRX / DIRZ (facing meta & 3)
  int m = w.getMeta(h.x, h.y, h.z) & 3;
  return vec3(h.x + 0.5f + DX[m] * 0.7f, h.y + 0.5f, h.z + 0.5f + DZ[m] * 0.7f);
}
void beeReturned(Level& lv, Creature& b);   // creature_spawn.cpp (colony bookkeeping)
static OptMove beeAI(Level& lv, Creature& b, float dt) {
  World& w = lv.world(b.realm);
  b.noGravity = true; b.hasFlyVy = true; b.perched = false;
  BlockId hid = w.getBlock(b.hive.x, b.hive.y, b.hive.z);
  if (!reg().block(hid).has("hive")) { b.hasHive = false; return Move{0, b.yaw}; }
  bool day = envDay(lv, dimAt(b)) > 0.45f && lv.info.data.value("rain", 0.f) <= 0.3f;
  vec3 t; bool hasT = false;
  if (b.wantVisits < 0) b.wantVisits = (int)(frand() * 2);
  if (!day || (b.pollen && b.visits >= 1 + b.wantVisits) || b.beeState == 2 || b.age > 240) b.beeState = 2;
  if (b.beeState == 0) {
    b.beeSeekT -= dt;
    if (!b.hasFlower || b.beeSeekT <= 0) {
      b.beeSeekT = 3;
      b.hasFlower = beeFlower(w, vec3(b.hive), 12, 12, b.flower);
      if (!b.hasFlower) { b.beeWanderT += 3; if (b.beeWanderT > 30) b.beeState = 2; }
    }
    if (b.hasFlower) {
      t = vec3(b.flower.x + 0.5f, b.flower.y + 0.45f, b.flower.z + 0.5f); hasT = true;
      if (std::hypot(t.x - b.pos.x, t.z - b.pos.z) < 0.5f && std::fabs(t.y - b.pos.y) < 0.6f) { b.beeState = 1; b.workT = 2.5f + frand() * 3; }
    } else { float a = b.age * 0.7f; t = vec3(b.hive.x + 0.5f + std::cos(a) * 4, b.hive.y + 1, b.hive.z + 0.5f + std::sin(a) * 4); hasT = true; }
  }
  if (b.beeState == 1) {
    ivec3 f = b.hasFlower ? b.flower : ivec3(b.pos);
    if (!isFlower(w.getBlock(f.x, f.y, f.z))) { b.beeState = 0; b.hasFlower = false; return Move{0, b.yaw}; }
    t = vec3(f.x + 0.5f + std::sin(b.age * 3.1f) * 0.15f, f.y + 0.4f + std::sin(b.age * 5) * 0.06f, f.z + 0.5f + std::cos(b.age * 2.7f) * 0.15f); hasT = true;
    if ((b.workT -= dt) <= 0) {
      b.pollen = true; b.visits++;
      b.hasFlower = false; b.beeState = b.visits >= 1 + b.wantVisits ? 2 : 0;
      mobBurst(lv, vec3(f) + vec3(0.5f, 0.4f, 0.5f), "pollen", 3, 0.3f);
    }
  }
  if (b.beeState == 2) {
    t = hiveFront(w, b.hive); hasT = true;
    if (std::hypot(t.x - b.pos.x, t.z - b.pos.z) < 0.6f && std::fabs(t.y - b.pos.y) < 0.7f) {
      if (b.pollen) { u8 m = w.getMeta(b.hive.x, b.hive.y, b.hive.z); if ((m & 0x0c) != 0x0c) w.setMeta(b.hive.x, b.hive.y, b.hive.z, m + 4); }
      beeReturned(lv, b);
      b.removed = true;
      return Move{0, b.yaw};
    }
  }
  if (b.pollen) {   // dusted yellow, helping crops it passes over
    if (frand() < dt * 3) lv.fx->particles("pollen", b.pos, 1);
    if (frand() < dt * 0.15f) {
      int cx = ifloor(b.pos.x), cz = ifloor(b.pos.z);
      for (int cy = ifloor(b.pos.y); cy > ifloor(b.pos.y) - 3; cy--) {
        BlockId id = w.getBlock(cx, cy, cz); const BlockDef& d = reg().block(id);
        if (d.has("crop") && d.render == Render::Cross) {
          int stages = d.props["crop"].is_object() ? d.props["crop"].value("stages", 4) : 4; u8 m = w.getMeta(cx, cy, cz);
          if (m < stages - 1) { w.setBlock(cx, cy, cz, id, m + 1); mobBurst(lv, vec3(cx + 0.5f, cy + 0.5f, cz + 0.5f), "pollen", 3, 0.3f); }
          break;
        }
        if (reg().tSolid[id]) break;
      }
    }
  }
  if (!hasT) return Move{0, b.yaw};
  b.flyVy = clampv((t.y - b.pos.y) * 2.5f, -2.f, 2.f);
  float d = std::hypot(t.x - b.pos.x, t.z - b.pos.z);
  return Move{d < 0.3f ? 0 : std::min(b.T->fly, d * 2.5f), std::atan2(-(t.x - b.pos.x), -(t.z - b.pos.z)) + std::sin(b.age * 7) * 0.25f};
}

static Move flyerAI(Level& lv, Creature& m, Entity* p, float dist, float dt) {
  const auto& T = *m.T;
  if (m.type == "bee" && m.hasHive && !(m.angryT > 0 && m.foe)) if (OptMove b = beeAI(lv, m, dt)) return *b;
  World& w = lv.world(m.realm);
  m.noGravity = true; m.hasFlyVy = true;
  float day = envDay(lv, dimAt(m));
  if (T.roosts) {   // bats hang from a cave roof while it's light outside or nobody's near
    if (m.roosting) {
      m.flyVy = 0; m.vel.x = m.vel.z = 0;
      if (!solidAt(w, m.pos.x, m.pos.y + m.h + 0.2f, m.pos.z) || (p && dist < 4 && !pawnSneaking(p)) || m.hurtTime > 0) { m.roosting = false; lv.fx->voice("bat", "say", m.pos); }
      return {0, m.yaw};
    }
    if (m.roostT < 0) m.roostT = 4 + frand() * 10;
    m.roostT -= dt;
    if (m.roostT <= 0 && solidAt(w, m.pos.x, m.pos.y + m.h + 0.35f, m.pos.z)) { m.roosting = true; m.roostT = 8 + frand() * 20; m.pos.y = std::floor(m.pos.y + m.h + 0.35f) - m.h - 0.01f; return {0, m.yaw}; }
    m.flitT -= dt;
    if (m.flitT <= 0 || m.hitWall) { m.flitT = 0.4f + frand() * 1.2f; m.flitDir = frand() * TAU; m.flitUp = m.roostT < 3 ? 2.5f : (frand() - 0.45f) * 4; }
    if (solidAt(w, m.pos.x, m.pos.y - 0.6f, m.pos.z)) m.flitUp = std::max(m.flitUp, 1.5f);
    m.flyVy = m.flitUp;
    return {T.fly, m.flitDir};
  }
  if (T.stings && m.angryT > 0 && m.foe && m.hasSting) {   // bees sting whoever bothered them (once)
    if (Entity* f = m.ent(lv, m.foe)) {
      float d = std::hypot(f->pos.x - m.pos.x, f->pos.z - m.pos.z);
      m.flyVy = clampv((f->pos.y + 1.2f - m.pos.y) * 2, -3.f, 3.f);
      if (d < 1 + m.w && std::fabs(f->pos.y + 1 - m.pos.y) < 1.4f && m.attackCd <= 0) { m.attackCd = 1; DamageSrc s; s.knock = 2; damageEntity(lv, *f, T.damage > 0 ? T.damage : 2, &m, "mob", s); m.hasSting = false; m.angryT = 0; }
      return {T.fly * 1.3f, heading(m.pos.x, m.pos.z, f->pos.x, f->pos.z)};
    }
  }
  if (m.perched) {
    m.flyVy = -1; m.noGravity = false; m.hasFlyVy = false;
    m.perchT -= dt;
    if (m.perchT <= 0 || (p && dist < 3 && !pawnSneaking(p) && T.perches) || m.hurtTime > 0) { m.perched = false; m.noGravity = true; m.hasFlyVy = true; m.flyVy = 3; }
    m.dancing = T.perches && lv.info.data.contains("jukeboxes") && [&] {
      for (auto& j : lv.info.data["jukeboxes"]) if (j.is_array() && j.size() >= 3 && std::hypot(j[0].get<float>() - m.pos.x, j[2].get<float>() - m.pos.z) < 5) return true;
      return false; }();
    return {0, m.yaw};
  }
  m.flitT -= dt;
  if (!m.hasHome) { m.home = m.pos; m.hasHome = true; }
  if (m.flitT <= 0 || m.hitWall) {
    m.flitT = 2 + frand() * 3;
    bool back = std::hypot(m.home.x - m.pos.x, m.home.z - m.pos.z) > 14;
    m.flitDir = back ? heading(m.pos.x, m.pos.z, m.home.x, m.home.z) : frand() * TAU;
    m.flyAlt = m.groundY(lv) + (T.perches ? 3 + frand() * 6 : 1 + frand() * 2.5f); m.hasFlyAlt = true;
    if (frand() < (day < 0.35f ? 0.6f : 0.25f)) m.wantPerch = true;
  }
  if (m.wantPerch && (solidAt(w, m.pos.x, m.pos.y - 0.3f, m.pos.z) || m.onGround)) { m.wantPerch = false; m.perched = true; m.perchT = 6 + frand() * 14; }
  m.flyVy = clampv(((m.wantPerch ? (float)m.groundY(lv) : m.hasFlyAlt ? m.flyAlt : m.pos.y) - m.pos.y) * 1.5f, -2.5f, 2.5f);
  if (m.hitWall) m.flyVy = 2.5f;
  return {T.fly * (m.wantPerch ? 0.5f : 1), m.flitDir};
}

// ---- aquatic.js: stranded, flop toward water if it's near, weaker each time, then suffocate ----
void strandedFlop(Level& lv, Creature& m, float dt, float air, float every, float dmg, bool heavy) {
  World& w = lv.world(m.realm);
  m.dryT += dt; m.noGravity = false; m.hasFlyVy = false;
  if (!m.flopSide) m.flopSide = frand() < 0.5f ? -1 : 1;
  if (m.flopT > 0) m.flopT -= dt;
  float weak = clampv(1 - m.dryT / air, 0.25f, 1.f);
  if (!(m.seekT > 0)) {
    m.seekT = 1.2f; m.hasWaterTo = false;
    int bx = ifloor(m.pos.x), by = ifloor(m.pos.y), bz = ifloor(m.pos.z); int best = 30;
    for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) for (int dy = -2; dy <= 1; dy++) {
      if (reg().tLiquid[w.getBlock(bx + dx, by + dy, bz + dz)] != 1) continue;
      int dd = dx * dx + dz * dz + dy * dy * 2; if (dd < best) { best = dd; m.waterTo = vec2(bx + dx + 0.5f, bz + dz + 0.5f); m.hasWaterTo = true; }
    }
  }
  m.seekT -= dt;
  if (m.onGround && frand() < dt * (0.8f + 1.8f * weak)) {
    m.vel.y = (2.4f + frand() * 1.6f) * (0.55f + 0.45f * weak) * (heavy ? 0.8f : 1);
    if (m.hasWaterTo && frand() < 0.75f) {
      float a = heading(m.pos.x, m.pos.z, m.waterTo.x, m.waterTo.y), sp = 1.6f + weak;
      m.vel.x = -std::sin(a) * sp; m.vel.z = -std::cos(a) * sp; m.yaw = a + (frand() - 0.5f) * 0.8f;
    } else { m.vel.x += (frand() - 0.5f) * 2.6f * weak; m.vel.z += (frand() - 0.5f) * 2.6f * weak; m.yaw += (frand() - 0.5f) * 2; }
    if (frand() < 0.35f) m.flopSide = -m.flopSide;
    m.flopT = 0.35f;
    lv.fx->sound("flop", m.pos, 14);
  }
  if (m.dryT > air) { m.dryT -= every; DamageSrc s; s.type = "drown"; s.bypassArmor = true; m.hurt(lv, dmg, s); }
}

static Move swimmerAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float dt) {
  const auto& T = *m.T;
  World& w = lv.world(m.realm);
  if (!m.inWater) {
    strandedFlop(lv, m, dt, T.needsAir ? 50 : T.sound == "squid" ? 14 : 10, T.needsAir ? 2 : 1.5f, 1, T.needsAir);
    return {0, m.yaw};
  }
  m.flopT = 0; m.dryT = 0;
  if (T.needsAir) { m.airT -= dt; if (!wetAt(w, m.pos.x, m.pos.y + 1.2f, m.pos.z)) m.airT = 90; }
  if (T.puffs) {
    if ((p && dist < 2.6f && std::fabs(dyp) < 2) || m.hurtTime > 0) m.puffT = 3;
    m.puffT -= dt; bool was = m.puffed; m.puffed = m.puffT > 0;
    if (m.puffed && !was) lv.fx->sound("puff", m.pos, 14);
    if (m.puffed && p && dist < 1.2f && std::fabs(dyp) < 1.4f && m.attackCd <= 0) { m.attackCd = 1; DamageSrc s; s.knock = 2; damageEntity(lv, *p, 1, &m, "mob", s); }
  }
  if (m.hurtTime > 0.3f && T.sound == "squid" && !m.inked) { m.inked = true; mobBurst(lv, m.pos + vec3(0, 0.5f, 0), "black_wool", 24, 1.6f); }
  if (m.hurtTime <= 0) m.inked = false;
  m.swimT -= dt;
  float want = T.speed, dir = m.hasSwimDir ? m.swimDir : m.yaw, ty = m.hasSwimY ? m.swimY : m.pos.y;
  bool fleeing = !T.playful && (m.hurtTime > 0 || (p && dist < 4 && std::fabs(dyp) < 3 && !T.puffs && T.sound != "squid"));
  Entity* foe = m.angryT > 0 && T.damage > 0 ? m.ent(lv, m.foe) : nullptr;
  auto* leader = T.school ? dynamic_cast<Creature*>(m.ent(lv, m.leader)) : nullptr;
  if (foe) { m.bite(lv, foe, T.damage); dir = heading(m.pos.x, m.pos.z, foe->pos.x, foe->pos.z); ty = foe->pos.y + 0.5f; want = T.speed * 1.4f; }
  else if (fleeing) { if (p) dir = heading(p->pos.x, p->pos.z, m.pos.x, m.pos.z); want = T.speed * 2.2f; m.swimT = 0; }
  else if (T.needsAir && m.airT < 25) { ty = m.pos.y + 6; want = T.speed; }
  else if (T.playful && p && dist < 14 && dist > 3 && (p->inWater || p->riding)) { dir = heading(m.pos.x, m.pos.z, p->pos.x, p->pos.z) + std::sin(m.age * 0.8f) * 0.9f; ty = p->pos.y; want = T.speed * 0.9f; }
  else if (leader && leader->alive()) { dir = heading(m.pos.x, m.pos.z, leader->pos.x + std::sin((float)m.id) * 0.8f, leader->pos.z + std::cos((float)m.id) * 0.8f); ty = leader->pos.y; want = std::hypot(leader->pos.x - m.pos.x, leader->pos.z - m.pos.z) > 1.5f ? T.speed * 1.2f : T.speed * 0.4f; }
  else if (m.swimT <= 0 || m.hitWall) {
    m.swimT = 2 + frand() * 4;
    for (int i = 0; i < 6; i++) {
      float a = frand() * TAU, r = 3 + frand() * 5, nx = m.pos.x - std::sin(a) * r, nz = m.pos.z - std::cos(a) * r, ny = m.pos.y + (frand() - 0.5f) * 4;
      if (wetAt(w, nx, ny, nz) && wetAt(w, nx, ny + 0.4f, nz)) { m.swimDir = a; m.hasSwimDir = true; m.swimY = ny; m.hasSwimY = true; break; }
    }
    if (m.hasSwimDir) dir = m.swimDir; if (m.hasSwimY) ty = m.swimY;
    if (T.sound == "squid") want = T.speed * (0.4f + frand() * 0.8f);
  }
  if (!wetAt(w, m.pos.x, ty + m.h, m.pos.z) && !(T.playful && frand() < 0.002f)) ty = std::min(ty, m.pos.y);
  if (T.playful && m.speedNow > 3 && !wetAt(w, m.pos.x, m.pos.y + 1.2f, m.pos.z) && frand() < dt * 0.6f) { m.vel.y = 7; lv.fx->sound("splash", m.pos + vec3(0, 1, 0), 20); }
  m.swimVy = clampv((ty - m.pos.y) * 1.2f, -2.f, 2.f);
  m.swimPitch = clampv(-m.vel.y * 0.2f, -0.6f, 0.6f);
  return {want, dir};
}

static Move amphibianAI(Level& lv, Creature& m, Entity* p, float dist, float toPlayer, float dt) {
  const auto& T = *m.T;
  if (T.playsDead) {   // axolotls play dead when badly hurt, and get better
    if (m.hurtTime > 0 && m.hp < m.maxHp * 0.5f && m.inWater && !m.playDead && frand() < 0.5f) m.playDeadT = 10;
    m.playDeadT -= dt; m.playDead = m.playDeadT > 0;
    if (m.playDead) { if (frand() < dt * 0.5f) m.hp = std::min(m.maxHp, m.hp + 1); m.swimVy = 0; return {0, m.yaw}; }
  }
  if (T.frogTongue && m.attackCd <= 0) {   // frogs gulp down small slimes
    for (auto& e : lv.entities()) {
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c->type != "slime" || !c->alive() || c->removed || c->size > 1 || c->realm != m.realm) continue;
      if (std::hypot(c->pos.x - m.pos.x, c->pos.z - m.pos.z) < 3 && std::fabs(c->pos.y - m.pos.y) < 1.5f) {
        m.yaw = heading(m.pos.x, m.pos.z, c->pos.x, c->pos.z); m.tongueT = 0.3f; m.tongueOut = true; m.attackCd = 2;
        c->removed = true; c->deathTime = 0.0001f; dropItem(lv, m.realm, ItemStack("slime_ball", 1), c->pos + vec3(0, 0.3f, 0), true); lv.fx->voice("frog", "say", m.pos);
        break;
      }
    }
  }
  if (!T.prey.empty() && m.inWater && !m.baby) if (auto* pr = dynamic_cast<Creature*>(m.ent(lv, m.nearPrey))) if (pr->inWater) {
    m.bite(lv, pr, T.damage > 0 ? T.damage : 2);
    m.swimVy = clampv((pr->pos.y - m.pos.y) * 1.5f, -2.f, 2.f);
    return {T.swimSpeed, heading(m.pos.x, m.pos.z, pr->pos.x, pr->pos.z)};
  }
  if (m.inWater) {
    m.swimT -= dt;
    if (m.swimT <= 0 || m.hitWall) { m.swimT = 3 + frand() * 4; m.swimDir = frand() * TAU; m.swimUp = (frand() - (T.playsDead ? 0.6f : 0.35f)) * 1.6f; }
    if (m.hitWall) m.swimUp = 1.6f;   // climb out onto the bank
    m.swimVy = m.swimUp;
    m.swimPitch = clampv(-m.vel.y * 0.2f, -0.5f, 0.5f);
    if (T.farm) if (OptMove f = m.farmAI(lv, p, dist, toPlayer, dt)) return {T.swimSpeed, f->dir};
    return {T.swimSpeed * 0.7f, m.swimDir};
  }
  m.swimPitch = 0;
  bool spooked = p && dist < 4 && !pawnSneaking(p) && T.sound == "frog";
  if (spooked && m.panicT <= 0) { m.panicT = 1.5f; m.fleeFrom = p->id; }
  if (m.panicT > 0) { m.panicT -= dt; return {T.speed * 2.5f, m.fleeDir(m.ent(lv, m.fleeFrom))}; }
  if (T.farm) if (OptMove f = m.farmAI(lv, p, dist, toPlayer, dt)) return *f;
  m.wander(dt);
  Move mv = m.wanderMove();
  return {mv.want * (T.hopper ? 1.4f : 1), mv.dir};
}

Move Creature::archetypeAI(Level& lv, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  // monsters.js: the big cats stalk you at night
  if (T->stalker && p && !pawnCreative(p) && !baby && !(angryT > 0)) {
    int dim = dimAt(*this);
    bool night = dim == DIM_OVERWORLD && envDay(lv, dim) < 0.4f;
    if (night && dist < 14 && std::fabs(dyp) < 4) {
      float lx = pos.x - p->pos.x, lz = pos.z - p->pos.z, ly = -std::sin(p->yaw) * lx - std::cos(p->yaw) * lz;
      bool watched = ly > dist * 0.6f;
      if (dist < 6 || (dist < 9 && !watched && frand() < dt * 0.5f)) { provoke(lv, p); lv.fx->voice(T->sound, "angry", pos + vec3(0, 1, 0)); }
      else { sitting = sleeping = false; return {watched ? 0 : T->speed * 0.55f, goToward(lv, p->pos, dt)}; }
    }
  }
  if (T->stings && !hasSting && data.value("stingInit", false) == false) { hasSting = true; data["stingInit"] = true; }
  if (angryT > 0) angryT -= dt;
  if (rearT > 0) rearT -= dt;
  if (displayT > 0) displayT -= dt;
  if (croakT > 0) croakT -= dt;
  if (tongueT > 0) { tongueT -= dt; tongueOut = tongueT > 0; }
  scanT -= dt;
  if (scanT <= 0) { scanT = 0.5f + frand() * 0.3f; scanAround(lv, *this); }
  beastVoice(lv, *this, dt, dist);
  float far = T->water ? 72 : T->ambient ? 64 : 110;
  if (dist > far && !kept) removed = true;
  if (T->ai == "flyer") return flyerAI(lv, *this, p, dist, dt);
  if (T->ai == "swimmer") return swimmerAI(lv, *this, p, dist, dyp, dt);
  if (T->ai == "amphibian") return amphibianAI(lv, *this, p, dist, toPlayer, dt);
  return landAI(lv, *this, p, dist, toPlayer, dt);
}

// ------------------------------------------------------------------ farm animals (blocks2.js + husbandry.js)
static bool holdsFood(Entity* p, const CreatureType& T) {
  auto* pl = dynamic_cast<Player*>(p);
  if (!pl || T.food.empty()) return false;
  return T.eats(pl->held().id) || (!pl->offhand.empty() && T.eats(pl->offhand[0].id));
}
OptMove Creature::farmAI(Level& lv, Entity* p, float dist, float toPlayer, float dt) {
  if (loveT > 0) {
    loveT -= dt;
    if (frand() < dt * 3) hearts(lv, pos + vec3(0, h, 0), 1);
    Creature* mate = nullptr; float md = 8;
    for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get()))
      if (c != this && c->type == type && c->alive() && !c->removed && c->loveT > 0 && !c->baby && c->realm == realm) { float d = std::hypot(c->pos.x - pos.x, c->pos.z - pos.z); if (d < md) { md = d; mate = c; } }
    if (mate) {
      if (md < 1.2f + w) { breedWith(lv, *mate); return Move{0, yaw}; }
      return Move{T->speed, std::atan2(-(mate->pos.x - pos.x), -(mate->pos.z - pos.z))};
    }
  }
  if (p && dist < 9 && holdsFood(p, *T)) return Move{dist > 2.2f ? T->speed * 1.1f : 0, toPlayer};
  // husbandry.js: whoever holds their food is followed a little further; youngsters keep close to a grown one
  if (p && dist < 10 && holdsFood(p, *T)) return Move{dist > 2.2f ? T->speed * 1.15f : 0, toPlayer};
  if (baby) {
    parentT -= dt;
    if (!ent(lv, parent) || parentT <= 0) {
      parentT = 3; parent = 0; float bd = 10;
      for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get()))
        if (c != this && c->type == type && !c->baby && c->alive() && !c->removed) { float d = std::hypot(c->pos.x - pos.x, c->pos.z - pos.z); if (d < bd) { bd = d; parent = c->id; } }
    }
    if (Entity* q = ent(lv, parent)) if (std::hypot(q->pos.x - pos.x, q->pos.z - pos.z) > 3) return Move{T->speed * 1.2f, std::atan2(-(q->pos.x - pos.x), -(q->pos.z - pos.z))};
  }
  return std::nullopt;
}
void Creature::breedWith(Level& lv, Creature& mate) {
  fed = mate.fed = true;
  loveT = mate.loveT = 0; breedCd = mate.breedCd = 300;
  Creature* baby = spawnCreature(lv, realm, type, vec3((pos.x + mate.pos.x) / 2, std::max(pos.y, mate.pos.y) + 0.1f, (pos.z + mate.pos.z) / 2));
  if (!baby) return;
  baby->makeBaby();
  if (T->woolly) baby->color = frand() < 0.5f ? color : mate.color;
  if (!variant.empty()) baby->variant = frand() < 0.5f ? variant : (mate.variant.empty() ? variant : mate.variant);
  kept = mate.kept = true;
  hearts(lv, baby->pos + vec3(0, 0.5f, 0), 7);
  lv.fx->voice(T->sound.empty() ? "pop" : T->sound, "say", baby->pos);
  if (Entity* lp = nearestPawn(lv, realm, baby->pos, 16)) grantXP(lv, *lp, 1 + (int)(frand() * 7), baby->pos + vec3(0, 1, 0));
}
void Creature::farmTick(Level& lv, float dt) {
  World& wd = lv.world(realm);
  if (breedCd > 0) breedCd -= dt;
  if (baby) { growT -= dt; if (growT <= 0) grow(lv); }
  if (T->layer && !baby) {
    eggT -= dt;
    if (eggT <= 0) { eggT = 300 + frand() * 300; dropItem(lv, realm, ItemStack("egg", 1), pos + vec3(0, 0.3f, 0), false); lv.fx->sound("pop", pos); }
  }
  if (T->woolly && sheared && onGround && frand() < dt / 45) {
    static BlockId grass = reg().blockId("grass"), dirt = reg().blockId("dirt"), tall = reg().blockId("tall_grass");
    int bx = ifloor(pos.x), by = ifloor(pos.y - 0.1f), bz = ifloor(pos.z);
    if (grass && wd.getBlock(bx, by, bz) == grass) { wd.setBlock(bx, by, bz, dirt); sheared = false; mobBurst(lv, pos + vec3(0, 0.2f, 0), "grass", 6, 0.5f); }
    else if (tall && wd.getBlock(bx, by + 1, bz) == tall) { wd.setBlock(bx, by + 1, bz, AIR); sheared = false; }
  }
}

// ------------------------------------------------------------------ pets.js
OptMove Creature::controlAI(Level& lv, float dt) {
  if (riddenBy) {   // rideAI
    auto* r = dynamic_cast<Player*>(ent(lv, riddenBy));
    if (!r || r->riding != id || r->dead) { riddenBy = 0; return std::nullopt; }
    float sp, jump; mountStats(type, sp, jump);
    grazeT = 0; sitting = sleeping = false; stay = false;
    if (rideIn.jump && onGround) { vel.y = jump; onGround = false; }
    float f = rideIn.fwd, s = rideIn.strafe;
    float vx = -std::sin(r->yaw) * f + std::cos(r->yaw) * s, vz = -std::cos(r->yaw) * f - std::sin(r->yaw) * s, mm = std::hypot(vx, vz);
    if (mm < 0.1f) return Move{0, yaw};
    return Move{sp * (rideIn.sprint ? 1.3f : 0.75f) * std::min(1.f, mm), std::atan2(-vx, -vz)};
  }
  if (owner.empty()) return std::nullopt;
  // petAI
  Player* o = ownerPawn(lv);
  if (T->ai == "swimmer" || T->ai == "flyer") {
    if (!o || stay) return std::nullopt;
    float d = std::hypot(o->pos.x - pos.x, o->pos.z - pos.z);
    if (d > 8) { if (T->ai == "flyer") flyVy = clampv((o->pos.y + 2 - pos.y) * 1.5f, -3.f, 3.f); return Move{T->run > 0 ? T->run : T->speed * 2, heading(pos.x, pos.z, o->pos.x, o->pos.z)}; }
    return std::nullopt;
  }
  Entity* f = ent(lv, petFoe);
  if (petFoe && (!isAlive(f) || std::hypot(f->pos.x - pos.x, f->pos.z - pos.z) > 24 || f == o)) { petFoe = 0; f = nullptr; }
  if (f && T->damage > 0 && !stay) {
    sitting = sleeping = false;
    bite(lv, f, T->damage);
    return Move{T->run > 0 ? T->run : T->speed * 1.8f, goToward(lv, f->pos, dt)};
  }
  if (stay) { sitting = true; sleeping = false; return Move{0, yaw}; }
  sitting = false;
  if (!o) {
    if (T->hostile) { wander(dt); Move m = wanderMove(); m.want *= 0.5f; return m; }
    return std::nullopt;
  }
  float d = std::hypot(o->pos.x - pos.x, o->pos.z - pos.z), dy = o->pos.y - pos.y;
  if ((d > 28 || std::fabs(dy) > 12) && (o->onGround || o->riding)) { if (teleportNear(lv, *o)) return Move{0, yaw}; }
  if (d > 6) return Move{d > 11 ? (T->run > 0 ? T->run : T->speed * 2) : T->speed * 1.5f, goToward(lv, o->pos, dt)};
  if (d < 2.2f) { wanderOn = false; return Move{0, heading(pos.x, pos.z, o->pos.x, o->pos.z)}; }
  wander(dt); Move m = wanderMove(); m.want *= 0.6f; return m;
}
bool Creature::teleportNear(Level& lv, Entity& o) {
  World& wd = lv.world(realm);
  int ox = ifloor(o.pos.x), oy = ifloor(o.pos.y), oz = ifloor(o.pos.z);
  for (int k = 0; k < 16; k++) {
    float a = k / 16.f * TAU; int x = ox + (int)std::round(std::cos(a) * 2), z = oz + (int)std::round(std::sin(a) * 2);
    for (int y : {oy, oy + 1, oy - 1}) {
      BlockId below = wd.getBlock(x, y - 1, z);
      if (!reg().tSolid[below] || reg().tLiquid[below]) continue;
      vec3 p(x + 0.5f, y, z + 0.5f);
      bool clear = true;
      for (int yy = y; yy < y + (int)std::ceil(h); yy++) if (reg().tSolid[wd.getBlock(x, yy, z)]) clear = false;
      if (!clear) continue;
      pos = p; vel = vec3(0); return true;
    }
  }
  return false;
}
bool isRideable(const std::string& type) { float a, b; return mountStats(type, a, b); }

void dismount(Level& lv, Creature& m, Player& p) {
  if (m.riddenBy != p.id) return;
  m.riddenBy = 0; p.riding = 0;
  World& w = lv.world(m.realm);
  for (int side : {1, -1}) {
    float a = m.yaw + side * PI / 2, x = m.pos.x - std::sin(a) * (m.w + 0.8f), z = m.pos.z - std::cos(a) * (m.w + 0.8f);
    for (int y = ifloor(m.pos.y) + 1; y >= ifloor(m.pos.y) - 1; y--)
      if (reg().tSolid[w.getBlock(ifloor(x), y - 1, ifloor(z))] && !reg().tSolid[w.getBlock(ifloor(x), y, ifloor(z))] && !reg().tSolid[w.getBlock(ifloor(x), y + 1, ifloor(z))]) { p.pos = vec3(x, y, z); p.fallDist = 0; return; }
  }
  p.pos.y = m.pos.y + m.h + 0.1f; p.fallDist = 0;
}
Creature* rideControl(Level& lv, Player& p, float fwd, float strafe, bool jump, bool sprint, bool sneak) {
  auto* m = p.riding ? dynamic_cast<Creature*>(lv.entity(p.riding)) : nullptr;
  if (!m || m->removed || !m->alive() || m->riddenBy != p.id) { p.riding = 0; return nullptr; }
  if (sneak) { dismount(lv, *m, p); return nullptr; }
  m->rideIn = {fwd, strafe, jump, sprint};
  return m;
}
void petsDefend(Level& lv, Player& owner, Entity& foe) {
  if (!isAlive(&foe) || &foe == &owner) return;
  if (auto* fc = dynamic_cast<Creature*>(&foe)) if (fc->owner == owner.name) return;
  for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get()))
    if (c->alive() && !c->removed && c->owner == owner.name && !c->stay && !c->riddenBy && c != &foe && std::hypot(c->pos.x - owner.pos.x, c->pos.z - owner.pos.z) < 20) c->petFoe = foe.id;
}

static void use1(Player& p, ItemStack& held) { if (!p.creative()) { held.n--; if (held.n <= 0) held.clear(); } }
static std::string petLabel(const Creature& e) {
  if (!e.petName.empty()) return e.petName;
  std::string l = e.T->label.empty() ? e.type : e.T->label;
  if (l.rfind("a ", 0) == 0) l = l.substr(2); else if (l.rfind("an ", 0) == 0) l = l.substr(3);
  for (auto& c : l) if (c == '_') c = ' ';
  return l;
}
bool useOnCreature(Level& lv, Player& p, Creature& e, bool sneaking) {
  if (!e.alive() || e.removed) return false;
  if (creatureHooks().use && creatureHooks().use(lv, p, e, sneaking)) return true;
  ItemStack& held = p.held();
  const ItemDef* def = held.def();
  bool mine = e.isPetOf(&p);
  std::string name = petLabel(e);
  float sp, jump; bool mountable = mountStats(e.type, sp, jump);
  auto toast = [&](const std::string& t, const std::string& k = "") { lv.fx->message(p.id, t, k); };
  // ---- pets.js petUse ----
  if (mine && !held.empty() && held.id == "saddle") {
    if (!mountable || e.baby) { toast("A saddle won't fit that"); return true; }
    if (e.saddled) { toast("It already has a saddle"); return true; }
    e.saddled = true; use1(p, held); lv.fx->soundUi(p.id, "equip"); toast("Saddled \xE2\x80\x94 use it to ride", "good"); return true;
  }
  if (mine && !held.empty() && tameFood(e.type, held.id) && e.hp < e.maxHp) {
    e.hp = std::min(e.maxHp, e.hp + 4); use1(p, held); lv.fx->soundUi(p.id, "eat"); hearts(lv, e.pos + vec3(0, e.h, 0), 2); return true;
  }
  if (e.owner.empty() && !held.empty() && held.id == "saddle" && mountable) {
    std::string foods;
    if (auto it = tameTable().find(e.type); it != tameTable().end()) for (size_t i = 0, n = 0; i < it->second.foods.size() && n < 2; i++) if (auto* d = reg().item(it->second.foods[i])) { foods += (n++ ? " or " : "") + d->name; }
    toast("Tame it first: feed it " + (foods.empty() ? std::string("its food") : foods)); return true;
  }
  if (e.owner.empty() && !held.empty() && tameFood(e.type, held.id) && (sneaking || !e.T->farm || (mountable && !e.baby))) {
    if ((e.type == "slime" || e.type == "magma_slime") && e.size > 1) { toast("Only a little one can be tamed"); return true; }
    if (e.T->boss) return false;
    use1(p, held); lv.fx->soundUi(p.id, "eat");
    float chance = p.creative() ? 1 : std::min(1.f, tameTable().at(e.type).chance);
    if (frand() < chance) {
      e.owner = p.name; e.tamed = true; e.kept = true; e.angryT = 0; e.foe = 0; e.panicT = 0; e.quarrel = 0; e.petFoe = 0; e.stay = false; e.loveT = 0;
      hearts(lv, e.pos + vec3(0, e.h, 0), 7); lv.fx->sound("levelup", e.pos, 12);
      toast("Tamed! The " + name + " is yours" + (mountable ? " (a saddle lets you ride it)" : ""), "good");
    } else mobBurst(lv, e.pos + vec3(0, e.h, 0), "smoke", 6, 0.4f);
    return true;
  }
  if (mine && held.empty()) {
    if (sneaking && e.T->carry) return false;   // Carry On picks it up (gameplay)
    if (!sneaking && mountable && e.saddled && !e.baby && !p.riding) {
      if (e.riddenBy) { toast("Someone is riding it"); return true; }
      e.riddenBy = p.id; p.riding = e.id; p.vel = vec3(0); p.yaw = e.yaw;
      toast("WASD to ride \xC2\xB7 jump \xC2\xB7 sprint to gallop \xC2\xB7 sneak to get off");
      lv.fx->voice(e.T->sound.empty() ? "pop" : e.T->sound, "say", e.pos + vec3(0, 1, 0));
      return true;
    }
    e.stay = !e.stay; e.petFoe = 0;
    toast(e.stay ? "Your " + name + " will stay here" : "Your " + name + " will follow you");
    return true;
  }
  // ---- blocks2.js useOnAnimal ----
  if (!e.T->farm) return false;
  bool did = false;
  if (!held.empty() && e.T->eats(held.id)) {
    if (e.baby) e.growT -= 60;
    else if (e.loveT > 0 || e.breedCd > 0) return true;
    else e.loveT = 30;
    use1(p, held);
    e.kept = true; hearts(lv, e.pos + vec3(0, e.h, 0), e.baby ? 2 : 5); lv.fx->soundUi(p.id, "eat");
    did = true;
  } else if (e.type == "cow" && def && def->str("bucket") == "empty" && !e.baby) {
    held.n--; ItemStack milk("milk_bucket", 1);
    if (held.n <= 0) held = milk; else if (p.give(milk) > 0) dropItem(lv, p.realm, milk, p.pos + vec3(0, 1, 0), false);
    lv.fx->soundUi(p.id, "bucket"); e.kept = true; did = true;
  } else if (e.T->woolly && def && def->has("shears") && !e.sheared && !e.baby) {
    e.sheared = true; e.kept = true;
    dropItem(lv, e.realm, ItemStack(e.color == "white" || e.color.empty() ? "wool" : e.color + "_wool", 1 + (int)(frand() * 3)), e.pos + vec3(0, 1, 0), true);
    if (!p.creative()) { held.d++; if (def->hasTool && def->tool.durability > 0 && held.d >= def->tool.durability) held.clear(); }
    lv.fx->soundUi(p.id, "shear"); did = true;
  } else if (e.T->woolly && def && def->has("dye") && def->str("dye") != e.color) {
    e.color = def->str("dye"); e.kept = true; use1(p, held); lv.fx->soundUi(p.id, "click"); did = true;
  }
  if (did) e.fed = true;   // husbandry.js: fed or bred, tame enough not to bolt
  return did;
}

// ------------------------------------------------------------------ dismember.js: going back for a weapon
OptMove Creature::fetchWeapon(Level& lv, float dt) {
  if (!regrab.on) return std::nullopt;
  regrab.t -= dt;
  Entity* it = ent(lv, regrab.item);
  if (!it || it->type != "item") {
    it = nullptr; regrab.item = 0;
    for (auto& e : lv.entities()) if (e->type == "item" && !e->removed && e->realm == realm && e->data.contains("stack") && e->data["stack"].value("id", "") == regrab.id && std::hypot(e->pos.x - pos.x, e->pos.z - pos.z) < 8) { it = e.get(); regrab.item = e->id; break; }
  }
  if (!it || regrab.t <= 0) { regrab.on = false; return std::nullopt; }
  float d = std::hypot(it->pos.x - pos.x, it->pos.z - pos.z);
  if (d < 1.1f && std::fabs(it->pos.y - pos.y) < 1.5f) {
    it->removed = true; offItem = regrab.id; regrab.on = false;
    lv.fx->sound("pop", pos);
    return std::nullopt;
  }
  auto nd = navDir(lv, it->pos, dt);
  return Move{T->speed * (crawling ? 0.35f : 1), nd ? *nd : std::atan2(-(it->pos.x - pos.x), -(it->pos.z - pos.z))};
}
void Creature::dropWeapon(Level& lv, bool main) {
  std::string& slot = main ? heldItem : offItem;
  std::string itemId = slot; slot.clear();
  if (itemId.empty()) return;
  Entity* it = dropItem(lv, realm, ItemStack(itemId, 1), pos + vec3(0, 1, 0), true);
  if (!lost.count("armL") && !lost.count("foreL") && T->j.value("regrab", true) != false) { regrab.on = true; regrab.id = itemId; regrab.item = it ? it->id : 0; regrab.t = 8; }
}

} // namespace dq
