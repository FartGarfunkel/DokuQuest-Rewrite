// render/head_layer.cpp — placed mob heads: the mob_head block is an empty 8x8x8 px box (you bump into it and pick it);
// this layer draws the creature's own head on it (creatureHeadParts), turned one of 16 ways. The block entity is
// {type:'head', mob, rot, item}.
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/creature_models.h"
#include "assets/voxel.h"
#include "core/registry.h"

namespace dq {

namespace {

class HeadLayer : public RenderLayer {
public:
  const char* name() const override { return "mob-heads"; }
  int order() const override { return 121; }
  void update(GameSession& s, double dt) override {
    if (!s.player) return;
    World& w = s.world();
    static BlockId head = 0;
    if (!head) head = reg().blockId("mob_head");
    if (!head) return;
    if (scanT -= dt, scanT <= 0) {   // the heads near the camera, a few times a second
      scanT = 0.5;
      found.clear();
      forBlockEntities(w, s.frame, 40.f, [&](const Chunk&, json& e, int x, int y, int z) {
        if (e.value("type", "") != "head" || !e.contains("mob") || !e["mob"].is_string()) return;
        if (w.getBlock(x, y, z) != head) return;
        found.push_back({ivec3(x, y, z), e["mob"].get<std::string>(), e.value("rot", 0)});
      });
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (found.empty()) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    std::vector<std::pair<int, mat4>> parts;
    for (auto& h : found) {
      const CreatureModelGL* CM = nullptr;
      if (!creatureHeadParts(h.mob, CM, parts)) continue;
      vec3 rel = f.cam.rel(glm::dvec3(h.at.x + 0.5, h.at.y, h.at.z + 0.5));
      if (!f.cam.frustum.sphere(rel + vec3(0, 0.25f, 0), 0.5f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      u8 l = w.getLight(h.at.x, h.at.y, h.at.z);
      VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      mat4 m = glm::rotate(glm::translate(mat4(1), rel), h.rot * (TAU / 16), vec3(0, 1, 0));
      m = glm::translate(glm::scale(m, vec3(0.5f)), vec3(-0.5f, 0, -0.5f));   // the unit-cube head, half a block across
      for (auto& [i, pm] : parts) VR.draw(CM->parts[i]->gl, m * pm * CM->parts[i]->local, L);
    }
    if (begun) VR.end();
  }

private:
  struct Head { ivec3 at; std::string mob; int rot; };
  double scanT = 0;
  std::vector<Head> found;
};

} // namespace

void registerHeadLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<HeadLayer>(); });
}

} // namespace dq
