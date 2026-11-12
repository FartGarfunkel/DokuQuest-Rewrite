// sim/spawners.cpp — monster cages (game/dungeons.js cage tick, nether2.js fortress cages): a monster_spawner block
// whose block entity is {type:'spawner', mob} lets out its mob every 5–10 s while a player is within 14 blocks, up to
// four hostiles around it, on solid ground with headroom inside a 7×7 patch. Placed by gen/dungeons_classic and the
// structures (SP::spawner). Breaking the cage stops it.
#include "sim/creature.h"
#include "sim/player.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

namespace {
class SpawnerSystem : public LevelSystem {
public:
  const char* name() const override { return "spawners"; }
  void tick(Level& lv, double dt) override {
    if (lv.isRemote()) return;
    acc += dt;
    if (acc < 1.0) return;
    acc = 0;
    if (!cage) cage = reg().blockId("monster_spawner");
    if (!cage) return;
    std::vector<Player*> ps;
    for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && !p->dead && p->mode != "spectator") ps.push_back(p);
    std::unordered_map<u64, bool> seen;
    for (Player* p : ps) {
      World& w = lv.world(p->realm);
      ivec3 c = ivec3(glm::floor(p->pos));
      for (int y = c.y - 6; y <= c.y + 6; y++) for (int z = c.z - 14; z <= c.z + 14; z++) for (int x = c.x - 14; x <= c.x + 14; x++) {
        if (y < 1 || y >= WORLD_H - 2 || w.getBlock(x, y, z) != cage) continue;
        u64 k = ((u64)(u32)x << 40) ^ ((u64)(u32)y << 20) ^ (u64)(u32)z ^ ((u64)p->realm << 62);
        if (seen.count(k)) continue;
        seen[k] = true;
        if (glm::distance(p->pos, vec3(x + 0.5f, (float)y, z + 0.5f)) >= 14) continue;
        float& cd = cooldown[k];
        if ((cd -= 1) > 0) continue;
        cd = 5 + (float)(rand() % 6);
        const json* be = w.blockEntity(x, y, z);
        std::string mob = be && be->is_object() ? be->value("mob", std::string()) : std::string();
        if (mob.empty() || !isCreatureType(mob)) continue;
        int n = 0;
        for (auto& e : lv.entities()) if (auto* m = dynamic_cast<Creature*>(e.get()))
          if (!m->removed && m->alive() && m->realm == p->realm && m->T && m->T->hostile && std::hypot(m->pos.x - x, m->pos.z - z) < 9 && std::fabs(m->pos.y - y) < 5) n++;
        if (n >= 4) continue;
        for (int t = 0; t < 6; t++) {
          int sx = x + rand() % 7 - 3, sz = z + rand() % 7 - 3;
          if (!reg().tSolid[w.getBlock(sx, y - 1, sz)] || w.getBlock(sx, y, sz) || w.getBlock(sx, y + 1, sz)) continue;
          if (Creature* m = spawnCreature(lv, p->realm, mob, vec3(sx + 0.5f, (float)y, sz + 0.5f))) {
            m->huntT = 15; m->data["fromCage"] = true;
            lv.fx->particles("smoke", vec3(sx + 0.5f, y + 0.5f, sz + 0.5f), 12);
            lv.fx->particles("flame", vec3(x + 0.5f, y + 0.5f, z + 0.5f), 6);
          }
          break;
        }
      }
    }
    if (cooldown.size() > 4096) cooldown.clear();
  }
private:
  double acc = 0;
  BlockId cage = 0;
  std::unordered_map<u64, float> cooldown;
};
} // namespace

void registerSpawners() {
  registerLevelSystem([](Level&) { return std::make_shared<SpawnerSystem>(); });
}

} // namespace dq
