#include "render/mesher.h"
#include "assets/tileset.h"
#include <cstring>

namespace dq {

// ---------------------------------------------------------------- gathering (main thread)
bool gatherMeshInput(const World& w, const SectionKey& k, MeshInput& in) {
  constexpr int N = MeshInput::N;
  Chunk* center = w.chunk(k.dim, k.cx, k.cz);
  if (!center) return false;
  in.dim = k.dim; in.cx = k.cx; in.sy = k.sy; in.cz = k.cz;
  std::memset(in.ids, 0, sizeof in.ids); std::memset(in.meta, 0, sizeof in.meta); std::memset(in.light, 0xF0, sizeof in.light);
  std::memset(in.climate, 128, sizeof in.climate);
  in.empty = true;
  const Section* cs = center->section(k.sy);
  if (!cs || cs->solidCount == 0) {
    // an empty section draws nothing, but keep going if a neighbour face could show (handled by the neighbour)
    return true;
  }
  in.empty = false;
  int baseY = dimBase(k.dim) + k.sy * 16;
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    Chunk* c = w.chunk(k.dim, k.cx + dx, k.cz + dz);
    int x0 = dx < 0 ? 15 : 0, x1 = dx > 0 ? 0 : 15, z0 = dz < 0 ? 15 : 0, z1 = dz > 0 ? 0 : 15;
    if (dx == 0) { x0 = 0; x1 = 15; } if (dz == 0) { z0 = 0; z1 = 15; }
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
      int ox = x + dx * 16, oz = z + dz * 16;   // -1..16
      if (c) { in.climate[((oz + 1) * N + (ox + 1)) * 2] = c->climate[(z * 16 + x) * 2]; in.climate[((oz + 1) * N + (ox + 1)) * 2 + 1] = c->climate[(z * 16 + x) * 2 + 1]; }
      for (int y = -1; y <= 16; y++) {
        int gy = baseY + y;
        int i = MeshInput::idx(ox, y, oz);
        if (!inWorldY(gy)) { in.ids[i] = AIR; in.light[i] = gy < 0 ? 0 : 0xF0; continue; }
        if (y < 0 || y > 15) {   // vertical apron may cross into the band below/above
          const Chunk* vc = (gy >> 8) == k.dim ? c : w.chunk(gy >> 8, k.cx + dx, k.cz + dz);
          if (vc) { in.ids[i] = vc->get(x, gy & 255, z); in.meta[i] = vc->getMeta(x, gy & 255, z); in.light[i] = vc->getLight(x, gy & 255, z); }
          continue;
        }
        if (!c) { in.ids[i] = AIR; continue; }
        const Section* s = c->section(k.sy);
        if (!s) { in.light[i] = c->airLight[k.sy]; continue; }
        int si = secIdx(x, y, z);
        in.ids[i] = s->blocks[si]; in.meta[i] = s->meta[si]; in.light[i] = s->light[si];
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------- tints
u32 climateTint(const char* kind, u8 temp, u8 humid) {
  { bool ok = false; u32 c = packTint(kind, temp, humid, ok); if (ok) return c; }   // the resource pack's colormap
  float t = temp / 255.f, h = humid / 255.f * t;
  // corners of the classic colour triangle (warm-wet / warm-dry / cold)
  vec3 grassWarmWet(0.28f, 0.70f, 0.17f), grassDry(0.75f, 0.71f, 0.33f), grassCold(0.50f, 0.71f, 0.58f);
  vec3 folWarmWet(0.20f, 0.60f, 0.05f), folDry(0.68f, 0.64f, 0.16f), folCold(0.38f, 0.60f, 0.40f);
  bool fol = kind[0] == 'f';
  vec3 a = fol ? folWarmWet : grassWarmWet, b = fol ? folDry : grassDry, c = fol ? folCold : grassCold;
  vec3 col = glm::mix(glm::mix(c, b, t), a, h);
  return (u32)(col.r * 255) | (u32)(col.g * 255) << 8 | (u32)(col.b * 255) << 16;
}

// ---------------------------------------------------------------- building (worker threads)
namespace {
struct FaceDef { ivec3 n; ivec3 v[4]; u8 uv[4][2]; };
const FaceDef FACES[6] = {
  {{1, 0, 0}, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 32}, {32, 32}, {32, 0}, {0, 0}}},
  {{-1, 0, 0}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {{0, 32}, {32, 32}, {32, 0}, {0, 0}}},
  {{0, 1, 0}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{0, 32}, {32, 32}, {32, 0}, {0, 0}}},
  {{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {{0, 0}, {32, 0}, {32, 32}, {0, 32}}},
  {{0, 0, 1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{0, 32}, {32, 32}, {32, 0}, {0, 0}}},
  {{0, 0, -1}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {{0, 32}, {32, 32}, {32, 0}, {0, 0}}},
};

struct Ctx {
  const MeshInput& in; MeshOutput& out; const MesherOptions& opt; const Registry& R;
  BlockId at(int x, int y, int z) const { return in.ids[MeshInput::idx(x, y, z)]; }
  u8 lightAt(int x, int y, int z) const { return in.light[MeshInput::idx(x, y, z)]; }
  bool opaqueAt(int x, int y, int z) const { return R.tOpaque[at(x, y, z)]; }
  u32 tint(const BlockDef& b, int x, int z) const {
    std::string t = b.str("tint");
    if (t.empty()) return 0xFFFFFF;
    const u8* c = &in.climate[((z + 1) * MeshInput::N + (x + 1)) * 2];
    return climateTint(t.c_str(), c[0], c[1]);
  }
};

void pushVert(std::vector<ChunkVertex>& v, vec3 p, u16 layer, u8 u, u8 vv, int normal, int ao, u8 light, u32 tint, int wave, u8 mat) {
  ChunkVertex cv;
  cv.x = (i16)std::lround(p.x * 32); cv.y = (i16)std::lround(p.y * 32); cv.z = (i16)std::lround(p.z * 32);
  cv.layer = layer; cv.u = u; cv.v = vv;
  cv.nf = (u8)(normal | (ao << 3) | (wave << 5));
  cv.light = light; cv.r = tint & 255; cv.g = (tint >> 8) & 255; cv.b = (tint >> 16) & 255; cv.mat = mat;
  v.push_back(cv);
}

// full-cube face with AO and smooth light
void cubeFace(Ctx& c, std::vector<ChunkVertex>& dst, int x, int y, int z, int f, u16 layer, u32 tint, u8 mat, int wave) {
  const FaceDef& F = FACES[f];
  int fx = x + F.n.x, fy = y + F.n.y, fz = z + F.n.z;
  int aos[4]; u8 lights[4];
  for (int k = 0; k < 4; k++) {
    ivec3 v = F.v[k];
    ivec3 d(v.x ? 1 : -1, v.y ? 1 : -1, v.z ? 1 : -1);
    ivec3 t1, t2;
    if (F.n.x) { t1 = {0, d.y, 0}; t2 = {0, 0, d.z}; }
    else if (F.n.y) { t1 = {d.x, 0, 0}; t2 = {0, 0, d.z}; }
    else { t1 = {d.x, 0, 0}; t2 = {0, d.y, 0}; }
    bool s1 = c.opaqueAt(fx + t1.x, fy + t1.y, fz + t1.z), s2 = c.opaqueAt(fx + t2.x, fy + t2.y, fz + t2.z);
    bool cr = c.opaqueAt(fx + t1.x + t2.x, fy + t1.y + t2.y, fz + t1.z + t2.z);
    aos[k] = (s1 && s2) ? 0 : 3 - (s1 + s2 + cr);
    // smooth light: average over the non-opaque cells touching this corner
    int sky = 0, blk = 0, n = 0;
    auto add = [&](int ax, int ay, int az, bool solid) { if (solid) return; u8 l = c.lightAt(ax, ay, az); sky += l >> 4; blk += l & 15; n++; };
    add(fx, fy, fz, false);
    add(fx + t1.x, fy + t1.y, fz + t1.z, s1);
    add(fx + t2.x, fy + t2.y, fz + t2.z, s2);
    if (!(s1 && s2)) add(fx + t1.x + t2.x, fy + t1.y + t2.y, fz + t1.z + t2.z, cr);
    if (!n) n = 1;
    lights[k] = (u8)(((sky + n / 2) / n) << 4 | ((blk + n / 2) / n));
  }
  for (int k = 0; k < 4; k++) {
    vec3 p(x + F.v[k].x, y + F.v[k].y, z + F.v[k].z);
    pushVert(dst, p, layer, F.uv[k][0], F.uv[k][1], f, aos[k], lights[k], tint, wave, mat);
  }
}

// axis-aligned box (in block units, 0..1) with flat light
void boxFaces(Ctx& c, std::vector<ChunkVertex>& dst, int x, int y, int z, vec3 lo, vec3 hi, const std::array<u16, 6>& layers, u32 tint, u8 mat, u8 light, int wave = 0) {
  for (int f = 0; f < 6; f++) {
    const FaceDef& F = FACES[f];
    for (int k = 0; k < 4; k++) {
      vec3 v = vec3(F.v[k]);
      vec3 p = glm::mix(lo, hi, v);
      // UVs follow the box position so a slab shows the lower half of the tile, etc.
      float u, vv;
      if (F.n.x) { u = F.n.x > 0 ? 1 - p.z : p.z; vv = 1 - p.y; }
      else if (F.n.y) { u = p.x; vv = F.n.y > 0 ? p.z : p.z; }
      else { u = F.n.z > 0 ? p.x : 1 - p.x; vv = 1 - p.y; }
      pushVert(dst, vec3(x, y, z) + p, layers[f], (u8)std::lround(u * 32), (u8)std::lround(vv * 32), f, 3, light, tint, wave, mat);
    }
  }
}

void crossQuads(Ctx& c, std::vector<ChunkVertex>& dst, int x, int y, int z, u16 layer, u32 tint, u8 light, int wave, float jitterSeed) {
  // two diagonal planes, each drawn from both sides; slight per-position offset like the original
  float ox = (std::fmod(jitterSeed * 12.9898f, 1.f) - 0.5f) * 0.25f, oz = (std::fmod(jitterSeed * 78.233f, 1.f) - 0.5f) * 0.25f;
  const vec3 base(x + 0.5f + ox, y, z + 0.5f + oz);
  const float r = 0.45f;
  vec2 dirs[2] = {{r, r}, {r, -r}};
  for (auto d : dirs) {
    vec3 a = base + vec3(-d.x, 0, -d.y), b = base + vec3(d.x, 0, d.y);
    vec3 q[4] = {a, b, b + vec3(0, 1, 0), a + vec3(0, 1, 0)};
    u8 uv[4][2] = {{0, 32}, {32, 32}, {32, 0}, {0, 0}};
    for (int k = 0; k < 4; k++) pushVert(dst, q[k], layer, uv[k][0], uv[k][1], 2, 3, light, tint, k >= 2 ? wave : 0, Mat::Cutout | Mat::NoRelief);
    for (int k = 3; k >= 0; k--) pushVert(dst, q[k], layer, uv[k][0], uv[k][1], 2, 3, light, tint, k >= 2 ? wave : 0, Mat::Cutout | Mat::NoRelief);
  }
}

float liquidHeight(u8 meta) {
  int lvl = meta & 15;
  if (lvl == 0) return 14.f / 16.f;     // source
  if (lvl >= 8) return 1.f;             // falling
  return std::max(0.1f, (8 - lvl) / 9.f);
}
} // namespace

void buildSectionMesh(const MeshInput& in, MeshOutput& out, const MesherOptions& opt) {
  out.opaque.clear(); out.cutout.clear(); out.trans.clear();
  if (in.empty) return;
  const Registry& R = reg();
  Ctx c{in, out, opt, R};
  for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    BlockId id = c.at(x, y, z);
    if (!id) continue;
    const BlockDef& b = R.block(id);
    u8 meta = in.meta[MeshInput::idx(x, y, z)];
    u32 tint = c.tint(b, x, z);
    u8 emis = R.tLight[id] ? Mat::Emissive : 0;
    switch (b.render) {
      case Render::Cube:
      case Render::Cutout: {
        bool cut = b.render == Render::Cutout;
        bool leaves = R.tLeaves[id];
        bool trans = R.tTrans[id];
        auto& dst = trans ? out.trans : cut ? out.cutout : out.opaque;
        u8 mat = emis | (cut ? Mat::Cutout : 0) | (trans ? Mat::Translucent : 0);
        for (int f = 0; f < 6; f++) {
          const FaceDef& F = FACES[f];
          BlockId nb = c.at(x + F.n.x, y + F.n.y, z + F.n.z);
          if (R.tOpaque[nb]) continue;
          if (cut && nb == id && !(leaves && opt.fancyLeaves)) continue;   // glass next to glass, fast leaves
          if (trans && nb == id) continue;
          cubeFace(c, dst, x, y, z, f, b.faces[f], tint, mat, leaves && opt.wavingPlants ? WaveKind::Leaves : 0);
        }
        break;
      }
      case Render::Cross: {
        u8 l = c.lightAt(x, y, z);
        crossQuads(c, out.cutout, x, y, z, b.faces[0], tint, l, opt.wavingPlants ? WaveKind::Plant : 0, (float)((in.cx * 16 + x) * 31 + (in.cz * 16 + z) * 17 % 97) * 0.0137f);
        break;
      }
      case Render::Liquid: {
        bool lava = R.tLiquid[id] == 2;
        u8 mat = Mat::Liquid | (lava ? Mat::Lava | Mat::Emissive : Mat::Translucent);
        auto& dst = lava ? out.opaque : out.trans;
        float hTop = (c.at(x, y + 1, z) == id || R.tLiquid[c.at(x, y + 1, z)] == R.tLiquid[id]) ? 1.f : liquidHeight(meta);
        u32 wt = lava ? 0xFFFFFF : 0xE0A03C;   // water tint (BGR) — the shader tints by biome later
        for (int f = 0; f < 6; f++) {
          const FaceDef& F = FACES[f];
          BlockId nb = c.at(x + F.n.x, y + F.n.y, z + F.n.z);
          if (R.tLiquid[nb] == R.tLiquid[id]) continue;
          if (R.tOpaque[nb] && f != 2) continue;
          if (f == 2 && R.tOpaque[nb]) continue;
          u8 l = c.lightAt(x + F.n.x, y + F.n.y, z + F.n.z);
          for (int k = 0; k < 4; k++) {
            vec3 v = vec3(F.v[k]);
            if (v.y > 0) v.y = hTop;
            pushVert(dst, vec3(x, y, z) + v, b.faces[f], F.uv[k][0], F.uv[k][1], f, 3, l, wt, f == 2 ? WaveKind::Liquid : 0, mat | Mat::NoRelief);
          }
        }
        break;
      }
      case Render::Torch: {
        std::array<u16, 6> L; L.fill(b.faces[0]);
        boxFaces(c, out.cutout, x, y, z, vec3(7 / 16.f, 0, 7 / 16.f), vec3(9 / 16.f, 10 / 16.f, 9 / 16.f), L, 0xFFFFFF, Mat::Emissive | Mat::NoRelief, c.lightAt(x, y, z));
        break;
      }
      case Render::None: break;
      default: {
        // Shapes and models: placeholder boxes until render/shapes.cpp provides the original geometry
        float h = 1;
        switch (b.shapeKind) {
          case ShapeKind::Slab: h = (meta & 1) ? 1 : 0.5f; break;
          case ShapeKind::Carpet: h = 1 / 16.f; break;
          case ShapeKind::Layer: h = ((meta & 7) + 1) / 8.f; break;
          case ShapeKind::Path: h = 15 / 16.f; break;
          default: break;
        }
        vec3 lo(0), hi(1, h, 1);
        if (b.shapeKind == ShapeKind::Fence || b.shapeKind == ShapeKind::Wall) { lo = vec3(6 / 16.f, 0, 6 / 16.f); hi = vec3(10 / 16.f, 1, 10 / 16.f); }
        else if (b.shapeKind == ShapeKind::Pane || b.shapeKind == ShapeKind::Rod) { lo = vec3(7 / 16.f, 0, 7 / 16.f); hi = vec3(9 / 16.f, 1, 9 / 16.f); }
        boxFaces(c, b.opaque ? out.opaque : out.cutout, x, y, z, lo, hi, b.faces, tint, emis | Mat::Cutout, c.lightAt(x, y + (h < 1 ? 0 : 1), z));
      }
    }
  }
}

} // namespace dq
