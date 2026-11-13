// render/furnace_layer.cpp — inside a furnace, smoker or blast furnace (their openings hollowed: assets/pack_models.h
// buildFurnaceModels): the fuel in its fuel slot lies on the floor of the opening (a few of the stack, side by side), and
// while it burns (the lit block) it glows, embers lie there if the slot is empty, and flames lick up out of it.
#include "client/session.h"
#include "render/be_scan.h"
#include "render/particles.h"
#include "assets/item_models.h"
#include "assets/voxel.h"
#include "assets/pack_models.h"
#include "core/registry.h"
#include "core/itemstack.h"
#include "world/world.h"

namespace dq {
namespace {
float rnd() { return (float)rand() / RAND_MAX; }

class FurnaceLayer : public RenderLayer {
public:
  const char* name() const override { return "furnace-fire"; }
  struct Spot { vec3 at; vec3 right, in; float width; std::string fuel; int n; bool burning; VoxelLight light; };
  std::vector<Spot> spots;
  // the floor of the opening in the world: model (x right-to-left as the front is seen, y up, z into the block from its
  // front) turned to the furnace's facing as the mesher turns it
  static vec3 turn(vec3 p, int q) { for (int i = 0; i < q; i++) { const float dx = p.x - 8, dz = p.z - 8; p.x = 8 - dz; p.z = 8 + dx; } return p; }
  void collect(GameSession& s, const FrameCtx& f) {
    spots.clear();
    World& w = s.world();
    forBlockEntities(w, f, 24.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const BlockId id = w.getBlock(x, y, z);
      if (!id) return;
      const BlockDef& b = reg().block(id);
      if (!b.has("furnace")) return;
      const FurnaceModel* FM = furnaceModelFor(b.faces[4]);
      if (!FM) return;
      static const int Q[4] = {2, 1, 0, 3}, DX[4] = {0, 1, 0, -1}, DZ[4] = {1, 0, -1, 0};
      const int fd = w.getMeta(x, y, z) & 3, q = Q[fd];
      Spot sp;
      const float mx = (FM->floorX0 + FM->floorX1) * 0.5f, dz = FURNACE_DEPTH * 0.55f;
      sp.at = vec3(x, y, z) + turn(vec3(mx, FM->floorY, dz), q) / 16.f;
      sp.right = (turn(vec3(mx - 1, FM->floorY, dz), q) - turn(vec3(mx, FM->floorY, dz), q));   // (model -x: to the picture's right)
      sp.in = (turn(vec3(mx, FM->floorY, dz + 1), q) - turn(vec3(mx, FM->floorY, dz), q));
      sp.width = std::max(0.08f, (FM->floorX1 - FM->floorX0) / 16.f - 0.08f);
      const json& it = e.contains("items") && e["items"].is_array() && e["items"].size() > 1 ? e["items"][1] : json();
      const ItemStack fuel = ItemStack::fromJson(it);
      sp.fuel = fuel.empty() ? "" : fuel.id; sp.n = fuel.n;
      sp.burning = b.light > 0;
      if (sp.fuel.empty() && !sp.burning) return;
      const u8 l = w.getLight(x + DX[fd], y, z + DZ[fd]);
      sp.light = sp.burning ? VoxelLight{1, 1} : VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f};
      spots.push_back(sp);
    });
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    collect(s, f);
    if (spots.empty()) return;
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    for (const Spot& sp : spots) {
      // the fuel (or, burning with none left in the slot, its embers: charcoal glowing)
      const std::string key = !sp.fuel.empty() ? sp.fuel : std::string("charcoal");
      const VoxelModel& im = itemModel(key);
      if (!im.ready()) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      const int pieces = sp.fuel.empty() ? 3 : std::clamp(sp.n, 1, 3);
      const vec3 R = glm::normalize(sp.right), I = glm::normalize(sp.in), U(0, 1, 0);
      for (int k = 0; k < pieces; k++) {
        const float off = pieces == 1 ? 0.f : ((float)k / (pieces - 1) - 0.5f) * sp.width * 0.6f;
        const vec3 p = sp.at + R * off + U * (0.012f + k * 0.004f);
        mat4 m(1); m[0] = vec4(R, 0); m[1] = vec4(-I, 0); m[2] = vec4(U, 0); m[3] = vec4(f.cam.rel(glm::dvec3(p)), 1);   // (lying flat, its face up)
        m = glm::rotate(m, (k - 1) * 0.5f, vec3(0, 0, 1));
        const float sc = std::min(0.32f, sp.width * 0.55f);
        m = glm::scale(m, vec3(sc));
        const vec4 tint = sp.burning ? vec4(1.f, 0.45f, 0.1f, 0.35f) : vec4(0);   // (glowing hot)
        VR.draw(im.gl, m * im.matrix(), sp.light, tint);
      }
    }
    if (begun) VR.end();
  }
  void update(GameSession& s, double dt) override {
    ParticleSystem& P = particles();
    if (P.level == 0) return;
    for (const Spot& sp : spots) {
      if (!sp.burning) continue;
      if (rnd() < (float)dt * 10.f) {
        const vec3 R = glm::normalize(sp.right);
        const vec3 p = sp.at + R * ((rnd() - 0.5f) * sp.width * 0.7f) + vec3(0, 0.03f, 0);
        P.spawn("flame", p, 1, {{"vel", {0, 0.18f + rnd() * 0.12f, 0}}, {"life", 0.35f + rnd() * 0.2f}, {"size", 0.035f}, {"shrink", true}, {"glow", true}, {"grav", 0}});
      }
    }
  }
};
}  // namespace

void registerFurnaceLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<FurnaceLayer>(); }); }

}  // namespace dq
