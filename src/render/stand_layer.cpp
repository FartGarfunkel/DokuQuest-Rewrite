// render/stand_layer.cpp — armour stands (render NONE blocks): a wooden figure on a stone plate, wearing whatever was
// put on it (the players' own armour meshes, assets/char_model armorMeshes) and holding a weapon in its right hand.
// The gear is the block entity's "items" (helmet, chest, legs, boots, hand; sim/build.cpp), "pose" 0..4.
// Meta & 3 = the way the placer looked; the stand faces back at them.
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "client/app.h"
#include "assets/char_model.h"
#include "assets/item_models.h"
#include "assets/voxel.h"
#include "assets/tileset.h"
#include "core/itemstack.h"
#include "core/item_glint.h"
#include <unordered_map>

namespace dq {
namespace {

mat4 heldMatrix(float tilt, float s, vec2 grip, bool upright) {   // as render/creature_layer.cpp
  vec3 X, Y;
  if (upright) { X = vec3(0, 0, -s); Y = vec3(0, s * std::cos(tilt), -s * std::sin(tilt)); }
  else {
    vec3 F(0, std::sin(tilt), -std::cos(tilt)), Bn(0, -std::cos(tilt), -std::sin(tilt));
    float r2 = std::sqrt(0.5f); X = (F - Bn) * r2 * s; Y = (F + Bn) * r2 * s;
  }
  vec3 Z(s, 0, 0), t = -(grip.x * X + grip.y * Y);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(t, 1));
}

struct Pose { vec3 armR, armL, head; };   // x pitch (+ swings forward), z roll (+ out to the side), in radians
const Pose POSES[5] = {
  {vec3(0.f, 0, 0.12f), vec3(0.f, 0, -0.12f), vec3(0)},                // standing easy
  {vec3(1.45f, 0, 0.05f), vec3(1.45f, 0, -0.05f), vec3(0)},             // arms out in front
  {vec3(2.9f, 0, 0.15f), vec3(0.f, 0, -0.1f), vec3(-0.2f, 0, 0)},       // one hand raised
  {vec3(0.f, 0, 1.45f), vec3(0.f, 0, -1.45f), vec3(0)},                 // arms out to the sides
  {vec3(0.9f, 0, 0.1f), vec3(-0.25f, 0, -0.45f), vec3(0.1f, 0, 0.12f)}, // the hero: blade forward, chin up
};

// One stick or plate of the stand: the whole tile stretched across each face, two voxels per sixteenth.
struct StandKey {
  u16 tile = 0;
  u8 n[3] = {};
  bool operator==(const StandKey& o) const { return tile == o.tile && n[0] == o.n[0] && n[1] == o.n[1] && n[2] == o.n[2]; }
};
struct StandHash {
  size_t operator()(const StandKey& k) const { return ((size_t)k.tile * 1315423911u) ^ k.n[0] ^ ((size_t)k.n[1] << 8) ^ ((size_t)k.n[2] << 16); }
};
static const VoxelMeshGL* standPartMesh(const TileSet& ts, StandKey key) {
  struct Cache {
    const u8* px = nullptr;
    int count = -1, size = -1;
    std::unordered_map<StandKey, std::unique_ptr<VoxelMeshGL>, StandHash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  for (int i = 0; i < 3; i++) if (!key.n[i]) key.n[i] = 1;
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int nx = key.n[0], ny = key.n[1], nz = key.n[2];
  const int src = ts.size > 0 ? ts.size : 32;
  VoxelGrid g(ivec3(nx, ny, nz));
  auto sample = [&](float u, float v) -> u32 {
    if (key.tile >= ts.count) return 0;
    int sx = clampv((int)(u * src), 0, src - 1), sy = clampv((int)(v * src), 0, src - 1);
    const u8* p = ts.tile(key.tile) + ((size_t)sy * src + sx) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2]);
  };
  u32 fill = voxRGBA(1, 1, 1);
  for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) g.set(x, y, z, fill);
  for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) {
    float v = 1.f - (y + 0.5f) / ny;
    if (u32 c = sample(1.f - (z + 0.5f) / nz, v)) g.set(nx - 1, y, z, c);
    if (u32 c = sample((z + 0.5f) / nz, v)) g.set(0, y, z, c);
  }
  for (int z = 0; z < nz; z++) for (int x = 0; x < nx; x++) {
    float u = (x + 0.5f) / nx, v = (z + 0.5f) / nz;
    if (u32 c = sample(u, v)) g.set(x, ny - 1, z, c);
    if (u32 c = sample(1.f - u, v)) g.set(x, 0, z, c);
  }
  for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) {
    float v = 1.f - (y + 0.5f) / ny;
    if (u32 c = sample((x + 0.5f) / nx, v)) g.set(x, y, nz - 1, c);
    if (u32 c = sample(1.f - (x + 0.5f) / nx, v)) g.set(x, y, 0, c);
  }
  VoxelMeshData md;
  buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

class StandLayer : public RenderLayer {
public:
  const char* name() const override { return "armour-stands"; }
  int order() const override { return 111; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    App& a = app();
    struct One { mat4 base; vec2 light; std::array<ItemStack, 5> gear; int pose; };
    std::vector<One> list;
    forBlockEntities(w, f, 48.f, [&](const Chunk&, json& e, int x, int y, int z) {
      if (e.value("type", "") != "stand") return;
      if (!reg().block(w.getBlock(x, y, z)).has("armorStand")) return;
      vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f, 1, 0.5f), 1.6f)) return;
      u8 l = w.getLight(x, y, z), l2 = w.getLight(x, y + 1, z);
      vec2 light(std::max(l >> 4, l2 >> 4) / 15.f, std::max(l & 15, l2 & 15) / 15.f);
      int fdir = w.getMeta(x, y, z) & 3;
      mat4 base = glm::rotate(glm::translate(mat4(1), rel + vec3(0.5f, 0, 0.5f)), PI - fdir * PI * 0.5f, vec3(0, 1, 0));
      One o{base, light, {}, clampv(e.value("pose", 0), 0, 4)};
      if (e.contains("items") && e["items"].is_array())
        for (size_t i = 0; i < 5 && i < e["items"].size(); i++) if (!e["items"][i].is_null()) o.gear[i] = ItemStack::fromJson(e["items"][i]);
      list.push_back(std::move(o));
    });
    if (list.empty()) return;
    // ---- the wooden figure, then what it wears and holds ----
    static const BlockId PL = reg().blockId("planks"), ST = reg().blockId("smooth_stone");
    const u16 woodTile = PL ? reg().block(PL).faces[0] : 0;
    const u16 stoneTile = ST ? reg().block(ST).faces[2] : woodTile;
    const bool woodVox = woodTile < a.tiles.count;
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    auto stick = [&](const mat4& M, vec3 a0, vec3 b0, u16 tile, VoxelLight L) {
      if (!woodVox || tile >= a.tiles.count) return;
      vec3 d = b0 - a0;
      int nx = std::max(1, (int)std::lround(std::fabs(d.x) * 2.f));
      int ny = std::max(1, (int)std::lround(std::fabs(d.y) * 2.f));
      int nz = std::max(1, (int)std::lround(std::fabs(d.z) * 2.f));
      StandKey key; key.tile = tile; key.n[0] = (u8)std::min(nx, 255); key.n[1] = (u8)std::min(ny, 255); key.n[2] = (u8)std::min(nz, 255);
      const VoxelMeshGL* mesh = standPartMesh(a.tiles, key);
      if (!mesh || !mesh->ready()) return;
      mat4 model = M * glm::translate(mat4(1), a0) * glm::scale(mat4(1), vec3(d.x / key.n[0], d.y / key.n[1], d.z / key.n[2]));
      VR.draw(*mesh, model, L);
    };
    for (auto& o : list) {
      const Pose& P = POSES[o.pose];
      mat4 b = glm::scale(o.base, vec3(1.f / 16));
      VoxelLight L{o.light.x, o.light.y};
      if (woodVox) {
        stick(b, vec3(-6, 0, -6), vec3(6, 1, 6), stoneTile, L);
        for (float sx : {-2.f, 2.f}) stick(b, vec3(sx - 1, 1, -1), vec3(sx + 1, 12.5f, 1), woodTile, L);
        stick(b, vec3(-4, 12, -1), vec3(4, 14, 1), woodTile, L);
        stick(b, vec3(-1, 14, -1), vec3(1, 23, 1), woodTile, L);
        stick(b, vec3(-6, 22, -1.5f), vec3(6, 25, 1.5f), woodTile, L);
        stick(part(b, vec3(0, 25, 0), P.head), vec3(-1, 0, -1), vec3(1, 7, 1), woodTile, L);
        stick(part(b, vec3(6, 23, 0), P.armR), vec3(-1, -10, -1), vec3(1, 0, 1), woodTile, L);
        stick(part(b, vec3(-6, 23, 0), P.armL, true), vec3(-1, -10, -1), vec3(1, 0, 1), woodTile, L);
      }
      mat4 armR = part(b, vec3(6, 23, 0), P.armR), armL = part(b, vec3(-6, 23, 0), P.armL, true);
      auto partMat = [&](const std::string& pn) -> mat4 {
        if (pn == "head") return part(b, vec3(0, 25, 0), P.head);
        if (pn == "body") return glm::translate(b, vec3(0, 13, 0));
        if (pn == "armR_u") return armR;
        if (pn == "armR_d") return glm::translate(armR, vec3(0, -4, 0));
        if (pn == "armL_u") return armL;
        if (pn == "armL_d") return glm::translate(armL, vec3(0, -4, 0));
        if (pn == "legR_u") return glm::translate(b, vec3(2, 13, 0));
        if (pn == "legR_d") return glm::translate(b, vec3(2, 7, 0));
        if (pn == "legL_u") return glm::translate(b, vec3(-2, 13, 0));
        if (pn == "legL_d") return glm::translate(b, vec3(-2, 7, 0));
        return mat4(0);
      };
      for (int slot = 0; slot < 4; slot++) {
        const ItemStack& g = o.gear[slot];
        if (g.empty() || !reg().item(g.id)) continue;
        std::string dye;
        if (g.tag.is_object() && g.tag.contains("col") && g.tag["col"].is_string()) { std::string c = g.tag["col"]; dye = c.size() == 7 ? c.substr(1) : ""; }
        const ArmorSet& A = armorMeshes(g.id, dye, slot, false, false, a.tiles);
        for (auto& [pn, meshes] : A.byPart) {
          mat4 pm = partMat(pn);
          if (pm[3][3] == 0) continue;
          for (const CharMesh* cm : meshes) VR.draw(cm->gl, pm * cm->local(), L, vec4(0), itemGlints(g) ? 1.f : 0.f);
        }
      }
      const ItemStack& h = o.gear[4];
      if (!h.empty()) {
        const VoxelModel& im = itemModel(h.id);
        if (im.ready()) {
          static const mat4 TOOL = heldMatrix(0.25f, 0.85f, vec2(-0.34f, -0.24f), false), ITEM = heldMatrix(0.2f, 0.55f, vec2(0, -0.25f), true);
          mat4 hm = glm::translate(armR, vec3(0, -9, -0.5f));
          hm = glm::scale(hm, vec3(16));   // back to blocks for the item's own frame
          hm = hm * (isHandheld(h.id) ? TOOL : ITEM);
          VR.draw(im.gl, hm * im.matrix(), L, vec4(0), itemGlints(h) ? 1.f : 0.f);
        }
      }
    }
    VR.end();
  }
private:
  // a limb hung at pivot (px), pitched forward by r.x and swung out by r.z (mirrored for the left side)
  static mat4 part(const mat4& b, vec3 pivot, vec3 r, bool left = false) {
    mat4 m = glm::translate(b, pivot);
    (void)left;
    m = glm::rotate(m, r.z, vec3(0, 0, 1));
    m = glm::rotate(m, r.x, vec3(1, 0, 0));
    return m;
  }
};

} // namespace

void registerStandLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<StandLayer>(); }); }

} // namespace dq
