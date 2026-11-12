// sim/wither_storm.cpp — the Wither Storm as Minecraft: Story Mode has it, from what Cracker's Wither Storm Mod does
// (docs/wither_storm_mod_spec.md). It wakes from a T with a command block at its heart (sim/wither.cpp tryWitherSummon,
// or Herobrine's own at the end of his realm: sim/rift.cpp) looking like a Wither, a command block glowing in a cage of
// ribs, and grows with everything it eats: a hump, a jawed head, tentacles, and at last the destroyer, a mass of
// obsidian with three great heads. It eats the world in clumps torn out whole (block clusters: they tremble, lift and
// fly into it); each working head pulls its mark up a tractor beam to be chewed, and blows to a head only drive that
// head off for a while. Nothing wears a grown one down: a Formidibomb in its maw knocks it out of the sky, and while it
// lies on its back its command block hangs bare over a podium of bone. Four blows to that and the Storm dies; left too
// long, it wakes again.
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/player.h"
#include "sim/explosion.h"
#include "sim/physics.h"
#include "core/registry.h"
#include "sim/mob_combat.h"
#include "sim/magic_fx.h"
#include "sim/effects.h"
#include "sim/creature_rig.h"
#include "gen/dimgens.h"

namespace dq {

namespace {
float dget(const Creature& m, const char* k, float d) { auto it = m.data.find(k); return it != m.data.end() && it->is_number() ? it->get<float>() : d; }
std::string kh(const char* k, int h) { return std::string(k) + std::to_string(h); }
float hget(const Creature& m, const char* k, int h, float d) { return dget(m, kh(k, h).c_str(), d); }
json jv(vec3 v) { return json::array({v.x, v.y, v.z}); }
bool vj(const json& j, vec3& out) { if (!j.is_array() || j.size() != 3) return false; out = vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()); return true; }

constexpr float CHARGE = 12.f;   // gathering itself out of the T
constexpr int MAXP = 5;
// what it must have eaten to grow past each phase (in his realm everything counts thrice: the finale can't wait hours)
const float GROW[MAXP] = {60, 240, 700, 2000, 6000};
// each phase's look (its own model, converted from the mod's: tools/port/wither_storm_models.py), its box (enough to
// strike: the mod's are smaller than what you see), its strength, and how high it hangs over the ground
struct Look { const char* model; float s, w, h, hp, hover; };
const Look LOOK[MAXP + 1] = {
  {"wither_storm_p0", 1.f, 1.2f, 3.6f, 400, 10},    // the Wither it rises as, the command block caged in its ribs
  {"wither_storm_p1", 1.f, 2.2f, 4.2f, 400, 10},    // a hump on its back
  {"wither_storm_p2", 1.f, 3.0f, 5.0f, 500, 12},    // the middle head a storm head
  {"wither_storm_p3", 1.f, 4.0f, 6.0f, 600, 14},    // the mass tipped over it, tentacles, the command block grown over
  {"wither_storm", 1.f, 22.f, 36.f, 1024, 22},      // the destroyer
  {"wither_storm_p5", 1.f, 30.f, 50.f, 1500, 28}};  // the evolved destroyer
// its heads' mouths (0 the main one), model space in blocks: the Wither's heads, then a storm head in the middle, then
// the destroyer's three (the mod's head1 in the middle, head0 to its right, head2 to its left)
const vec3 MOUTH[MAXP + 1][3] = {
  {{0, 3.f, -0.6f}, {1.1f, 2.6f, -0.5f}, {-1.3f, 2.6f, -0.5f}}, {{0, 3.f, -0.6f}, {1.1f, 2.6f, -0.5f}, {-1.3f, 2.6f, -0.5f}},
  {{0, 2.8f, -1.4f}, {1.1f, 2.6f, -0.5f}, {-1.3f, 2.6f, -0.5f}}, {{0, 2.8f, -1.4f}, {1.1f, 2.6f, -0.5f}, {-1.3f, 2.6f, -0.5f}},
  {{0, 11.1f, -13.1f}, {8.25f, 23.5f, -19.5f}, {-12.f, 21.6f, -13.5f}}, {{0, 11.1f, -13.1f}, {8.25f, 23.5f, -19.5f}, {-12.f, 21.6f, -13.5f}}};
enum St { NORMAL = 0, FALLING = 1, DOWN = 2, RISING = 3, DYING = 4 };
int phaseOf(const Creature& m) { return std::clamp((int)dget(m, "phase", 0), 0, MAXP); }
int stOf(const Creature& m) { return (int)dget(m, "st", NORMAL); }
bool awake(const Creature& m) { return dget(m, "spawnT", 99.f) >= CHARGE; }
float realmFloorY() { return (float)(dimBase(DIM_OVERWORLD) + RealmGen::FLOOR); }

// each head's mouth in the world, turned with the body (the model's front is -z)
vec3 mouthOf(const Creature& m, int h) {
  const vec3 o = MOUTH[phaseOf(m)][h] * dget(m, "growS", 1);
  const float c = std::cos(m.yaw), sn = std::sin(m.yaw);
  return m.pos + vec3(o.x * c + o.z * sn, o.y, -o.x * sn + o.z * c);
}
// a head's tractor beam works: one on the middle head from phase 2, all three once it's grown; not while it's hurt
bool beamOn(const Creature& m, int h) { const int ph = phaseOf(m); return hget(m, "inj", h, 0) <= 0 && (ph >= 4 || (ph >= 2 && h == 0)); }

bool edible(BlockId id) {
  if (!id) return false;
  const Registry& R = reg();
  if (R.tLiquid[id]) return false;
  const BlockDef& b = R.block(id);
  if (b.hardness < 0 || b.hardness >= 50 || b.str("mech") == "none") return false;   // bedrock, command blocks, portals
  static const BlockId keep[] = {R.blockId("rift_core"), R.blockId("formidibomb"), R.blockId("command_block")};
  for (BlockId k : keep) if (k && id == k) return false;
  return true;
}

// a clump of the world torn out whole round c (radius r): its blocks go, and a cluster carries them (it trembles a moment
// where it was, then flies into the head that took it). Chests spill what they held. counts: toward its growth
Entity* tearCluster(Level& lv, Creature& m, ivec3 c, float r, int head, bool counts) {
  World& w = lv.world(m.realm);
  const int R = (int)std::ceil(r);
  const float floorY = m.realm == REALM_HIS ? realmFloorY() : -1e9f;   // (his realm's floor stays: there's nothing under it)
  json blocks = json::array(); int n = 0;
  for (int dy = -R; dy <= R; dy++) for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
    if (n >= 64 || dx * dx + dy * dy + dz * dz > r * r + 0.3f) continue;
    const int x = c.x + dx, y = c.y + dy, z = c.z + dz;
    if (y <= floorY) continue;
    const BlockId id = w.getBlock(x, y, z);
    if (!edible(id)) continue;
    if (json* be = w.blockEntity(x, y, z)) {
      if (be->contains("items") && (*be)["items"].is_array()) for (auto& s : (*be)["items"]) { ItemStack st = ItemStack::fromJson(s); if (!st.empty()) dropItem(lv, m.realm, st, vec3(x + 0.5f, y + 0.5f, z + 0.5f), true); }
      w.setBlockEntity(x, y, z, nullptr);
    }
    blocks.push_back(json::array({dx, dy, dz, reg().block(id).key, w.getMeta(x, y, z)}));
    w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
    n++;
  }
  if (!n) return nullptr;
  auto e = createEntity("block_cluster");
  if (!e) return nullptr;
  e->realm = m.realm; e->pos = vec3(c) + vec3(0.5f);
  e->data = {{"blocks", blocks}, {"storm", m.id}, {"head", head}, {"n", n}, {"counts", counts}, {"shake", 0.6f + frand() * 0.8f}, {"spin", frand() * 6.f}};
  if (n >= 6) lv.fx->sound("block_cluster_shake", e->pos, 32);
  return lv.spawn(std::move(e));
}
// where it bites into the world at (x,z): the top of the ground under it (in his realm: the first thing standing up off
// the floor, or the roof)
bool biteAt(Level& lv, const Creature& m, int x, int z, ivec3& out) {
  World& w = lv.world(m.realm);
  if (m.realm == REALM_HIS) {
    const int f = (int)realmFloorY();
    for (int y = f + 1; y < f + 50; y++) if (edible(w.getBlock(x, y, z))) { out = ivec3(x, y, z); return true; }
    return false;
  }
  for (int y = ifloor(m.pos.y) - 1; y > ifloor(m.pos.y) - 64; y--) {
    const BlockId id = w.getBlock(x, y, z);
    if (!id || reg().tLiquid[id]) continue;
    if (!edible(id)) return false;
    out = ivec3(x, y, z); return true;
  }
  return false;
}
float groundUnder(Level& lv, const Creature& m) {
  if (m.realm == REALM_HIS) return realmFloorY();
  World& w = lv.world(m.realm);
  const int x = ifloor(m.pos.x), z = ifloor(m.pos.z);
  for (int y = ifloor(m.pos.y) + 2; y > ifloor(m.pos.y) - 160; y--) { const BlockId id = w.getBlock(x, y, z); if (id && reg().tSolid[id]) return (float)y; }
  return m.pos.y - 40;
}

// what a head goes for: players first, then any creature that isn't one of the great ones (or one of its own kind)
bool prey(const Creature& m, const Entity* e) {
  if (!e || e == &m || e->removed || e->hp <= 0 || e->realm != m.realm) return false;
  if (e->isPlayer()) return isPawn(e) && dget(m, ("ign" + std::to_string(e->id)).c_str(), 0) <= 0;
  const auto* c = dynamic_cast<const Creature*>(e);
  return c && c->alive() && c->T && !c->T->boss && c->type != "wither_skeleton" && c->type != "enderman";
}
// right under it (within ten blocks of its body, sideways) nothing is seen: the safe place
bool underIt(const Creature& m, const Entity& e) { return std::abs(e.pos.x - m.pos.x) < m.w * 0.5f + 10 && std::abs(e.pos.z - m.pos.z) < m.w * 0.5f + 10 && e.pos.y < m.pos.y; }

void injure(Level& lv, Creature& m, int h) {
  const int ph = phaseOf(m);
  m.data[kh("inj", h)] = ph < 4 ? 9.f : 36.f; m.data[kh("hits", h)] = 0;
  // whoever it was pulling goes free, and it pays them no mind for a while
  if (Entity* t = lv.entity(m.data.value(kh("tgt", h), 0u)); t && t->isPlayer()) m.data["ign" + std::to_string(t->id)] = 40.f;
  m.data[kh("tgt", h)] = 0u;
  const vec3 mo = mouthOf(m, h);
  lv.fx->sound("wither_storm_hurt", mo, 120);
  sfx::burst(lv, mo, "mana_arcane", 40, 3.f, json{{"life", 0.8f}, {"size", 0.15f}});
  // and a blue skull, flung anywhere
  vec3 d(frand() - 0.5f, -0.4f, frand() - 0.5f); d = glm::normalize(d);
  if (Entity* sk = spawnProjectile(lv, m.realm, mo, d * 6.f, "wither_skull", &m)) sk->data["blue"] = true;
}

void knockDown(Level& lv, Creature& m) {
  if (stOf(m) != NORMAL || !awake(m)) return;
  m.data["st"] = FALLING; m.data["stT"] = 0.f;
  for (int h = 0; h < 3; h++) { m.data[kh("tgt", h)] = 0u; m.data[kh("inj", h)] = 999.f; }
  lv.fx->sound("wither_storm_roar", m.pos, 300);
  lv.fx->message(0, "The Formidibomb goes off in its maw. The Wither Storm reels, and falls!", "boss");
}

} // namespace

// ---- its mind (the creature hook) ----
static OptMove stormBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  (void)dist; (void)dyp; (void)toPlayer;
  m.noGravity = true; m.hasFlyVy = true;
  const int ph = phaseOf(m), st = stOf(m);
  const Look& L = LOOK[ph];
  // its look, size and strength follow its phase (it swells out of the T while it gathers itself)
  if (m.variant != L.model) m.variant = L.model;
  float spawnT = dget(m, "spawnT", 99.f);
  float s = dget(m, "growS", 1);
  const float want = spawnT < CHARGE ? 0.6f + 0.4f * spawnT / CHARGE : 1.f;
  s += (want - s) * std::min(1.f, dt * 0.8f);
  m.data["growS"] = s; m.w = L.w * s; m.h = L.h * s;
  if (m.maxHp != L.hp) { m.hp = std::max(1.f, m.hp / std::max(1.f, m.maxHp) * L.hp); m.maxHp = L.hp; }
  for (int h = 0; h < 3; h++) m.data[kh("mouth", h)] = jv(mouthOf(m, h));
  if (float r = dget(m, "roarT", 0); r > 0) m.data["roarT"] = r - dt;
  // gathering itself out of the T: untouchable, rising slowly, command-block motes streaming into its chest; then it
  // bursts out (power 7) with the Wither's cry heard everywhere
  if (spawnT < CHARGE) {
    spawnT += dt; m.data["spawnT"] = spawnT;
    m.flyVy = 0.5f; m.vel.x = m.vel.z = 0;
    if (frand() < dt * 30) { const vec3 a = m.pos + vec3((frand() - 0.5f) * 4, frand() * 4, (frand() - 0.5f) * 4); sfx::burst(lv, a, "mana_arcane", 1, 0.2f, json{{"life", 0.5f}, {"size", 0.06f}}); }
    if (spawnT >= CHARGE) {
      ExplodeOpts o; o.source = m.id; o.noBlocks = m.realm == REALM_HIS || !lv.info.rules.value("mobGriefing", true);
      explode(lv, m.realm, m.pos + vec3(0, 2, 0), 7, o);
      lv.fx->sound("wither_spawn", m.pos, 100000);
      m.data["roarT"] = 2.f;
    }
    return Move{0, m.yaw};
  }
  // ---- knocked out of the sky, lying there, waking again, dying ----
  if (st != NORMAL) {
    float stT = dget(m, "stT", 0) + dt; m.data["stT"] = stT;
    m.vel.x *= std::max(0.f, 1 - dt * 2); m.vel.z *= std::max(0.f, 1 - dt * 2);
    if (st == FALLING) {
      const float g = groundUnder(lv, m);
      m.flyVy = std::max(-30.f, m.flyVy - 25 * dt);
      if (frand() < dt * 20) sfx::burst(lv, m.pos + vec3((frand() - 0.5f) * m.w, frand() * m.h, (frand() - 0.5f) * m.w), "burst", 2, 1.f, json{{"tile", "obsidian"}});
      if (m.pos.y <= g + 1 || stT > 20) {   // it lands: the ground shakes
        m.pos.y = std::max(m.pos.y, g + 1); m.flyVy = 0; m.vel = vec3(0);
        m.data["stT"] = 0.f;
        lv.fx->sound("wither_storm_thump", m.pos, 300);
        for (Entity* q : pawnsOf(lv, m.realm)) if (glm::distance(q->pos, m.pos) < 120) lv.fx->shake(q->id, 2.5f);
        if (jflag(m.data, "final")) { m.data["st"] = DYING; lv.fx->sound("command_block_destruct", m.pos, 300); }
        else { m.data["st"] = DOWN; lv.fx->message(0, "It lies on its back, its command block bare. Everything you have hurts it twice over now, before it rises!", "boss"); }
      }
      return Move{0, m.yaw};
    }
    if (st == DOWN) {   // knocked down: half a minute on its back, its command block showing
      m.flyVy = 0;
      if (frand() < dt * 25) sfx::burst(lv, m.pos + vec3(0, m.h * 0.35f, 0), "mana_arcane", 3, 2.f, json{{"life", 0.7f}, {"size", 0.14f}});
      if (float b = dget(m, "pulseT", 0) - dt; b <= 0) { m.data["pulseT"] = 11.f; lv.fx->sound("command_block_pulse_loop", m.pos + vec3(0, m.h * 0.35f, 0), 64); } else m.data["pulseT"] = b;
      if (stT > 30) {   // it wakes, and rises again
        m.data["st"] = RISING; m.data["stT"] = 0.f;
        lv.fx->sound("wither_storm_reactivates", m.pos, 100000);
        ExplodeOpts o; o.source = m.id; o.noBlocks = true;
        explode(lv, m.realm, m.pos + vec3(0, 2, 0), 8, o);
        lv.fx->message(0, "The Wither Storm wakes, and rises again.", "boss");
      }
      return Move{0, m.yaw};
    }
    if (st == RISING) {
      const float g = groundUnder(lv, m);
      m.flyVy = clampv((g + L.hover - m.pos.y) * 0.5f, -2.f, 4.f);
      if (stT > 6) { m.data["st"] = NORMAL; for (int h = 0; h < 3; h++) m.data[kh("inj", h)] = 0.f; lv.fx->sound("wither_storm_tractor_beam_activate", m.pos, 200); }
      return Move{0, m.yaw};
    }
    // dying: it shakes and roars, pieces of it falling away; then it's gone
    m.flyVy = 0;
    if (frand() < dt * 3) lv.fx->sound(frand() < 0.5f ? "wither_storm_roar" : "wither_storm_hurt", m.pos + vec3(0, m.h * 0.6f, 0), 300);
    if (frand() < dt * 30) sfx::burst(lv, m.pos + vec3((frand() - 0.5f) * m.w * 1.5f, frand() * m.h, (frand() - 0.5f) * m.w * 1.5f), "burst", 4, 2.f, json{{"tile", frand() < 0.5f ? "obsidian" : "crying_obsidian"}});
    if (stT > 6) { DamageSrc d; d.type = "storm_core"; d.bypassArmor = true; m.hurt(lv, m.hp + 1, d); }
    return Move{0, m.yaw};
  }
  // ---- awake ----
  m.hp = std::min(m.maxHp, m.hp + (ph < 4 ? 1.f : 2.f) * dt);   // it mends, slowly
  // its sounds: the Wither's voice while it's young, its own growl after; its loop round it
  if (float a = dget(m, "sayT", 3) - dt; a <= 0) { m.data["sayT"] = 4 + frand() * 2; if (ph < 4) lv.fx->voice("wither", "say", mouthOf(m, 0)); else lv.fx->sound("wither_storm_ambient", mouthOf(m, 0), 300); }
  else m.data["sayT"] = a;
  if (float a = dget(m, "loopT", 0) - dt; a <= 0) {
    m.data["loopT"] = ph < 3 ? 11.f : ph < 4 ? 14.f : 8.5f;
    lv.fx->sound(ph < 3 ? "command_block_pulse_loop" : ph < 4 ? "wither_storm_loop" : "wither_storm_close_loop", m.pos + vec3(0, m.h * 0.5f, 0), ph < 4 ? 48.f : 300.f);
  } else m.data["loopT"] = a;
  if (float a = dget(m, "roarCd", 15) - dt; a <= 0) {   // a roar now and then, every head
    m.data["roarCd"] = 20 + frand() * 30; m.data["roarT"] = 2.f;
    if (ph >= 2) lv.fx->sound("wither_storm_roar", mouthOf(m, 0), 300);
  } else m.data["roarCd"] = a;
  // the ones it has let go of for a while
  for (auto it = m.data.begin(); it != m.data.end();) {
    if (it.key().rfind("ign", 0) == 0 && it->is_number()) { const float v = it->get<float>() - dt; if (v <= 0) { it = m.data.erase(it); continue; } *it = v; }
    ++it;
  }
  // ---- its heads: each its own mark ----
  const float range = ph < 4 ? 40.f : 120.f;
  auto valid = [&](Entity* e) { return prey(m, e) && glm::distance(e->pos, m.pos) < range + m.w && !underIt(m, *e); };
  Entity* main = valid(p) ? p : nullptr;
  for (int h = 0; h < 3; h++) {
    if (float inj = hget(m, "inj", h, 0); inj > 0) {
      inj -= dt; m.data[kh("inj", h)] = inj;
      if (inj <= 0 && h == 0) lv.fx->sound("wither_storm_tractor_beam_activate", mouthOf(m, h), 200);
      continue;
    }
    Entity* t = lv.entity(m.data.value(kh("tgt", h), 0u));
    if (h == 0 && main) t = main;
    if (!valid(t)) {
      t = nullptr;
      if (h == 0) { float bd = range; for (auto& e : lv.entities()) if (valid(e.get())) { const float d = glm::distance(e->pos, m.pos); if (d < bd) { bd = d; t = e.get(); } } }
      else if (frand() < dt * 2) {   // a side head looks round for something of its own (players first)
        std::vector<Entity*> c;
        for (auto& e : lv.entities()) if (valid(e.get()) && e->id != m.data.value("tgt0", 0u) && e->id != m.data.value(kh("tgt", 3 - h), 0u)) c.push_back(e.get());
        std::vector<Entity*> pl; for (Entity* e : c) if (e->isPlayer()) pl.push_back(e);
        auto& from = !pl.empty() && frand() < 0.75f ? pl : c;
        if (!from.empty()) t = from[(size_t)(frand() * from.size()) % from.size()];
      }
    }
    m.data[kh("tgt", h)] = t ? t->id : 0u;
    if (!t) { m.data.erase(kh("aim", h)); continue; }
    const vec3 mo = mouthOf(m, h), tp = t->pos + vec3(0, t->isPlayer() ? 1.f : t->h * 0.5f, 0);
    m.data[kh("aim", h)] = jv(tp);
    if (beamOn(m, h)) {
      // the tractor beam: drawn up it to the mouth, and chewed there
      vec3 to = mo - tp; const float l = std::max(0.1f, glm::length(to)); to /= l;
      t->vel += to * ((t->isPlayer() ? 16.f : 20.f) * dt);
      if (t->vel.y < 1.5f) t->vel.y += 34 * dt;   // (gravity doesn't hold them down in it)
      if (float b = hget(m, "beamFx", h, 0) - dt; b <= 0) {
        m.data[kh("beamFx", h)] = 0.12f;
        lv.fx->particles("mana_arcane", mo, 1, json{{"m", "beam"}, {"to", jv(tp + to * 1.5f)}, {"size", 0.2f + 0.05f * ph}, {"width", 0.8f + 0.4f * ph}, {"life", 0.3f}});
      } else m.data[kh("beamFx", h)] = b;
      if (float b = hget(m, "beamSnd", h, 0) - dt; b <= 0) { m.data[kh("beamSnd", h)] = 3.5f; lv.fx->sound("wither_storm_tractor_beam", mo, 120); } else m.data[kh("beamSnd", h)] = b;
      if (l < 1.8f + dget(m, "growS", 1) * (ph < 4 ? 0.4f : 1.2f)) {
        if (t->isPlayer()) {
          if (float c = hget(m, "chomp", h, 0) - dt; c <= 0) { m.data[kh("chomp", h)] = 0.5f; DamageSrc d; d.knock = 0; damageEntity(lv, *t, 3.5f, &m, "mob", d); lv.fx->sound("wither_storm_bite", mo, 48); }
          else m.data[kh("chomp", h)] = c;
        } else {   // a creature: swallowed whole
          t->removed = true; m.data["eaten"] = dget(m, "eaten", 0) + (m.realm == REALM_HIS ? 12 : 4);
          sfx::burst(lv, tp, "mana_arcane", 20, 1.5f, json{{"life", 0.5f}, {"size", 0.1f}});
          lv.fx->sound("wither_storm_bite", mo, 48);
          m.data[kh("tgt", h)] = 0u;
        }
      }
      // and it tears up the ground where its beam bites
      if (float g = hget(m, "grab", h, 1) - dt; g <= 0) {
        m.data[kh("grab", h)] = ph <= 2 ? 1.2f : ph == 3 ? 0.75f : 0.3f + frand() * 0.9f;
        ivec3 at;
        if (biteAt(lv, m, ifloor(t->pos.x + (frand() - 0.5f) * (ph < 4 ? 2 : 4.5f)), ifloor(t->pos.z + (frand() - 0.5f) * (ph < 4 ? 2 : 4.5f)), at))
          tearCluster(lv, m, at, ph < 4 ? 0.5f : ph == 4 ? 1.f + frand() * 0.5f : 1.f + frand() * 2.f, h, true);
      } else m.data[kh("grab", h)] = g;
    } else {
      // a head without a beam spits skulls at its mark
      if (float c = hget(m, "skull", h, 1.5f) - dt; c <= 0) {
        m.data[kh("skull", h)] = ph < 4 ? 2.f + frand() : 9.f + frand() * 4;
        vec3 d = tp - mo; const float l = std::max(0.01f, glm::length(d));
        spawnProjectile(lv, m.realm, mo, d / l * 15.f, "wither_skull", &m);
        lv.fx->sound("wither_storm_shoot", mo, 64);
      } else m.data[kh("skull", h)] = c;
    }
  }
  // ---- it eats the world round it, in single blocks while it's young, in great clumps once it's grown ----
  const float fast = m.realm == REALM_HIS ? 2.f : 1.f;
  const float eaten = dget(m, "eaten", 0);
  int flying = 0;   // (no more than so many clumps in the air at once)
  for (auto& q : lv.entities()) if (q->type == "block_cluster" && !q->removed && q->data.value("storm", 0u) == m.id) flying++;
  if (float e = dget(m, "eatT", 2) - dt * fast; e <= 0 && flying < 120) {
    const int n = ph < 4 ? (ph == 0 ? 1 : ph == 1 ? 3 : ph == 2 ? 6 : 10) : 1;
    const float rad = ph < 4 ? std::min(48.f, 12.f + eaten * 0.02f) : 60.f;
    for (int k = 0; k < n; k++) {
      const float a = frand() * 6.2832f, d = rad * std::sqrt(frand());
      ivec3 at;
      if (biteAt(lv, m, ifloor(m.pos.x + std::cos(a) * d), ifloor(m.pos.z + std::sin(a) * d), at))
        tearCluster(lv, m, at, ph < 4 ? 0.5f : ph == 4 ? 2.f : 3.f, frand() < 0.5f ? 0 : (int)(frand() * 3) % 3, true);
    }
    m.data["eatT"] = ph < 4 ? std::max(0.25f, 3.f - eaten * 0.004f) : 2.f;
  } else m.data["eatT"] = std::max(0.f, e);
  // ---- it grows ----
  if (ph < MAXP && dget(m, "eaten", 0) > GROW[ph]) {
    m.data["phase"] = ph + 1; m.data["roarT"] = 2.f;
    lv.fx->sound("wither_storm_roar", m.pos, 300);
    if (ph + 1 == 4) { lv.fx->sound("wither_storm_evolves", m.pos, 100000); lv.fx->message(0, "The Wither Storm has become something else entirely.", "boss"); }
    else lv.fx->message(0, "The Wither Storm grows.", "boss");
  }
  // ---- where it goes: over its mark, keeping its height over the ground ----
  const float g = groundUnder(lv, m);
  float hover = g + L.hover * dget(m, "growS", 1);
  if (m.realm == REALM_HIS) hover = std::min(hover, realmFloorY() + 30);
  m.flyVy = clampv((hover - m.pos.y) * 0.6f, -3.f, 3.f);
  Entity* lead = main ? main : lv.entity(m.data.value("tgt0", 0u));
  // a lit Formidibomb near it holds its eye
  if (Entity* b = lv.entity(m.data.value("bomb", 0u)); b && !b->removed) lead = b;
  if (!lead) { m.wander(dt); Move wm = m.wanderMove(); wm.want *= ph < 4 ? 1.f : 0.4f; return wm; }
  const float dx = lead->pos.x - m.pos.x, dz = lead->pos.z - m.pos.z, hd = std::hypot(dx, dz);
  const float keep = ph < 4 ? 20.f : 40.f;
  const float sp = hd > keep ? m.T->speed * (ph < 4 ? 1.6f : 0.7f) : 0.f;
  return Move{sp, std::atan2(-dx, -dz)};
}

// what a blow is worth against it (Story Mode's way: nothing ordinary will do): an enchanted hand weapon, the more
// enchanted the better and Smite most of all (it's undead); arrows from an enchanted bow, likewise; magic, and a splash
// of healing (undead: it hurts it, and comes as magic). Ordinary blows and arrows, fire and explosions glance off. 0 = nothing
static float stormWorth(Level& lv, const DamageSrc& src) {
  if (src.type == "magic") return 1.f;
  if (src.type == "arrow" || src.tool == "arrow" || src.type == "projectile")   // only an enchanted bow's arrows: plain ones bounce off
    return src.ench > 0 && lv.entity(src.attacker) && lv.entity(src.attacker)->isPlayer() ? std::min(3.f, 0.6f + 0.15f * src.ench) : 0.f;
  auto* pl = dynamic_cast<Player*>(lv.entity(src.attacker));
  if (!pl || src.type == "explosion" || src.type == "burn" || src.type == "lava") return 0;
  const ItemStack& held = pl->inv[pl->selected];
  int levels = 0, smite = 0;
  for (const char* k : {"en", "ench"}) if (auto it = held.tag.find(k); it != held.tag.end() && it->is_object())
    for (auto& [name, lv_] : it->items()) { const int n = lv_.is_number() ? lv_.get<int>() : 1; levels += n; if (name == "smite") smite += n; }
  if (!levels) return 0;
  return std::min(3.f, 0.6f + 0.15f * levels + 0.25f * smite);
}

// a blow: to a head (the one nearest whoever struck) it counts toward driving that head off for a while, whatever it was;
// to the body only the worthy ones hurt (twice over while it lies knocked down, when anything at all will). At the end
// it doesn't just drop: it falls out of the sky and comes apart where it lands
static bool stormBeforeHurt(Level& lv, Creature& m, float& amount, const DamageSrc& src) {
  if (src.type == "storm_core") return true;   // (its end)
  const int st = stOf(m);
  if (!awake(m) || st == FALLING || st == DYING || st == RISING) return false;
  Entity* a = lv.entity(src.attacker);
  const int ph = phaseOf(m);
  if (st == NORMAL) {
    const vec3 from = src.hasRay ? src.rayO : a ? a->pos : m.pos;
    int best = -1; float bd = 1e9f;
    for (int h = 0; h < 3; h++) { if (hget(m, "inj", h, 0) > 0 || (ph < 2 && h > 0 && src.type == "explosion")) continue; const float d = glm::distance(mouthOf(m, h), from); if (d < bd) { bd = d; best = h; } }
    if (best >= 0 && (src.type == "explosion" ? bd < (ph < 4 ? 5.f : 12.f) : true)) {
      const int hits = (int)hget(m, "hits", best, 0) + 1;
      if (hits >= (ph < 4 ? 2 : 4)) injure(lv, m, best); else m.data[kh("hits", best)] = hits;
    }
  }
  float k = stormWorth(lv, src);
  if (st == DOWN) k = std::max(k, src.type == "explosion" ? 0.f : 1.f) * 2;
  if (k <= 0) {
    if (a && a->isPlayer() && dget(m, "glanceT", 0) <= m.age) {
      m.data["glanceT"] = m.age + 20;
      lv.fx->message(a->id, "Ordinary blows glance off the Wither Storm. Enchanted weapons and bows, magic or a splash of healing will tell.", "warn");
    }
    return false;
  }
  amount *= k;
  if (m.hp - amount <= 0.5f) {   // its last: it falls
    m.hp = 1; amount = 0; m.data["final"] = true; m.data["st"] = FALLING; m.data["stT"] = 0.f;
    for (int h = 0; h < 3; h++) { m.data[kh("tgt", h)] = 0u; m.data[kh("inj", h)] = 999.f; }
    lv.fx->sound("wither_storm_roar", m.pos, 300);
    lv.fx->message(0, "The Wither Storm shudders, and falls out of the sky!", "boss");
    return false;
  }
  return true;
}

// when it falls: what it swallowed comes down. The one he raised in his realm: his ward goes with it
void stormFalls(Level& lv, Creature& m) {
  ExplodeOpts o; o.source = m.id; o.noBlocks = true;
  explode(lv, m.realm, m.pos + vec3(0, m.h * 0.5f, 0), 5, o);
  lv.fx->sound("wither_storm_death", m.pos, 300);
  const int grown = (int)dget(m, "eaten", 0);
  static const char* rain[] = {"cobblestone", "dirt", "gravel", "obsidian", "netherrack"};
  for (int i = 0; i < std::min(48, grown / 40 + 8); i++)
    dropItem(lv, m.realm, ItemStack(rain[i % 5], 1 + (int)(frand() * 3)), m.pos + vec3((frand() - 0.5f) * 8, m.h * frand() * 0.5f, (frand() - 0.5f) * 8), true);
  lv.fx->message(0, "The Wither Storm falls", "boss");
  if (jflag(m.data, "finale")) {   // the Storm he raised in his realm: his ward breaks with it
    json& R = lv.info.data["rift"];
    if (!R.is_object()) R = json::object();
    R["stormBroken"] = 1; R["storm"] = 3;
    for (auto [id, n] : std::vector<std::pair<const char*, int>>{{"nether_star", 3}, {"heart_crystal", 3}, {"rift_crystal", 8}, {"diamond", 16}})
      if (reg().item(id)) dropItem(lv, m.realm, ItemStack(id, n), m.pos + vec3(0, 2, 0), true);
    lv.fx->message(0, "The Storm comes apart, and the ward round Herobrine breaks with it. Finish this.", "epic");
    for (auto& e : lv.entities()) if (e && !e->removed && e->isPlayer()) {
      Player& pl = *static_cast<Player*>(e.get());
      grantXP(lv, pl, 600, m.pos);
      if (!pl.stats.is_object()) pl.stats = json::object();
      pl.stats["story"]["storm"] = 1;
    }
    if (lv.events) lv.events->emit("riftChanged", json::array({R}));
  } else if (reg().item("nether_star")) dropItem(lv, m.realm, ItemStack("nether_star", 1 + phaseOf(m) / 2), m.pos + vec3(0, 2, 0), true);
  if (lv.events) lv.events->emit("stormBroken", json::array());
}

// ---- the clumps it tears out of the world: they tremble, lift, and fly into the head that took them ----
class BlockCluster : public Entity {
public:
  BlockCluster() { type = "block_cluster"; w = 0.5f; h = 1; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    const float dt = (float)dtd;
    auto* st = dynamic_cast<Creature*>(lv.entity(data.value("storm", 0u)));
    if (!st || st->removed || !st->alive() || st->realm != realm || stOf(*st) != NORMAL || age > 40) { letGo(lv); return; }
    data["spin"] = data.value("spin", 0.f) + dt * (0.6f + std::min(2.f, age * 0.2f));
    if (float s = data.value("shake", 0.f); s > 0) { data["shake"] = s - dt; return; }   // trembling where it was
    vec3 mo = st->pos;
    vj(st->data.value(kh("mouth", data.value("head", 0)), json()), mo);
    vec3 to = mo - pos; const float l = glm::length(to);
    if (l < 1.5f + st->w * 0.15f) {   // swallowed
      removed = true;
      if (data.value("counts", true)) st->data["eaten"] = st->data.value("eaten", 0.f) + data.value("n", 1) * (realm == REALM_HIS ? 3.f : 1.f);
      sfx::burst(lv, mo, "burst", 6, 1.5f, json{{"tile", "obsidian"}});
      return;
    }
    to /= std::max(0.01f, l);
    const vec3 side = glm::normalize(glm::cross(to, vec3(0, 1, 0)) + vec3(1e-4f));
    const float sp = std::min(30.f, 3.f + age * 3.f);
    vel += (to * sp + side * 2.5f - vel) * std::min(1.f, dt * 1.5f);
    pos += vel * dt;
  }
  // let go (the Storm knocked down, or gone): it comes apart and falls, every block of it
  void letGo(Level& lv) {
    removed = true;
    if (!data.contains("blocks")) return;
    for (auto& b : data["blocks"]) {
      auto e = createEntity("falling_block");
      if (!e) return;
      e->realm = realm; e->pos = pos + vec3(b[0].get<float>(), b[1].get<float>(), b[2].get<float>()) - vec3(0.5f);
      e->vel = vec3((frand() - 0.5f) * 3, frand() * 2, (frand() - 0.5f) * 3);
      e->data = {{"block", b[3]}, {"meta", b[4]}, {"fling", true}};
      lv.spawn(std::move(e));
    }
  }
};

// ---- Super TNT and the Formidibomb, lit ----
// Super TNT: a bigger bang (4 s). The Formidibomb pulses a long while (30 s); a Wither Storm that sees it drags it up
// into its maw, and once it's there it goes off in a moment. Whatever Storm it bursts in (or right by) is knocked down.
class PrimedBomb : public Entity {
public:
  PrimedBomb() { type = "formidibomb"; w = 0.49f; h = 0.98f; }
  void tick(Level& lv, double dtd) override {
    const float dt = (float)dtd;
    const bool formidi = data.value("kind", "formidibomb") == "formidibomb";
    float fuse = data.value("fuse", 4.f) - dt;
    if (formidi) {
      if (float pt = data.value("pulseT", 0.f) - dt; pt <= 0) { data["pulseT"] = 1.6f; lv.fx->sound("formidibomb_pulse_loop", pos + vec3(0, 0.5f, 0), 48); sfx::burst(lv, pos + vec3(0, 0.5f, 0), "mana_arcane", 12, 1.2f, json{{"life", 0.5f}, {"size", 0.08f}}); }
      else data["pulseT"] = pt;
      // the nearest awake Storm that can see it takes it
      Creature* best = nullptr; float bd = 170;
      for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get()); c && c->type == "wither_storm" && c->realm == realm && !c->removed && c->alive() && awake(*c) && stOf(*c) == NORMAL) {
        const float d = glm::distance(c->pos, pos); if (d < bd) { bd = d; best = c; }
      }
      if (best) {
        best->data["bomb"] = id;
        vec3 mo = best->pos; vj(best->data.value("mouth0", json()), mo);
        vec3 to = mo - (pos + vec3(0, 0.5f, 0)); const float l = glm::length(to);
        noGravity = true;
        if (l < 1.5f + best->w * 0.15f) { pos = mo - vec3(0, 0.5f, 0); vel = vec3(0); fuse = std::min(fuse, 2.5f); data["held"] = best->id; }   // in its maw
        else { vel = to / std::max(0.01f, l) * 4.f; pos += vel * dt; if (!data.value("seen", false)) { data["seen"] = true; lv.fx->sound("wither_storm_roar", mo, 300); } }
      } else noGravity = false;
    }
    data["fuse"] = fuse;
    if (!noGravity) {
      vel.y = std::max(-30.f, vel.y - 22 * dt);
      vel.x *= std::max(0.f, 1 - dt * 3); vel.z *= std::max(0.f, 1 - dt * 3);
      moveEntity(lv.world(realm), *this, dt, {0, 0, false});
    }
    if (frand() < dt * 20) lv.fx->particles("smoke", pos + vec3(0, 1.05f, 0), 1);
    if (fuse > 0) return;
    removed = true;
    const vec3 c = pos + vec3(0, 0.5f, 0);
    ExplodeOpts o; o.noBlocks = realm == REALM_HIS || !lv.info.rules.value("mobGriefing", true) || !lv.info.rules.value("tntExplodes", true);
    explode(lv, realm, c, formidi ? 7.f : 8.f, o);
    if (!formidi) return;
    lv.fx->sound("formidibomb_explosion", c, 600);
    lv.fx->particles("explosion", c, 120, json{{"radius", 14}});
    for (Entity* q : pawnsOf(lv, realm)) if (glm::distance(q->pos, c) < 160) lv.fx->shake(q->id, 3.f);
    for (auto& e : lv.entities()) if (auto* s = dynamic_cast<Creature*>(e.get()); s && s->type == "wither_storm" && s->realm == realm && !s->removed && s->alive()) {
      vec3 mo = s->pos; vj(s->data.value("mouth0", json()), mo);
      if (data.value("held", 0u) == s->id || glm::distance(mo, c) < 24 || glm::distance(s->pos + vec3(0, s->h * 0.5f, 0), c) < s->h * 0.5f + 12) knockDown(lv, *s);
    }
  }
};
void primeFormidi(Level& lv, World& w, int x, int y, int z, const std::string& key) {
  const vec3 c(x + 0.5f, y + 0.5f, z + 0.5f);
  auto e = std::make_unique<PrimedBomb>();
  e->realm = w.realm; e->pos = vec3(x + 0.5f, y, z + 0.5f); e->vel = vec3(0, 2, 0);
  const bool formidi = key == "formidibomb";
  e->data = {{"kind", key}, {"fuse", formidi ? 30.f : 4.f}};
  lv.spawn(std::move(e));
  lv.fx->sound(formidi ? "formidibomb_pulse_loop" : "super_tnt_fuse", c, 48);
  if (formidi) lv.fx->message(0, "The Formidibomb is lit. Let the Storm take it.", "warn");
}

// ---- how it moves (rig "wither_storm": the mod's models, tools/port/wither_storm_models.py) ----
// The tentacles sway and curl as the mod's TentacleModel has them (each out of step with the next); each head turns to
// its own mark and droops, shaking, while it's hurt; the storm heads' jaws chew, and gape when it roars; knocked down it
// tips over onto its back.
static void animStorm(Creature& m, const RigModel& M, RigAnimOut& R) {
  const float t = m.age;
  auto set = [&](const std::string& n, vec3 v) { const int i = M.find(n); if (i < 0) return false; R.has[i] = 1; R.rot[i] = v; return true; };
  static const float K[6] = {-0.1f, 0.1f, 0.075f, 0.05f, 0.1f, 0.1f};
  for (int i = 0; i < 7; i++) {
    std::string seg = "tentacle" + std::to_string(i) + "/base";
    if (M.find(seg) < 0) break;
    const float a = (t * 20 + i * 100) * 0.6f * 0.1f, f = std::cos(a) * 2, sw = std::sin(a * 0.5f) * 2;
    set(seg, vec3(-f * sw * 0.05f, -sw * f * 0.05f, 0));
    for (int k = 0; k < 6; k++) { seg += "/segment" + std::to_string(k + 1); set(seg, vec3(-f * K[k], 0, 0)); }
  }
  const int ph = phaseOf(m);
  // the parts that are its heads (0 the main one): the Wither's, then a storm head in the middle, then the destroyer's
  static const char* HN[3][3] = {{"center_head", "right_head", "left_head"}, {"head0", "right_head", "left_head"}, {"head1", "head0", "head2"}};
  const auto& hn = HN[ph < 2 ? 0 : ph < 4 ? 1 : 2];
  const bool roaring = dget(m, "roarT", 0) > 0;
  for (int h = 0; h < 3; h++) {
    const std::string name = hn[h];
    const bool storm = name.rfind("head", 0) == 0;   // (a storm head rests turned about: its pitch runs the other way)
    vec3 rot(std::sin(t * 0.7f + h) * 0.08f, std::sin(t * 0.45f + h) * 0.15f, 0);
    vec3 aim;
    if (hget(m, "inj", h, 0) > 0 && stOf(m) == NORMAL) rot = vec3(storm ? 0.5f : -0.5f, rot.y, std::sin(t * 9 + h) * 0.25f);   // hurt: drooping, shaking
    else if (vj(m.data.value(kh("aim", h), json()), aim)) {
      vec3 mo; if (!vj(m.data.value(kh("mouth", h), json()), mo)) mo = m.pos;
      const float dx = aim.x - mo.x, dz = aim.z - mo.z;
      const float pitch = clampv(std::atan2(aim.y - mo.y, std::hypot(dx, dz)), -1.f, 0.8f);
      rot = vec3(storm ? -pitch : pitch, clampv(wrapAngle(std::atan2(-dx, -dz) - m.yaw), -1.2f, 1.2f), 0);
    }
    set(name, rot);
    if (storm) set(name + "/lowerJaw", vec3(roaring ? 0.65f : 0.06f + 0.06f * std::sin(t * 2.2f + h), 0, 0));
  }
  const int st = stOf(m); const float stT = dget(m, "stT", 0);
  if (st == FALLING) R.pitch = -std::min(1.f, stT * 0.3f) * 0.9f;
  else if (st == DOWN || st == DYING) R.pitch = -std::min(1.f, 0.3f + stT * 0.25f) * 1.5f;   // on its back
  else if (st == RISING) R.pitch = -std::max(0.f, 1.f - stT * 0.3f) * 1.5f;
  if (st == DYING) R.roll = std::sin(t * 23) * 0.06f;
  R.lift = st == NORMAL ? std::sin(t * 0.8f) * 0.35f : 0.f;
}

void registerStormRig() { registerRigAnim("wither_storm", animStorm); }
void registerStorm() {
  CreatureHooks s; s.brain = stormBrain; s.beforeHurt = stormBeforeHurt; setCreatureHooks("wither_storm", s);
  registerEntityFactory("block_cluster", [](const std::string&) { return std::make_unique<BlockCluster>(); });
  registerEntityFactory("formidibomb", [](const std::string&) { return std::make_unique<PrimedBomb>(); });
}

} // namespace dq
