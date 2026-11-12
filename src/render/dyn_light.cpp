// render/dyn_light.cpp — dynamic point lights (port of render/gl.js dynLight + game/game.js renderViews "carried
// lights"): torches and lanterns in hands, on belts and backpacks, glowing things on the ground, lit TNT and
// fireballs light the world around them. Up to 8 per frame are gathered here (nearest and brightest within
// ~48 blocks) into FrameCtx::dyn; FrameCtx::apply (world_renderer.cpp) passes them as uniforms and
// common.glsl's dynLightRgb() adds them in their own colour, falling off one level per block and lighting only
// the faces turned toward them. A held torch stays the warm colour of a placed torch.
#include "render/env_layers.h"
#include "client/session.h"
#include "client/app.h"
#include "sim/player.h"
#include "sim/inventory.h"
#include "core/registry.h"
#include <algorithm>
#include <unordered_map>

namespace dq {

vec3 lightColourOfBlock(BlockId id);   // render/light_colour_layer.cpp — the colour a placed light of this block gives

namespace {
constexpr float MAX_DIST = 48.f;
const vec3 kWarm(1.f, 0.85f, 0.62f);

// the colour a carried or dropped item shines, the same one it would have if it were placed
vec3 colourOfId(const std::string& id) {
  if (id.empty()) return kWarm;
  const ItemDef* d = reg().item(id);
  BlockId b = 0;
  if (d && !d->block.empty()) b = reg().blockId(d->block);
  if (!b) b = reg().blockId(id);
  return b ? lightColourOfBlock(b) : kWarm;
}

class DynLightLayer : public RenderLayer {
public:
  int order() const override { return 1000; }   // runs after every layer that may add lights (e.g. the test harness), since it gathers and clears f.dyn
  const char* name() const override { return "dynlight"; }
  std::unordered_map<std::string, int> cache;   // item id -> light level
  int levelOf(const std::string& id) {
    if (id.empty()) return 0;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    return cache[id] = itemLight(id);
  }
  int carried(const Player& p, bool& torchy, vec3& col) {
    int l = 0;
    col = kWarm;
    auto take = [&](const std::vector<ItemStack>& v, size_t i) {
      if (i >= v.size() || v[i].empty()) return;
      int k = levelOf(v[i].id);
      if (v[i].id == "torch") torchy = true;
      if (k > l) { l = k; col = colourOfId(v[i].id); }
    };
    take(p.inv, (size_t)p.selected); take(p.offhand, 0); take(p.hook, 0); take(p.back, 0);
    return l;
  }

  void update(GameSession& s, double) override {
    FrameCtx& f = s.frame;
    f.dyn.clear(); f.dynRealm = s.player ? s.player->realm : 0;
    if (!s.player) return;
    struct Cand { glm::dvec3 pos; float level, score; vec3 color; };
    std::vector<Cand> c;
    glm::dvec3 cp = f.cam.pos;
    auto add = [&](glm::dvec3 pos, float level, vec3 color) {
      if (level <= 0.5f) return;
      float d = (float)glm::length(pos - cp);
      if (d > MAX_DIST || (level - d < 0.5f && d > 6.f)) return;   // too far to reach the camera's surroundings
      c.push_back({pos, level, level - d * 0.5f, color});
    };
    float t = (float)s.app.time;
    for (auto& e : s.level->entities()) {
      if (!e || e->removed || e->realm != f.dynRealm) continue;
      if (e->type == "player") {
        auto* p = dynamic_cast<Player*>(e.get());
        if (!p || p->dead) continue;
        bool torchy = false; vec3 col = kWarm;
        int lv = carried(*p, torchy, col);
        if (!lv) continue;
        float idx = p == s.player ? 0.f : (float)(e->id % 7) * 1.7f;
        // torches flicker, lanterns hold steady
        float flick = torchy && lv <= 14 ? std::sin((t + idx) * 9.1f) * 0.25f + std::sin((t + idx) * 23.7f) * 0.15f : 0.f;
        add(glm::dvec3(e->pos) + glm::dvec3(0, 1.25, 0), lv + flick, col);
      } else if (e->type == "item") {
        const json& sj = e->data.contains("stack") ? e->data["stack"] : e->data.contains("item") ? e->data["item"] : json();
        if (!sj.is_object()) continue;
        std::string id = sj.value("id", std::string());
        int lv = levelOf(id);
        if (lv > 0) add(glm::dvec3(e->pos) + glm::dvec3(0, 0.4, 0), (float)lv - 1, colourOfId(id));
      } else if (e->type == "tnt") add(glm::dvec3(e->pos) + glm::dvec3(0, 0.5, 0), 8.f, kWarm);
      else if (e->type == "projectile" && e->data.value("kind", std::string()) == "fireball") add(glm::dvec3(e->pos), 10.f, colourOfId("lava"));
      else if (e->data.contains("hat") && e->data["hat"] == "jack_o_lantern" && e->hp > 0) add(glm::dvec3(e->pos) + glm::dvec3(0, e->h - 0.3, 0), 9.f, colourOfId("jack_o_lantern"));   // Halloween: a jack-o'-lantern head
      else if (e->type == "pumpkin_creature" && e->hp > 0) add(glm::dvec3(e->pos) + glm::dvec3(0, e->h - 0.55, 0), 7.f, colourOfId("jack_o_lantern"));   // the pumpkin creature's carved face (lit whether it hides as a block or stands up)
    }
    std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    for (size_t i = 0; i < c.size() && i < 8; i++) f.dyn.push_back({c[i].pos, c[i].level, c[i].color});
  }
};
} // namespace

void registerDynLights() {
  registerRenderLayer([](GameSession&) { return std::make_shared<DynLightLayer>(); });
}

} // namespace dq
