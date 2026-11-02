// sim/wither.cpp — the Wither (never natural): three wither skeleton skulls set on a T of soul sand wake it. It
// gathers itself for a few seconds (nothing touches it), bursts out, then circles above whoever is near, its three
// heads sending skulls that burst and wither. Below half its strength its bones harden: arrows glance off.
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/player.h"
#include "sim/explosion.h"
#include "core/registry.h"
#include "common/fs.h"
#include "sim/mob_combat.h"

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

// the Storm's heart: the humming rift core, or a command block (as in the story the Storm comes from)
bool riftCoreAt(World& w, ivec3 p) {
  static const BlockId core = reg().blockId("rift_core"), cmd = reg().blockId("command_block");
  const BlockId id = w.getBlock(p.x, p.y, p.z);
  return id && (id == core || id == cmd);
}

// a wither skeleton skull (or a rift core / command block) was just set at `at`: does it finish a T of soul sand? Three
// skulls wake the Wither. The Wither Storm: a command block (or a rift core) in the middle of the T's top row with the
// three skulls on it, as in the mod the Storm comes from; or (the older way) the block in place of the middle skull
bool tryWitherSummon(Level& lv, int realm, ivec3 at) {
  World& w = lv.world(realm);
  const ivec3 dn(0, 1, 0);
  for (int axis = 0; axis < 2; axis++) {
    const ivec3 a = axis ? ivec3(0, 0, 1) : ivec3(1, 0, 0);
    for (int dy = 0; dy <= 1; dy++) for (int o = -1; o <= 1; o++) {
      const ivec3 m = at - a * o + dn * dy;   // the middle skull (placing the T's middle block: the skull row is above it)
      const bool coreHead = riftCoreAt(w, m), coreBody = riftCoreAt(w, m - dn), storm = coreHead || coreBody;
      static const BlockId realCore = reg().blockId("rift_core");
      const bool riftHeart = storm && realCore && (w.getBlock(m.x, m.y, m.z) == realCore || w.getBlock(m.x, m.y - 1, m.z) == realCore);
      if (!witherSkullAt(w, m - a) || !(coreHead || witherSkullAt(w, m)) || !witherSkullAt(w, m + a)) continue;
      const ivec3 body[4] = {m - a - dn, m - dn, m + a - dn, m - dn * 2};
      bool ok = true;
      for (int i = 0; i < 4; i++) if (!soulish(w.getBlock(body[i].x, body[i].y, body[i].z)) && !(i == 1 && coreBody)) ok = false;
      if (!ok) continue;
      for (int k = -1; k <= 1; k++) { ivec3 h = m + a * k; w.setBlock(h.x, h.y, h.z, AIR); }
      for (auto& b : body) w.setBlock(b.x, b.y, b.z, AIR);
      vec3 p = vec3(m) + vec3(0.5f, -2.f, 0.5f);
      if (Creature* c = spawnCreature(lv, realm, storm ? "wither_storm" : "wither", p)) {
        c->data["spawnT"] = 0.f; c->noGravity = true; c->hasFlyVy = true;
        lv.fx->sound("ghast_warn", p, storm ? 160 : 64);
        lv.fx->message(0, storm ? "The Wither Storm rises" : "The Wither stirs", "boss");
        // the story's last fight: the rift sealed, and a Storm woken from the rift's own crystals. It's bigger, and it
        // can't be worn down past a quarter until its heart shows (stormBrain); breaking the heart ends the story
        const json& R = lv.info.data.contains("rift") ? lv.info.data["rift"] : json();
        if (riftHeart && jflag(R, "sealed") && !jflag(R, "stormBroken")) {
          c->data["finale"] = true;
          c->maxHp = c->hp = c->T->hp * 1.5f;
          c->data["hpFloor"] = c->maxHp * 0.25f;
          lv.fx->message(0, "Everything he pulled through the rift comes back at once.", "epic");
        }
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

// ---- the Wither Storm: a Wither grown out of a rift core. It gathers itself longer, then hangs high over whoever is
// near, tearing up the ground under it with its pull (the blocks fly up into it and are gone, and every one feeds it),
// drawing you toward it, and three skulls at a time from its heads. It falls in a storm of what it swallowed.
std::optional<Move> stormBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  m.noGravity = true; m.hasFlyVy = true;
  float spawnT = dget(m, "spawnT", 99.f);
  if (frand() < dt * 25) lv.fx->particles("burst", m.pos + vec3((frand() - 0.5f) * 6, frand() * m.h, (frand() - 0.5f) * 6), 1, {{"tile", "obsidian"}, {"speed", 0.6f}});
  if (spawnT < 12) {
    spawnT += dt; m.data["spawnT"] = spawnT;
    m.flyVy = 0.6f; m.vel.x = m.vel.z = 0;
    if (frand() < dt * 40) lv.fx->particles("smoke_dark", m.pos + vec3((frand() - 0.5f) * 5, frand() * 6, (frand() - 0.5f) * 5), 2);
    if (spawnT >= 12) { ExplodeOpts o; o.source = m.id; explode(lv, m.realm, m.pos + vec3(0, 3, 0), 6, o); lv.fx->sound("explode", m.pos, 160); }
    return Move{0, m.yaw};
  }
  if (!p) { m.flyVy = (frand() - 0.5f) * 0.4f; m.wander(dt); return m.wanderMove(); }
  World& w = lv.world(m.realm);
  // the finale: worn down to a quarter it sinks and its heart shows. Only blows struck up close reach it then (arrows
  // and skulls glance off); left alone half a minute, the heart closes over, it heals to half and rises again
  if (jflag(m.data, "finale")) {
    float heartT = dget(m, "heartT", -1);
    if (heartT < 0 && m.hp <= m.maxHp * 0.25f + 0.01f) {
      heartT = 0; m.data["hpFloor"] = 0.f;
      lv.fx->message(0, "The Storm sinks, and its heart shows. Strike it!", "boss");
      lv.fx->sound("rift_boom", m.pos, 120);
    }
    if (heartT >= 0) {
      heartT += dt;
      const vec3 heart = m.pos + vec3(0, m.h * 0.45f, 0);
      if (frand() < dt * 30) lv.fx->particles("burst", heart + vec3((frand() - 0.5f) * 1.4f, (frand() - 0.5f) * 1.4f, (frand() - 0.5f) * 1.4f), 2, {{"tile", "rift_core"}, {"speed", 1.2f}});
      if (frand() < dt * 3) lv.fx->sound("rift_hum", heart, 40);
      m.flyVy = clampv((p->pos.y - 2.f - m.pos.y) * 0.8f, -2.f, 2.f);   // low enough to reach its heart from the ground
      if (heartT > 30) {
        heartT = -1; m.hp = std::max(m.hp, m.maxHp * 0.5f); m.data["hpFloor"] = m.maxHp * 0.25f;
        lv.fx->message(0, "Its heart closes over, and the Storm rises again.", "boss");
      }
      m.data["heartT"] = heartT;
      return Move{m.T->speed * 0.35f, toPlayer};
    }
    m.data["heartT"] = heartT;
  }
  // hang high over its mark, drifting after it
  m.flyVy = clampv((p->pos.y + 14 - m.pos.y) * 0.8f, -3.f, 3.f);
  // the pull: whoever is near is drawn in
  if (dist < 22 && p->isPlayer()) { vec3 to = m.pos + vec3(0, m.h * 0.4f, 0) - p->pos; float l = glm::length(to); if (l > 3) p->vel += to / l * (6.f * dt) * (1 - l / 22); }
  // tearing up the ground under it
  float pullT = dget(m, "pullT", 4) - dt;
  if (pullT <= 0) {
    pullT = 3 + frand() * 2;
    int taken = 0;
    const int gx = ifloor(m.pos.x), gz = ifloor(m.pos.z);
    for (int tries = 0; tries < 60 && taken < 10; tries++) {
      const int x = gx + (int)((frand() - 0.5f) * 14), z = gz + (int)((frand() - 0.5f) * 14);
      for (int y = ifloor(m.pos.y) - 1; y > ifloor(m.pos.y) - 40; y--) {
        const BlockId id = w.getBlock(x, y, z);
        if (!id) continue;
        const BlockDef& b = reg().block(id);
        if (reg().tLiquid[id] || b.hardness < 0 || b.hardness >= 20 || b.has("container") || b.has("blastproof")) break;
        w.setBlock(x, y, z, AIR, 0);
        lv.fx->particles("burst", vec3(x, y, z) + vec3(0.5f), 6, {{"tile", b.key}, {"speed", 3.f}});
        taken++;
        break;
      }
    }
    if (taken) {
      const float grown = dget(m, "grown", 0) + taken;
      m.data["grown"] = grown;
      m.maxHp = m.T->hp + grown * 2; m.hp = std::min(m.maxHp, m.hp + taken * 2.f);   // every block feeds it
      lv.fx->sound("rift_hum", m.pos, 64);
    }
  }
  m.data["pullT"] = pullT;
  // skulls from all three heads
  float shootT = dget(m, "shootT", 2) - dt;
  if (shootT <= 0 && dist < 48) {
    shootT = 1.4f + frand() * 0.6f;
    for (int head = 0; head < 3; head++) {
      const float side = head == 0 ? 0.f : head == 1 ? -4.f : 4.f;
      vec3 from = m.pos + vec3(std::cos(m.yaw) * side, m.h * 0.9f, -std::sin(m.yaw) * side);
      vec3 to = p->pos + vec3(0, 1.0f, 0) - from; float l = std::max(0.01f, glm::length(to));
      spawnProjectile(lv, m.realm, from, to / l * 15.f, "wither_skull", &m);
    }
    lv.fx->sound("ghast_shoot", m.pos, 64);
  }
  m.data["shootT"] = shootT;
  float dir = toPlayer, want = m.T->speed;
  if (dist < 10) dir = toPlayer + PI * 0.5f;
  return Move{want, dir};
}
bool stormPreHurt(Creature& m, const std::string& type, const std::string& tool) {
  if (dget(m, "spawnT", 99.f) < 12) return true;
  if (jflag(m.data, "finale") && dget(m, "heartT", -1) >= 0 && (tool == "arrow" || type == "arrow" || type == "projectile" || type == "explosion")) return true;   // the heart: up close only
  return false;
}
// when it falls: what it swallowed comes down
void stormFalls(Level& lv, Creature& m) {
  ExplodeOpts o; o.source = m.id; o.noBlocks = true;
  explode(lv, m.realm, m.pos + vec3(0, m.h * 0.5f, 0), 5, o);
  const int grown = (int)dget(m, "grown", 0);
  static const char* rain[] = {"cobblestone", "dirt", "gravel", "obsidian", "netherrack"};
  for (int i = 0; i < std::min(48, grown / 2 + 8); i++)
    dropItem(lv, m.realm, ItemStack(rain[i % 5], 1 + (int)(frand() * 3)), m.pos + vec3((frand() - 0.5f) * 8, m.h * frand(), (frand() - 0.5f) * 8), true);
  lv.fx->message(0, "The Wither Storm falls", "boss");
  if (jflag(m.data, "finale")) {   // the story's end
    json& R = lv.info.data["rift"];
    if (!R.is_object()) R = json::object();
    R["stormBroken"] = 1;
    for (auto [id, n] : std::vector<std::pair<const char*, int>>{{"nether_star", 3}, {"heart_crystal", 3}, {"rift_crystal", 8}, {"diamond", 16}})
      if (reg().item(id)) dropItem(lv, m.realm, ItemStack(id, n), m.pos + vec3(0, 2, 0), true);
    lv.fx->message(0, "Its heart breaks. The last of what he gathered is gone, and the rift's hum is silent at last.", "epic");
    for (auto& e : lv.entities()) if (e && !e->removed && e->isPlayer()) {
      Player& pl = *static_cast<Player*>(e.get());
      grantXP(lv, pl, 600, m.pos);
      if (!pl.data.is_object()) pl.data = json::object();
      if (!pl.stats.is_object()) pl.stats = json::object();
      pl.stats["story"]["storm"] = 1;
    }
    if (lv.events) lv.events->emit("riftChanged", json::array({R}));
    if (lv.events) lv.events->emit("stormBroken", json::array());
  }
}

// its hide: untouchable while it gathers itself; once its bones harden, arrows glance off
bool witherPreHurt(Creature& m, const std::string& type, const std::string& tool) {
  if (dget(m, "spawnT", 99.f) < 8) return true;
  if (m.hp < m.T->hp * 0.5f && (tool == "arrow" || type == "arrow" || type == "projectile")) return true;
  return false;
}

} // namespace dq
