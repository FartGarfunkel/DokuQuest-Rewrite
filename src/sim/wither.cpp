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
// three skulls on it, as in the mod the Storm comes from; or (the older way) the block in place of the middle skull.
// (Herobrine raises the story's own at the end of his realm: sim/rift.cpp heroRaiseStorm.)
bool tryWitherSummon(Level& lv, int realm, ivec3 at) {
  World& w = lv.world(realm);
  const ivec3 dn(0, 1, 0);
  for (int axis = 0; axis < 2; axis++) {
    const ivec3 a = axis ? ivec3(0, 0, 1) : ivec3(1, 0, 0);
    for (int dy = 0; dy <= 1; dy++) for (int o = -1; o <= 1; o++) {
      const ivec3 m = at - a * o + dn * dy;   // the middle skull (placing the T's middle block: the skull row is above it)
      const bool coreHead = riftCoreAt(w, m), coreBody = riftCoreAt(w, m - dn), storm = coreHead || coreBody;
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
        if (!storm) { c->hp = c->maxHp / 3; c->data["growS"] = 0.75f; }   // (it fills to whole while it gathers itself)
        if (storm) lv.fx->sound("command_block_activates", p, 160);   // (the Wither's T makes no sound of its own: its cry comes when it bursts out)
        lv.fx->message(0, storm ? "The Wither Storm rises" : "The Wither stirs", "boss");
      }
      return true;
    }
  }
  return false;
}

// ---- the Wither, as Minecraft has it (WitherBoss) ----
namespace {
constexpr float WITHER_CHARGE = 11.f;   // its 220 ticks of gathering itself
// what it goes for: any living thing that isn't undead (players, villagers, animals, monsters alike)
bool witherPrey(const Creature& m, const Entity* e) {
  if (!e || e == &m || e->removed || e->hp <= 0 || e->realm != m.realm) return false;
  if (e->isPlayer()) return isPawn(e);
  const auto* c = dynamic_cast<const Creature*>(e);
  return c && c->alive() && c->T && !c->T->undead && !c->T->boss;
}
vec3 aimPoint(const Entity& e) { return e.pos + vec3(0, e.isPlayer() ? 1.2f : e.h * 0.5f, 0); }
// its three heads: 0 the middle one (whatever it's after), 1 and 2 the side ones (each its own mark)
vec3 witherHead(const Creature& m, int h) {   // (its model: the Storm's first form without the cage, at scale 2)
  if (h == 0) return m.pos + vec3(0, 3.0f, 0);
  const float side = h == 1 ? -1.12f : 1.12f;
  return m.pos + vec3(std::cos(m.yaw) * side, 2.62f, -std::sin(m.yaw) * side);
}
void witherShoot(Level& lv, Creature& m, int h, vec3 at, bool blue) {
  const vec3 from = witherHead(m, h); vec3 to = at - from; const float l = std::max(0.01f, glm::length(to));
  if (Entity* sk = spawnProjectile(lv, m.realm, from, to / l * (blue ? 6.f : 15.f), "wither_skull", &m); sk && blue) sk->data["blue"] = true;
  lv.fx->sound("wither_shoot", from, 32);
}
// hurt, it breaks out: a second later every block round its body goes (3 x 4 x 3), so it can't be walled in
void witherBreakOut(Level& lv, Creature& m) {
  if (!lv.info.rules.value("mobGriefing", true)) return;
  World& w = lv.world(m.realm); const Registry& R = reg();
  const int x0 = ifloor(m.pos.x), y0 = ifloor(m.pos.y), z0 = ifloor(m.pos.z); bool any = false;
  for (int dy = 0; dy <= 3; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    const int x = x0 + dx, y = y0 + dy, z = z0 + dz;
    const BlockId id = w.getBlock(x, y, z);
    if (!id || R.tLiquid[id]) continue;
    const BlockDef& b = R.block(id);
    if (b.hardness < 0 || b.hardness >= 50 || b.str("mech") == "none") continue;   // (bedrock, command blocks, portals)
    if (json* be = w.blockEntity(x, y, z)) {   // a chest spills what it held
      if (be->contains("items") && (*be)["items"].is_array()) for (auto& st : (*be)["items"]) { ItemStack it = ItemStack::fromJson(st); if (!it.empty()) dropItem(lv, m.realm, it, vec3(x + 0.5f, y + 0.5f, z + 0.5f), true); }
      w.setBlockEntity(x, y, z, nullptr);
    }
    if (!b.drop.empty() && lv.info.rules.value("blockDrops", true)) dropItem(lv, m.realm, ItemStack(b.drop, 1), vec3(x + 0.5f, y + 0.5f, z + 0.5f), true);
    w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
    any = true;
  }
  if (any) lv.fx->sound("wither_break", m.pos + vec3(0, 1.5f, 0), 48);
}
} // namespace

// its mind: OptMove for the special-AI hook in sim/creature_monsters.cpp
std::optional<Move> witherBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  m.noGravity = true; m.hasFlyVy = true;
  float spawnT = dget(m, "spawnT", 99.f);
  if (spawnT < WITHER_CHARGE) {
    // gathering itself: untouchable, still, swelling from three quarters of its size while its strength fills from a
    // third to whole; then it bursts out (power 7, the T's ground torn open) with a cry heard everywhere
    spawnT += dt; m.data["spawnT"] = spawnT;
    m.data["growS"] = 0.75f + 0.25f * std::min(1.f, spawnT / WITHER_CHARGE);
    m.hp = std::min(m.maxHp, m.hp + m.maxHp * (2.f / 3.f) / (WITHER_CHARGE - 1) * dt);
    m.flyVy = 0; m.vel = vec3(0);
    if (frand() < dt * 30) lv.fx->particles(frand() < 0.5f ? "smoke" : "mana_arcane", m.pos + vec3((frand() - 0.5f) * 2, frand() * 3, (frand() - 0.5f) * 2), 2);
    if (spawnT >= WITHER_CHARGE) {
      m.hp = m.maxHp; m.data["growS"] = 1.f;
      ExplodeOpts o; o.source = m.id; o.noBlocks = !lv.info.rules.value("mobGriefing", true);
      explode(lv, m.realm, m.pos + vec3(0, 1.5f, 0), 7, o);
      lv.fx->sound("wither_spawn", m.pos, 100000);
    }
    return Move{0, m.yaw};
  }
  m.hp = std::min(m.maxHp, m.hp + dt);   // it mends a point a second
  const bool armored = m.hp <= m.maxHp * 0.5f;
  m.data["armored"] = armored;
  if (float breakT = dget(m, "breakT", 0); breakT > 0) { breakT -= dt; m.data["breakT"] = breakT; if (breakT <= 0) witherBreakOut(lv, m); }
  if (float sayT = dget(m, "sayT", 2) - dt; sayT <= 0) { m.data["sayT"] = 4 + frand() * 5; lv.fx->voice("wither", "say", witherHead(m, 0)); }
  else m.data["sayT"] = sayT;
  // the middle head's mark: the player near it, else the nearest living thing (and it keeps to it while it can)
  Entity* main = p && witherPrey(m, p) && glm::distance(p->pos, m.pos) < 40 ? p : nullptr;
  if (!main) {
    Entity* kept = lv.entity(m.data.value("tgt0", 0u));
    if (witherPrey(m, kept) && glm::distance(kept->pos, m.pos) < 40) main = kept;
    else {
      float bd = 20;
      for (auto& e : lv.entities()) if (witherPrey(m, e.get())) { const float d = glm::distance(e->pos, m.pos); if (d < bd) { bd = d; main = e.get(); } }
    }
  }
  m.data["tgt0"] = main ? main->id : 0u;
  // the middle head: a skull every two seconds at its mark within 20 blocks it can see (blue one time in a thousand)
  float hT0 = dget(m, "hT0", 1.f) - dt;
  if (main) m.data["aim0"] = json::array({aimPoint(*main).x, aimPoint(*main).y, aimPoint(*main).z});
  if (hT0 <= 0 && main && glm::distance(main->pos, m.pos) < 20 && m.lineOfSight(lv, *main)) { witherShoot(lv, m, 0, aimPoint(*main), frand() < 0.001f); hT0 = 2.f; }
  m.data["hT0"] = std::max(hT0, 0.f);
  // the side heads: each picks a living thing of its own within 20 (8 up or down) and fires at it every 2-3 s while it
  // can see it within 30; one with nothing to aim at for a while sends a blue skull somewhere round about
  for (int h = 1; h <= 2; h++) {
    const std::string hk = "hT" + std::to_string(h), tk = "tgt" + std::to_string(h), ik = "idle" + std::to_string(h), ak = "aim" + std::to_string(h);
    Entity* t = lv.entity(m.data.value(tk, 0u));
    if (t && witherPrey(m, t)) m.data[ak] = json::array({aimPoint(*t).x, aimPoint(*t).y, aimPoint(*t).z});
    float ht = dget(m, hk.c_str(), 0.5f + frand() * 0.5f) - dt;
    if (ht <= 0) {
      ht = 0.5f + frand() * 0.5f;
      int idle = (int)dget(m, ik.c_str(), 0) + 1;
      if (idle > 15) { witherShoot(lv, m, h, m.pos + vec3((frand() - 0.5f) * 20, (frand() - 0.5f) * 10, (frand() - 0.5f) * 20), true); idle = 0; }
      if (t) {
        if (witherPrey(m, t) && glm::distance(t->pos, m.pos) <= 30 && m.lineOfSight(lv, *t)) { witherShoot(lv, m, h, aimPoint(*t), false); ht = 2 + frand(); idle = 0; }
        else { m.data[tk] = 0u; m.data.erase(ak); }
      } else {
        std::vector<Entity*> near;
        for (auto& e : lv.entities()) if (witherPrey(m, e.get()) && std::abs(e->pos.y - m.pos.y) <= 8 && std::hypot(e->pos.x - m.pos.x, e->pos.z - m.pos.z) <= 20) near.push_back(e.get());
        if (!near.empty()) m.data[tk] = near[(size_t)(frand() * near.size()) % near.size()]->id;
      }
      m.data[ik] = idle;
    }
    m.data[hk] = ht;
  }
  if (!main) { m.flyVy = (frand() - 0.5f) * 0.6f; m.wander(dt); return m.wanderMove(); }
  // it keeps five blocks over its mark (once its bones harden it comes down to it) and closes in to three
  const float wantY = main->pos.y + (armored ? 0.5f : 5.f);
  m.flyVy = m.pos.y < wantY ? std::min(4.f, (wantY - m.pos.y) * 1.5f + 0.6f) : std::max(-1.5f, (wantY - m.pos.y) * 0.5f);
  const float dx = main->pos.x - m.pos.x, dz = main->pos.z - m.pos.z, hd = std::hypot(dx, dz);
  const float want = hd > 3 ? m.T->speed * (hd > 16 ? 1.6f : 1.f) : 0;
  return Move{want, std::atan2(-dx, -dz)};
}

// its heads turn each to its own mark (rig "wither")
static void animWither(Creature& m, const RigModel& M, RigAnimOut& R) {
  static const char* HN[3] = {"head", "headL", "headR"};
  for (int h = 0; h < 3; h++) {
    const int i = M.find(HN[h]); if (i < 0) continue;
    vec3 rot(0);
    if (auto a = m.data.find("aim" + std::to_string(h)); a != m.data.end() && a->is_array() && a->size() == 3) {
      const vec3 at((*a)[0].get<float>(), (*a)[1].get<float>(), (*a)[2].get<float>()), from = witherHead(m, h);
      const float dx = at.x - from.x, dz = at.z - from.z;
      rot = vec3(clampv(std::atan2(at.y - from.y, std::hypot(dx, dz)), -1.f, 1.f), clampv(wrapAngle(std::atan2(-dx, -dz) - m.yaw), -1.4f, 1.4f), 0);
    } else rot = vec3(std::sin(m.age * 0.5f + h) * 0.1f, std::sin(m.age * 0.3f + h * 2) * 0.3f, 0);
    R.has[i] = 1; R.rot[i] = rot;
  }
  R.lift = std::sin(m.age * 1.2f) * 0.08f;
}
void registerWitherRig() { registerRigAnim("wither", animWither); }

// its hide: untouchable while it gathers itself; once its bones harden, arrows glance off
bool witherPreHurt(Creature& m, const std::string& type, const std::string& tool) {
  if (dget(m, "spawnT", 99.f) < WITHER_CHARGE) return true;
  if (m.hp <= m.maxHp * 0.5f && (tool == "arrow" || type == "arrow" || type == "projectile")) return true;
  if (type == "wither" || type == "drown" || type == "fall" || type == "suffocate") return true;
  if (dget(m, "breakT", 0) <= 0) m.data["breakT"] = 1.f;   // a second later it breaks out of whatever is round it
  return false;
}

} // namespace dq
