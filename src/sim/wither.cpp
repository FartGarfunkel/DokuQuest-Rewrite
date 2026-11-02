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
#include "sim/magic_fx.h"
#include "sim/effects.h"
#include "sim/creature_rig.h"

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
void spawnPulledBlock(Level& lv, int realm, vec3 at, const std::string& key, u8 meta, u32 puller);   // sim/blockfx.cpp

// How big it has grown: it rises out of the T a third of its size, and every block and creature it eats makes it bigger
// (Story Mode's Storm grows from a Wither into a behemoth). The renderer and its hit box both scale by this.
float stormScale(const Creature& m) {
  const float spawnT = dget(m, "spawnT", 99.f);
  if (spawnT < 12) return 0.35f + 0.65f * spawnT / 12;
  return 1 + std::min(dget(m, "grown", 0), 400.f) / 250;
}
// its middle head's maw, where the tractor beam starts and what it pulls in goes
static vec3 stormMaw(const Creature& m) {
  const float s = stormScale(m);
  return m.pos + vec3(-std::sin(m.yaw), 0, -std::cos(m.yaw)) * (3.0f * s) + vec3(0, 6.6f * s, 0);
}

std::optional<Move> stormBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  m.noGravity = true; m.hasFlyVy = true;
  float spawnT = dget(m, "spawnT", 99.f);
  {   // its size: the hit box follows the body
    const float s = stormScale(m);
    m.data["growS"] = s; m.w = m.T->w * s; m.h = m.T->h * s;
  }
  float roarT = dget(m, "roarT", 3) - dt;
  if (roarT <= 0) { roarT = 8 + frand() * 7; lv.fx->sound(frand() < 0.5f ? "dragon_roar" : "warden_roar", m.pos, 160); }
  m.data["roarT"] = roarT;
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
  // the tractor beam: whoever is near is drawn up it toward the maw, and too close to it the wither sickness takes them
  const vec3 maw = stormMaw(m);
  const float reach = 22 * std::max(1.f, stormScale(m) * 0.8f);
  float beamT = dget(m, "beamT", 0) - dt;
  const bool drawBeam = beamT <= 0;
  if (drawBeam) beamT = 0.12f;
  m.data["beamT"] = beamT;
  if (dist < reach && p->isPlayer()) {
    vec3 to = maw - p->pos; float l = glm::length(to);
    if (l > 3) p->vel += to / l * (7.f * dt) * (1 - l / reach);
    if (drawBeam && l > 4) {   // (it stops short of the eyes: up close the motes would fill the view)
      const vec3 end = p->pos + vec3(0, 1, 0) + to / l * 2.5f;
      lv.fx->particles("mana_arcane", maw, 1, json{{"m", "beam"}, {"to", json::array({end.x, end.y, end.z})}, {"size", 0.22f}, {"width", 0.9f}, {"life", 0.3f}});
    }
    if (l < 8) if (auto* pl = dynamic_cast<Player*>(p)) giveEffect(lv, *pl, "wither", 3);
  }
  {   // the beam down to the ground it's tearing up
    float tearT = dget(m, "tearT", 0) - dt; m.data["tearT"] = tearT;
    if (tearT > 0 && drawBeam && m.data.contains("tear") && m.data["tear"].is_array()) {
      const json& t = m.data["tear"];
      lv.fx->particles("mana_arcane", maw, 1, json{{"m", "beam"}, {"to", t}, {"size", 0.24f}, {"width", 1.4f}, {"life", 0.3f}});
    }
  }
  // and the creatures round it: drawn up the beam and eaten (each one feeds it)
  for (auto& e : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(e.get());
    if (!c || c == &m || c->removed || !c->alive() || c->realm != m.realm || (c->T && c->T->boss)) continue;
    const vec3 to = maw - (c->pos + vec3(0, c->h * 0.5f, 0)); const float l = glm::length(to);
    if (l > reach * 0.8f) continue;
    c->vel += to / std::max(0.5f, l) * (9.f * dt); c->vel.y = std::max(c->vel.y, 2.5f);
    if (drawBeam && frand() < 0.3f) lv.fx->particles("mana_arcane", maw, 1, json{{"m", "beam"}, {"to", json::array({c->pos.x, c->pos.y + c->h * 0.5f, c->pos.z})}, {"size", 0.18f}, {"width", 0.6f}, {"life", 0.3f}});
    if (l < 2.2f * stormScale(m)) {
      c->removed = true;
      sfx::burst(lv, c->pos + vec3(0, c->h * 0.5f, 0), "mana_arcane", 30, 2.f, json{{"life", 0.6f}, {"size", 0.12f}});
      lv.fx->sound("dragon_bite", maw, 48);
      const float grown = dget(m, "grown", 0) + 4;
      m.data["grown"] = grown; m.maxHp = std::max(m.maxHp, m.T->hp + grown * 2); m.hp = std::min(m.maxHp, m.hp + 8.f);
    }
  }
  // tearing up the ground under it
  float pullT = dget(m, "pullT", 4) - dt;
  if (pullT <= 0) {
    const float sc = stormScale(m);   // the bigger it is, the faster and wider it eats
    pullT = (3 + frand() * 2) / sc;
    int taken = 0;
    const int gx = ifloor(m.pos.x), gz = ifloor(m.pos.z);
    for (int tries = 0; tries < 60 && taken < (int)(10 * sc); tries++) {
      const int x = gx + (int)((frand() - 0.5f) * 14 * sc), z = gz + (int)((frand() - 0.5f) * 14 * sc);
      for (int y = ifloor(m.pos.y) - 1; y > ifloor(m.pos.y) - 40; y--) {
        const BlockId id = w.getBlock(x, y, z);
        if (!id) continue;
        const BlockDef& b = reg().block(id);
        if (reg().tLiquid[id] || b.hardness < 0 || b.hardness >= 20 || b.has("container") || b.has("blastproof")) break;
        const u8 meta = w.getMeta(x, y, z);
        w.setBlock(x, y, z, AIR, 0);
        spawnPulledBlock(lv, m.realm, vec3(x + 0.5f, (float)y, z + 0.5f), b.key, meta, m.id);   // it flies up the beam into the maw
        if (taken == 0) { m.data["tear"] = json::array({x + 0.5f, y + 0.5f, z + 0.5f}); m.data["tearT"] = 2.0f; }   // the beam holds on the ground it tears
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

// how it moves (rig "wither_storm", tools/port/wither_storm.py): the tentacles sway and coil, each segment more than the
// one above it; the mandibles work, faster while it's pulling something in; the heads turn a little each their own way
static void animStorm(Creature& m, const RigModel& M, RigAnimOut& R) {
  const float t = m.age;
  auto set = [&](const std::string& n, vec3 v) { const int i = M.find(n); if (i < 0) return; R.has[i] = 1; R.rot[i] = v; };
  for (int i = 0; i < 6; i++) for (int j = 0; j < 4; j++) {
    const float k = 0.18f + 0.12f * j, ph = t * (1.1f + 0.13f * i) + i * 1.7f + j * 0.7f;
    set("tent" + std::to_string(i) + "_" + std::to_string(j), vec3(std::sin(ph) * k, std::sin(ph * 0.6f + i) * 0.2f, std::cos(ph * 0.8f) * k));
  }
  const bool feeding = dget(m, "pullT", 4) < 1.5f;
  const float chew = (feeding ? 0.35f : 0.18f) + std::sin(t * (feeding ? 6.f : 2.2f)) * (feeding ? 0.25f : 0.1f);
  for (const char* h : {"head", "headL", "headR"}) {
    const std::string hn = h;
    set(hn + "JawL", vec3(chew, 0, -chew * 0.6f)); set(hn + "JawR", vec3(chew, 0, chew * 0.6f));
  }
  set("head", vec3(std::sin(t * 0.7f) * 0.08f, std::sin(t * 0.45f) * 0.12f, 0));
  set("headL", vec3(std::sin(t * 0.9f + 1) * 0.1f, 0.25f + std::sin(t * 0.6f) * 0.2f, 0));
  set("headR", vec3(std::sin(t * 0.8f + 2) * 0.1f, -0.25f + std::sin(t * 0.55f + 1) * 0.2f, 0));
  R.lift = std::sin(t * 0.8f) * 0.35f;
}
void registerStormRig() { registerRigAnim("wither_storm", animStorm); }

// its hide: untouchable while it gathers itself; once its bones harden, arrows glance off
bool witherPreHurt(Creature& m, const std::string& type, const std::string& tool) {
  if (dget(m, "spawnT", 99.f) < 8) return true;
  if (m.hp < m.T->hp * 0.5f && (tool == "arrow" || type == "arrow" || type == "projectile")) return true;
  return false;
}

} // namespace dq
