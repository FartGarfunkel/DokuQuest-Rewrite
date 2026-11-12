// render/furnace_layer.cpp — what's in a furnace (smoker, blast furnace), seen in the world. After Cinematic Interactions'
// FurnaceRenderer (MIT; docs/CREDITS.md): the fuel lies on the fire box's floor (its bed: u .26-.74, a px up, up to 4
// copies), each stack lands with a 0.3 s slide (outCubic from 0.06 out), copies by count (1, 2+: 2, 9+: 3, 32+: 4). And,
// beyond the mod (the user's wish: the cooking seen, not only a gauge), what's cooking is on the top:
//   food in an iron pan, browning as it cooks, a wisp of smoke and the sizzle while it's lit;
//   ore in a clay mould, the lumps shrinking as the glowing metal fills the mould, cooling to an ingot as it's done;
//   anything else (sand to glass, clay to brick) on the ledge of the upper opening, as the mod lays it;
// and what's done along the top's front edge, the side you take it from.
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/item_layers.h"
#include "render/particles.h"
#include "assets/item_models.h"
#include "assets/voxel.h"
#include "sim/furnace_work.h"
#include "sim/worldsys.h"
#include "core/registry.h"
#include "core/itemstack.h"
#include <cmath>
#include <map>

namespace dq {
namespace {

const vec3 FRONT[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};   // meta & 3 -> its front (render/mesher.cpp FACING_FACE)

// a small voxel model built here once (the pan, the mould, the molten metal)
const VoxelModel& made(const char* key, int W, int H, int D, const std::function<u32(int, int, int)>& at) {
  static auto& cache = *new std::map<std::string, std::unique_ptr<VoxelModel>>();
  auto it = cache.find(key);
  if (it != cache.end()) return *it->second;
  VoxelGrid g(ivec3(W, H, D));
  for (int y = 0; y < H; y++) for (int z = 0; z < D; z++) for (int x = 0; x < W; x++) if (u32 c = at(x, y, z)) g.set(x, y, z, c);
  auto m = std::make_unique<VoxelModel>();
  VoxelMeshData md; buildVoxelMesh(g, md);
  m->gl.upload(md);
  m->frame.voxel = 1.f / 32; m->frame.origin = vec3(-W * 0.5f, 0, -D * 0.5f) / 32.f;   // (its foot's middle at the origin)
  m->size = g.size; m->kind = VoxelModel::Flat; m->built = true; m->key = key;
  return *(cache[key] = std::move(m));
}
// an iron frying pan: a round floor, a low rim, a long handle out to one side
const VoxelModel& pan() {
  return made("furnace_pan", 22, 3, 14, [](int x, int y, int z) -> u32 {
    const float dx = x - 6.5f, dz = z - 6.5f, r = std::sqrt(dx * dx + dz * dz);
    const u32 iron = voxRGBA(62, 62, 68), rim = voxRGBA(84, 84, 92), wood = voxRGBA(96, 62, 34);
    if (x >= 13 && z >= 6 && z <= 7) return y == 1 ? (x >= 16 ? wood : iron) : 0u;   // the handle (iron, then a wooden grip)
    if (r > 6.6f) return 0u;
    if (y == 0) return voxShade(iron, 0.9f);
    return r > 5.6f ? rim : 0u;
  });
}
// a clay ingot mould: a tray with a cavity an ingot long
const VoxelModel& mould() {
  return made("furnace_mould", 14, 4, 8, [](int x, int y, int z) -> u32 {
    const u32 clay = voxRGBA(156, 98, 66);
    const bool cavity = x >= 2 && x <= 11 && z >= 2 && z <= 5 && y >= 1;
    return cavity ? 0u : voxShade(clay, 0.88f + 0.06f * ((x + z) & 1));
  });
}
// the molten metal filling the mould's cavity (glowing): drawn scaled up with the fill
const VoxelModel& melt() {
  return made("furnace_melt", 10, 3, 4, [](int, int, int) -> u32 { return voxRGBA(255, 150, 40, VOXEL_GLOW); });
}

float outCubic(float u) { u = std::clamp(u, 0.f, 1.f); return 1 - (1 - u) * (1 - u) * (1 - u); }
int copiesOf(int n) { return n >= 32 ? 4 : n >= 9 ? 3 : n >= 2 ? 2 : n >= 1 ? 1 : 0; }   // (ItemPlacer: copies by count)
bool isFood(const std::string& in, const std::string& out) {
  const ItemDef* a = reg().item(in); const ItemDef* b = reg().item(out);
  return (a && a->has("food")) || (b && b->has("food"));
}
bool isMetal(const std::string& out) { return out.find("ingot") != std::string::npos || out.find("nugget") != std::string::npos; }

class FurnaceLayer : public RenderLayer {
public:
  const char* name() const override { return "furnaces"; }
  int order() const override { return 121; }
  void update(GameSession& s, double dt) override {
    if (!s.player) { found.clear(); return; }
    clock += dt;
    if ((scanT -= dt) <= 0) {
      scanT = 0.2;
      found.clear();
      World& w = s.world();
      forBlockEntities(w, s.frame, 48.f, [&](const Chunk&, json& e, int x, int y, int z) {
        if (e.value("type", "") != "furnace") return;
        const BlockId id = w.getBlock(x, y, z);
        const BlockDef& b = reg().block(id);
        if (!b.has("furnace")) return;
        F f; f.at = ivec3(x, y, z); f.kind = b.str("furnace"); f.front = FRONT[w.getMeta(x, y, z) & 3];
        const std::vector<ItemStack> v = furnaceItems(w, f.at);
        for (int k = 0; k < 3; k++) f.it[k] = v[k];
        f.cook = furnaceCook(w, f.at); f.lit = furnaceBurn(w, f.at) > 0;
        // the slide in: a slot that gained or changed things lands again (ClientFurnaces.apply landedAt)
        Mem& m = mem[f.at];
        for (int k = 0; k < 3; k++) {
          const std::string sig = f.it[k].id + ":" + std::to_string(f.it[k].n);
          if (sig != m.sig[k]) { if (f.it[k].n > m.n[k] || f.it[k].id != m.id[k]) m.landed[k] = clock; m.sig[k] = sig; m.n[k] = f.it[k].n; m.id[k] = f.it[k].id; }
          f.landed[k] = m.landed[k];
        }
        found.push_back(f);
      });
    }
    // smoke off a pan, a shimmer off the metal, while it's lit and something's cooking
    ParticleSystem& P = particles();
    for (const F& f : found) {
      if (!f.lit || f.it[0].empty()) continue;
      const std::string out = smeltResult(f.it[0].id, f.kind);
      const vec3 top = vec3(f.at) + vec3(0.5f, 1.05f, 0.5f);
      if (isFood(f.it[0].id, out)) { if (std::fmod(clock * 1.7 + f.at.x * 0.31, 1.0) < dt * 1.7) P.spawn("smoke", top + vec3(0, 0.08f, 0), 1); }
      else if (isMetal(out)) { if (std::fmod(clock * 1.1 + f.at.z * 0.27, 1.0) < dt * 1.1) P.spawn("smoke", top + vec3(0, 0.1f, 0), 1); }
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (found.empty()) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    for (const F& F : found) {
      const vec3 rel = f.cam.rel(glm::dvec3(F.at.x, F.at.y, F.at.z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.4f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      const u8 l = w.getLight(F.at.x + (int)F.front.x, F.at.y, F.at.z + (int)F.front.z), lu = w.getLight(F.at.x, F.at.y + 1, F.at.z);
      const VoxelLight Lfront{(l >> 4) / 15.f, (l & 15) / 15.f}, Ltop{(lu >> 4) / 15.f, (lu & 15) / 15.f};
      const VoxelLight Lfire = F.lit ? VoxelLight{Lfront.sky, 1.f} : Lfront;   // (lit from inside, as the mod lights the input and the fuel)
      const vec3 right(-F.front.z, 0, F.front.x);   // across the front, left to right as you face it
      const float yaw = std::atan2(F.front.x, F.front.z);
      u32 h = (u32)(F.at.x * 73856093) ^ (u32)(F.at.y * 19349663) ^ (u32)(F.at.z * 83492791);
      auto jit = [&] { h = h * 1664525u + 1013904223u; return ((h >> 8) & 1023) / 1023.f - 0.5f; };
      auto slide = [&](int k) { return 0.06f * (1 - outCubic((float)((clock - F.landed[k]) / 0.3))); };
      auto flat = [&](const ItemStack& st, vec3 at, float sc, VoxelLight L, float spin, vec4 tint = vec4(0)) {
        const VoxelModel& m = itemModel(st.id);
        if (!m.ready()) return;
        mat4 M = glm::translate(mat4(1), rel + at);
        M = glm::rotate(M, yaw + spin, vec3(0, 1, 0));
        if (m.kind == VoxelModel::Block) M = glm::scale(M, vec3(sc * 0.55f));
        else { M = glm::rotate(M, -PI / 2, vec3(1, 0, 0)); M = glm::scale(M, vec3(sc)); }
        VR.draw(m.gl, M * m.matrix(), L, tint);
      };
      const vec3 face = vec3(0.5f, 0, 0.5f) + F.front * 0.5f;
      // the fuel on the fire box's floor (FurnaceLayout FURNACE fuel bed: u .26-.74, a px up), just inside the mouth
      if (!F.it[1].empty()) {
        const int n = copiesOf(F.it[1].n);
        for (int c = 0; c < std::min(4, n); c++) {
          const float u = 0.26f + 0.48f * (c + 0.5f) / n;
          flat(F.it[1], face + right * (u - 0.5f) + vec3(0, 1.f / 16 + c * 0.004f, 0) + F.front * (0.02f + slide(1) + jit() * 0.02f), 0.2f, Lfire, jit() * 0.3f);
        }
      }
      const std::string out = F.it[0].empty() ? std::string() : smeltResult(F.it[0].id, F.kind);
      const bool food = !F.it[0].empty() && isFood(F.it[0].id, out), metal = !F.it[0].empty() && isMetal(out);
      const vec3 top = vec3(0.5f, 1.f, 0.5f) - F.front * 0.08f;
      if (food) {   // the pan on the top, the food in it browning as it cooks
        const VoxelModel& pm = pan();
        mat4 M = glm::rotate(glm::translate(mat4(1), rel + top), yaw + 0.4f, vec3(0, 1, 0));
        VR.draw(pm.gl, M * pm.matrix(), Ltop);
        const int n = std::min(3, copiesOf(F.it[0].n));
        const float brown = F.cook * 0.55f;
        for (int c = 0; c < n; c++) {
          const vec3 at = top + vec3(0, 1.5f / 32 + c * 0.01f, 0) + right * ((c - (n - 1) * 0.5f) * 0.09f) + F.front * (jit() * 0.04f);
          flat(F.it[0], at + vec3(0, slide(0), 0), 0.24f, Ltop, jit() * 0.6f + c, vec4(0.32f, 0.18f, 0.08f, brown));
        }
      } else if (metal) {   // the mould on the top: the ore lumps shrinking as the glowing metal fills it
        const VoxelModel& mm = mould();
        mat4 M = glm::rotate(glm::translate(mat4(1), rel + top), yaw, vec3(0, 1, 0));
        VR.draw(mm.gl, M * mm.matrix(), Ltop);
        if (F.cook > 0.02f) {
          const VoxelModel& me = melt();
          mat4 E = glm::translate(M, vec3(0, 1.f / 32, 0));
          E = glm::scale(E, vec3(1, std::max(0.05f, F.cook), 1));
          VR.draw(me.gl, E * me.matrix(), VoxelLight{1, 1});
        }
        const int n = std::min(3, copiesOf(F.it[0].n));
        const float sc = 0.2f * (1 - 0.55f * F.cook);
        for (int c = 0; c < n; c++) {
          // (resting on the metal as it rises round them, sinking into it as they melt)
          const vec3 at = top + vec3(0, (1.5f + 2.5f * F.cook + c * 0.6f) / 32.f, 0) + right * ((c - (n - 1) * 0.5f) * 0.08f) + F.front * (jit() * 0.03f);
          flat(F.it[0], at + vec3(0, slide(0), 0), sc, Ltop, jit() * 0.8f, F.lit ? vec4(1.f, 0.45f, 0.12f, 0.35f * F.cook) : vec4(0));
        }
      } else if (!F.it[0].empty()) {   // the mod's way: on the ledge of the upper opening (input bed u .22-.47, floor px 9)
        const int n = copiesOf(F.it[0].n);
        for (int c = 0; c < std::min(3, n); c++) {
          const float u = 0.22f + 0.25f * (c + 0.5f) / n;
          flat(F.it[0], face + right * (u - 0.5f) + vec3(0, 9.f / 16 + c * 0.004f, 0) + F.front * (0.02f + slide(0)), 0.2f, Lfire, jit() * 0.3f);
        }
      }
      // what's done: on the top along its front edge, before the pan or mould; a fresh ingot still a little warm
      if (!F.it[2].empty()) {
        const int n = copiesOf(F.it[2].n);
        for (int c = 0; c < n; c++) {
          const vec3 at = top + F.front * 0.42f + right * ((c - (n - 1) * 0.5f) * 0.1f) + vec3(0, 0.012f + c * 0.006f + slide(2), 0);   // (along the front edge: the side you take them from)
          flat(F.it[2], at, 0.24f, Ltop, jit() * 0.5f, isMetal(F.it[2].id) && F.lit ? vec4(1.f, 0.5f, 0.15f, 0.18f) : vec4(0));
        }
      }
    }
    if (begun) VR.end();
  }

private:
  struct F { ivec3 at; std::string kind; vec3 front; ItemStack it[3]; float cook = 0; bool lit = false; double landed[3] = {-10, -10, -10}; };
  struct Mem { std::string sig[3], id[3]; int n[3] = {0, 0, 0}; double landed[3] = {-10, -10, -10}; };
  struct Cmp { bool operator()(const ivec3& a, const ivec3& b) const { return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z; } };
  std::vector<F> found;
  std::map<ivec3, Mem, Cmp> mem;
  double scanT = 0, clock = 0;
};

} // namespace

void registerFurnaceLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<FurnaceLayer>(); });
}

} // namespace dq
