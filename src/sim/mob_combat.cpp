// sim/mob_combat.cpp — simple implementation of the creature combat adapter (see mob_combat.h).
// Player side follows game/local.js hurt + game/player.js hurt (armour 3.2% a point, at most 80%;
// half-second hurt time; knockback away from the attacker) and game/monsters.js (venom, frost, fire).
#include "sim/mob_combat.h"
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

static float armorPoints(const Player& p) {
  float pts = 0;
  for (auto& s : p.armor) if (auto* d = s.def()) if (d->props.contains("armor") && d->props["armor"].is_object()) pts += d->props["armor"].value("points", 0.0f);
  return pts;
}
static float fireResist(const Player& p) {
  float r = 0;
  for (auto& s : p.armor) if (auto* d = s.def()) if (d->props.contains("armor") && d->props["armor"].is_object()) r += d->props["armor"].value("fireResist", 0.0f);
  return clampv(r, 0.f, 1.f);
}

static float hurtPlayer(Level& lv, Player& p, float amount, Entity* attacker, const DamageSrc& src) {
  if (p.dead || amount <= 0) return 0;
  if (p.creative() || p.mode == "spectator") return 0;
  const json& R = lv.info.rules;
  if ((src.type == "drown" && R.value("drowning", true) == false) || ((src.type == "lava" || src.type == "burn") && R.value("fireDamage", true) == false)) return 0;
  if (p.hurtTime > 0) return 0;   // invulnerable for a moment after a hit
  float dmg = amount;
  if (!src.bypassArmor) dmg *= 1 - std::min(0.8f, armorPoints(p) * 0.032f);
  if (src.element == "fire" || src.type == "lava" || src.type == "burn") dmg *= 1 - fireResist(p);
  dmg = std::max(0.5f, std::round(dmg * 2) / 2);
  p.hp = std::max(0.f, p.hp - dmg);
  p.hurtTime = src.type == "burn" || src.type == "lava" ? 0.45f : 0.6f;
  lv.fx->soundUi(p.id, "hurt");
  if (attacker && src.knock != 0) {
    float kx = p.pos.x - attacker->pos.x, kz = p.pos.z - attacker->pos.z, l = std::hypot(kx, kz); if (l < 1e-4f) l = 1;
    float k = src.knock < 0 ? 5 : src.knock;
    p.vel.x += kx / l * k; p.vel.z += kz / l * k; p.vel.y = std::max(p.vel.y, 4.f);
  }
  if (src.element == "fire" && src.type != "burn") setPlayerEffect(p, "burn", std::max(playerEffect(p, "burn"), 2.f));
  // what a creature's touch leaves behind (monsters.js)
  if (auto* c = dynamic_cast<Creature*>(attacker)) {
    const auto& fx = c->T->onHit;
    if (fx.any && !src.noEffect) {
      if (fx.poison > 0 && (c->type != "scorpion" || src.type == "mob")) setPlayerEffect(p, "poison", std::max(playerEffect(p, "poison"), fx.poison));
      if (fx.slow > 0) setPlayerEffect(p, "slow", std::max(playerEffect(p, "slow"), fx.slow));
      if (fx.chill > 0) setPlayerEffect(p, "chill", std::max(playerEffect(p, "chill"), fx.chill));
      if (fx.fire > 0) setPlayerEffect(p, "burn", std::max(playerEffect(p, "burn"), fx.fire));
    }
    if (c->alive()) petsDefend(lv, p, *c);
  }
  if (p.hp <= 0) {
    p.dead = true;
    if (lv.events) lv.events->emit("playerDeath", json::array({p.name, src.type, attacker ? json(attacker->type) : json(nullptr)}));
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
  p->xp += xp;
  floatText(lv, p->id, at, "+" + std::to_string(xp) + " XP", "#b6f06a");
}

} // namespace dq
