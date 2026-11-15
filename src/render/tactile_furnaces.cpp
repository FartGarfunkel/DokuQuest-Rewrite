// render/tactile_furnaces.cpp — Tactile Blocks' furnace (four), smoker (fumoir) and blast furnace (haut_fourneau) drawn
// over ours where their models are imported (assets/local_models.h): the block entity renderers read from the mod (their
// spots, tilts and sizes, in sixteenths of the block, north-facing then turned) over our three slots — what cooks on the
// shelf (the blast furnace: on its crucible), what's done on the tier above it (the blast furnace: in its mould), the fuel
// tossed loose on the floor; the smoker's door and the blast furnace's hatch swung on their hinges; flames while it burns.
// Worked by hand in the world: sim/tactile.h tactileFurnaceUse.
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/particles.h"
#include "render/tactile_kit.h"
#include "render/content_draw.h"
#include "assets/local_models.h"
#include "sim/tactile.h"
#include "sim/blockinfo.h"
#include "core/registry.h"
#include "world/world.h"

namespace dq {
namespace {
float rnd01(u32 h) { return (h & 0xFFFF) / 65535.f; }

struct Furn { ivec3 at; int facing; std::string kind; bool lit, door; ItemStack in, fuel, out; std::string burn; VoxelLight L; };

class TactileFurnaces : public RenderLayer {
public:
  const char* name() const override { return "tactile-furnaces"; }
  std::vector<Furn> list;
  double dtLast = 0;
  void update(GameSession& s, double dt) override {
    dtLast = dt;
    ParticleSystem& P = particles();
    if (P.level == 0) return;
    for (const Furn& f : list) {
      if (!f.lit) continue;
      // (FourBlockEntity.clientTick: two flames a tick about the fire, smoke now and then)
      const vec3 c = vec3(f.at) + tactileFromNorth(f.facing, vec3(0.5f, 0.125f, f.kind == "blast" ? 0.5f : 0.25f));
      static u32 seq = 1;
      if (rnd01(seq = seq * 1664525u + 1013904223u) < dt * 20) {
        const vec3 p = c + vec3((rnd01(seq * 7) - 0.5f) * 0.5f, rnd01(seq * 13) * 0.06f, (rnd01(seq * 29) - 0.5f) * 0.5f);
        P.spawn("flame", p, 1, {{"vel", {0, 0.16f, 0}}, {"life", 0.5f}, {"size", 0.04f}, {"shrink", true}, {"glow", true}, {"grav", 0}});
      }
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    list.clear();
    forBlockEntities(w, f, 24.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const BlockId id = w.getBlock(x, y, z);
      if (!stationModel(id)) return;
      const BlockInfo& bi = binfo(id);
      if (bi.furnace != "furnace" && bi.furnace != "smoker" && bi.furnace != "blast") return;
      Furn fu; fu.at = ivec3(x, y, z); fu.facing = tactileFacing(w, fu.at); fu.kind = bi.furnace;
      fu.lit = reg().block(id).light > 0; fu.door = e.value("door", false);
      const json& it = e.contains("items") && e["items"].is_array() ? e["items"] : json::array();
      auto at = [&](size_t k) { return k < it.size() ? ItemStack::fromJson(it[k]) : ItemStack(); };
      fu.in = at(0); fu.fuel = at(1); fu.out = at(2);
      fu.burn = fu.lit ? e.value("burnId", std::string()) : std::string();
      fu.L = tactileLight(w, fu.at);
      list.push_back(fu);
    });
    if (list.empty()) return;
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    for (const Furn& fu : list) drawItems(f, fu);
    VR.end();
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    for (const Furn& fu : list) {
      if (fu.kind == "furnace") continue;
      const PackModel* door = tactilePart(fu.kind == "smoker" ? "fumoir_door" : "haut_fourneau_porte");
      if (!door) continue;
      const float open = tactileEase(fu.at, 0, fu.door, dtLast);
      TPose p = tactileBlockPose(f, fu.at, fu.facing);
      if (fu.kind == "smoker") p.t(0.1875f, 0.625f, 0.f).ry(open * 100.f).t(-0.1875f, -0.625f, 0.f);       // (FumoirBlockEntityRenderer)
      else p.t(0.25f, 0.875f, 0.34375f).ry(open * 112.5f).t(-0.25f, -0.875f, -0.34375f);                  // (HautFourneauBlockEntityRenderer)
      tactilePartDraw(s.app, cd, *door, p.m, vec2(fu.L.sky, fu.L.block));
    }
    cd.end();
  }
  // a stack shown as Tactile's slots would hold it: a slot takes up to 16 of a kind, four slots a tier
  static int pieces(const ItemStack& s, int perSlot = 16) { return s.empty() ? 0 : std::min(4, (s.n + perSlot - 1) / perSlot); }
  void drawItems(const FrameCtx& f, const Furn& fu) {
    const TPose base = tactileBlockPose(f, fu.at, fu.facing);
    const VoxelLight glow{fu.L.sky, 1.f};
    if (fu.kind == "blast") {   // (HautFourneauBlockEntityRenderer: the raw on the crucible, the mould's top, the fuel)
      static const float RAW[4][2] = {{6.5f, 8.5f}, {9.5f, 8.5f}, {6.5f, 11.5f}, {9.5f, 11.5f}};
      for (int i = 0; i < pieces(fu.in); i++) { TPose p = base; p.t(RAW[i][0] / 16, 0.88125f, RAW[i][1] / 16).rx(90).s(0.3f, 0.3f, 0.75f); tactileItem(fu.in, p.m, fu.lit ? glow : fu.L); }
      if (!fu.out.empty()) { TPose p = base; p.t(0.5f, 0.75625f, -0.0625f).rx(90).s(0.3f, 0.3f, 0.75f); tactileItem(fu.out, p.m, fu.L); }
    } else {   // (Four/FumoirBlockEntityRenderer: four spots a tier, what cooks on the shelf, what's done above)
      const bool smoker = fu.kind == "smoker";
      static const float FOUR[4][2] = {{5.5f, 5.f}, {10.5f, 5.f}, {5.5f, 9.5f}, {10.5f, 9.5f}}, FUMOIR[4][2] = {{5.5f, 3.f}, {10.5f, 3.f}, {5.5f, 7.f}, {10.5f, 7.f}};
      const float (*POS)[2] = smoker ? FUMOIR : FOUR;
      const float inY = 9.f, outY = smoker ? 9.5f : 10.f;
      for (int i = 0; i < pieces(fu.in); i++) { TPose p = base; p.t(POS[i][0] / 16, (inY + 0.1f) / 16, POS[i][1] / 16).rx(90).s(0.3f, 0.3f, 0.75f); tactileItem(fu.in, p.m, fu.L); }
      for (int i = 0; i < pieces(fu.out); i++) { TPose p = base; p.t(POS[i][0] / 16, (outY + 0.1f) / 16, POS[i][1] / 16).rx(90).s(0.3f, 0.3f, 0.75f); tactileItem(fu.out, p.m, fu.L); }
    }
    // the fuel tossed on the floor, up to eight, each where the block's own seed throws it (what burns, glowing, first)
    std::vector<ItemStack> fuel;
    if (!fu.burn.empty()) fuel.push_back(ItemStack(fu.burn, 1));
    for (int k = 0; k < fu.fuel.n && fuel.size() < 8; k++) fuel.push_back(ItemStack(fu.fuel.id, 1));
    const float fz = fu.kind == "blast" ? 8.f : 4.f;
    u32 h = (u32)(fu.at.x * 73856093) ^ (u32)(fu.at.y * 19349663) ^ (u32)(fu.at.z * 83492791);
    auto next = [&] { h = h * 1664525u + 1013904223u; return rnd01(h >> 8); };
    for (size_t k = 0; k < fuel.size(); k++) {
      const float jx = (next() - 0.5f) * 4.f, jz = (next() - 0.5f) * 4.f, spin = next() * 360.f, tx = (next() - 0.5f) * 30.f, tz = (next() - 0.5f) * 30.f;
      TPose p = base; p.t((8.f + jx) / 16, (1.1f + k * 0.5f) / 16, (fz + jz) / 16).ry(spin).rx(90.f + tx).rz(tz).s(0.4f);
      const bool burning = k == 0 && !fu.burn.empty();
      tactileItem(fuel[k], p.m, burning ? glow : fu.L, burning ? vec4(1.f, 0.45f, 0.1f, 0.35f) : vec4(0));
    }
  }
};
}  // namespace

void registerTactileFurnaces() { registerRenderLayer([](GameSession&) { return std::make_shared<TactileFurnaces>(); }); }

} // namespace dq
