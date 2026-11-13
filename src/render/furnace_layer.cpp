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
  struct Spot { vec3 at; vec3 right, in; float width; std::string fuel, burnId; int n; bool burning; float left; VoxelLight light, lightOff; };
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
      const float mx = (FM->floorX0 + FM->floorX1) * 0.5f, dz = FURNACE_DEPTH * 0.5f;
      sp.at = vec3(x, y, z) + turn(vec3(mx, FM->floorY, dz), q) / 16.f;
      sp.right = (turn(vec3(mx - 1, FM->floorY, dz), q) - turn(vec3(mx, FM->floorY, dz), q));   // (model -x: to the picture's right)
      sp.in = (turn(vec3(mx, FM->floorY, dz + 1), q) - turn(vec3(mx, FM->floorY, dz), q));
      sp.width = std::max(0.08f, (FM->floorX1 - FM->floorX0) / 16.f - 0.08f);
      const json& it = e.contains("items") && e["items"].is_array() && e["items"].size() > 1 ? e["items"][1] : json();
      const ItemStack fuel = ItemStack::fromJson(it);
      sp.fuel = fuel.empty() ? "" : fuel.id; sp.n = fuel.n;
      sp.burning = b.light > 0;
      sp.burnId = e.value("burnId", std::string());
      const double bm = e.value("burnMax", 0.0);
      sp.left = bm > 0 ? (float)std::clamp(e.value("burn", 0.0) / bm, 0.0, 1.0) : 1.f;   // (how much of what burns is left)
      if (sp.fuel.empty() && !sp.burning) return;
      const u8 l = w.getLight(x + DX[fd], y, z + DZ[fd]);
      sp.lightOff = VoxelLight{(l >> 4) / 15.f, std::max((l & 15) / 15.f, sp.burning ? 0.75f : 0.f)};
      sp.light = sp.burning ? VoxelLight{1, 1} : sp.lightOff;
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
      const vec3 R = glm::normalize(sp.right), I = glm::normalize(sp.in), U(0, 1, 0);
      const float sc = std::min(0.2f, sp.width * 0.4f);
      auto lay = [&](const std::string& id, vec3 p, float turn, float scale, VoxelLight L, vec4 tint) {
        const VoxelModel& im = itemModel(id);
        if (!im.ready()) return;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        mat4 m(1); m[0] = vec4(R, 0); m[1] = vec4(-I, 0); m[2] = vec4(U, 0); m[3] = vec4(f.cam.rel(glm::dvec3(p)), 1);   // (lying flat, its face up)
        m = glm::rotate(m, turn, vec3(0, 0, 1));
        m = glm::scale(m, vec3(scale));
        VR.draw(im.gl, m * im.matrix(), L, tint);
      };
      // what burns: in the middle of the fire, glowing, burning down as its time runs out (sim/worldsys.cpp tickFurnace)
      if (sp.burning) {
        const std::string what = !sp.burnId.empty() ? sp.burnId : !sp.fuel.empty() ? sp.fuel : std::string("charcoal");
        const float k = 0.3f + 0.7f * sp.left;
        lay(what, sp.at + U * 0.01f, 0.3f, sc * k, sp.light, vec4(1.f, 0.42f, 0.08f, 0.25f + 0.3f * (1 - sp.left)));
      }
      // the rest of the stack waiting in the fuel slot: further in, side by side (not yet alight)
      const int pieces = sp.fuel.empty() ? 0 : std::clamp(sp.n, 1, 4);
      for (int k = 0; k < pieces; k++) {
        const float off = pieces == 1 ? 0.f : ((float)k / (pieces - 1) - 0.5f) * sp.width * 0.75f;
        const vec3 p = sp.at + I * (sp.burning ? 0.12f : 0.f) + R * off + U * (0.012f + k * 0.003f);
        lay(sp.fuel, p, (k - 1.5f) * 0.45f, sc * 0.85f, sp.lightOff, vec4(0));
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
