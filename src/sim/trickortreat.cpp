// sim/trickortreat.cpp — Halloween (October, see common/holiday.h): on a night in a village, a villager standing near
// you now and then tosses you a sweet treat, at most once per villager per night.
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/content.h"
#include "sim/villagers.h"
#include "common/holiday.h"
#include "core/registry.h"
#include <vector>

namespace dq {

namespace {
class TrickOrTreat : public LevelSystem {
public:
  const char* name() const override { return "trickortreat"; }
  double acc = 0;
  void tick(Level& lv, double dt) override {
    if (!halloween() || lv.isRemote() || !lv.isNight()) return;
    acc += dt;
    if (acc < 1.0) return;   // once a second is plenty
    acc = 0;
    const int night = (int)lv.info.day;
    std::vector<std::pair<Creature*, Player*>> todo;   // (collected first: dropping an item spawns an entity)
    for (auto& pe : lv.entities()) {
      auto* p = dynamic_cast<Player*>(pe.get());
      if (!p || p->removed || p->dead) continue;
      for (auto& ve : lv.entities()) {
        auto* v = dynamic_cast<Creature*>(ve.get());
        if (!v || v->type != "villager" || v->removed || v->ghost || !v->alive() || v->realm != p->realm) continue;
        if (std::hypot(v->pos.x - p->pos.x, v->pos.z - p->pos.z) > 6.f || std::fabs(v->pos.y - p->pos.y) > 3.f) continue;
        if (v->data.value("treatNight", -1) == night) continue;   // already had its turn tonight
        if (frand() > 0.15f) continue;                             // now and then
        if (!villageAtPos(lv, v->realm, v->pos, 0)) continue;     // only in a village
        v->data["treatNight"] = night;
        todo.push_back({v, p});
        break;   // one treat per player per pass
      }
    }
    static const char* SWEETS[] = {"cookie", "cookie", "pumpkin_pie", "honey_bottle"};
    for (auto& [v, p] : todo) {
      const char* k = SWEETS[(int)(frand() * 4) & 3];
      if (!reg().item(k)) continue;
      vec3 from = v->pos + vec3(0, 1.4f, 0), to = p->pos + vec3(0, 1.0f, 0), d = to - from;
      float len = std::max(0.5f, glm::length(d));
      vec3 vel = d / len * 3.2f + vec3(0, 2.0f, 0);
      v->yaw = heading(v->pos.x, v->pos.z, p->pos.x, p->pos.z);
      dropItem(lv, v->realm, ItemStack(k, std::string(k) == "cookie" ? 1 + (int)(frand() * 3) : 1), from, vel);
      lv.fx->message(p->id, "A villager gives you a treat", "good");
    }
  }
};
} // namespace

void registerTrickOrTreat() {
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> { return std::make_shared<TrickOrTreat>(); });
}

} // namespace dq
