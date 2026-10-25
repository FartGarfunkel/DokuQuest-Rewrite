// render/spawner_layer.cpp — monster cages show a little spinning copy of their monster inside (game/batchb.js
// cageFind / blockEntityDraws: every monster_spawner within 24 blocks, its mob scaled to fit the cage, turning
// 2.2 rad/s), with a few flames licking up from the cage (dungeons.js burst 'flame'). The block entity is
// {type:'spawner', mob}.
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/creature_models.h"
#include "assets/voxel.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {
namespace {

// one creature per type, posed once at rest: its part matrices are the little copy in the cage
Creature* showCreature(const std::string& type) {
  static std::unordered_map<std::string, std::unique_ptr<Creature>> cache;
  auto it = cache.find(type);
  if (it != cache.end()) return it->second.get();
  std::unique_ptr<Creature> c;
  if (reg().mobs.contains(type)) {
    c = std::make_unique<Creature>(type);
    if (const RigModel* RM = rigModel(c->modelKey())) rigPose(*c, *RM, creatureBase(*c, false));
    else c.reset();
  }
  return (cache[type] = std::move(c)).get();
}

class SpawnerLayer : public RenderLayer {
public:
  const char* name() const override { return "spawner-show"; }
  int order() const override { return 120; }
  void update(GameSession& s, double dt) override {
    time += dt;
    if (!s.player) return;
    flameAcc += dt;
    cages.clear();
    World& w = s.world();
    static BlockId cage = 0;
    if (!cage) cage = reg().blockId("monster_spawner");
    if (!cage) return;
    // the cages near the camera, found a few times a second (the original looks them up once a second)
    if (scanT -= dt, scanT <= 0) {
      scanT = 0.5;
      found.clear();
      const FrameCtx& f = s.frame;
      forBlockEntities(w, f, 24.f, [&](const Chunk&, json& e, int x, int y, int z) {
        if (e.value("type", "") != "spawner" || !e.contains("mob") || !e["mob"].is_string()) return;
        if (w.getBlock(x, y, z) != cage) return;
        found.push_back({ivec3(x, y, z), e["mob"].get<std::string>()});
      });
    }
    Level& lv = *s.level;
    vec3 cam = vec3(s.frame.cam.pos);
    for (auto& [at, mob] : found) {
      vec3 c = vec3(at) + vec3(0.5f, 0.5f, 0.5f);
      if (glm::distance(c, cam) > 24.f) continue;
      cages.push_back({at, mob});
      if (flameAcc > 0.05 && frand() < 0.5f && glm::distance(c, cam) < 16.f) {
        float a = frand() * TAU;
        lv.fx->particles("flame", c + vec3(std::cos(a) * 0.32f, -0.3f + frand() * 0.2f, std::sin(a) * 0.32f), 1,
          {{"vel", {0, 0.6f + frand() * 0.5f, 0}}, {"life", 0.35f + frand() * 0.25f}, {"size", 0.06}, {"shrink", true}, {"glow", true}, {"grav", 0}});
      }
    }
    if (flameAcc > 0.05) flameAcc = 0;
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (cages.empty()) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    for (auto& [at, mob] : cages) {
      Creature* cr = showCreature(mob);
      if (!cr || cr->mats.empty()) continue;
      const RigModel* RM = rigModel(cr->modelKey());
      const CreatureModelGL* CM = creatureModel(cr->modelKey());
      const CreatureType* T = creatureType(mob);
      if (!RM || !CM || !T) continue;
      // scaled to fit the cage; centred in the block
      float h = std::max(T->h, 0.1f), sc = std::min(0.45f, 0.8f / std::max({h, T->w * 2.4f, 0.6f}));
      vec3 rel = f.cam.rel(glm::dvec3(at.x + 0.5, at.y + 0.5 - h * sc * 0.5, at.z + 0.5));
      if (!f.cam.frustum.sphere(rel + vec3(0, h * sc * 0.5f, 0), 0.8f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      u8 l = w.getLight(at.x, at.y, at.z);
      VoxelLight L{(l >> 4) / 15.f, std::max<int>(l & 15, 6) / 15.f};   // the cage glows a little
      mat4 pm = glm::translate(mat4(1), rel);
      pm = glm::rotate(pm, (float)time * 2.2f, vec3(0, 1, 0));
      pm = glm::scale(pm, vec3(sc));
      for (size_t i = 0; i < RM->parts.size() && i < CM->parts.size(); i++) {
        const CreaturePartMesh& part = *CM->parts[i];
        if (part.hidden || !rigPartPosed(*cr, (int)i) || !rigPartShown(*cr, RM->parts[i]) || RM->parts[i].alpha < 1) continue;
        VR.draw(part.gl, pm * cr->mats[i] * part.local, RM->parts[i].glow ? VoxelLight{1, 1} : L);
      }
    }
    if (begun) VR.end();
  }

private:
  double time = 0, scanT = 0, flameAcc = 0;
  std::vector<std::pair<ivec3, std::string>> found, cages;
};

} // namespace

void registerSpawnerLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<SpawnerLayer>(); });
}

} // namespace dq
