// sim/creature_monsters.cpp — the hostile brains: game/nightmobs.js (skeletons, spiders, fuselings, slimes),
// game/monsters.js (0.8.3 monsters), game/quarrels.js (infighting, calling for help, noticing then
// remembering, shade-seeking, wariness), game/skymobs.js (the cockatrice) and the Cinder Golem's
// telegraphed attacks (game/entities.js bossAI).
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/explosion.h"
#include "sim/mob_combat.h"
#include "sim/physics.h"
#include "sim/player.h"
#include "core/registry.h"

namespace dq {

void strandedFlop(Level& lv, Creature& m, float dt, float air, float every, float dmg, bool heavy);   // creature_beasts.cpp

static bool rule(Level& lv, const char* k) { return lv.info.rules.value(k, true); }
static void shakeNear(Level& lv, vec3 p, int realm, float s) { for (Entity* q : pawnsOf(lv, realm)) if (glm::distance(q->pos, p) < 24) lv.fx->shake(q->id, s); }
static bool solidAt(World& w, float x, float y, float z) { return reg().tSolid[w.getBlock(ifloor(x), ifloor(y), ifloor(z))]; }

// ------------------------------------------------------------------ quarrels.js
Entity* Creature::aiTarget(Level& lv, Entity* p) {
  if (quarrel) {
    auto* q = dynamic_cast<Creature*>(ent(lv, quarrel));
    if (!q || !q->alive() || !(quarrelT > 0) || std::hypot(q->pos.x - pos.x, q->pos.z - pos.z) > 32 || std::fabs(q->pos.y - pos.y) > 16) quarrel = 0;
    else if (T->special || (T->hostile && T->ai.empty())) return q;
  }
  return p;
}

static bool findShade(Level& lv, Creature& m, vec3& out) {
  World& w = lv.world(m.realm);
  const Registry& R = reg();
  int x0 = ifloor(m.pos.x), y0 = ifloor(m.pos.y), z0 = ifloor(m.pos.z);
  float bd = 1e9f; bool found = false;
  for (int k = 0; k < 24; k++) {
    int x = x0 + (int)(frand() * 17) - 8, z = z0 + (int)(frand() * 17) - 8;
    for (int y = y0 + 3; y >= y0 - 4; y--) {
      if (R.tSolid[w.getBlock(x, y - 1, z)] && !R.tSolid[w.getBlock(x, y, z)] && !R.tSolid[w.getBlock(x, y + 1, z)]) {
        bool water = R.tLiquid[w.getBlock(x, y, z)] == 1;
        if (water || (w.getLight(x, y + 1, z) >> 4) < 14) { float d = std::hypot(x - x0, z - z0); if (d < bd) { bd = d; out = vec3(x + 0.5f, y, z + 0.5f); found = true; } }
        break;
      }
    }
  }
  return found;
}

// ------------------------------------------------------------------ shared know-how (monsters.js / nightmobs.js)
bool Creature::lineOfSight(Level& lv, Entity& p) const {
  vec3 e = pos + vec3(0, h * 0.85f, 0);
  vec3 t = p.pos + vec3(0, p.isPlayer() ? 1.5f : p.h * 0.8f, 0) - e;
  float l = glm::length(t);
  if (l < 1) return true;
  RayHit hit = raycastBlocks(lv.world(realm), glm::dvec3(e), t / l, l, false);
  return !hit.hit || hit.dist >= l - 0.5f;
}
Creature::Senses Creature::senses(Level& lv, Entity* p, float dist, float dyp, float dt, float range) {
  Senses s;
  s.near = p && dist < range && std::fabs(dyp) < 12;
  losT -= dt;
  if (losT <= 0) { losT = 0.2f + frand() * 0.12f; sees = s.near && lineOfSight(lv, *p); }
  s.sees = s.near && sees;
  if (s.sees) huntT = 10; else if (huntT > 0) huntT -= dt;
  s.hunting = s.near && (s.sees || huntT > 0 || dist < 3.5f || alertT > 0 || (p && quarrel == p->id));
  return s;
}
Move Creature::chase(Level& lv, Entity& p, float dist, float toPlayer, float dt, float k) {
  std::optional<float> nd; if (dist > 2.2f) nd = navDir(lv, p.pos, dt);
  return {dist < 0.9f ? 0 : T->speed * k, nd ? *nd : toPlayer};
}
void Creature::voiceTick(Level& lv, float dt, float dist, float lo, float span) {
  soundT -= dt;
  if (soundT < 0) { soundT = lo + frand() * span; if (dist < 18 && !T->voice.empty()) lv.fx->voice(T->voice, "say", pos + vec3(0, h * 0.8f, 0)); }
}
void Creature::meleeAt(Level& lv, Entity& p, float dist, float dyp) {
  if (dist < 1.35f + w && std::fabs(dyp) < 1.6f && attackCd <= 0) {
    attackCd = 1.1f; swingT = 0.3f;
    float dmg = T->damage * (type == "slime" || type == "magma_slime" ? std::max(1, size - 1) : 1) + weaponBonus();
    damageEntity(lv, p, dmg, this, "mob");
  }
}
static void shootAt(Level& lv, Creature& m, Entity& p) {
  float sp = 22;
  vec3 e = m.pos + vec3(-std::sin(m.yaw) * 0.4f, 1.45f, -std::cos(m.yaw) * 0.4f);
  float tx = p.pos.x - e.x, tz = p.pos.z - e.z, hd = std::hypot(tx, tz), t = hd / sp;
  float ty = p.pos.y + 1.2f - e.y + 0.5f * 20 * t * t;   // aim high for the drop
  float l = std::sqrt(tx * tx + ty * ty + tz * tz); if (l < 1e-3f) l = 1;
  float spread = 0.035f;
  vec3 v((tx / l + (frand() - 0.5f) * spread) * sp, (ty / l + (frand() - 0.5f) * spread) * sp, (tz / l + (frand() - 0.5f) * spread) * sp);
  spawnArrow(lv, m.realm, e, v, &m, 5, false);
  lv.fx->sound("bow", e);
  m.swingT = 0.3f;
}
static void throwFlask(Level& lv, Creature& m, Entity& p) {
  vec3 e = m.pos + vec3(0, 1.6f, 0); float sp = 11;
  float tx = p.pos.x - e.x, tz = p.pos.z - e.z, hd = std::hypot(tx, tz); if (hd < 1e-3f) hd = 1;
  float t = hd / sp, vy = ((p.pos.y + 0.8f) - e.y) / t + 0.5f * 20 * t;
  std::string kind = playerEffect(p, "poison") > 0 ? "slow" : frand() < 0.6f ? "poison" : "slow";
  spawnHexFlask(lv, m.realm, e, vec3(tx / hd * sp, vy, tz / hd * sp), &m, kind);
  m.swingT = 0.3f;
  lv.fx->sound("bow", e);
}
static void fuseBurst(Level& lv, Creature& m) {
  m.removed = true; m.deathTime = 0.0001f;
  ExplodeOpts o; o.noBlocks = !rule(lv, "mobGriefing"); o.source = m.id;
  explode(lv, m.realm, m.pos + vec3(0, 0.6f, 0), 3, o);
}

// ------------------------------------------------------------------ spiders: ground, walls and ceilings
static Move spiderAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, bool near, float dt) {
  World& w = lv.world(m.realm);
  const CreatureType& T = *m.T;
  float sky = m.light(lv, 0.5f).x;
  bool angry = m.panicT > 0 || envDay(lv, dimAt(m)) < 0.5f || sky < 10;
  if (m.panicT > 0) m.panicT -= dt;
  float fx = -std::sin(m.yaw), fz = -std::cos(m.yaw), reach = m.w + 0.18f;
  auto front = [&](float dy) { return solidAt(w, m.pos.x + fx * reach, m.pos.y + dy, m.pos.z + fz * reach); };
  auto roofAt = [&](float x, float z) { return solidAt(w, x, m.pos.y + m.h + 0.15f, z); };
  auto land = [&] { m.surf = 0; m.noGravity = false; m.hasFlyVy = false; m.climbing = false; };
  if (m.burn > 0 || m.hurtTime > 0.3f) { if (m.surf != 0) land(); }
  OptMove res;
  if (m.surf == 0 && m.hitWall && m.lastWant > 0.5f && front(0.3f)) { m.surf = 1; m.wallYaw = m.yaw; }
  if (m.surf == 1) {
    m.noGravity = true; m.climbing = true;
    if (!front(0.15f) && !front(m.h * 0.6f)) { land(); m.vel.y = 3.2f; m.vel.x += fx * 2.5f; m.vel.z += fz * 2.5f; }     // over the top
    else if (roofAt(m.pos.x, m.pos.z)) m.surf = 2;                                                                         // a roof: carry on upside down
    else {
      bool down = p && angry && near && dyp < -1.5f && dist < 4;
      m.vel.y = down ? -2.4f : 2.7f;
      if (down && m.onGround) land();
      res = Move{0.7f, m.wallYaw};
    }
  }
  if (m.surf == 2) {
    m.noGravity = true; m.climbing = true; m.vel.y = 1.4f;
    if (!roofAt(m.pos.x, m.pos.z)) land();
    else if (p && angry && near && std::hypot(p->pos.x - m.pos.x, p->pos.z - m.pos.z) < 1.5f && dyp < -0.5f) {
      land(); m.vel.y = -2; m.pounceCd = 0.4f;
      lv.fx->voice(T.voice.empty() ? "spider" : T.voice, "say", m.pos);
    } else {
      float dir;
      if (near && angry) dir = toPlayer; else { m.wander(dt); dir = m.wanderDir; }
      float nx = m.pos.x - std::sin(dir) * 0.8f, nz = m.pos.z - std::cos(dir) * 0.8f;
      if (!roofAt(nx, nz)) { dir += PI; m.wanderDir = dir; }
      res = Move{T.speed * (near && angry ? 0.85f : 0.4f), dir};
    }
  }
  float tp = m.surf == 1 ? PI / 2 : 0, tr = m.surf == 2 ? PI : 0, k = std::min(1.f, dt * 9);
  m.tp += (tp - m.tp) * k; m.tr += (tr - m.tr) * k;
  m.hasTilt = std::fabs(m.tp) + std::fabs(m.tr) > 0.01f; m.surfaceTilt = vec2(m.tp, m.tr);
  if (res) return *res;
  if (!near || !angry) { m.wander(dt); return m.wanderMove(); }
  m.soundT -= dt;
  if (m.soundT < 0) { m.soundT = 4 + frand() * 6; if (dist < 16) lv.fx->voice(T.voice.empty() ? "spider" : T.voice, "say", m.pos); }
  m.pounceCd -= dt;
  int legsLost = 0; for (auto& n : m.lost) if (n.rfind("sleg", 0) == 0) legsLost++;
  if (m.onGround && dist < 3.6f && dist > 1.2f && m.pounceCd <= 0 && std::fabs(dyp) < 1.5f && legsLost < 3) {
    m.pounceCd = 1.4f + frand() * 0.8f;
    float l = dist > 0 ? dist : 1;
    m.vel.y = 5.6f; m.vel.x = (p->pos.x - m.pos.x) / l * 7; m.vel.z = (p->pos.z - m.pos.z) / l * 7;
  }
  m.meleeAt(lv, *p, dist, dyp);
  return {dist < 0.9f ? 0 : T.speed * std::max(0.3f, 1 - legsLost * 0.12f), toPlayer};
}

// ------------------------------------------------------------------ nightmobs.js: the four night creatures (`as` = whose brain)
static Move nightBase(Level& lv, Creature& m, const std::string& as, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  const CreatureType& T = *m.T;
  if ((as == "slime") && !m.size) m.setSize(2);
  m.soundT -= dt;
  bool near = p && dist < 20 && std::fabs(dyp) < 10;
  m.losT -= dt;
  if (m.losT <= 0) { m.losT = 0.2f + frand() * 0.1f; m.sees = near && m.lineOfSight(lv, *p); }
  bool sees = near && m.sees;
  if (dist > 72) m.removed = true;
  if (as == "skeleton") {
    if (!near) { m.wander(dt); return m.wanderMove(); }
    if (m.soundT < 0) { m.soundT = 5 + frand() * 6; if (dist < 16) lv.fx->voice(T.voice.empty() ? "rattle" : T.voice, "say", m.pos); }
    if (m.shootT < -0.5f) m.shootT = 1 + frand();
    m.shootT -= dt;
    m.aiming = sees && dist < 16 && m.heldItem == "bow";
    if (m.heldItem.empty()) { m.meleeAt(lv, *p, dist, dyp); return {dist < 0.9f ? 0 : T.speed, toPlayer}; }   // lost its bow: comes at you
    if (m.aiming && m.shootT <= 0) { m.shootT = 1.6f + frand() * 1.2f; shootAt(lv, m, *p); }
    m.strafeT -= dt;
    if (m.strafeT <= 0) { m.strafeT = 1.5f + frand() * 2; m.strafe = frand() < 0.5f ? -1 : 1; }
    if (!sees || dist > 11) { auto nd = m.navDir(lv, p->pos, dt); return {T.speed, nd ? *nd : toPlayer}; }
    if (dist < 5) return {T.speed * 0.9f, toPlayer + PI + m.strafe * 0.4f};
    return {T.speed * 0.45f, toPlayer + m.strafe * PI / 2};
  }
  if (as == "spider") return spiderAI(lv, m, p, dist, dyp, toPlayer, near, dt);
  if (as == "fuseling") {
    if (near && dist < 2.8f && std::fabs(dyp) < 2.2f && sees) {
      if (m.fuse == 0) lv.fx->sound("hiss", m.pos, 20);
      m.fuse += dt;
      if (m.fuse >= 1.5f) fuseBurst(lv, m);
      return {0, toPlayer};
    }
    if (m.fuse > 0) m.fuse = std::max(0.f, m.fuse - dt * 1.5f);
    if (!near) { m.wander(dt); return m.wanderMove(); }
    std::optional<float> nd; if (dist > 2.2f) nd = m.navDir(lv, p->pos, dt);
    return {T.speed, nd ? *nd : toPlayer};
  }
  if (as == "slime") {
    if (m.soundT < 0 || (m.onGround && !m.data.value("wasGround", true))) { m.soundT = 3 + frand() * 4; if (dist < 14) lv.fx->voice(T.voice.empty() ? "squish" : T.voice, "say", m.pos); }
    m.data["wasGround"] = m.onGround;
    if (m.onGround && (m.hitWall || frand() < dt * (near ? 1.4f : 0.4f))) m.wantJump = true;
    if (!near) { m.wander(dt); return m.wanderMove(); }
    if (m.size > 1) m.meleeAt(lv, *p, dist, dyp);
    return {T.speed * (m.size > 1 ? 1 : 1.2f), toPlayer};
  }
  m.wander(dt); return m.wanderMove();
}

// ------------------------------------------------------------------ monsters.js
static Move zombieAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  auto s = m.senses(lv, p, dist, dyp, dt, 24);
  m.voiceTick(lv, dt, dist);
  if (!s.hunting) { m.wander(dt); return m.wanderMove(); }
  m.meleeAt(lv, *p, dist, dyp);
  return m.chase(lv, *p, dist, toPlayer, dt);
}

static OptMove monsterAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  const std::string& t = m.type;
  const CreatureType& T = *m.T;
  int dim = dimAt(m);
  float day = envDay(lv, dim);
  if (t == "husk") return zombieAI(lv, m, p, dist, dyp, toPlayer, dt);
  if (t == "drowned") {
    if (!m.inWater) { m.swimVy = 0; return zombieAI(lv, m, p, dist, dyp, toPlayer, dt); }
    auto s = m.senses(lv, p, dist, dyp, dt, 24);
    m.voiceTick(lv, dt, dist, 6, 8);
    if (!s.hunting) { m.swimVy = std::sin(m.age * 0.6f + m.id) * 0.5f - 0.3f; m.wander(dt); return m.wanderMove(); }
    m.swimVy = clampv(((p->pos.y + 0.4f) - m.pos.y) * 2.5f, -3.2f, 3.2f);
    m.meleeAt(lv, *p, dist, dyp);
    return Move{dist < 0.8f ? 0 : T.speed * 1.15f, toPlayer};
  }
  if (t == "stray") return nightBase(lv, m, "skeleton", p, dist, dyp, toPlayer, dt);
  if (t == "cave_spider") return nightBase(lv, m, "spider", p, dist, dyp, toPlayer, dt);
  if (t == "magma_slime") {
    if (m.onGround && m.lastWant > 0.1f && frand() < dt * 0.6f) m.wantJump = true;   // they leap higher than the green ones
    return nightBase(lv, m, "slime", p, dist, dyp, toPlayer, dt);
  }
  if (t == "witch") {
    auto s = m.senses(lv, p, dist, dyp, dt, 18);
    m.voiceTick(lv, dt, dist, 6, 9);
    m.drinkCd -= dt;
    if (m.drinkT > 0) {
      m.drinkT -= dt; m.heldItem = "glass_bottle";
      if (m.drinkT <= 0) { m.hp = std::min(m.maxHp, m.hp + 8); m.heldItem.clear(); lv.fx->sound("burp", m.pos + vec3(0, 1.5f, 0)); mobBurst(lv, m.pos + vec3(0, 1.8f, 0), "white", 6, 0.4f); }
      return Move{0, toPlayer};
    }
    if (m.hp < m.maxHp * 0.5f && m.drinkCd <= 0) { m.drinkT = 1.4f; m.drinkCd = 12; lv.fx->sound("eat", m.pos + vec3(0, 1.5f, 0)); return Move{0, toPlayer}; }
    if (!s.hunting) { m.wander(dt); return m.wanderMove(); }
    m.throwT -= dt;
    if (s.sees && dist < 11 && m.throwT <= 0) { m.throwT = 2.6f + frand() * 1.2f; throwFlask(lv, m, *p); }
    if (dist < 4) return Move{T.speed, toPlayer + PI + std::sin(m.age) * 0.5f};
    if (dist > 8 || !s.sees) return m.chase(lv, *p, dist, toPlayer, dt);
    return Move{T.speed * 0.3f, toPlayer + PI / 2 * (std::sin(m.age * 0.5f) > 0 ? 1 : -1)};
  }
  if (t == "stone_mite") {
    auto s = m.senses(lv, p, dist, dyp, dt, 16);
    m.voiceTick(lv, dt, dist, 2, 4);
    if (!s.hunting) { m.wander(dt); return m.wanderMove(); }
    if (dist < 0.9f + m.w && std::fabs(dyp) < 1.2f && m.attackCd <= 0) { m.attackCd = 0.6f; m.swingT = 0.2f; DamageSrc d; d.knock = 1.5f; damageEntity(lv, *p, T.damage, &m, "mob", d); }
    std::optional<float> nd; if (dist > 2) nd = m.navDir(lv, p->pos, dt);
    return Move{dist < 0.5f ? 0 : T.speed, nd ? *nd : toPlayer};
  }
  if (t == "wraith") {
    m.noGravity = true; m.hasFlyVy = true;
    if (dim == DIM_OVERWORLD && day > 0.55f && m.light(lv, 1).x >= 12) {   // daylight undoes them
      m.removed = true; mobBurst(lv, m.pos + vec3(0, 1, 0), "white", 18, 0.8f); lv.fx->voice("wraith", "death", m.pos + vec3(0, 1, 0));
      return Move{0, m.yaw};
    }
    auto s = m.senses(lv, p, dist, dyp, dt, 28);
    m.voiceTick(lv, dt, dist, 7, 9);
    m.retreatT -= dt;
    if (!s.hunting) { m.flyVy = std::sin(m.age * 0.8f) * 0.6f; m.wander(dt); return m.wanderMove(); }
    float hover = m.retreatT > 0 ? 2.6f : 0.4f;
    m.flyVy = clampv(((p->pos.y + hover) - m.pos.y) * 1.8f, -3.f, 3.f);
    if (m.retreatT > 0) return Move{T.speed * 0.8f, toPlayer + PI + 0.6f};
    if (dist < 1.5f + m.w && std::fabs(dyp) < 1.8f && m.attackCd <= 0) {
      m.attackCd = 1.6f; m.swingT = 0.4f;
      DamageSrc d; d.knock = 2; damageEntity(lv, *p, T.damage, &m, "mob", d);
      m.retreatT = 1.2f + frand();
    }
    return Move{T.speed, toPlayer};
  }
  if (t == "cinder_wisp") {
    World& w = lv.world(m.realm);
    m.noGravity = true; m.hasFlyVy = true;
    if (m.inWater) { DamageSrc d; d.type = "drown"; m.hurt(lv, 1, d); }
    int floor = 0; for (int k = 1; k <= 6; k++) { if (solidAt(w, m.pos.x, m.pos.y - k, m.pos.z)) break; floor = k; }
    auto s = m.senses(lv, p, dist, dyp, dt, 26);
    m.voiceTick(lv, dt, dist, 3, 4);
    if (frand() < dt * 8) flame(lv, m.pos + vec3((frand() - 0.5f) * 0.6f, 0.4f + frand() * 0.6f, (frand() - 0.5f) * 0.6f));
    if (!s.hunting) { m.flyVy = floor < 2 ? 0.8f : floor > 4 ? -0.6f : std::sin(m.age) * 0.3f; m.wander(dt); return m.wanderMove(); }
    float wantY = std::max(p->pos.y + 2, m.pos.y - floor + 2.5f);
    m.flyVy = clampv((wantY - m.pos.y) * 1.5f, -2.f, 2.f);
    m.volleyT -= dt;
    if (m.volleyT <= 0 && s.sees && dist < 20) {
      m.shots++;
      vec3 e = m.pos + vec3(0, 0.6f, 0), d = p->pos + vec3(0, 1, 0) - e;
      float l = glm::length(d); if (l < 1e-3f) l = 1;
      auto jit = [] { return (frand() - 0.5f) * 0.08f; };
      spawnProjectile(lv, m.realm, e, vec3(d.x / l + jit(), d.y / l + jit(), d.z / l + jit()) * 13.f, "fireball", &m);
      lv.fx->sound("ignite", e);
      if (m.shots >= 3) { m.shots = 0; m.volleyT = 3.5f + frand() * 1.5f; } else m.volleyT = 0.35f;
    }
    if (dist < 5) return Move{T.speed * 0.6f, toPlayer + PI};
    if (dist > 12 || !s.sees) return Move{T.speed, toPlayer};
    return Move{T.speed * 0.3f, toPlayer + PI / 2};
  }
  if (t == "raider") {
    if (!m.offItem.empty() && m.heldItem.empty() && !m.lost.count("armR") && !m.lost.count("foreR")) { m.heldItem = m.offItem; m.offItem.clear(); }
    auto s = m.senses(lv, p, dist, dyp, dt, 24);
    m.voiceTick(lv, dt, dist, 6, 10);
    if (!s.hunting) {
      auto* lead = dynamic_cast<Creature*>(m.ent(lv, m.leader));
      if (lead && lead->alive() && std::hypot(lead->pos.x - m.pos.x, lead->pos.z - m.pos.z) > 5) {
        auto nd = m.navDir(lv, lead->pos, dt);
        return Move{T.speed * 0.7f, nd ? *nd : std::atan2(m.pos.x - lead->pos.x, m.pos.z - lead->pos.z)};
      }
      m.wander(dt); return m.wanderMove();
    }
    if (s.sees && dist < 16 && !m.yelled) { m.yelled = true; lv.fx->voice("raider", "say", m.pos + vec3(0, 1.6f, 0)); }
    m.meleeAt(lv, *p, dist, dyp);
    return m.chase(lv, *p, dist, toPlayer, dt, m.heldItem.empty() ? 1.1f : 1);
  }
  if (t == "werewolf") {
    if (dim == DIM_OVERWORLD && day > 0.6f) {   // morning: it runs for the trees and is gone
      m.dawnT += dt;
      if (m.dawnT > 6 || (dist > 24 && m.dawnT > 1)) { m.removed = true; return Move{0, m.yaw}; }
      return Move{T.speed, p ? toPlayer + PI : m.yaw};
    }
    auto s = m.senses(lv, p, dist, dyp, dt, 32);
    m.soundT -= dt;
    if (m.soundT < 0) { m.soundT = 8 + frand() * 10; if (dist < 48) lv.fx->voice("werewolf", s.hunting ? "angry" : "say", m.pos + vec3(0, 1.8f, 0)); }
    if (!s.hunting) { m.wander(dt); Move mv = m.wanderMove(); mv.want *= 1.4f; return mv; }
    m.leapCd -= dt;
    if (m.onGround && dist < 6 && dist > 2.2f && m.leapCd <= 0 && std::fabs(dyp) < 2 && s.sees) {
      m.leapCd = 2.2f + frand();
      float l = dist > 0 ? dist : 1; m.vel.y = 6.4f; m.vel.x = (p->pos.x - m.pos.x) / l * 9; m.vel.z = (p->pos.z - m.pos.z) / l * 9;
      lv.fx->voice("werewolf", "angry", m.pos + vec3(0, 1.8f, 0));
    }
    m.meleeAt(lv, *p, dist, dyp);
    return m.chase(lv, *p, dist, toPlayer, dt);
  }
  if (t == "ogre") {
    auto s = m.senses(lv, p, dist, dyp, dt, 22);
    m.voiceTick(lv, dt, dist, 6, 8);
    if (m.slamT > 0) {   // a long wind-up (club high), then down it comes
      m.slamT -= dt; m.swingT = 0.35f; m.lockMove = false;
      if (m.slamT <= 0) {
        vec3 f(m.pos.x - std::sin(m.yaw) * 1.4f, m.pos.y, m.pos.z - std::cos(m.yaw) * 1.4f);
        shakeNear(lv, m.pos, m.realm, 0.45f);
        mobBurst(lv, f + vec3(0, 0.1f, 0), "dirt", 26, 3);
        lv.fx->sound("thud", f); lv.fx->sound("explode", f, 18);
        auto hit = [&](Entity& q, bool pawn) {
          float d = std::hypot(q.pos.x - f.x, q.pos.z - f.z);
          if (d > 3.2f || std::fabs(q.pos.y - m.pos.y) > 2.5f) return;
          float k = 1 - d / 3.6f;
          DamageSrc ds; ds.knock = 9 * k + 3;
          damageEntity(lv, q, std::round(T.damage * (0.5f + 0.5f * k)) + (m.heldItem.empty() ? 0 : 2), &m, "mob", ds);
          if (pawn) q.vel.y = std::max(q.vel.y, 7 * k + 2);
        };
        for (Entity* q : pawnsOf(lv, m.realm)) hit(*q, true);
        for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (c != &m && c->alive() && !c->removed && c->realm == m.realm && !c->T->hostile) hit(*c, false);
        m.attackCd = 2.2f;
      }
      return Move{0, toPlayer};
    }
    if (!s.hunting) { m.wander(dt); Move mv = m.wanderMove(); mv.want *= 0.7f; return mv; }
    if (dist < 3 && std::fabs(dyp) < 2.5f && m.attackCd <= 0) { m.slamT = 0.9f; lv.fx->voice("ogre", "angry", m.pos + vec3(0, 2.4f, 0)); return Move{0, toPlayer}; }
    return m.chase(lv, *p, dist, toPlayer, dt);
  }
  if (t == "crocodile") {
    m.voiceTick(lv, dt, dist, 10, 14);
    Entity* foe = m.quarrel ? m.ent(lv, m.quarrel) : (m.angryT > 0 ? m.ent(lv, m.foe) : nullptr);
    if (m.angryT > 0) m.angryT -= dt;
    Entity* target = foe ? foe : p;
    bool near = target && dist < 7 && std::fabs(dyp) < 2.5f && !pawnCreative(target);
    if (m.inWater) m.swimVy = m.lungeT > 0 ? 0 : clampv((std::floor(m.pos.y) + 0.65f - m.pos.y) * 3, -1.f, 1.f);
    if (m.lungeT > 0) {
      m.lungeT -= dt;
      if (m.bite(lv, target, T.damage)) { m.lungeT = 0; m.restT = 2.5f; }
      return Move{T.run, toPlayer};
    }
    m.restT -= dt;
    if ((foe || (near && dist < (m.inWater ? 6 : 3.5f))) && m.restT <= 0) {
      if (dist < 7) { m.lungeT = 0.8f; if (m.onGround || m.inWater) m.vel.y = m.inWater ? 2 : 3.5f; lv.fx->voice("croc", "hurt", m.pos + vec3(0, 0.4f, 0)); }
      return Move{T.speed * 1.4f, toPlayer};
    }
    m.wander(dt);
    Move mv = m.wanderMove(); mv.want *= m.inWater ? 0.6f : 0.4f;
    return mv;
  }
  if (t == "shark") {
    if (!m.inWater) { strandedFlop(lv, m, dt, 8, 1.5f, 2, true); return Move{0, m.yaw}; }   // aquatic.js
    m.flopT = 0;
    bool targetIn = p && p->inWater;
    Entity* foe = m.quarrel ? m.ent(lv, m.quarrel) : (targetIn && dist < 18 && std::fabs(dyp) < 10 && !pawnCreative(p) ? p : nullptr);
    if (foe) {
      m.swimVy = clampv(((foe->pos.y + 0.5f) - m.pos.y) * 2, -3.f, 3.f);
      if (std::hypot(foe->pos.x - m.pos.x, foe->pos.z - m.pos.z) < 1.6f + m.w && std::fabs(foe->pos.y - m.pos.y) < 1.6f && m.attackCd <= 0) {
        m.attackCd = 1.3f; m.swingT = 0.3f;
        DamageSrc d; d.knock = 3; damageEntity(lv, *foe, T.damage, &m, "mob", d);
        lv.fx->sound("splash_small", m.pos + vec3(0, 0.5f, 0));
      }
      return Move{T.speed * 1.2f, toPlayer};
    }
    m.swimVy = std::sin(m.age * 0.4f + m.id) * 0.5f;
    m.wander(dt); m.wanderOn = true;
    return Move{T.speed * 0.45f, m.wanderDir};
  }
  if (T.snake) {
    Entity* foe = m.quarrel ? m.ent(lv, m.quarrel) : (m.angryT > 0 ? m.ent(lv, m.foe) : nullptr);
    if (m.angryT > 0) m.angryT -= dt;
    if (m.striking > 0) m.striking -= dt;
    Entity* tgt = foe ? foe : p;
    float td = foe ? std::hypot(foe->pos.x - m.pos.x, foe->pos.z - m.pos.z) : dist;
    bool quiet = tgt && pawnSneaking(tgt);
    float warnR = quiet ? 1.6f : 4.5f, strikeR = quiet ? 0.9f : 2.2f;
    m.rattling = false; m.coiled = false;
    if (tgt && !pawnCreative(tgt) && std::fabs(dyp) < 1.6f && (foe || td < warnR)) {
      m.coiled = true;
      if (t == "rattlesnake") { m.rattling = true; m.rattleT -= dt; if (m.rattleT <= 0) { m.rattleT = 1.2f; lv.fx->voice("rattlesnake", "say", m.pos); } }
      else { m.soundT -= dt; if (m.soundT < 0) { m.soundT = 2.5f; lv.fx->voice(T.voice, "say", m.pos); } }
      m.strikeCd -= dt;
      if ((foe || td < strikeR) && m.strikeCd <= 0 && td < 2.6f) {
        m.strikeCd = 1.4f; m.striking = 0.3f; m.tongueOut = true;
        float l = td > 0 ? td : 1; m.vel.x = (tgt->pos.x - m.pos.x) / l * 6; m.vel.z = (tgt->pos.z - m.pos.z) / l * 6; m.vel.y = 1.5f;
        if (td < 1.6f + m.w) { DamageSrc d; d.knock = 1; damageEntity(lv, *tgt, T.damage, &m, "mob", d); }
      }
      return Move{foe && td > 1.2f ? T.speed * 1.4f : 0, std::atan2(-(tgt->pos.x - m.pos.x), -(tgt->pos.z - m.pos.z))};
    }
    m.tongueOut = std::sin(m.age * 3 + m.id) > 0.85f;
    m.wander(dt);
    Move mv = m.wanderMove(); mv.want *= 0.7f;
    return mv;
  }
  if (t == "scorpion") {
    if (m.striking > 0) m.striking -= dt;
    bool night = day < 0.45f || m.light(lv, 0.3f).x < 10;
    auto s = m.senses(lv, p, dist, dyp, dt, 16);
    bool angry = night || m.panicT > 0 || m.quarrel;
    if (m.panicT > 0) m.panicT -= dt;
    if (!angry || !s.hunting) { m.wander(dt); Move mv = m.wanderMove(); mv.want *= 0.8f; return mv; }
    m.voiceTick(lv, dt, dist, 4, 6);
    if (dist < 1.4f + m.w && std::fabs(dyp) < 1.4f && m.attackCd <= 0) {
      m.attackCd = 1.0f; m.swingT = 0.25f;
      m.stings = (m.stings + 1) % 2;
      DamageSrc d;
      if (m.stings) { m.striking = 0.4f; d.knock = 2; damageEntity(lv, *p, T.damage + 1, &m, "mob", d); }
      else { d.knock = 3; d.noEffect = true; damageEntity(lv, *p, T.damage, &m, "mob", d); }
    }
    return m.chase(lv, *p, dist, toPlayer, dt);
  }
  if (t == "cockatrice") {   // skymobs.js
    auto s = m.senses(lv, p, dist, dyp, dt, 20);
    m.voiceTick(lv, dt, dist, 4, 6);
    if (!s.hunting) { m.wander(dt); return m.wanderMove(); }
    if (dist < 1.3f + m.w && std::fabs(dyp) < 1.8f && m.attackCd <= 0) { m.attackCd = 1.1f; m.swingT = 0.3f; DamageSrc d; d.knock = 2; damageEntity(lv, *p, T.damage, &m, "mob", d); }
    std::optional<float> nd; if (dist > 2.2f) nd = m.navDir(lv, p->pos, dt);
    return Move{dist < 0.9f ? 0 : T.speed, nd ? *nd : toPlayer};
  }
  return std::nullopt;
}

// The outer layer of every special creature's thinking (quarrels.js), then the 0.8.3 brains
// (monsters.js), then the four night creatures (nightmobs.js).
Move Creature::specialAI(Level& lv, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  int dim = dimAt(*this);
  // skeletons and strays caught in the sun look for shade
  if (T->fleesSun && burn > 0 && dim == DIM_OVERWORLD && envDay(lv, dim) > 0.6f && !inWater && !quarrel) {
    shadeT -= dt;
    if (!hasShade || shadeT <= 0) { hasShade = findShade(lv, *this, shade); shadeT = 3; }
    if (hasShade && std::hypot(shade.x - pos.x, shade.z - pos.z) > 0.6f) {
      auto nd = navDir(lv, shade, dt);
      return {T->speed * 1.2f, nd ? *nd : std::atan2(pos.x - shade.x, pos.z - shade.z)};
    }
  } else hasShade = false;
  // keep away from what hunts you
  const char* wa[2] = {nullptr, nullptr};
  if (type == "skeleton" || type == "stray") wa[0] = "wolf";
  else if (type == "fuseling") { wa[0] = "ocelot"; wa[1] = "cat"; }
  if (wa[0]) {
    waryT -= dt;
    if (waryT <= 0) {
      waryT = 0.5f; wary = 0;
      for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get()))
        if (c->alive() && !c->removed && c->realm == realm && !c->baby && (c->type == wa[0] || (wa[1] && c->type == wa[1])) && std::hypot(c->pos.x - pos.x, c->pos.z - pos.z) < 6) { wary = c->id; break; }
    }
    auto* f = dynamic_cast<Creature*>(ent(lv, wary));
    if (f && f->alive() && !quarrel) { fuse = 0; return {T->speed * 1.3f, std::atan2(-(pos.x - f->pos.x), -(pos.z - f->pos.z)) + std::sin(age * 2) * 0.3f}; }
  }
  if (OptMove m = monsterAI(lv, *this, p, dist, dyp, toPlayer, dt)) { if (dist > 80 && !kept) removed = true; return *m; }
  return nightBase(lv, *this, type, p, dist, dyp, toPlayer, dt);
}

// ------------------------------------------------------------------ the Cinder Golem: telegraphed attacks
static void beginAttack(Level& lv, Creature& m, const std::string& kind, float tele, float yaw) {
  m.state = "tele"; m.stateT = tele;
  m.attack = Creature::Attack(); m.attack.on = true; m.attack.kind = kind; m.attack.tele = tele; m.attack.yaw = yaw; m.attack.start = m.age;
  if (kind == "charge") m.yaw = yaw;
  lv.fx->sound(kind == "charge" ? "charge" : "warn", m.pos);
}
static void executeAttack(Level& lv, Creature& m, bool enraged) {
  auto& a = m.attack;
  if (a.kind == "slam") {
    lv.fx->sound("slam", m.pos); shakeNear(lv, m.pos, m.realm, 0.6f);
    float R = 4.6f;
    for (int i = 0; i < 40; i++) { float ang = frand() * TAU, r = frand() * R; mobBurst(lv, m.pos + vec3(std::cos(ang) * r, 0.2f, std::sin(ang) * r), "golem_rock", 1, 0.9f); }
    for (Entity* q : pawnsOf(lv, m.realm)) {
      float d = std::hypot(q->pos.x - m.pos.x, q->pos.z - m.pos.z);
      if (d < R + 0.3f && std::fabs(q->pos.y - m.pos.y) < 2.2f) { DamageSrc s; s.knock = 9; damageEntity(lv, *q, enraged ? 8 : 7, &m, "mob", s); knockback(*q, vec3(0, 7, 0)); }
    }
    m.state = "recover"; m.stateT = 1.3f; a.on = false;
  } else if (a.kind == "charge") {
    m.state = "charging"; m.stateT = 1.15f; a.start = m.age;
    lv.fx->sound("roar", m.pos);
  } else { m.state = "volleying"; a.left = enraged ? 5 : 3; a.t = 0; }
}
Move Creature::bossAI(Level& lv, Entity* p, float dist, float toPlayer, float dt) {
  bool enraged = hp < maxHp * 0.5f;
  if (enraged && !enragedOnce) { enragedOnce = true; lv.fx->sound("roar", pos); lv.fx->message(0, "The Cinder Golem is enraged!", "boss"); shakeNear(lv, pos, realm, 0.5f); }
  stateT -= dt;
  lockMove = false;
  if (state == "spawning") {
    lockMove = true; vel.x = vel.z = 0;
    if (frand() < dt * 30) flame(lv, pos + vec3((frand() - 0.5f) * 2, frand() * 3, (frand() - 0.5f) * 2));
    if (stateT <= 0) { state = "chase"; stateT = 1.2f; }
    return {0, toPlayer};
  }
  if (!p || dist > 48) return {0, yaw};
  float tf = enraged ? 0.68f : 1;
  if (state == "idle" || state == "chase") {
    if (stateT <= 0) {
      float r = frand();
      if (dist < 6 && r < 0.55f) beginAttack(lv, *this, "slam", 1.25f * tf, yaw);
      else if (dist > 7 && r < 0.5f) beginAttack(lv, *this, "charge", 1.0f * tf, toPlayer);
      else beginAttack(lv, *this, "volley", 0.95f * tf, yaw);
    }
    return {dist > 4 ? T->speed : 0, toPlayer};
  }
  if (state == "tele") {
    lockMove = true; vel.x *= 0.8f; vel.z *= 0.8f;
    attack.progress = clampv(1 - stateT / attack.tele, 0.f, 1.f);
    if (attack.kind == "charge") { if (stateT <= 0) executeAttack(lv, *this, enraged); return {0, attack.yaw}; }
    if (stateT <= 0) executeAttack(lv, *this, enraged);
    return {0, attack.kind == "volley" ? toPlayer : yaw};
  }
  if (state == "charging") {
    lockMove = true;
    vel.x = -std::sin(attack.yaw) * 13; vel.z = -std::cos(attack.yaw) * 13;
    if (frand() < dt * 40) flame(lv, pos + vec3(0, 0.3f, 0));
    for (Entity* q : pawnsOf(lv, realm)) {
      if (attack.hit.count(q->id)) continue;
      if (std::fabs(q->pos.x - pos.x) < w + 0.6f && std::fabs(q->pos.z - pos.z) < w + 0.6f && std::fabs(q->pos.y - pos.y) < 2.5f) {
        attack.hit.insert(q->id); DamageSrc s; s.knock = 12; s.element = "fire"; damageEntity(lv, *q, 7, this, "mob", s);
      }
    }
    if (stateT <= 0 || (hitWall && age - attack.start > 0.15f)) {
      if (hitWall) { shakeNear(lv, pos, realm, 0.4f); lv.fx->sound("thud", pos); }
      state = "recover"; stateT = 1.1f * tf; attack.on = false;
    }
    return {0, attack.yaw};
  }
  if (state == "volleying") {
    lockMove = true;
    attack.t -= dt;
    if (attack.t <= 0 && attack.left > 0) {
      attack.left--; attack.t = 0.2f;
      vec3 e = pos + vec3(-std::sin(yaw) * 1.1f, 2.3f, -std::cos(yaw) * 1.1f);
      float lead = 0.25f * attack.left;
      vec3 d = vec3(p->pos.x + p->vel.x * lead, p->pos.y + 1.0f, p->pos.z + p->vel.z * lead) - e;
      float l = glm::length(d); if (l < 1e-3f) l = 1;
      spawnProjectile(lv, realm, e, d / l * 14.f, "fireball", this);
      lv.fx->sound("fireball", e);
    }
    if (attack.left <= 0 && attack.t <= 0) { state = "recover"; stateT = 0.9f * tf; attack.on = false; }
    return {0, toPlayer};
  }
  // recover
  if (stateT <= 0) { state = "chase"; stateT = (enraged ? 0.8f : 1.6f) + frand() * 0.8f; }
  return {T->speed * 0.5f, toPlayer};
}

void wakeMites(Level&, ivec3, int, bool) {}   // husbandry.js: stone mites are gone

} // namespace dq
