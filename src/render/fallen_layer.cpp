// render/fallen_layer.cpp — the fallen adventurers (sim/fallen.h) as real skeletons: our skeleton mob's own model and
// variants (a ranger on the road, a mossy one in the jungle, a miner in the caves), posed where they fell: sitting slumped
// against nothing, lying on their back, or kneeling over a pick. The block itself (remains_*) keeps its shape for
// selecting and standing on, but the mesher leaves it undrawn (fallenDrawnAt); this layer draws the bones instead.
#include "client/session.h"
#include "sim/fallen.h"
#include "client/app.h"
#include "render/item_layers.h"
#include "render/creature_models.h"
#include "assets/voxel.h"
#include "assets/char_model.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include "assets/item_models.h"
#include <map>
#include <unordered_map>

namespace dq {

// any thread (the mesher): is this one of the remains blocks this layer draws?
bool fallenDrawnAt(BlockId id) {
  static const std::vector<u8> is = [] {
    std::vector<u8> v(reg().blocks.size() + 1, 0);
    for (size_t i = 0; i < reg().blocks.size(); i++) if (reg().blocks[i].has("remains")) v[i] = 1;
    return v;
  }();
  return id < is.size() && is[id];
}

namespace {

// the posed skeletons are sim/fallen.cpp's (its hitboxes are the same bones): here only their meshes are looked up
struct Shown { const Creature* c = nullptr; const RigModel* RM = nullptr; const CreatureModelGL* CM = nullptr; const FallenKind* K = nullptr; std::vector<u8> hide; bool held = true; };
Shown* shownFor(const std::string& kind, int dim, int pose, int jitter) {
  static std::unordered_map<std::string, std::unique_ptr<Shown>> cache;
  const std::string ck = kind + "#" + std::to_string(dim) + "#" + std::to_string(pose) + "#" + std::to_string(jitter);
  auto it = cache.find(ck);
  if (it != cache.end()) return it->second.get();
  auto s = std::make_unique<Shown>();
  if (const FallenPose* P = fallenPose(kind, dim, pose, jitter); P && P->c) {
    s->c = P->c.get(); s->RM = P->RM; s->K = P->K; s->hide = P->hide; s->held = P->held;
    s->CM = creatureModel(P->c->modelKey());
    if (!s->CM) s->c = nullptr;
  }
  return (cache[ck] = std::move(s)).get();
}

class FallenLayer : public RenderLayer {
public:
  const char* name() const override { return "fallen"; }
  int order() const override { return 121; }
  void update(GameSession& s, double) override {
    if (!s.player) return;
    // the chunks near the camera, one a frame: where the remains lie in each (block reads only)
    World& w = s.world();
    const FrameCtx& f = s.frame;
    const int pcx = ifloor(f.cam.pos.x) >> 4, pcz = ifloor(f.cam.pos.z) >> 4;
    if (pcx != lastCx || pcz != lastCz || f.dim != lastDim) {   // forget the far ones
      lastCx = pcx; lastCz = pcz; lastDim = f.dim;
      for (auto it = found.begin(); it != found.end();) {
        if (std::abs(it->first.first - pcx) > R || std::abs(it->first.second - pcz) > R || dimOf != f.dim) it = found.erase(it); else ++it;
      }
      dimOf = f.dim;
    }
    const int side = 2 * R + 1;
    scanI = (scanI + 1) % (side * side);
    const int cx = pcx - R + scanI % side, cz = pcz - R + scanI / side;
    Chunk* c = w.chunk(f.dim, cx, cz);
    auto& list = found[{cx, cz}];
    list.clear();
    if (!c) return;
    for (int sy = 0; sy < (int)c->secs.size(); sy++) {
      const Section* sec = c->section(sy);
      if (!sec) continue;
      for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        const BlockId id = c->get(x, sy * 16 + y, z);
        if (!fallenDrawnAt(id)) continue;
        list.push_back({ivec3(cx * 16 + x, c->baseY() + sy * 16 + y, cz * 16 + z), id});
      }
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    for (auto& [cc, list] : found) for (auto& [at, id] : list) {
      if (w.getBlock(at.x, at.y, at.z) != id) continue;   // (taken since the scan)
      const std::string kind = reg().block(id).str("remains");
      const FallenKind* kd = fallenKind(kind);
      if (!kd) continue;
      const FallenVariant var = fallenVariant(at, *kd);
      Shown* sh = shownFor(kind, dimOfY(at.y), var.pose, var.jitter);
      if (!sh || !sh->c) continue;
      vec3 rel = f.cam.rel(glm::dvec3(at.x + 0.5, at.y, at.z + 0.5));
      if (glm::dot(rel, rel) > 64.f * 64.f || !f.cam.frustum.sphere(rel + vec3(0, 0.5f, 0), 1.6f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      u8 l = w.getLight(at.x, at.y, at.z);
      VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      // facing as the block's model does (world/shapes.cpp facingModel: meta & 3 quarter turns)
      const int rf = w.getMeta(at.x, at.y, at.z) & 3;
      const mat4 T0 = glm::rotate(glm::translate(mat4(1), rel), -rf * PI / 2, vec3(0, 1, 0));
      const Creature& m = *sh->c;
      const RigModel& RM = *sh->RM;
      for (size_t i = 0; i < RM.parts.size() && i < sh->CM->parts.size(); i++) {
        const CreaturePartMesh& pm = *sh->CM->parts[i];
        const RigPart& p = RM.parts[i];
        if (pm.hidden || p.alpha < 1 || p.glow || !rigPartShown(m, p) || (i < sh->hide.size() && sh->hide[i])) continue;   // (no glowing eyes in the dead)
        VR.draw(pm.gl, T0 * m.mats[i] * pm.local, L);
      }
      // what they still wear and hold
      if (*sh->K->helmet && reg().item(sh->K->helmet)) {
        int hi = RM.find("head");
        const ArmorSet& A = armorMeshes(sh->K->helmet, "", 0, false, false, app().tiles);
        auto pit = A.byPart.find("head");
        if (hi >= 0 && pit != A.byPart.end()) {
          const RigPart& rp = RM.parts[hi];
          const vec3 lo(-4, 0, -4), hiB(4, 8, 4), k = (rp.max - rp.min) / (hiB - lo);
          mat4 pm = glm::translate(T0 * m.mats[hi], rp.min) * glm::scale(mat4(1), k) * glm::translate(mat4(1), -lo);
          for (const CharMesh* cm : pit->second) VR.draw(cm->gl, pm * cm->local(), L);
        }
      }
      // arrows still in it: into the ribs, the skull, a thigh, each at its own slant
      if (var.arrows) if (const VoxelModel& am = itemModel("arrow"); am.ready()) {
        Mulberry32 ar(var.seed ^ 0xA770u);
        static const char* where[] = {"body", "body", "body", "head", "legR", "legL", "armR"};
        for (int k = 0; k < var.arrows; k++) {
          const int pi = RM.find(where[(int)(ar() * 7) % 7]);
          if (pi < 0 || (pi < (int)sh->hide.size() && sh->hide[pi])) continue;
          const RigPart& rp = RM.parts[pi];
          const vec3 pt = rp.min + (rp.max - rp.min) * vec3(0.2f + 0.6f * (float)ar(), 0.15f + 0.7f * (float)ar(), (float)ar() < 0.5f ? 0.1f : 0.9f);
          const float yaw = (float)ar() * TAU, pitch = ((float)ar() - 0.5f) * 1.6f;
          mat4 am4 = glm::translate(T0 * m.mats[pi], pt);
          am4 = glm::rotate(am4, yaw, vec3(0, 1, 0)); am4 = glm::rotate(am4, pitch, vec3(1, 0, 0));
          am4 = glm::translate(am4, vec3(0, 0, 0.16f));   // (half its length in the bone)
          am4 = glm::rotate(am4, PI / 2, vec3(0, 1, 0)); am4 = glm::rotate(am4, -PI / 4, vec3(0, 0, 1)); am4 = glm::scale(am4, vec3(0.6f));
          VR.draw(am.gl, am4 * am.matrix(), L);
        }
      }
      if (*sh->K->held && sh->held) {
        int hi = RM.find("foreR");
        const VoxelModel& im = itemModel(sh->K->held);
        if (hi >= 0 && im.ready()) {
          const RigPart& hp = RM.parts[hi];
          vec3 hand = hp.hasHand ? hp.hand : vec3(0, -5, 0);
          mat4 hm = glm::translate(T0 * m.mats[hi], vec3(hand.x / 16, hand.y / 16, -0.5f / 16));
          hm = glm::rotate(hm, -PI / 2, vec3(1, 0, 0));
          hm = glm::rotate(hm, PI / 2, vec3(0, 1, 0));
          VR.draw(im.gl, hm * im.matrix(), L);
        }
      }
    }
    if (begun) VR.end();
  }

private:
  static constexpr int R = 3;   // chunks about the camera (48 blocks)
  std::map<std::pair<int, int>, std::vector<std::pair<ivec3, BlockId>>> found;
  int scanI = 0, lastCx = 1 << 30, lastCz = 0, lastDim = -1, dimOf = -1;
};

} // namespace

void registerFallenLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<FallenLayer>(); });
}

} // namespace dq
