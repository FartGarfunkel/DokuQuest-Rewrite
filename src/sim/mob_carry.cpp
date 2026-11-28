// sim/mob_carry.cpp — lifting creatures in your arms and in a wicker basket, by the data (sim/mob_carry.h).
#include "sim/mob_carry.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "core/registry.h"

namespace dq {

bool canCarryInArms(const Creature& c, const Player& p, std::string* why) {
  auto no = [&](const char* s) { if (why) *why = s; return false; };
  if (!c.alive() || c.removed) return false;
  if (c.riddenBy) return no("Someone is riding it");
  const std::string& m = c.T->carryMode;
  if (m != "any" && m != "pet") return no(m == "basket" || fitsBasket(c, &p) ? "It wriggles out of your arms: a wicker basket would hold it" : "");
  if (!c.owner.empty() && c.owner != p.name) return no("It isn't yours to pick up");
  if (m == "pet" && c.owner.empty()) return no(fitsBasket(c, &p) ? "It won't let you pick it up until it's tamed: a wicker basket would hold it" : "It won't let you pick it up until it's tamed");
  return true;
}

bool fitsBasket(const Creature& c, const Player* p) {
  if (!c.alive() || c.removed || c.riddenBy) return false;
  if (!c.owner.empty() && (!p || c.owner != p->name)) return false;   // (someone else's pet stays theirs)
  if (c.T->boss || (c.T->hostile && c.owner.empty())) return false;
  const std::string& m = c.T->carryMode;
  if (m == "pet" || m == "basket") return true;
  if (m == "any" && c.T->w <= 0.3f && c.T->h <= 1.0f) return true;   // (a chicken, a rabbit, a duck: small enough)
  return c.baby;                                                     // any young animal fits
}

bool pickUpCreature(Level& lv, Player& p, Creature& c, std::string* why) {
  if (!p.carrying.is_null()) { if (why) *why = "Your arms are full"; return false; }
  if (!canCarryInArms(c, p, why)) return false;
  // (save: its whole self, coat, owner, name and stable place; set down it's the same animal)
  p.carrying = {{"kind", "mob"}, {"type", c.type}, {"hp", c.hp}, {"data", c.data}, {"save", c.save()}};
  c.removed = true;
  (void)lv;
  return true;
}

Creature* setDownCarriedCreature(Level& lv, Player& p, vec3 at, float yaw) {
  const json& c = p.carrying;
  if (!c.is_object() || c.value("kind", "") != "mob") return nullptr;
  auto e = createEntity(c.value("type", std::string("boar")));
  if (!e) return nullptr;
  if (c.contains("save") && c["save"].is_object()) e->load(c["save"]);   // (its coat, colour, age, name, owner... as it was)
  e->realm = p.realm; e->pos = e->prevPos = at; e->vel = vec3(0); e->yaw = yaw; e->removed = false;
  if (c.contains("hp") && c["hp"].is_number()) e->hp = c["hp"];
  if (c.contains("data") && c["data"].is_object()) e->data = c["data"];
  if (reg().mobs.contains(e->type) && reg().mobs[e->type].value("farm", false)) e->data["kept"] = true;
  Entity* placed = lv.spawn(std::move(e));
  p.carrying = nullptr;
  return dynamic_cast<Creature*>(placed);
}

Entity* setDownCarried(Level& lv, Player& p, vec3 at, float yaw) {
  return setDownCarriedCreature(lv, p, at, yaw);
}

} // namespace dq
