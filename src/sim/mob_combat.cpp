// sim/mob_combat.cpp — simple implementation of the creature combat adapter (see mob_combat.h).
// Player side follows game/local.js hurt + game/player.js hurt (armour 3.2% a point, at most 80%;
// half-second hurt time; knockback away from the attacker) and game/monsters.js (venom, frost, fire).
#include "sim/mob_combat.h"
#include "sim/combat.h"
#include "sim/magic_combat.h"
#include "sim/player.h"

namespace dq {

float playerEffect(const Entity& p, const char* name) {
  auto it = p.data.find("fx");
  if (it == p.data.end() || !it->is_object()) return 0;
  return it->value(name, 0.0f);
}
void setPlayerEffect(Entity& p, const char* name, float t) {
  if (!p.data.contains("fx") || !p.data["fx"].is_object()) p.data["fx"] = json::object();
  if (t <= 0) p.data["fx"].erase(name); else p.data["fx"][name] = t;
}

// The player's side is sim/combat.cpp's damagePlayer (armour, invulnerable moment, knockback, sounds, and
// playerDie: the grave, the death compass, the death screen). Before, this adapter had its own copy that
// set `dead` directly, so a creature's kill skipped all of that.
static float hurtPlayer(Level& lv, Player& p, float amount, Entity* attacker, const DamageSrc& src) {
  if (p.dead || amount <= 0) return 0;
  DamageSource ds;
  ds.type = src.type; ds.element = src.element; ds.tool = src.tool; ds.attacker = attacker ? attacker->id : src.attacker;
  ds.knock = src.knock; ds.bypassArmor = src.bypassArmor;
  float dmg = damagePlayer(lv, p, amount, ds);
  if (dmg <= 0) return 0;
  // what a creature's touch leaves behind (monsters.js)
  if (auto* c = dynamic_cast<Creature*>(attacker)) {
    const auto& fx = c->T->onHit;
    if (fx.any && !src.noEffect) {
      if (fx.poison > 0 && (c->type != "scorpion" || src.type == "mob")) setPlayerEffect(p, "poison", std::max(playerEffect(p, "poison"), fx.poison));
      if (fx.slow > 0) setPlayerEffect(p, "slow", std::max(playerEffect(p, "slow"), fx.slow));
      if (fx.chill > 0) setPlayerEffect(p, "chill", std::max(playerEffect(p, "chill"), fx.chill));
      if (fx.fire > 0) setPlayerEffect(p, "burn", std::max(playerEffect(p, "burn"), fx.fire));
    }
    if (c->alive() && !p.dead) petsDefend(lv, p, *c);
  }
  return dmg;
}

float damageEntity(Level& lv, Entity& target, float amount, Entity* attacker, const std::string& cause, const DamageSrc& extra) {
  DamageSrc src = extra;
  src.type = cause;
  if (attacker) src.attacker = attacker->id;
  if (auto* c = dynamic_cast<Creature*>(&target)) return c->hurt(lv, amount, src);
  if (auto* p = dynamic_cast<Player*>(&target)) return hurtPlayer(lv, *p, amount, attacker, src);
  return 0;
}

// sim/combat.cpp's damageEntity (the player's blows, explosions, guests' hits) hands creatures to Creature::hurt
bool creatureTakesHit(Level& lv, Entity& e, float amount, const DamageSource& src, float& dealt) {
  auto* c = dynamic_cast<Creature*>(&e);
  if (!c) return false;
  DamageSrc s;
  s.type = src.type; s.element = src.element; s.tool = src.tool; s.attacker = src.attacker; s.knock = src.knock;
  s.weapon = !src.weapon.empty(); s.bypassArmor = src.bypassArmor;
  s.hasRay = src.hasRay; s.rayO = src.rayO; s.rayD = src.rayD;
  if (s.type == "arrow" && s.tool.empty()) s.tool = "arrow";
  dealt = c->hurt(lv, amount, s);
  return true;
}

void knockback(Entity& e, vec3 v) {
  e.vel.x += v.x; e.vel.z += v.z;
  if (v.y) e.vel.y = std::max(e.vel.y, v.y);
}

void igniteEntity(Entity& e, float t) {
  if (auto* c = dynamic_cast<Creature*>(&e)) { if (!c->T->fireImmune) c->burn = std::max(c->burn, t); return; }
  setPlayerEffect(e, "burn", std::max(playerEffect(e, "burn"), t));
}

void tickPlayerEffects(Level& lv, Player& p, float dt) {
  if (p.dead) { if (p.data.contains("fx")) p.data.erase("fx"); return; }
  if (p.hurtTime > 0) p.hurtTime = std::max(0.f, p.hurtTime - dt);
  float poison = playerEffect(p, "poison");
  if (poison > 0) {
    float t = playerEffect(p, "poisonTick") + dt;
    if (t >= 1.25f) { t = 0; if (p.hp > 1) { DamageSrc s; s.bypassArmor = true; hurtPlayer(lv, p, 1, nullptr, [&] { s.type = "poison"; return s; }()); } }
    setPlayerEffect(p, "poisonTick", t);
    setPlayerEffect(p, "poison", poison - dt);
  }
  float chill = playerEffect(p, "chill");
  if (chill > 0) { setPlayerEffect(p, "chill", chill - dt); if (playerEffect(p, "slow") < 0.2f) setPlayerEffect(p, "slow", 0.2f); }
  float slow = playerEffect(p, "slow");
  if (slow > 0) setPlayerEffect(p, "slow", slow - dt);
  float burn = playerEffect(p, "burn");
  if (burn > 0) {
    if (p.inWater) { setPlayerEffect(p, "burn", 0); return; }
    float t = playerEffect(p, "burnTick") + dt;
    if (t > 0.8f) { t = 0; DamageSrc s; s.type = "burn"; hurtPlayer(lv, p, 1, nullptr, s); }
    setPlayerEffect(p, "burnTick", t);
    setPlayerEffect(p, "burn", burn - dt);
  }
}

void grantXP(Level& lv, Entity& who, int xp, vec3 at) {
  auto* p = dynamic_cast<Player*>(&who);
  if (!p || xp <= 0) return;
  grantXP(lv, *p, xp, &at);   // the real one (sim/magic_combat.cpp): levels, Insight, Mending
}

} // namespace dq
