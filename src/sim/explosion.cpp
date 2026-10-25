// sim/explosion.cpp — game/blockfx.js explode: blow a round hole, knock everyone back.
#include "sim/explosion.h"
#include "sim/creature.h"
#include "sim/mob_combat.h"
#include "core/registry.h"

namespace dq {

void explode(Level& lv, int realm, vec3 c, float power, const ExplodeOpts& o) {
  World& w = lv.world(realm);
  const Registry& R = reg();
  int r = (int)std::ceil(power);
  lv.fx->sound("explode", c, 64);
  for (Entity* q : pawnsOf(lv, realm)) if (glm::distance(q->pos, c) < 32) lv.fx->shake(q->id, 0.8f);
  if (!o.noBlocks) {
    for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      float d = std::sqrt((float)(dx * dx + dy * dy + dz * dz));
      if (d > power * (0.75f + frand() * 0.3f)) continue;
      int bx = ifloor(c.x + dx), by = ifloor(c.y + dy), bz = ifloor(c.z + dz);
      if (by < 0 || by >= WORLD_H) continue;
      BlockId id = w.getBlock(bx, by, bz);
      const BlockDef& b = R.block(id);
      if (!id || R.tLiquid[id] || b.hardness < 0 || b.hardness >= 20 || b.str("mech") == "none") continue;
      if (o.onBlock && !o.onBlock(bx, by, bz, id)) continue;
      if (json* be = w.blockEntity(bx, by, bz)) {   // spill what a chest held
        if (be->contains("items") && (*be)["items"].is_array()) for (auto& s : (*be)["items"]) { ItemStack st = ItemStack::fromJson(s); if (!st.empty()) dropItem(lv, realm, st, vec3(bx + 0.5f, by + 0.5f, bz + 0.5f), true); }
        w.setBlockEntity(bx, by, bz, nullptr);
      }
      w.setBlock(bx, by, bz, AIR, 0, SetFlags::Urgent);
      if (frand() < 0.3f && !b.drop.empty() && lv.info.rules.value("blockDrops", true)) dropItem(lv, realm, ItemStack(b.drop, 1), vec3(bx + 0.5f, by + 0.5f, bz + 0.5f), true);
    }
  }
  lv.fx->particles("explosion", c, 40, json{{"radius", power}});
  for (Entity* p : pawnsOf(lv, realm)) {
    float d = glm::distance(p->pos + vec3(0, 1, 0), c);
    if (d < power * 2) {
      float k = 1 - d / (power * 2); DamageSrc s; s.knock = 0;
      damageEntity(lv, *p, std::round(k * 14), nullptr, "explosion", s);
      float l = d > 0 ? d : 1;
      knockback(*p, vec3((p->pos.x - c.x) / l * 10 * k, 6 * k, (p->pos.z - c.z) / l * 10 * k));
    }
  }
  std::vector<Creature*> hit;
  for (auto& e : lv.entities()) if (auto* m = dynamic_cast<Creature*>(e.get())) if (m->alive() && !m->removed && m->realm == realm && m->id != o.source) hit.push_back(m);
  for (Creature* m : hit) {
    float d = glm::distance(m->pos, c);
    if (d < power * 2) { DamageSrc s; s.type = "explosion"; m->hurt(lv, std::round((1 - d / (power * 2)) * 14), s); }
  }
}

} // namespace dq
