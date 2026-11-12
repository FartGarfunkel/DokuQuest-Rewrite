// render/place_pop.cpp — see place_pop.h
#include "render/place_pop.h"
#include "client/session.h"
#include "client/app.h"
#include "assets/item_models.h"
#include "core/registry.h"
#include "world/world.h"

namespace dq {

float placePopScale(float t) {
  if (t <= 0 || t >= PLACE_POP_TIME) return 1;
  const float u = t / PLACE_POP_TIME;
  return 1 + 0.075f * std::sin(u * PI) * std::sqrt(1 - u);   // (A Good Place's scale_curve -0.7: quick out, eased back)
}

namespace {
struct Pop { ivec3 at; double t0; };
std::vector<Pop> g_pops;
double g_clock = 0;

class PlacePopLayer : public RenderLayer {
public:
  const char* name() const override { return "place-pop"; }
  int order() const override { return 120; }
  void update(GameSession&, double dt) override {
    g_clock += dt;
    g_pops.erase(std::remove_if(g_pops.begin(), g_pops.end(), [](const Pop& p) { return g_clock - p.t0 > PLACE_POP_TIME; }), g_pops.end());
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (g_pops.empty() || !s.player) return;
    World& w = s.world();
    const Registry& R = reg();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    for (const Pop& p : g_pops) {
      const float sc = placePopScale((float)(g_clock - p.t0));
      if (sc <= 1.002f) continue;   // (at its own size it would only fight the real block)
      const BlockId b = w.getBlock(p.at.x, p.at.y, p.at.z);
      if (b == AIR || !R.tOpaque[b]) continue;   // whole cubes only: the item model is the block's shape
      const std::string& key = R.block(b).key;
      if (!R.item(key)) continue;
      const VoxelModel& im = itemModel(key);
      if (!im.ready() || im.kind != VoxelModel::Block) continue;
      const vec3 rel = f.cam.rel(glm::dvec3(p.at));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      const u8 l = w.getLight(p.at.x, p.at.y + 1, p.at.z);
      mat4 m = glm::translate(mat4(1), rel + vec3(0.5f, 0, 0.5f));   // grown about the middle of its bottom
      m = glm::scale(m, vec3(sc));
      m = glm::translate(m, vec3(0, 0.5f, 0));
      VR.draw(im.gl, m * im.matrix(), VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f});
    }
    if (begun) VR.end();
  }
};
}

void notePlacement(vec3 pos) {
  if (app().settings.particles <= 0) return;
  if (g_pops.size() > 64) g_pops.erase(g_pops.begin());
  g_pops.push_back({ivec3(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z)), g_clock});
}

void registerPlacePopLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<PlacePopLayer>(); });
}

} // namespace dq
