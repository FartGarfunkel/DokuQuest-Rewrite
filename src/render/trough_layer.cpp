// render/trough_layer.cpp — the food in a feeding trough (sim/trough.h): its own 3D item models lying in the trough, three
// more for each level it's filled to (a 16 of them a level, as the mod shows it), heaped a little higher as it fills.
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/item_layers.h"
#include "assets/item_models.h"
#include "assets/voxel.h"
#include "sim/trough.h"
#include "core/registry.h"

namespace dq {
namespace {

class TroughLayer : public RenderLayer {
public:
  const char* name() const override { return "troughs"; }
  int order() const override { return 122; }
  void update(GameSession& s, double dt) override {
    if (!s.player) { found.clear(); return; }
    if ((scanT -= dt) > 0) return;
    scanT = 0.25;
    found.clear();
    World& w = s.world();
    static const BlockId trough = reg().blockId("feeding_trough");
    if (!trough) return;
    forBlockEntities(w, s.frame, 32.f, [&](const Chunk&, json& e, int x, int y, int z) {
      if (e.value("type", "") != "trough" || w.getBlock(x, y, z) != trough) return;
      const ItemStack st = troughStack(w, x, y, z);
      if (!st.empty()) found.push_back({ivec3(x, y, z), st.id, troughLevel(st.n)});
    });
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (found.empty()) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    for (auto& t : found) {
      const VoxelModel& m = itemModel(t.food);
      if (!m.ready()) continue;
      const vec3 rel = f.cam.rel(glm::dvec3(t.at.x, t.at.y, t.at.z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      const u8 l = w.getLight(t.at.x, t.at.y + 1, t.at.z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      const int n = t.level * 3;
      u32 hsh = (u32)(t.at.x * 73856093) ^ (u32)(t.at.y * 19349663) ^ (u32)(t.at.z * 83492791);
      for (int i = 0; i < n; i++) {
        hsh = hsh * 1664525u + 1013904223u; const float a = (hsh >> 8 & 1023) / 1023.f;
        hsh = hsh * 1664525u + 1013904223u; const float b = (hsh >> 8 & 1023) / 1023.f;
        hsh = hsh * 1664525u + 1013904223u; const float c = (hsh >> 8 & 1023) / 1023.f;
        // inside the walls (x 1.5..14.5, z 2.5..13.5 px), on the floor (4 px up), the later ones on top of the first
        const vec3 at(0.2f + a * 0.6f, 4.6f / 16 + (i / 4) * 0.035f + t.level * 0.02f, 0.25f + b * 0.5f);
        mat4 M = glm::translate(mat4(1), rel + at);
        M = glm::rotate(M, c * TAU, vec3(0, 1, 0));
        M = glm::rotate(M, -PI / 2, vec3(1, 0, 0));   // laid flat
        M = glm::scale(M, vec3(m.kind == VoxelModel::Block ? 0.22f : 0.36f));
        VR.draw(m.gl, M * m.matrix(), L);
      }
    }
    if (begun) VR.end();
  }

private:
  struct Trough { ivec3 at; std::string food; int level = 0; };
  std::vector<Trough> found;
  double scanT = 0;
};

} // namespace

void registerTroughLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<TroughLayer>(); });
}

} // namespace dq
