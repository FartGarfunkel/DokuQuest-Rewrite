// sim/magic_bolt.cpp — SpellBolt / ArcBolt (game/magic.js, arcana.js) and the thrown splash potion (brewing.js).
#include "sim/magic_entities.h"
#include "sim/magic.h"
#include "sim/brewing.h"
#include "sim/magic_fx.h"

namespace dq {

static Player* asPlayer(Level& lv, u32 id) { Entity* e = id ? lv.entity(id) : nullptr; return e && e->isPlayer() ? static_cast<Player*>(e) : nullptr; }

void SpellBolt::tick(Level& lv, double dt) {
  if (age > 2.4f) { removed = true; return; }
  if (spell == "missiles" && target) {
    Entity* t = lv.entity(target);
    if (t && isAlive(*t)) {
      float sp = glm::length(vel); if (sp < 1e-3f) sp = 18;
      vec3 d = t->pos + vec3(0, t->h * 0.6f, 0) - pos;
      float l = std::max(1e-3f, glm::length(d)), k = std::min(1.f, (float)dt * 7);
      vel += (d / l * sp - vel) * k;
    }
  }
  World& w = lv.world(realm);
  for (int s = 0; s < 4; s++) {
    pos += vel * (float)dt / 4.f;
    for (auto& up : lv.entities()) {
      Entity& e = *up;
      if (e.realm != realm || e.id == caster || !isSpellTarget(e) || (e.ghost && !lv.isRemote())) continue;
      if (std::abs(e.pos.x - pos.x) < e.w + 0.25f && std::abs(e.pos.z - pos.z) < e.w + 0.25f && pos.y > e.pos.y - 0.1f && pos.y < e.pos.y + e.h + 0.1f) { hit(lv, &e); return; }
    }
    if (reg().tSolid[w.getBlock(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z))]) { hit(lv, nullptr); return; }
  }
}

void SpellBolt::hit(Level& lv, Entity* e) {
  removed = true;
  Player* by = asPlayer(lv, caster);
  if (spell == "fireball") {   // the fireball bursts: everything near it burns
    lv.fx->sound("explode", pos, 40);
    for (auto& up : lv.entities()) {
      Entity& m = *up;
      if (m.realm != realm || !isSpellTarget(m) || (m.data.contains("owner") && !m.data["owner"].is_null() && m.data["owner"] != "")) continue;
      if (m.ghost && !lv.isRemote()) continue;
      float d = glm::distance(m.pos + vec3(0, m.h / 2, 0), pos);
      if (d > 3.4f) continue;
      float dmg2 = std::round(dmg * (1 - d / 5));
      combatHurt(lv, m, dmg2, json{{"type", "magic"}, {"element", "fire"}, {"knock", 3}}, by);
      if (!isFireImmune(m)) setMobStatus(lv, m, "burnT", 5, by);
    }
    return;
  }
  lv.fx->sound("spell_hit", pos, 28);
  if (!e) return;
  if (spell == "missiles") { combatHurt(lv, *e, dmg, json{{"type", "magic"}, {"knock", 0.5}}, by); return; }
  combatHurt(lv, *e, dmg, json{{"type", "magic"}, {"element", spell == "firebolt" ? "fire" : "frost"}, {"knock", 1}}, by);
  if (spell == "firebolt" && !isFireImmune(*e)) setMobStatus(lv, *e, "burnT", 4, by);
  if (spell == "frost") setMobStatus(lv, *e, "slowT", 4, by);
}

json SpellBolt::save() const { json j = Entity::save(); j["data"]["spell"] = spell; j["data"]["caster"] = caster; j["data"]["target"] = target; j["data"]["dmg"] = dmg; return j; }
void SpellBolt::load(const json& j) {
  Entity::load(j);
  spell = data.value("spell", "firebolt"); caster = data.value("caster", 0u); target = data.value("target", 0u); dmg = data.value("dmg", 0.f);
}

void PotionFlask::tick(Level& lv, double dt) {
  if (age > 8) { removed = true; return; }
  World& w = lv.world(realm);
  for (int s = 0; s < 3; s++) {
    float h = (float)dt / 3;
    vel.y -= 20 * h;
    pos += vel * h;
    bool grace = age < 0.3f;
    if (lv.isRemote()) { if (reg().tSolid[w.getBlock(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z))]) { removed = true; return; } continue; }
    for (auto& up : lv.entities()) {
      Entity& e = *up;
      if (e.removed || e.realm != realm || e.id == id || (grace && e.id == owner)) continue;
      if (e.isPlayer()) {
        if (static_cast<Player&>(e).dead) continue;
        if (std::abs(e.pos.x - pos.x) < 0.5f && std::abs(e.pos.z - pos.z) < 0.5f && pos.y > e.pos.y && pos.y < e.pos.y + 1.9f) { smash(lv); return; }
      } else if (isMob(e) && isAlive(e)) {
        if (std::abs(e.pos.x - pos.x) < e.w + 0.2f && std::abs(e.pos.z - pos.z) < e.w + 0.2f && pos.y > e.pos.y && pos.y < e.pos.y + e.h) { smash(lv); return; }
      }
    }
    if (reg().tSolid[w.getBlock(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z))]) { smash(lv); return; }
  }
}
void PotionFlask::smash(Level& lv) {
  removed = true;
  splashPotionAt(lv, realm, pos, potion, asPlayer(lv, owner));
}
json PotionFlask::save() const { json j = Entity::save(); j["data"]["potion"] = potion; j["data"]["owner"] = owner; return j; }
void PotionFlask::load(const json& j) { Entity::load(j); potion = data.value("potion", ""); owner = data.value("owner", 0u); }

void registerMagicEntities() {
  registerEntityFactory("spell_bolt", [](const std::string&) { return std::make_unique<SpellBolt>(); });
  registerEntityFactory("potion_flask", [](const std::string&) { return std::make_unique<PotionFlask>(); });
}

} // namespace dq
