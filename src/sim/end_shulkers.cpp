// sim/end_shulkers.cpp — shulkers keep the End cities: the first time someone comes within reach of a city, its
// shulkers are set in their places (the crown room, the hall roof, the ship's deck). Each city is done once
// (level data "shulkerCities").
#include "sim/creature.h"
#include "sim/player.h"
#include "gen/structures_api.h"
#include "gen/structures.h"

namespace dq {

void registerEndShulkers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> {
    struct S : LevelSystem {
      double t = 0;
      const char* name() const override { return "end-shulkers"; }
      void tick(Level& lv, double dt) override {
        if (lv.isRemote() || !lv.gen || (t += dt) < 3) return;
        t = 0;
        if (!lv.info.data.is_object()) lv.info.data = json::object();
        json& doneJ = lv.info.data["shulkerCities"];
        if (!doneJ.is_object()) doneJ = json::object();
        for (auto& e : lv.entities()) {
          auto* p = dynamic_cast<Player*>(e.get());
          if (!p || p->removed || p->dead || p->realm != REALM_MAIN || dimOfY(ifloor(p->pos.y)) != DIM_END) continue;
          const StructurePlan* c = locateStructure(*lv.gen, DIM_END, "end_city", ifloor(p->pos.x), ifloor(p->pos.z), 2);
          if (!c || doneJ.contains(c->id) || std::hypot(c->cx - p->pos.x, c->cz - p->pos.z) > 72) continue;
          World& w = lv.world(REALM_MAIN);
          const int base = dimBase(DIM_END), x = c->cx, z = c->cz, y = base + c->y;
          const int roof = base + c->data.value("roofY", c->y + 20), dir = c->data.value("dir", 0);
          std::vector<vec3> spots = {{x - 3.5f, (float)roof, z - 3.5f}, {x + 3.5f + 1, (float)roof, z + 3.5f + 1}, {x - 4.5f + 1, (float)(y + 6), z + 4.5f + 1}};
          if (c->data.value("ship", false)) {
            static const int D[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            const int ax = D[dir & 3][0], az = D[dir & 3][1], sx = -az, sz = ax, cx = x + ax * 20, cz = z + az * 20;
            spots.push_back(vec3(cx + ax * 3 + sx + 0.5f, (float)roof, cz + az * 3 + sz + 0.5f));
            spots.push_back(vec3(cx - ax * 2 - sx + 0.5f, (float)roof, cz - az * 2 - sz + 0.5f));
          }
          bool loaded = true;
          for (auto& s : spots) if (!w.isLoaded(ifloor(s.x), ifloor(s.y), ifloor(s.z))) loaded = false;
          if (!loaded) continue;   // try again when the city's all there
          for (auto& s : spots) if (Creature* m = spawnCreature(lv, REALM_MAIN, "shulker", s)) m->yaw = frandAngle();
          doneJ[c->id] = true;
        }
      }
      static float frandAngle() { return (float)(rand() % 628) / 100.f; }
    };
    return std::make_shared<S>();
  });
}

} // namespace dq
