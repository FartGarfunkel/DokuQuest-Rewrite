// render/dyn_light.cpp — dynamic point lights (port of render/gl.js dynLight + game/game.js renderViews "carried
// lights"): torches and lanterns in hands, on belts and backpacks, glowing things on the ground, lit TNT and
// fireballs light the world around them. Up to 8 per frame are gathered here (nearest and brightest within
// ~48 blocks) into FrameCtx::dyn; FrameCtx::apply (world_renderer.cpp) passes them as uniforms and
// common.glsl's dynLight() maxes them with the block light, falling off one level per block and lighting only
// the faces turned toward them.
#include "render/env_layers.h"
#include "client/session.h"
#include "client/app.h"
#include "sim/player.h"
#include "sim/inventory.h"
#include <algorithm>
#include <unordered_map>

namespace dq {

namespace {
constexpr float MAX_DIST = 48.f;

class DynLightLayer : public RenderLayer {
public:
  const char* name() const override { return "dynlight"; }
  std::unordered_map<std::string, int> cache;   // item id -> light level
  int levelOf(const std::string& id) {
    if (id.empty()) return 0;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    return cache[id] = itemLight(id);
  }
  int carried(const Player& p, bool& torchy) {
    int l = 0;
    auto take = [&](const std::vector<ItemStack>& v, size_t i) {
      if (i >= v.size() || v[i].empty()) return;
      int k = levelOf(v[i].id);
      if (v[i].id == "torch") torchy = true;
      l = std::max(l, k);
    };
    take(p.inv, (size_t)p.selected); take(p.offhand, 0); take(p.hook, 0); take(p.back, 0);
    return l;
  }

  void update(GameSession& s, double) override {
    FrameCtx& f = s.frame;
    f.dyn.clear(); f.dynRealm = s.player ? s.player->realm : 0;
    if (!s.player) return;
    struct Cand { glm::dvec3 pos; float level, score; };
    std::vector<Cand> c;
    glm::dvec3 cp = f.cam.pos;
    auto add = [&](glm::dvec3 pos, float level) {
      if (level <= 0.5f) return;
      float d = (float)glm::length(pos - cp);
      if (d > MAX_DIST || (level - d < 0.5f && d > 6.f)) return;   // too far to reach the camera's surroundings
      c.push_back({pos, level, level - d * 0.5f});
    };
    float t = (float)s.app.time;
    for (auto& e : s.level->entities()) {
      if (!e || e->removed || e->realm != f.dynRealm) continue;
      if (e->type == "player") {
        auto* p = dynamic_cast<Player*>(e.get());
        if (!p || p->dead) continue;
        bool torchy = false;
        int lv = carried(*p, torchy);
        if (!lv) continue;
        float idx = p == s.player ? 0.f : (float)(e->id % 7) * 1.7f;
        // torches flicker, lanterns hold steady
        float flick = torchy && lv <= 14 ? std::sin((t + idx) * 9.1f) * 0.25f + std::sin((t + idx) * 23.7f) * 0.15f : 0.f;
        add(glm::dvec3(e->pos) + glm::dvec3(0, 1.25, 0), lv + flick);
      } else if (e->type == "item") {
        const json& sj = e->data.contains("stack") ? e->data["stack"] : e->data.contains("item") ? e->data["item"] : json();
        if (!sj.is_object()) continue;
        int lv = levelOf(sj.value("id", std::string()));
        if (lv > 0) add(glm::dvec3(e->pos) + glm::dvec3(0, 0.4, 0), (float)lv - 1);
      } else if (e->type == "tnt") add(glm::dvec3(e->pos) + glm::dvec3(0, 0.5, 0), 8.f);
      else if (e->type == "projectile" && e->data.value("kind", std::string()) == "fireball") add(glm::dvec3(e->pos), 10.f);
    }
    std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    for (size_t i = 0; i < c.size() && i < 8; i++) f.dyn.push_back({c[i].pos, c[i].level});
  }
};
} // namespace

void registerDynLights() {
  registerRenderLayer([](GameSession&) { return std::make_shared<DynLightLayer>(); });
}

} // namespace dq
