// sim/talent_fx.cpp — see talent_fx.h (the numbers: sim/balance.h)
#include "sim/talent_fx.h"
#include "sim/balance.h"
#include "sim/creature.h"
#include "sim/magic_combat.h"
#include <algorithm>

namespace dq {
using namespace bal;

float bleedLeft(const Entity& e) { return e.data.is_object() ? e.data.value("bleedT", 0.f) : 0.f; }

void talentOnHit(Level& lv, Player& p, Entity& foe, float dealt, double roll) {
  if (dealt <= 0 || foe.ghost || lv.isRemote()) return;
  auto* c = dynamic_cast<Creature*>(&foe);
  if (!c || !c->alive()) return;
  auto comes = [&](const char* attr) { const float ch = talent(p, attr); return ch > 0 && (roll < 0 ? frand() : roll) < ch; };
  const vec3 at = c->pos + vec3(0, c->h * 0.6f, 0);
  if (comes("bleed_chance")) {
    c->data["bleedT"] = std::max(bleedLeft(*c), HIT_BLEED_SECS);
    c->data["bleedBy"] = p.id;   // (a bleeding kill is still yours: its drops, its experience)
    lv.fx->particles("crit", at, 4);
  }
  if (comes("poison_chance") && (!c->T->undead || talent(p, "venom_undead") > 0)) {
    c->poisonT = std::max(c->poisonT, HIT_POISON_SECS);
    if (talent(p, "venom_lethal") > 0) c->data["venomKills"] = true;
  }
  if (comes("slow_chance")) c->slowT = std::max(c->slowT, HIT_SLOW_SECS);
  if (comes("burn_chance") && !c->T->fireImmune) c->burn = std::max(c->burn, HIT_BURN_SECS);
  if (const float ls = talent(p, "lifesteal"); ls > 0) combatHeal(lv, p, dealt * ls);
}

} // namespace dq
