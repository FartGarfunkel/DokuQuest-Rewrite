// render/statue_layer.cpp — statues (BTA's statue, our own make): a stone figure of a creature standing on its plinth,
// carved from a spawn egg used on the plinth (sim/build.cpp). Drawn from the creature's own model, posed still and
// tinted the colour of stone. Creatures drawn from a player-style skin show as their rig where they have one.
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "render/creature_draw.h"
#include "render/creature_models.h"
#include "client/session.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

class StatueLayer : public RenderLayer {
public:
  const char* name() const override { return "statues"; }
  int order() const override { return 113; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    CreatureDraw& D = creatureDraw();
    bool begun = false;
    forBlockEntities(w, f, 64.f, [&](const Chunk&, json& e, int x, int y, int z) {
      if (e.value("type", "") != "statue") return;
      const std::string mob = e.value("mob", "");
      if (mob.empty() || !reg().block(w.getBlock(x, y, z)).has("statue")) return;
      vec3 rel = f.cam.rel(glm::dvec3(x + 0.5, y + 1, z + 0.5));
      if (!f.cam.frustum.sphere(rel + vec3(0, 1, 0), 2.f)) return;
      auto& c = dummies[mob];
      if (!c) c = std::make_unique<Creature>(mob);
      Creature& M = *c;
      const RigModel* RM = rigModel(M.modelKey());
      const CreatureModelGL* CM = creatureModel(M.modelKey());
      if (!RM || !CM) return;
      if (!begun) { if (!D.begin(f)) return; begun = true; }
      M.pos = vec3(x + 0.5f, y + 1, z + 0.5f); M.yaw = e.value("yaw", 0.f); M.age = 0; M.walk = 0; M.walkAmt = 0; M.headPitch = 0; M.headYawRel = 0;
      rigPose(M, *RM, creatureBase(M, true));
      const u8 l = w.getLight(x, y + 1, z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      const vec4 stone(0.66f, 0.65f, 0.62f, 0.6f);   // stone, with enough of the creature's own shading left to know it by
      const mat4 T0 = glm::translate(mat4(1), rel);
      for (size_t i = 0; i < RM->parts.size(); i++) {
        const RigPart& part = RM->parts[i];
        const CreaturePartMesh& pm = *CM->parts[i];
        if (pm.hidden || !rigPartPosed(M, (int)i) || !rigPartShown(M, part) || part.alpha < 1) continue;
        D.draw(pm.gl, T0 * M.mats[i] * pm.local, L, stone);
      }
    });
    if (begun) D.end();
  }
  std::unordered_map<std::string, std::unique_ptr<Creature>> dummies;
};

void registerStatueLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<StatueLayer>(); }); }

} // namespace dq
