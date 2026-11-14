// render/fallen_layer.cpp — the fallen adventurers (sim/fallen.h) as real skeletons: our skeleton mob's own model and
// variants (a ranger on the road, a mossy one in the jungle, a miner in the caves), posed where they fell: sitting slumped
// against nothing, lying on their back, or kneeling over a pick. The block itself (remains_*) keeps its shape for
// selecting and standing on, but the mesher leaves it undrawn (fallenDrawnAt); this layer draws the bones instead.
#include "client/session.h"
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

enum Pose { SIT, LIE, KNEEL, FACEDOWN, CURLED, FOLDED, SPRAWL, REACH, RIBCAGE, SCATTERED, POSES };
struct Kind { const char* model; Pose pose; const char* helmet; const char* held; };
const Kind* kindOf(const std::string& k) {
  static const std::map<std::string, Kind> K = {
    {"traveller", {"skeleton_ranger", SIT, "", ""}},
    {"desert", {"skeleton", LIE, "", ""}},
    {"snowy", {"skeleton", SIT, "", ""}},
    {"jungle", {"jungle_skeleton", LIE, "", ""}},
    {"swamp", {"skeleton_mossy", LIE, "", ""}},
    {"miner", {"skeleton_miner", KNEEL, "", "stone_pickaxe"}},
  };
  auto it = K.find(k);
  return it == K.end() ? nullptr : &it->second;
}

// each one lies its own way (from where it lies): one of the poses, its kind's own the likeliest, each limb a little off,
// and some with arrows still in them
struct Variant { int pose = 0, jitter = 0, arrows = 0; u32 seed = 0; };
Variant variantAt(ivec3 at, const Kind& K) {
  const u32 h = (u32)(posHashF(at.x, at.y, at.z, 0xFA11) * 4294967295.0);
  Variant v; v.seed = h;
  const int r = (int)(h % 100);
  v.pose = r < 22 ? K.pose : (r - 22) * (POSES - 1) / 78; if (v.pose >= K.pose && r >= 22) v.pose++;   // (the others evenly)
  v.pose = std::min(v.pose, POSES - 1);
  v.jitter = (int)((h >> 8) % 4);
  const int a = (int)((h >> 12) % 100);
  v.arrows = a < 55 ? 0 : a < 75 ? 1 : a < 88 ? 2 : a < 96 ? 3 : 4;
  return v;
}

// one posed skeleton per kind, realm, pose and twist (they lie still: posed once)
struct Shown { std::unique_ptr<Creature> c; const RigModel* RM = nullptr; const CreatureModelGL* CM = nullptr; const Kind* K = nullptr; std::vector<u8> hide; bool held = true; };
Shown* shownFor(const std::string& kind, int dim, int pose, int jitter) {
  static std::unordered_map<std::string, std::unique_ptr<Shown>> cache;
  const std::string ck = kind + "#" + std::to_string(dim) + "#" + std::to_string(pose) + "#" + std::to_string(jitter);
  auto it = cache.find(ck);
  if (it != cache.end()) return it->second.get();
  auto s = std::make_unique<Shown>();
  s->K = kindOf(kind);
  if (s->K && reg().mobs.contains("skeleton")) {
    s->c = std::make_unique<Creature>("skeleton");
    // the realm's own dead: scorched bones in the Nether
    const char* model = dim == DIM_NETHER ? "scorched_skeleton_chained" : s->K->model;
    if (!rigModel(model) || !creatureModel(model)) model = s->K->model;
    if (rigModel(model) && creatureModel(model)) s->c->variant = model;
    s->RM = rigModel(s->c->modelKey());
    s->CM = creatureModel(s->c->modelKey());
  }
  if (!s->RM || !s->CM) s->c.reset();
  else {
    // the pose: part turns (x forward/back, y, z sideways) on the biped's rest; +x swings a leg or arm forward (-z)
    const RigModel& M = *s->RM;
    std::vector<vec3> rot(M.parts.size(), vec3(0));
    s->hide.assign(M.parts.size(), 0);
    auto set = [&](const char* n, vec3 r) { if (int i = M.find(n); i >= 0) rot[i] = r; };
    auto hideP = [&](const char* n) { if (int i = M.find(n); i >= 0) s->hide[i] = 1; };
    mat4 root(1);
    int headLoose = -1; mat4 headAt(1);   // (the skull rolled off on its own: where it lies)
    auto onBack = [&](float dz) {          // lying on its back along +z (the feet that way)
      root = glm::translate(root, vec3(0, 0.07f, dz)); root = glm::rotate(root, PI / 2, vec3(1, 0, 0)); root = glm::translate(root, vec3(0, -0.75f, 0));
    };
    switch (pose) {
      case SIT:            // sat down for the last time: legs out in front, leaning back, head on its chest
        root = glm::translate(root, vec3(0, -0.69f, 0));
        set("legR", {1.45f, 0.12f, 0.1f}); set("legL", {1.3f, -0.15f, -0.08f}); set("shinL", {-0.35f, 0, 0});
        set("body", {0.32f, 0, 0.05f}); set("head", {-0.75f, 0.25f, 0.3f});
        set("armR", {0.25f, 0, 0.18f}); set("foreR", {-0.3f, 0, 0}); set("armL", {0.15f, 0, -0.12f}); set("foreL", {-0.2f, 0, 0});
        break;
      case LIE:            // on its back, arms flung out, the skull turned aside
        onBack(-0.35f);
        set("legR", {0, 0, 0.12f}); set("legL", {0.45f, 0, -0.1f}); set("shinL", {-0.9f, 0, 0});
        set("head", {0.1f, 0.7f, 0});
        set("armR", {0.2f, 0, 1.1f}); set("foreR", {-0.6f, 0, 0}); set("armL", {-0.1f, 0, -0.5f});
        break;
      case KNEEL:          // knelt over its pick, slumped forward
        root = glm::translate(root, vec3(0, -0.33f, 0.1f));
        set("legR", {0.08f, 0, 0.06f}); set("shinR", {-1.55f, 0, 0}); set("legL", {0.05f, 0, -0.06f}); set("shinL", {-1.5f, 0, 0});
        set("body", {-0.45f, 0, 0}); set("head", {-0.6f, 0.2f, 0.15f});
        set("armR", {0.9f, 0, 0.05f}); set("foreR", {0.25f, 0, 0}); set("armL", {0.6f, 0, -0.1f}); set("foreL", {0.3f, 0, 0});
        break;
      case FACEDOWN:       // fallen on its face crawling for the door: one arm reaching on ahead, a knee drawn up
        root = glm::translate(root, vec3(0, 0.1f, 0.35f)); root = glm::rotate(root, -PI / 2, vec3(1, 0, 0)); root = glm::translate(root, vec3(0, -0.75f, 0));
        set("armR", {-2.9f, 0, 0.1f}); set("foreR", {-0.2f, 0, 0}); set("armL", {0.2f, 0, -0.35f}); set("foreL", {-1.1f, 0, 0});
        set("legL", {0.9f, 0, -0.15f}); set("shinL", {-1.4f, 0, 0}); set("legR", {0, 0, 0.08f});
        set("head", {0.35f, -1.0f, 0});
        break;
      case CURLED:         // curled on its side, knees up to its chest
        root = glm::translate(root, vec3(-0.1f, 0.24f, 0)); root = glm::rotate(root, PI / 2, vec3(0, 0, 1)); root = glm::translate(root, vec3(0, -0.75f, 0));
        set("legR", {1.3f, 0, 0}); set("shinR", {-1.6f, 0, 0}); set("legL", {1.1f, 0, 0}); set("shinL", {-1.5f, 0, 0});
        set("body", {0.35f, 0, 0}); set("head", {0.5f, 0, 0.2f});
        set("armR", {1.2f, 0, 0.1f}); set("foreR", {1.1f, 0, 0}); set("armL", {0.9f, 0, -0.1f}); set("foreL", {1.3f, 0, 0});
        break;
      case FOLDED:         // sat and folded over its knees, the skull down between them
        root = glm::translate(root, vec3(0, -0.69f, 0.05f));
        set("legR", {1.5f, 0.05f, 0.08f}); set("legL", {1.5f, -0.05f, -0.08f});
        set("body", {-0.95f, 0, 0}); set("head", {-0.5f, 0, 0});
        set("armR", {1.4f, 0, 0.1f}); set("foreR", {0.2f, 0, 0}); set("armL", {1.5f, 0, -0.1f}); set("foreL", {0.1f, 0, 0});
        break;
      case SPRAWL:         // flat on its back, every limb thrown out
        onBack(-0.35f);
        set("legR", {0, 0, 0.55f}); set("legL", {0, 0, -0.5f});
        set("armR", {0, 0, 1.5f}); set("armL", {0, 0, -1.45f}); set("foreL", {-0.3f, 0, 0});
        set("head", {-0.2f, -0.5f, 0});
        break;
      case REACH:          // on its back, one hand still raised to the roof
        onBack(-0.35f);
        set("armR", {-1.55f, 0, 0.1f}); set("foreR", {-0.25f, 0, 0}); set("armL", {0.1f, 0, -0.3f});
        set("legL", {0.6f, 0, -0.05f}); set("shinL", {-1.2f, 0, 0});
        set("head", {0, 0.45f, 0});
        break;
      case RIBCAGE:        // all that's left: the ribcage on the floor and the skull rolled beside it
        onBack(-0.05f);
        for (const char* n : {"armR", "foreR", "armL", "foreL", "legR", "shinR", "legL", "shinL"}) hideP(n);
        headLoose = M.find("head");
        headAt = glm::rotate(glm::rotate(glm::translate(mat4(1), vec3(0.32f, 0, -0.42f)), 1.1f, vec3(0, 1, 0)), 0.35f, vec3(0, 0, 1));
        s->held = false;
        break;
      case SCATTERED:      // the ribcage, the skull a step off and one leg bone left
        onBack(-0.15f);
        for (const char* n : {"armR", "foreR", "armL", "foreL", "legL", "shinL"}) hideP(n);
        set("legR", {0.25f, 0, 0.7f}); set("shinR", {-0.6f, 0, 0});
        headLoose = M.find("head");
        headAt = glm::rotate(glm::rotate(glm::translate(mat4(1), vec3(-0.45f, 0, -0.55f)), -0.6f, vec3(0, 1, 0)), -0.25f, vec3(1, 0, 0));
        s->held = false;
        break;
      default: break;
    }
    // each one a little different: every turn off by up to a few degrees, the head more
    if (jitter) {
      Mulberry32 jr(hash32(pose, jitter, 0x7A11, 3));
      for (size_t i = 0; i < rot.size(); i++) {
        const float k = M.parts[i].name == "head" ? 0.35f : 0.14f;
        rot[i] += vec3(((float)jr() - 0.5f) * k, ((float)jr() - 0.5f) * k, ((float)jr() - 0.5f) * k);
      }
    }
    Creature& m = *s->c;
    m.mats.assign(M.parts.size(), mat4(0));
    for (size_t i = 0; i < M.parts.size(); i++) {   // (sim/creature_rig.cpp rigPose: parent, pivot, then y, x, z)
      const RigPart& p = M.parts[i];
      mat4 mm;
      if ((int)i == headLoose) {   // the loose skull: on the floor by itself, the same way up as on the neck, turned
        vec3 half = (p.max - p.min) * 0.5f;
        mm = glm::translate(headAt, vec3(0, half.y > 0 ? -p.min.y : 0, 0) - vec3(0, 0, 0));
        mm = glm::translate(mm, -vec3(0, 0, 0));
      } else {
        const mat4& parent = p.parent >= 0 ? m.mats[p.parent] : root;
        mm = glm::translate(parent, p.pivot);
      }
      vec3 r = (int)i == headLoose ? rot[i] * 0.3f : p.rest + rot[i];
      if (r.y) mm = glm::rotate(mm, r.y, vec3(0, 1, 0));
      if (r.x) mm = glm::rotate(mm, r.x, vec3(1, 0, 0));
      if (r.z) mm = glm::rotate(mm, r.z, vec3(0, 0, 1));
      m.mats[i] = mm;
    }
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
      const Kind* kd = kindOf(kind);
      if (!kd) continue;
      const Variant var = variantAt(at, *kd);
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
