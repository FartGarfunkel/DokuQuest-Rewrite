// render/hands_furnaces.cpp — the hands-on stations' furnace, smoker and blast furnace drawn
// over ours where their models are imported (assets/local_models.h): the block entity renderers read from the mod (their
// spots, tilts and sizes, in sixteenths of the block, north-facing then turned) over our three slots — what cooks on the
// shelf (the blast furnace: on its crucible), what's done on the tier above it (the blast furnace: in its mould), the fuel
// tossed loose on the floor; the smoker's door and the blast furnace's hatch swung on their hinges; flames while it burns.
// Worked by hand in the world: sim/hands.h handsFurnaceUse.
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/particles.h"
#include "render/hands_kit.h"
#include "render/content_draw.h"
#include "assets/local_models.h"
#include "sim/hands.h"
#include "sim/blockinfo.h"
#include "core/registry.h"
#include "world/world.h"
#include "render/tile_box.h"
#include "sim/worldsys.h"

namespace dq {
namespace {
float rnd01(u32 h) { return (h & 0xFFFF) / 65535.f; }

struct Furn { ivec3 at; int facing; std::string kind; bool lit, door; ItemStack in[4], fuel, out[4]; float cook[4] = {0, 0, 0, 0}; std::string burn; VoxelLight L; };

class HandsFurnaces : public RenderLayer {
public:
  const char* name() const override { return "hands-furnaces"; }
  std::vector<Furn> list;
  double dtLast = 0;
  void update(GameSession& s, double dt) override {
    dtLast = dt;
    ParticleSystem& P = particles();
    if (P.level == 0) return;
    for (const Furn& f : list) {
      if (!f.lit) continue;
      // (FourBlockEntity.clientTick: two flames a tick about the fire, smoke now and then)
      const vec3 c = vec3(f.at) + handsFromNorth(f.facing, vec3(0.5f, 0.125f, f.kind == "blast" ? 0.5f : 0.25f));
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
      Furn fu; fu.at = ivec3(x, y, z); fu.facing = handsFacing(w, fu.at); fu.kind = bi.furnace;
      fu.lit = reg().block(id).light > 0; fu.door = e.value("door", false);
      const json& it = e.contains("items") && e["items"].is_array() ? e["items"] : json::array();
      auto at = [&](size_t k) { return k < it.size() ? ItemStack::fromJson(it[k]) : ItemStack(); };
      static const int IN[4] = {0, 3, 4, 5}, OUT[4] = {2, 6, 7, 8};   // (the hands-on furnace's spots: sim/worldsys.cpp tickFurnace)
      for (int k = 0; k < 4; k++) { fu.in[k] = at(IN[k]); fu.out[k] = at(OUT[k]); }
      fu.fuel = at(1);
      for (int k = 0; k < 4; k++) {   // (how far each spot has cooked: sim/worldsys.cpp tickFurnace)
        const double c = e.contains("cooks") && e["cooks"].is_array() && (int)e["cooks"].size() > k ? e["cooks"][k].get<double>() : k == 0 ? e.value("cook", 0.0) : 0.0;
        fu.cook[k] = std::clamp((float)(c / SMELT_TIME), 0.f, 1.f);
      }
      fu.burn = fu.lit ? e.value("burnId", std::string()) : std::string();
      fu.L = handsLight(w, fu.at);
      list.push_back(fu);
    });
    if (list.empty()) return;
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    for (const Furn& fu : list) drawItems(f, fu);
    VR.end();
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    // the blast furnace's melt: down the trough from the crucible into the mould, a pool there as the ingot forms
    if (const GLuint lava = tileTexture(s.app, "lava")) for (const Furn& fu : list) {
      if (fu.kind != "blast" || !fu.lit) continue;
      float c = 0; for (float k : fu.cook) c = std::max(c, k);
      if (c < 0.25f) continue;
      const float flow = std::min(1.f, (c - 0.25f) / 0.2f);   // (it starts to run a quarter of the way in)
      const vec2 hot(fu.L.sky, 1.f);
      const vec3 a(8.f / 16, 14.7f / 16, 6.0f / 16), b(8.f / 16, 12.4f / 16, 0.55f / 16);   // (the trough's sloped floor between its rails, north frame)
      const vec3 e2 = a + (b - a) * flow, mid = (a + e2) * 0.5f, d = e2 - a;
      TPose st = handsBlockPose(f, fu.at, fu.facing);
      st.t(mid.x, mid.y + 0.002f, mid.z).rx(-90.f + glm::degrees(std::atan2(d.y, -d.z)));
      st.s(1.9f / 16, glm::length(d), 1);
      cd.picture(lava, st.m, vec4(0.4f, 0.f, 0.6f, 1.f), vec4(1.f, 0.85f, 0.7f, 1.f), hot, false);
      if (flow >= 1.f) {   // (the pool in the mould, filling as the piece melts in)
        TPose pool = handsBlockPose(f, fu.at, fu.facing);
        pool.t(0.5f, (11.2f + 1.2f * c) / 16, -1.f / 16).rx(-90).s(5.f / 16, 5.f / 16, 1);
        cd.picture(lava, pool.m, vec4(0, 0, 1, 1), vec4(1.f, 0.8f, 0.6f, 1.f), hot, false);
      }
    }
    for (const Furn& fu : list) {
      if (fu.kind == "furnace") continue;
      const PackModel* door = handsPart(fu.kind == "smoker" ? "smoker_door" : "blast_furnace_door");
      if (!door) continue;
      const float open = handsEase(fu.at, 0, fu.door, dtLast);
      TPose p = handsBlockPose(f, fu.at, fu.facing);
      if (fu.kind == "smoker") p.t(0.1875f, 0.625f, 0.f).ry(open * 100.f).t(-0.1875f, -0.625f, 0.f);       //
      else p.t(0.25f, 0.875f, 0.34375f).ry(open * 112.5f).t(-0.25f, -0.875f, -0.34375f);                  //
      handsPartDraw(s.app, cd, *door, p.m, vec2(fu.L.sky, fu.L.block));
    }
    cd.end();
  }
  void drawItems(const FrameCtx& f, const Furn& fu) {
    const TPose base = handsBlockPose(f, fu.at, fu.facing);
    const VoxelLight glow{fu.L.sky, 1.f};
    if (fu.kind == "blast") {   // (the raw on the crucible, the mould's top, the fuel)
      static const float RAW[4][2] = {{6.5f, 8.5f}, {9.5f, 8.5f}, {6.5f, 11.5f}, {9.5f, 11.5f}};
      // the ore heating on the crucible: glowing as it goes, melting down to nothing as its piece runs off to the mould
      for (int i = 0; i < 4; i++) {
        if (fu.in[i].empty()) continue;
        const float c = fu.lit ? fu.cook[i] : 0.f, k = 1.f - 0.55f * c;
        TPose p = base; p.t(RAW[i][0] / 16, 0.88125f - 0.02f * c, RAW[i][1] / 16).rx(90).s(0.3f * k, 0.3f * k, 0.75f * k);
        handsItem(fu.in[i], p.m, fu.lit ? glow : fu.L, c > 0 ? vec4(1.f, 0.45f, 0.08f, 0.25f + 0.5f * c) : vec4(0));
      }
      for (int i = 0; i < 4; i++) if (!fu.out[i].empty()) { TPose p = base; p.t(0.5f, 0.75625f, -0.0625f).rx(90).s(0.3f, 0.3f, 0.75f); handsItem(fu.out[i], p.m, fu.L); break; }   // (the mould shows its top)
    } else {   // (Four/its renderer: four spots a tier, what cooks on the shelf, what's done above)
      const bool smoker = fu.kind == "smoker";
      static const float FOUR[4][2] = {{5.5f, 5.f}, {10.5f, 5.f}, {5.5f, 9.5f}, {10.5f, 9.5f}}, FUMOIR[4][2] = {{5.5f, 3.f}, {10.5f, 3.f}, {5.5f, 7.f}, {10.5f, 7.f}};
      const float (*POS)[2] = smoker ? FUMOIR : FOUR;
      const float inY = 9.f, outY = smoker ? 9.5f : 10.f;
      for (int i = 0; i < 4; i++) { TPose p = base; p.t(POS[i][0] / 16, (inY + 0.1f) / 16, POS[i][1] / 16).rx(90).s(0.3f, 0.3f, 0.75f); handsItem(fu.in[i], p.m, fu.L); }
      for (int i = 0; i < 4; i++) { TPose p = base; p.t(POS[i][0] / 16, (outY + 0.1f) / 16, POS[i][1] / 16).rx(90).s(0.3f, 0.3f, 0.75f); handsItem(fu.out[i], p.m, fu.L); }
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
      handsItem(fuel[k], p.m, burning ? glow : fu.L, burning ? vec4(1.f, 0.45f, 0.1f, 0.35f) : vec4(0));
    }
  }
};
}  // namespace

void registerHandsFurnaces() { registerRenderLayer([](GameSession&) { return std::make_shared<HandsFurnaces>(); }); }

} // namespace dq
