// sim/slime_goo.cpp — see slime_goo.h. The splat itself is the particle layer's "slime" kind (a spray of goo and a
// flat splat that dries in 4-7 s); here it is also a puddle the simulation knows about, so walking through it
// drags at your feet like a lighter cobweb (Player::data["goo"], read by Player::baseMove) until it dries.
#include "sim/slime_goo.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/content.h"
#include "core/registry.h"
#include <algorithm>
#include <unordered_map>

namespace dq {

namespace {
struct Goo { int realm; vec3 p; float r, t; };

struct GooSystem : LevelSystem {
  std::vector<Goo> goo;
  std::unordered_map<u32, float> air;   // slime id -> time in the air
  const char* name() const override { return "slimeGoo"; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    for (auto& g : goo) g.t -= dt;
    goo.erase(std::remove_if(goo.begin(), goo.end(), [](const Goo& g) { return g.t <= 0; }), goo.end());
    if (lv.isRemote()) return;
    // a slime coming down after a hop splats where it lands
    for (auto& e : lv.entities()) {
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c->removed || (c->type != "slime" && c->type != "magma_slime")) continue;
      float& a = air[c->id];
      if (!c->onGround) { a += dt; continue; }
      if (a > 0.12f && c->alive()) slimeGooAt(lv, c->realm, c->pos, std::max(0.3f, c->w), c->type == "magma_slime");
      a = 0;
    }
    if (air.size() > 512) air.clear();
    if (goo.empty()) return;
    // whoever stands in one is slowed for a moment after (refreshed while they stay in it)
    for (auto& e : lv.entities()) {
      auto* p = dynamic_cast<Player*>(e.get());
      if (!p || p->removed || p->dead || p->flying || !p->onGround) continue;
      for (auto& g : goo) {
        if (g.realm != p->realm || std::fabs(p->pos.y - g.p.y) > 0.6f) continue;
        float dx = p->pos.x - g.p.x, dz = p->pos.z - g.p.z, r = g.r + 0.2f;
        if (dx * dx + dz * dz < r * r) { p->data["goo"] = 0.35f; break; }
      }
    }
  }
};
} // namespace

void slimeGooAt(Level& lv, int realm, vec3 pos, float size, bool magma) {
  lv.fx->particles(magma ? "magma_slime" : "slime", pos, 1, json{{"size", size}});
  World& w = lv.world(realm);
  if (!reg().tSolid[w.getBlock(ifloor(pos.x), ifloor(pos.y - 0.1f), ifloor(pos.z))]) return;
  if (auto* S = lv.system<GooSystem>()) {
    if (S->goo.size() > 160) S->goo.erase(S->goo.begin());
    S->goo.push_back({realm, vec3(pos.x, std::floor(pos.y + 0.01f), pos.z), size * 0.6f, 5 + frand() * 2});
  }
}

void registerSlimeGoo() {
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { (void)lv; return std::make_shared<GooSystem>(); });
}

} // namespace dq
