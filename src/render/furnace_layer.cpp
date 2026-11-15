// render/furnace_layer.cpp — inside a furnace, smoker or blast furnace (their openings hollowed: assets/pack_models.h
// buildFurnaceModels): the fuel in its fuel slot lies on the floor of the opening (a few of the stack, side by side), and
// while it burns (the lit block) it glows, embers lie there if the slot is empty, and flames lick up out of it. Above the
// fire, an iron grill across the opening (Tactile Blocks' four/fumoir): what's cooking (the input slot) lies on it, browning
// as it cooks (food) or glowing as it heats (ore), turning into what it makes; what's done (the output slot) waits at the back.
#include "client/session.h"
#include "render/be_scan.h"
#include "render/particles.h"
#include "assets/item_models.h"
#include "assets/voxel.h"
#include "assets/pack_models.h"
#include "core/registry.h"
#include "core/itemstack.h"
#include "world/world.h"
#include "render/vehicle_parts.h"
#include "assets/local_models.h"
#include "sim/blockinfo.h"
#include "sim/worldsys.h"
#include "client/app.h"

namespace dq {
namespace {
float rnd() { return (float)rand() / RAND_MAX; }

class FurnaceLayer : public RenderLayer {
public:
  const char* name() const override { return "furnace-fire"; }
  struct Spot { vec3 at; vec3 right, in; float width, high = 0.3f; std::string fuel, burnId, input, output, makes; int n, inN = 0, outN = 0; bool burning, rack = false; vec3 rackAt{0}; float rackW = 0, rackH = 0; float left, cook = 0; VoxelLight light, lightOff; };
  // what's cooking, as it looks now: food browns and turns into its cooked self half way; ore glows as it heats
  struct Cooking { std::string id; vec4 tint{0}; bool glows = false; };
  static Cooking cooking(const Spot& sp) {
    const ItemDef* made = sp.makes.empty() ? nullptr : reg().item(sp.makes);
    const bool food = made && made->has("food");
    const float c = sp.burning ? sp.cook : sp.cook * 0.5f;
    Cooking k; k.id = food && c > 0.55f ? sp.makes : sp.input;
    if (food) k.tint = c > 0.55f ? vec4(0.25f, 0.13f, 0.05f, 0.25f * (c - 0.55f) / 0.45f) : vec4(0.45f, 0.25f, 0.12f, 0.45f * c / 0.55f);
    else if (c > 0) k.tint = vec4(1.f, 0.42f, 0.1f, 0.55f * c);
    k.glows = !food && c > 0.3f;
    return k;
  }
  struct Grill { VoxelMeshGL gl; vec3 origin{0}; bool built = false; };
  std::unordered_map<int, Grill> grills;   // by the opening's width in 32nds
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
      if (!b.has("furnace") || stationModel(id)) return;   // (drawn from its Tactile model: render/tactile_furnaces.cpp)
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
      sp.high = std::max(0.08f, (FM->topY - FM->floorY) / 16.f);
      if (FM->up || FM->grille) {   // what cooks stands behind the bars: in the chamber over the fire, or in the slots themselves
        const float fy = FM->up ? FM->upFloorY : FM->floorY, ty = FM->up ? FM->upTopY : FM->topY;
        const float x0 = FM->up ? FM->upX0 : FM->floorX0, x1 = FM->up ? FM->upX1 : FM->floorX1;
        sp.rack = true;
        sp.rackAt = vec3(x, y, z) + turn(vec3((x0 + x1) * 0.5f, fy, FM->up ? 4.5f : 2.5f), q) / 16.f;
        sp.rackW = (x1 - x0) / 16.f; sp.rackH = (ty - fy) / 16.f;
      }
      const ItemStack in = ItemStack::fromJson(e["items"].is_array() && !e["items"].empty() ? e["items"][0] : json());
      const ItemStack out = ItemStack::fromJson(e["items"].is_array() && e["items"].size() > 2 ? e["items"][2] : json());
      if (!in.empty()) { sp.input = in.id; sp.inN = in.n; sp.makes = smeltResult(in.id, binfo(id).furnace); }
      if (!out.empty()) { sp.output = out.id; sp.outN = out.n; }
      sp.cook = std::clamp((float)(e.value("cook", 0.0) / SMELT_TIME), 0.f, 1.f);
      if (sp.fuel.empty() && !sp.burning && sp.input.empty() && sp.output.empty()) return;
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
    for (const Spot& sp : spots) {   // the grill and what's on it
      if (sp.input.empty() && sp.output.empty()) continue;
      const vec3 R = glm::normalize(sp.right), I = glm::normalize(sp.in), U(0, 1, 0);
      // its height: a little over half way up the opening, clear of the fire under it and with room for the food on it
      const float gy = std::clamp(sp.high * 0.5f, 0.06f, std::max(0.06f, sp.high - 0.1f));
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      if (!sp.rack) {
        const Grill& g = grill(s, sp.width);
        mat4 base(1); base[0] = vec4(R, 0); base[1] = vec4(U, 0); base[2] = vec4(I, 0); base[3] = vec4(f.cam.rel(glm::dvec3(sp.at + U * gy)), 1);
        if (g.gl.ready()) VR.draw(g.gl, glm::scale(glm::translate(base, g.origin), vec3(1.f / 32)), sp.light);
      }
      const float sc = std::min(0.24f, sp.width * 0.5f), top = gy + 1.5f / 32;
      auto lay = [&](const std::string& id, vec3 p, float turn, float scale, VoxelLight L, vec4 tint) {
        const VoxelModel& im = itemModel(id);
        if (!im.ready()) return;
        if (reg().blockId(id)) scale *= 0.55f;   // (a block, ore: a lump, not a cube filling the opening)
        mat4 m(1); m[0] = vec4(R, 0); m[1] = vec4(-I, 0); m[2] = vec4(U, 0); m[3] = vec4(f.cam.rel(glm::dvec3(p)), 1);
        if (sp.rack) { m[1] = vec4(U, 0); m[2] = vec4(-I, 0); m[3] = vec4(f.cam.rel(glm::dvec3(p + U * scale * 0.5f)), 1); turn *= 0.15f; }   // (stood up behind the bars, facing out)
        m = glm::rotate(m, turn, vec3(0, 0, 1));
        m = glm::scale(m, vec3(scale));
        VR.draw(im.gl, m * im.matrix(), L, tint);
      };
      // done: at the back of the grill, a piece or two of what's made
      if (sp.rack) {   // racked: what cooks in the middle, what's done beside it
        const float rs = std::min(sp.rackH * 0.95f, sp.rackW * 0.4f);
        if (!sp.output.empty()) lay(sp.output, sp.rackAt + R * sp.rackW * 0.3f + I * 0.06f, 0.f, rs * 0.8f, sp.lightOff, vec4(0));
        if (sp.input.empty()) continue;
        const Cooking k = cooking(sp);
        lay(k.id, sp.rackAt - R * sp.rackW * 0.12f, 0.f, rs, k.glows ? VoxelLight{1, 1} : sp.light, k.tint);
        if (sp.inN > 1) lay(sp.input, sp.rackAt - R * sp.rackW * 0.36f + I * 0.06f, 0.f, rs * 0.8f, sp.lightOff, vec4(0));
        continue;
      }
      for (int k = 0; k < std::min(sp.outN, 2); k++)
        lay(sp.output, sp.at + U * (top + k * 0.004f) + I * 0.17f + R * ((k - 0.5f) * sp.width * 0.3f), 0.5f + k * 0.9f, sc * 0.75f, sp.lightOff, vec4(0));
      if (sp.input.empty()) continue;
      // cooking, at the front over the fire
      const Cooking k = cooking(sp);
      lay(k.id, sp.at + U * top - I * 0.06f, 0.2f, sc, k.glows ? VoxelLight{1, 1} : sp.light, k.tint);
      if (sp.inN > 1) lay(sp.input, sp.at + U * (top + 0.004f) + I * 0.08f - R * sp.width * 0.18f, -0.7f, sc * 0.7f, sp.lightOff, vec4(0));   // (the rest, waiting)
    }
    if (begun) VR.end();
  }
  // iron bars from the front of the opening to its back, on two rails (model: x right, y up, z in; centred, the bars' top at y 0)
  const Grill& grill(GameSession& s, float width) {
    const int key = std::max(4, (int)std::lround(width * 32));
    Grill& g = grills[key];
    if (g.built) return g;
    g.built = true;
    const TileSet& ts = s.app.tiles;
    const std::string t = ts.index.count("anvil") ? "anvil" : "iron_block";
    const float hw = key / 64.f, hd = FURNACE_DEPTH / 32.f - 1.f / 32, b = 1.f / 64;
    auto box = [&](vec3 lo, vec3 hi) { ModelPart p; p.c = (lo + hi) * 0.5f; p.h = (hi - lo) * 0.5f; p.tile.fill(t); return p; };
    std::vector<ModelPart> parts{box({-hw, -2 * b * 2, -hd}, {hw, -2 * b, -hd + 2 * b}), box({-hw, -2 * b * 2, hd - 2 * b}, {hw, -2 * b, hd})};
    const int bars = std::max(3, key / 4);
    for (int i = 0; i < bars; i++) { const float x = -hw + 2 * b + (hw * 2 - 4 * b) * i / (bars - 1); parts.push_back(box({x - b, -2 * b, -hd}, {x + b, 0, hd})); }
    VoxelGrid vg; vec3 lo; VoxelFaceColor fc;
    if (!partsGrid(parts, 1.f / 32, ts, vg, lo, fc)) return g;
    VoxelMeshData md; buildVoxelMesh(vg, md, &fc);
    g.origin = lo; g.gl.upload(md);
    return g;
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
