// sim/item_uses.cpp — the War Horn, the dragons' armour sets and the capes (sim/item_uses.h).
//   Wyrmblood (dragonscale): a tenth less harm, and wounds slowly close (fire is the pieces' own resistance)
//   Greenwood (verdant):     wounds close under the open sky by day; poison never takes
//   Dunewalker (sand):       quicker on sand; falls hurt half
//   Rime (frost):            frost hurts a fifth; still water freezes under your feet a while; what strikes you is slowed
//   Mire (bog):              poison never takes; what strikes you is poisoned
//   Cinderheart (ember):     the pieces' fire resistance (to the cap); what strikes you burns
//   Thunderhide (storm):     what strikes you is jolted and thrown back; falls hurt half
//   Deepcurrent (tide):      breath under water never runs out; quicker swimming
// The capes: ember (a fifth off fire, burning out sooner), frost (frost hurts 30% less, its chill never slows you),
// royal (the shops ask a tenth less).
#include "sim/item_uses.h"
#include "sim/creature.h"
#include "sim/controller.h"
#include "sim/gameplay.h"
#include "sim/effects.h"
#include "sim/mob_combat.h"
#include "sim/magic_combat.h"
#include "sim/rules.h"
#include "world/dims.h"
#include "core/registry.h"
#include <cmath>

namespace dq {
double playTime(Level& lv);   // sim/content.h (its toast clashes with sim/gameplay.h's)

std::string gearSet(const Player& p) {
  std::string set;
  for (int i = 0; i < 4; i++) {
    if (i >= (int)p.armor.size() || p.armor[i].empty()) return "";
    const ItemDef* d = reg().item(p.armor[i].id);
    if (!d || !d->props.contains("armor") || !d->props["armor"].is_object()) return "";
    const std::string s = d->props["armor"].value("set", std::string());
    if (s.empty() || (i && s != set)) return "";
    set = s;
  }
  return set;
}
std::string capeOf(const Player& p) {
  if (p.cape.empty() || p.cape[0].empty()) return "";
  const ItemDef* d = reg().item(p.cape[0].id);
  return d ? d->str("cape") : "";
}

float gearFireResist(const Player& p) { return capeOf(p) == "ember_cape" ? 0.2f : 0.f; }

float gearHarmFactor(const Player& p, const DamageSource& src) {
  const std::string set = gearSet(p), cape = capeOf(p);
  float k = 1;
  if (set == "dragonscale") k *= 0.9f;
  if (src.element == "frost") { if (set == "frostscale") k *= 0.2f; if (cape == "frost_cape") k *= 0.7f; }
  if (src.type == "fall" && (set == "sandscale" || set == "stormscale")) k *= 0.5f;
  return k;
}

void gearOnHurt(Level& lv, Player& p, const DamageSource& src, float dealt) {
  if (dealt <= 0 || !src.attacker || src.type == "magic" || src.type == "arrow" || src.type == "projectile" || src.type == "explosion") return;
  Entity* a = lv.entity(src.attacker);
  if (!a || a == &p || a->removed || !isAlive(*a) || glm::distance(a->pos, p.pos) > 6) return;
  const std::string set = gearSet(p);
  if (set == "frostscale") { setMobStatus(lv, *a, "slowT", 3, &p); if (lv.fx) lv.fx->particles("white", a->pos + vec3(0, a->h * 0.6f, 0), 6); }
  else if (set == "bogscale") setMobStatus(lv, *a, "poisonT", 4, &p);
  else if (set == "emberscale") { if (!isFireImmune(*a)) setMobStatus(lv, *a, "burnT", 3, &p); }
  else if (set == "stormscale") {
    combatHurt(lv, *a, 2, json{{"type", "magic"}, {"element", "lightning"}, {"knock", 0}}, &p);
    combatKnock(lv, *a, p.pos, 7);
    if (lv.fx) { lv.fx->particles("spark", a->pos + vec3(0, a->h * 0.6f, 0), 10); lv.fx->sound("zap", a->pos, 16); }
  }
}

float gearMoveFactor(Level& lv, const Player& p) {
  const std::string set = gearSet(p);
  if (set == "tidescale" && p.inWater) return 1.35f;
  if (set == "sandscale" && p.onGround) {
    static const BlockId sand = reg().blockId("sand"), red = reg().blockId("red_sand");
    const BlockId under = lv.world(p.realm).getBlock(ifloor(p.pos.x), ifloor(p.pos.y - 0.1f), ifloor(p.pos.z));
    if (under && (under == sand || under == red)) return 1.25f;
  }
  return 1;
}
float capeShopFactor(const Player& p) { return capeOf(p) == "royal_cape" ? 0.9f : 1.f; }

// ---- the War Horn ----
bool rallied(Level& lv, const Entity& e) { return e.data.is_object() && e.data.value("rally", -1.0) > playTime(lv); }

bool warHornBlow(Level& lv, Player& p, int& answered, std::string& why) {
  answered = 0;
  const double now = playTime(lv), ready = p.stats.value("warHornAt", 0.0);
  if (now < ready && ready - now <= HORN_COOLDOWN) { why = "Your breath's not back yet (" + std::to_string((int)std::ceil(ready - now)) + " s)"; return false; }
  p.stats["warHornAt"] = now + HORN_COOLDOWN;
  for (auto& o : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(o.get());
    if (!c || c->removed || !c->alive() || !c->tamed || c->owner != p.name || c->realm != p.realm) continue;
    if (glm::distance(c->pos, p.pos) > HORN_RANGE || c->riddenBy) continue;
    c->stay = false; c->data.erase("stay");
    if (glm::distance(c->pos, p.pos) > 6) {   // it comes to you: set down a little way off, on the ground you stand on
      const float a = (float)(answered * 2.4);
      c->pos = p.pos + vec3(std::cos(a) * 2.5f, 0.1f, std::sin(a) * 2.5f);
      c->vel = vec3(0);
    }
    c->data["rally"] = now + HORN_SECS;
    if (lv.fx) lv.fx->particles("note", c->pos + vec3(0, c->h + 0.3f, 0), 5);
    answered++;
  }
  if (lv.fx) lv.fx->sound("roar", p.pos + vec3(0, 1.6f, 0), 64, 1.f, 0.8f);
  return true;
}

namespace {
// the sets' and capes' effects over time: wounds closing, poison and chill shaken off, water freezing, breath kept
class GearEffects : public LevelSystem {
public:
  const char* name() const override { return "gear_effects"; }
  double acc = 0;
  struct Frozen { int realm; ivec3 at; double until; };
  std::vector<Frozen> frozen;
  void tick(Level& lv, double dt) override {
    if (lv.isRemote()) return;
    for (auto& up : lv.entities()) {
      auto* p = dynamic_cast<Player*>(up.get());
      if (!p || p->removed || p->dead || p->ghost) continue;
      const std::string set = gearSet(*p), cape = capeOf(*p);
      if (set.empty() && cape.empty()) continue;
      if (set == "tidescale") p->air = std::max(p->air, p->maxAir);
      if (cape == "ember_cape" && p->burn > 0) p->burn = std::max(0.f, p->burn - (float)dt * 0.5f);
      if (cape == "frost_cape" || set == "frostscale") { setPlayerEffect(*p, "chill", 0); if (playerEffect(*p, "slow") > 0 && cape == "frost_cape") setPlayerEffect(*p, "slow", 0); }
      if (set == "verdantscale" || set == "bogscale") {
        setPlayerEffect(*p, "poison", 0);
        if (p->stats.contains("fx") && p->stats["fx"].is_object()) p->stats["fx"].erase("poison");
      }
      if (set == "frostscale" && p->onGround) frostWalk(lv, *p);
      // wounds closing: a half heart every 5 s (the Wyrmblood) or 3 s by day under the open sky (Greenwood)
      float every = 0;
      if (set == "dragonscale") every = 5;
      if (set == "verdantscale") {
        World& w = lv.world(p->realm);
        const int x = ifloor(p->pos.x), y = ifloor(p->pos.y + p->eye), z = ifloor(p->pos.z);
        if (dimOfY(y) == DIM_OVERWORLD && daylight(lv) > 0.6f && (w.getLight(x, y, z) >> 4) >= 14) every = 3;
      }
      if (every > 0 && p->hp < p->maxHp) {
        float t = p->data.value("gearHealT", 0.f) + (float)dt;
        if (t >= every) { t = 0; p->heal(1); }
        p->data["gearHealT"] = t;
      }
    }
    if ((acc += dt) >= 0.5) {   // frozen water thaws again once nobody stands on it
      acc = 0;
      static const BlockId ice = reg().blockId("ice"), water = reg().blockId("water");
      const double now = playTime(lv);
      for (size_t i = 0; i < frozen.size();) {
        Frozen& f = frozen[i];
        if (now < f.until) { i++; continue; }
        World& w = lv.world(f.realm);
        bool stood = false;
        for (auto& up : lv.entities()) if (up->isPlayer() && up->realm == f.realm && ifloor(up->pos.x) == f.at.x && ifloor(up->pos.z) == f.at.z && std::abs(up->pos.y - (f.at.y + 1)) < 1.2f) stood = true;
        if (stood) { f.until = now + 2; i++; continue; }
        if (w.isLoaded(f.at.x, f.at.y, f.at.z) && w.getBlock(f.at.x, f.at.y, f.at.z) == ice) w.setBlock(f.at.x, f.at.y, f.at.z, water);
        frozen[i] = frozen.back(); frozen.pop_back();
      }
    }
  }
  void frostWalk(Level& lv, Player& p) {
    static const BlockId ice = reg().blockId("ice"), water = reg().blockId("water");
    if (!ice || !water) return;
    World& w = lv.world(p.realm);
    const int y = ifloor(p.pos.y - 0.1f), cx = ifloor(p.pos.x), cz = ifloor(p.pos.z);
    const double until = playTime(lv) + 8;
    for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
      if (dx * dx + dz * dz > 5) continue;
      const int x = cx + dx, z = cz + dz;
      if (w.getBlock(x, y, z) != water || (w.getMeta(x, y, z) & 15) != 0 || w.getBlock(x, y + 1, z) != AIR) continue;   // still water at the top only
      w.setBlock(x, y, z, ice);
      frozen.push_back({p.realm, ivec3(x, y, z), until});
    }
  }
};
} // namespace

void registerItemUses() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> { return std::make_shared<GearEffects>(); });
  hooks().useItem.push_back([](PlayerController& pc, ItemStack&, const ItemDef& def, bool repeat) {
    if (repeat || !def.has("warHorn")) return false;
    int n = 0; std::string why;
    if (!warHornBlow(pc.lv, pc.p, n, why)) { pc.toastMsg(why); return true; }
    pc.toastMsg(n ? "You sound the horn: " + std::to_string(n) + (n == 1 ? " companion comes" : " companions come") + ", quicker and stronger for a while"
                  : "You sound the horn, but no companion of yours is near enough to hear it", n ? "good" : "");
    pc.swingArm();
    return true;
  });
}

} // namespace dq
