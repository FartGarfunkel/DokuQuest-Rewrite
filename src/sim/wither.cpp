// sim/wither.cpp — the Wither (never natural): three wither skeleton skulls set on a T of soul sand wake it. It
// gathers itself for a few seconds (nothing touches it), bursts out, then circles above whoever is near, its three
// heads sending skulls that burst and wither. Below half its strength its bones harden: arrows glance off.
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/player.h"
#include "sim/explosion.h"
#include "core/registry.h"

namespace dq {

namespace {
bool soulish(BlockId id) { static const BlockId a = reg().blockId("soul_sand"), b = reg().blockId("soul_soil"); return id && (id == a || id == b); }
bool witherSkullAt(World& w, ivec3 p) {
  static const BlockId head = reg().blockId("mob_head");
  if (!head || w.getBlock(p.x, p.y, p.z) != head) return false;
  const json* be = w.blockEntity(p.x, p.y, p.z);
  return be && be->value("mob", "") == "wither_skeleton";
}
float dget(const Creature& m, const char* k, float d) { auto it = m.data.find(k); return it != m.data.end() && it->is_number() ? it->get<float>() : d; }
} // namespace

// a wither skeleton skull was just set at `at`: is it the third on a T of soul sand?
bool tryWitherSummon(Level& lv, int realm, ivec3 at) {
  World& w = lv.world(realm);
  for (int axis = 0; axis < 2; axis++) {
    const ivec3 a = axis ? ivec3(0, 0, 1) : ivec3(1, 0, 0);
    for (int o = -1; o <= 1; o++) {
      const ivec3 m = at - a * o;   // the middle skull
      if (!witherSkullAt(w, m - a) || !witherSkullAt(w, m) || !witherSkullAt(w, m + a)) continue;
      const ivec3 body[4] = {m - a - ivec3(0, 1, 0), m - ivec3(0, 1, 0), m + a - ivec3(0, 1, 0), m - ivec3(0, 2, 0)};
      bool ok = true;
      for (auto& b : body) if (!soulish(w.getBlock(b.x, b.y, b.z))) ok = false;
      if (!ok) continue;
      for (int k = -1; k <= 1; k++) { ivec3 h = m + a * k; w.setBlock(h.x, h.y, h.z, AIR); }
      for (auto& b : body) w.setBlock(b.x, b.y, b.z, AIR);
      vec3 p = vec3(m) + vec3(0.5f, -2.f, 0.5f);
      if (Creature* c = spawnCreature(lv, realm, "wither", p)) {
        c->data["spawnT"] = 0.f; c->noGravity = true; c->hasFlyVy = true;
        lv.fx->sound("ghast_warn", p, 64);
        lv.fx->message(0, "The Wither stirs", "boss");
      }
      return true;
    }
  }
  return false;
}

// its mind: OptMove for the special-AI hook in sim/creature_monsters.cpp
std::optional<Move> witherBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  m.noGravity = true; m.hasFlyVy = true;
  float spawnT = dget(m, "spawnT", 99.f);
  if (spawnT < 8) {   // gathering itself: rising slowly, swelling with dark motes
    spawnT += dt; m.data["spawnT"] = spawnT;
    m.flyVy = 0.4f; m.vel.x = m.vel.z = 0;
    if (frand() < dt * 30) lv.fx->particles("smoke", m.pos + vec3((frand() - 0.5f) * 2, frand() * 3, (frand() - 0.5f) * 2), 2);
    if (spawnT >= 8) {
      ExplodeOpts o; o.source = m.id;
      explode(lv, m.realm, m.pos + vec3(0, 1.5f, 0), 4, o);
      lv.fx->sound("explode", m.pos, 96);
    }
    return Move{0, m.yaw};
  }
  if (!p) { m.flyVy = (frand() - 0.5f) * 0.6f; m.wander(dt); return m.wanderMove(); }
  const bool armored = m.hp < m.T->hp * 0.5f;
  // hold a little above the target, circling at a distance
  const float wantY = p->pos.y + (armored ? 4.f : 7.f);
  m.flyVy = clampv((wantY - m.pos.y) * 1.5f, -4.f, 4.f);
  float dir = toPlayer, want = m.T->speed;
  if (dist < 9) dir = toPlayer + PI * 0.5f;          // close: wheel round
  if (dist < 5) dir = toPlayer + PI * 0.8f;          // too close: back off
  if (dist > 30) want = m.T->speed * 1.6f;
  // skulls from its heads
  float shootT = dget(m, "shootT", 1.5f) - dt;
  if (shootT <= 0 && dist < 40) {
    shootT = armored ? 0.7f + frand() * 0.5f : 1.1f + frand() * 0.8f;
    const int head = (int)(frand() * 3);   // the middle one, or one of the side ones
    const float side = head == 0 ? 0.f : head == 1 ? -1.2f : 1.2f;
    vec3 from = m.pos + vec3(std::cos(m.yaw) * side, m.h * 0.92f, -std::sin(m.yaw) * side);
    vec3 to = p->pos + vec3(0, 1.0f, 0) - from; float l = std::max(0.01f, glm::length(to));
    spawnProjectile(lv, m.realm, from, to / l * (armored ? 16.f : 13.f), "wither_skull", &m);
    lv.fx->sound("ghast_shoot", from, 32);
  }
  m.data["shootT"] = shootT;
  m.data["armored"] = armored;
  return Move{want, dir};
}

// its hide: untouchable while it gathers itself; once its bones harden, arrows glance off
bool witherPreHurt(Creature& m, const std::string& type, const std::string& tool) {
  if (dget(m, "spawnT", 99.f) < 8) return true;
  if (m.hp < m.T->hp * 0.5f && (tool == "arrow" || type == "arrow" || type == "projectile")) return true;
  return false;
}

} // namespace dq
