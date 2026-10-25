// render/mesher.cpp — the section mesher: gathering, climate tints, cubes (with greedy merging of
// flat, evenly lit faces), liquids, torches and the per-section loop. Port of world/mesher.js.
#include "render/mesh_ctx.h"
#include "render/chest_layer.h"
#include "assets/tileset.h"
#include "common/noise.h"
#include <cstring>
#include <mutex>

namespace dq {

// ---------------------------------------------------------------- tables (original FACES)
const FaceDef FACES[6] = {
  {{1, 0, 0}, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, 0.8f},
  {{-1, 0, 0}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, 0.8f},
  {{0, 1, 0}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, 1.0f},
  {{0, -1, 0}, {{1, 0, 1}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0}}, 0.52f},
  {{0, 0, 1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, 0.64f},
  {{0, 0, -1}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, 0.64f},
};
const int FACE_UV[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
const float AO_CURVE[4] = {0.5f, 0.68f, 0.84f, 1.0f};
static const int FACING_FACE[4] = {4, 0, 5, 1};

// six-way blocks: turn each side's picture so its top edge points the way the block faces
static int F6_ROT[6][6];
static void initF6Rot() {
  static std::once_flag once;
  std::call_once(once, [] {
    for (int dir = 0; dir < 6; dir++) for (int f = 0; f < 6; f++) {
      int best = 0; float bv = -9;
      for (int r = 0; r < 4; r++) {
        float mx = 0, my = 0, mz = 0; int n = 0;
        for (int k = 0; k < 4; k++) if (FACE_UV[(k + r) & 3][1] == 0) { mx += FACES[f].c[k][0] - 0.5f; my += FACES[f].c[k][1] - 0.5f; mz += FACES[f].c[k][2] - 0.5f; n++; }
        float v = (mx * D6X[dir] + my * D6Y[dir] + mz * D6Z[dir]) / n;
        if (v > bv + 1e-6f) { bv = v; best = r; }
      }
      F6_ROT[dir][f] = best;
    }
  });
}

// ---------------------------------------------------------------- tile snapshot for the plant builder
static std::mutex g_tilesMx;
static std::shared_ptr<const MeshTiles> g_tiles;
static u64 g_tilesGen = 0;
void setMeshTiles(const TileSet& ts) {
  auto t = std::make_shared<MeshTiles>();
  t->size = ts.size; t->count = ts.count; t->rgba = ts.rgba;
  std::lock_guard<std::mutex> lk(g_tilesMx);
  t->gen = ++g_tilesGen;
  g_tiles = t;
}
u64 meshTilesGeneration() { std::lock_guard<std::mutex> lk(g_tilesMx); return g_tilesGen; }
std::shared_ptr<const MeshTiles> meshTilesSnapshot() { std::lock_guard<std::mutex> lk(g_tilesMx); return g_tiles; }

// ---------------------------------------------------------------- climate colours
static const float COL_COLD[3] = {128, 168, 140}, COL_TEMP[3] = {124, 191, 74}, COL_LUSH[3] = {80, 160, 56}, COL_DRY[3] = {190, 178, 92};
static const float WCOL_COLD[3] = {70, 104, 176}, WCOL_TEMP[3] = {52, 104, 214}, WCOL_WARM[3] = {44, 150, 180};
static void grassF(u8 tb, u8 hb, float o[3]) {
  float t = (tb - 128) / 127.f, h = (hb - 128) / 127.f;
  float wt = smoothstepf(-0.55f, 0.25f, t), wh = smoothstepf(-0.3f, 0.45f, h), hot = smoothstepf(0.2f, 0.6f, t);
  for (int i = 0; i < 3; i++) {
    float c = lerpf(COL_COLD[i], COL_TEMP[i], wt);
    c = lerpf(c, COL_LUSH[i], wh * wt * 0.85f);
    c = lerpf(c, COL_DRY[i], hot * (1 - wh));
    o[i] = c;
  }
}
static void waterF(u8 tb, u8 hb, float o[3]) {
  float t = (tb - 128) / 127.f;
  for (int i = 0; i < 3; i++) o[i] = t < 0 ? lerpf(WCOL_TEMP[i], WCOL_COLD[i], smoothstepf(0, -0.6f, t)) : lerpf(WCOL_TEMP[i], WCOL_WARM[i], smoothstepf(0.1f, 0.6f, t));
}
static u32 pack3(const float c[3]) { return (u32)clampv((int)std::lround(c[0]), 0, 255) | (u32)clampv((int)std::lround(c[1]), 0, 255) << 8 | (u32)clampv((int)std::lround(c[2]), 0, 255) << 16; }
// a resource pack's colormap (grass.png / foliage.png) wins over the original's climate colours
static bool packTintF(int kind, u8 t, u8 h, float o[3]) {
  if (kind == 2) return false;
  bool ok = false; u32 c = packTint(kind == 1 ? "foliage" : "grass", t, h, ok);
  if (!ok) return false;
  o[0] = (float)(c & 255); o[1] = (float)((c >> 8) & 255); o[2] = (float)((c >> 16) & 255);
  return true;
}
u32 climateGrass(u8 t, u8 h) { float c[3]; if (!packTintF(0, t, h, c)) grassF(t, h, c); return pack3(c); }
u32 climateWater(u8 t, u8 h) { float c[3]; waterF(t, h, c); return pack3(c); }
u32 climateTint(const char* kind, u8 t, u8 h) {
  float c[3];
  bool fol = kind && kind[0] == 'f';
  if (kind && kind[0] == 'w') { waterF(t, h, c); return pack3(c); }
  if (packTintF(fol ? 1 : 0, t, h, c)) return pack3(c);
  grassF(t, h, c);
  if (fol) { c[0] *= 0.82f; c[1] *= 0.86f; c[2] *= 0.8f; }
  return pack3(c);
}

// ---------------------------------------------------------------- context
MeshCtx::MeshCtx(const MeshInput& in, MeshOutput& out, const MesherOptions& opt, const Registry& R, const MeshTiles* t)
  : in(in), out(out), opt(opt), R(R), tiles(t) {
  wx0 = in.cx * 16; wy0 = dimBase(in.dim) + in.sy * 16; wz0 = in.cz * 16;
  std::memset(tintSeen, 0, sizeof tintSeen);
}
u32 MeshCtx::cornerTint(float x, float z, int kind) {
  int key = kind * 361 + (clampv(ifloor(z), -1, 17) + 1) * 19 + clampv(ifloor(x), -1, 17) + 1;
  if (tintSeen[key]) return tintCache[key];
  float acc[3] = {0, 0, 0}, c1[3]; bool packed = false;
  for (int dz = -1; dz <= 0; dz++) for (int dx = -1; dx <= 0; dx++) {
    int cx = clampv(ifloor(x + dx), -1, 16), cz = clampv(ifloor(z + dz), -1, 16);
    const u8* cl = &in.climate[((cz + 1) * MeshInput::N + cx + 1) * 2];
    if (kind == 2) waterF(cl[0], cl[1], c1); else if (packTintF(kind, cl[0], cl[1], c1)) packed = true; else grassF(cl[0], cl[1], c1);
    acc[0] += c1[0]; acc[1] += c1[1]; acc[2] += c1[2];
  }
  bool fol = kind == 1 && !packed; float k = fol ? 0.2f : 0.25f;
  float o[3] = {acc[0] * k * (fol ? 0.82f : 1), acc[1] * k * (fol ? 0.86f : 1), acc[2] * k * (fol ? 0.8f : 1)};
  tintSeen[key] = 1;
  return tintCache[key] = pack3(o);
}
void MeshCtx::put(std::vector<ChunkVertex>& d, const MV& v, int tile, int normal, u8 mat) const {
  ChunkVertex cv;
  cv.x = (i16)std::lround(v.p.x * 256); cv.y = (i16)std::lround(v.p.y * 256); cv.z = (i16)std::lround(v.p.z * 256);
  cv.tile = (u16)((tile & 0xFFF) | (normal << 12));
  cv.u = (u16)clampv((int)std::lround((v.uv.x + 64) * 256), 0, 65535); cv.v = (u16)clampv((int)std::lround((v.uv.y + 64) * 256), 0, 65535);
  cv.sway = opt.wavingPlants ? v.sway : ((v.sway >= 32 && v.sway < 48) || v.sway >= 80 ? v.sway : 0);   // water and the grass tufts' fade mark stay
  cv.sky = (u8)clampv((int)std::lround(v.sky * 17), 0, 255); cv.blk = (u8)clampv((int)std::lround(v.blk * 17), 0, 255);
  cv.shade = (u8)clampv((int)std::lround(v.shade * 255), 0, 255);
  cv.r = tint & 255; cv.g = (tint >> 8) & 255; cv.b = (tint >> 16) & 255;
  cv.mat = mat | extraMat;
  d.push_back(cv);
}
void MeshCtx::quad(std::vector<ChunkVertex>& d, const MV v[4], int tile, int normal, u8 mat, bool flip) const {
  if (flip) { put(d, v[1], tile, normal, mat); put(d, v[2], tile, normal, mat); put(d, v[3], tile, normal, mat); put(d, v[0], tile, normal, mat); }
  else for (int k = 0; k < 4; k++) put(d, v[k], tile, normal, mat);
}
void MeshCtx::quad2(std::vector<ChunkVertex>& d, const MV v[4], int tile, int normal, u8 mat) const {
  quad(d, v, tile, normal, mat);
  MV r[4] = {v[1], v[0], v[3], v[2]};
  quad(d, r, tile, normal, mat);
}

void faceCorners(const MeshCtx& c, int x, int y, int z, int f, FaceCorners& fc) {
  const FaceDef& F = FACES[f];
  int fx = x + F.n[0], fy = y + F.n[1], fz = z + F.n[2];
  for (int k = 0; k < 4; k++) {
    int e1[3] = {0, 0, 0}, e2[3] = {0, 0, 0}, ax[2], n = 0;
    for (int a = 0; a < 3; a++) if (!F.n[a]) ax[n++] = a;
    e1[ax[0]] = F.c[k][ax[0]] ? 1 : -1; e2[ax[1]] = F.c[k][ax[1]] ? 1 : -1;
    bool s1 = c.opaque(fx + e1[0], fy + e1[1], fz + e1[2]), s2 = c.opaque(fx + e2[0], fy + e2[1], fz + e2[2]);
    bool cc = c.opaque(fx + e1[0] + e2[0], fy + e1[1] + e2[1], fz + e1[2] + e2[2]);
    int a = (s1 && s2) ? 0 : 3 - (s1 + s2 + cc);
    float sk = c.skyAt(fx, fy, fz), bl = c.blAt(fx, fy, fz); int cnt = 1;
    if (!s1) { sk += c.skyAt(fx + e1[0], fy + e1[1], fz + e1[2]); bl += c.blAt(fx + e1[0], fy + e1[1], fz + e1[2]); cnt++; }
    if (!s2) { sk += c.skyAt(fx + e2[0], fy + e2[1], fz + e2[2]); bl += c.blAt(fx + e2[0], fy + e2[1], fz + e2[2]); cnt++; }
    if (!cc && !(s1 && s2)) { sk += c.skyAt(fx + e1[0] + e2[0], fy + e1[1] + e2[1], fz + e1[2] + e2[2]); bl += c.blAt(fx + e1[0] + e2[0], fy + e1[1] + e2[1], fz + e1[2] + e2[2]); cnt++; }
    fc.sky[k] = sk / cnt; fc.bl[k] = bl / cnt; fc.ao[k] = AO_CURVE[a]; fc.aoi[k] = a;
  }
}

// ---------------------------------------------------------------- gathering (main thread)
bool gatherMeshInput(const World& w, const SectionKey& k, MeshInput& in) {
  constexpr int N = MeshInput::N;
  Chunk* center = w.chunk(k.dim, k.cx, k.cz);
  if (!center) return false;
  in.dim = k.dim; in.cx = k.cx; in.sy = k.sy; in.cz = k.cz;
  in.potPlants.clear();
  const Section* cs = center->section(k.sy);
  in.empty = !cs || cs->solidCount == 0;
  if (in.empty) return true;
  std::memset(in.climate, 128, sizeof in.climate);
  const Registry& R = reg();
  int baseY = dimBase(k.dim) + k.sy * 16;
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    Chunk* c = w.chunk(k.dim, k.cx + dx, k.cz + dz);
    Chunk* other[DIM_COUNT] = {};
    bool otherDone[DIM_COUNT] = {};
    int x0 = dx < 0 ? 15 : 0, x1 = dx > 0 ? 0 : 15, z0 = dz < 0 ? 15 : 0, z1 = dz > 0 ? 0 : 15;
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
      int ox = x + dx * 16, oz = z + dz * 16;   // -1..16
      if (c) { in.climate[((oz + 1) * N + (ox + 1)) * 2] = c->climate[(z * 16 + x) * 2]; in.climate[((oz + 1) * N + (ox + 1)) * 2 + 1] = c->climate[(z * 16 + x) * 2 + 1]; }
      for (int y = MeshInput::Y0; y <= 16; y++) {
        int gy = baseY + y, i = MeshInput::idx(ox, y, oz);
        in.meta[i] = 0;
        if (!inWorldY(gy)) { in.ids[i] = AIR; in.light[i] = gy < 0 ? 0 : 0xF0; continue; }
        int d = gy >> 8;
        const Chunk* vc = c;
        if (d != k.dim) { if (!otherDone[d]) { other[d] = w.chunk(d, k.cx + dx, k.cz + dz); otherDone[d] = true; } vc = other[d]; }
        if (!vc) { in.ids[i] = AIR; in.light[i] = 0xF0; continue; }
        int ly = gy & 255;
        const Section* s = vc->section(ly >> 4);
        if (!s) { in.ids[i] = AIR; in.light[i] = vc->airLight[ly >> 4]; continue; }
        int si = secIdx(x, ly & 15, z);
        in.ids[i] = s->blocks[si]; in.meta[i] = s->meta[si]; in.light[i] = s->light[si];
        if (dx == 0 && dz == 0 && y >= 0 && y < 16 && s->blocks[si] && traits(s->blocks[si]).pot) {
          auto it = vc->blockEntities.find(colIdx(x, ly, z));
          if (it != vc->blockEntities.end() && it->second.is_object() && it->second.contains("plant") && it->second["plant"].is_string()) {
            BlockId p = R.blockId(it->second["plant"].get<std::string>());
            if (p) in.potPlants.push_back({i, p});
          }
        }
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------- cubes
namespace {
struct FaceKey { u64 a = 0; u32 b = 0; bool operator==(const FaceKey& o) const { return a == o.a && b == o.b; } };
struct GreedyMasks {
  // [face][slice][b][a]: a/b are the face plane's axes
  FaceKey m[6][16][16][16];
  u8 pass[6][16][16][16];
  bool any[6] = {};
};
// plane axes per face direction: axis along the normal, then (a, b)
const int FAXIS[6] = {0, 0, 1, 1, 2, 2}, FA[6] = {2, 2, 0, 0, 0, 0}, FB[6] = {1, 1, 2, 2, 1, 1};

vec2 faceUV(int f, vec3 p) {
  switch (f) {
    case 0: return {16 - p.z, 16 - p.y};
    case 1: return {p.z, 16 - p.y};
    case 2: return {p.x, p.z};
    case 3: return {16 - p.x, p.z};
    case 4: return {p.x, 16 - p.y};
    default: return {16 - p.x, 16 - p.y};
  }
}

void emitCube(MeshCtx& c, GreedyMasks* gm, int pass, std::vector<ChunkVertex>& dst, BlockId id, int x, int y, int z, u8 meta) {
  const Registry& R = c.R;
  const BlockDef& self = R.block(id);
  const BlockTraits& T = traits(id);
  const auto& faces = self.faces;
  int frontFace = T.hasFront ? FACING_FACE[meta & 3] : -1;
  int axis = T.axis ? std::min(2, meta & 3) : 0;
  int dir6 = T.f6 ? std::min(5, meta & 7) : 0;
  u8 sway = T.leaves ? Sway::Leaves : 0;
  int tintKind = T.tint == 2 ? 1 : 0;
  u8 mat = 0;
  if (self.render == Render::Cutout) mat |= Mat::Cutout | Mat::NoRelief;
  if (R.tTrans[id]) mat |= Mat::Translucent | Mat::NoRelief;
  static const int AXIS_END[3][2] = {{2, 3}, {0, 1}, {4, 5}};
  for (int f = 0; f < 6; f++) {
    const FaceDef& F = FACES[f];
    BlockId nb = c.at(x + F.n[0], y + F.n[1], z + F.n[2]);
    if (R.tOpaque[nb]) continue;
    if (nb == id && (T.selfCull || (!c.opt.fancyLeaves && T.leaves))) continue;
    int tile = faces[f], rot = 0;
    if (T.hasFront && f != 2 && f != 3) tile = f == frontFace ? faces[4] : faces[0];
    if (axis) {
      tile = (f == AXIS_END[axis][0] || f == AXIS_END[axis][1]) ? faces[2] : faces[0];
      rot = (axis == 1 && f >= 2) || (axis == 2 && f <= 1) ? 1 : 0;
    }
    if (T.f6) {
      if (f == D6_FACE[dir6]) tile = dir6 < 2 ? T.f6t[0] : T.f6t[1];
      else if (f == D6_FACE[OPP6[dir6]]) tile = (meta & 8) ? T.f6t[3] : T.f6t[2];
      else { tile = !T.sideAll && dir6 >= 2 && (f == 2 || f == 3) ? T.f6t[4] : T.f6t[5]; rot = F6_ROT[dir6][f]; }
    }
    FaceCorners fc; faceCorners(c, x, y, z, f, fc);
    u32 tints[4];
    for (int k = 0; k < 4; k++) tints[k] = T.tint ? c.cornerTint((float)(x + F.c[k][0]), (float)(z + F.c[k][2]), tintKind) : 0xFFFFFF;
    bool uniform = rot == 0 && sway == 0;
    for (int k = 1; k < 4 && uniform; k++) uniform = fc.aoi[k] == fc.aoi[0] && fc.sky[k] == fc.sky[0] && fc.bl[k] == fc.bl[0] && tints[k] == tints[0];
    if (gm && uniform) {
      FaceKey key;
      u8 sk = (u8)std::lround(fc.sky[0] * 17), bl = (u8)std::lround(fc.bl[0] * 17), sh = (u8)std::lround(F.shade * fc.ao[0] * 255);
      key.a = (u64)(tile & 0xFFFF) | (u64)sk << 16 | (u64)bl << 24 | (u64)sh << 32 | (u64)mat << 40 | (u64)1 << 63;
      key.b = tints[0];
      int pos[3] = {x, y, z};
      int s = pos[FAXIS[f]], a = pos[FA[f]], b = pos[FB[f]];
      gm->m[f][s][b][a] = key; gm->pass[f][s][b][a] = (u8)pass; gm->any[f] = true;
      continue;
    }
    MV v[4];
    for (int k = 0; k < 4; k++) {
      vec3 p(x + F.c[k][0], y + F.c[k][1], z + F.c[k][2]);
      vec2 uv = rot == 0 ? faceUV(f, p) : vec2(FACE_UV[(k + rot) & 3][0], FACE_UV[(k + rot) & 3][1]);
      v[k] = {p, uv, fc.sky[k], fc.bl[k], F.shade * fc.ao[k], sway};
    }
    c.tint = tints[0];
    // per-corner tints: emit with each vertex's own colour
    if (T.tint) {
      bool flip = fc.aoi[0] + fc.aoi[2] < fc.aoi[1] + fc.aoi[3];
      int order[4] = {0, 1, 2, 3}; if (flip) { order[0] = 1; order[1] = 2; order[2] = 3; order[3] = 0; }
      for (int k = 0; k < 4; k++) { c.tint = tints[order[k]]; c.put(dst, v[order[k]], tile, f, mat); }
    } else c.quad(dst, v, tile, f, mat, fc.aoi[0] + fc.aoi[2] < fc.aoi[1] + fc.aoi[3]);
    c.tint = 0xFFFFFF;
  }
}

void flushGreedy(MeshCtx& c, GreedyMasks& gm) {
  std::vector<ChunkVertex>* passes[3] = {&c.out.opaque, &c.out.cutout, &c.out.trans};
  for (int f = 0; f < 6; f++) {
    if (!gm.any[f]) continue;
    const FaceDef& F = FACES[f];
    for (int s = 0; s < 16; s++) {
      auto& M = gm.m[f][s];
      for (int b = 0; b < 16; b++) for (int a = 0; a < 16;) {
        FaceKey k = M[b][a];
        if (!(k.a >> 63)) { a++; continue; }
        int w = 1; while (a + w < 16 && M[b][a + w] == k) w++;
        int h = 1;
        for (; b + h < 16; h++) { bool ok = true; for (int i = 0; i < w && ok; i++) ok = M[b + h][a + i] == k; if (!ok) break; }
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) M[b + j][a + i] = FaceKey();
        int tile = (int)(k.a & 0xFFFF);
        float sk = ((k.a >> 16) & 255) / 17.f, bl = ((k.a >> 24) & 255) / 17.f, sh = ((k.a >> 32) & 255) / 255.f;
        u8 mat = (u8)((k.a >> 40) & 255);
        vec3 lo, hi;
        lo[FAXIS[f]] = (float)s; hi[FAXIS[f]] = (float)s + 1;
        lo[FA[f]] = (float)a; hi[FA[f]] = (float)(a + w);
        lo[FB[f]] = (float)b; hi[FB[f]] = (float)(b + h);
        MV v[4];
        for (int q = 0; q < 4; q++) {
          vec3 p(F.c[q][0] ? hi.x : lo.x, F.c[q][1] ? hi.y : lo.y, F.c[q][2] ? hi.z : lo.z);
          v[q] = {p, faceUV(f, p), sk, bl, sh, 0};
        }
        c.tint = k.b;
        c.quad(*passes[gm.pass[f][s][b][a]], v, tile, f, mat);
        c.tint = 0xFFFFFF;
        a += w;
      }
    }
  }
}

// ---------------------------------------------------------------- liquids
float liquidHeight(const Registry& R, BlockId id, u8 meta) {
  if (R.tPortal[id]) return 1;
  int lvl = meta & 7;
  if (meta & 8) return 0.9f;
  return std::max(0.12f, 0.875f - lvl * 0.105f);
}
float liqH(const MeshCtx& c, int x, int y, int z, BlockId id, bool water) {
  BlockId b = c.at(x, y, z);
  if (water ? !c.water(x, y, z) : b != id) return -1;
  if (!c.R.tLiquid[b]) return 0.875f;   // a waterlogged block holds a full source
  return liquidHeight(c.R, b, c.metaAt(x, y, z));
}
float cornerH(const MeshCtx& c, int x, int y, int z, BlockId id, bool water, int dx, int dz) {
  float tot = 0, wsum = 0;
  for (int k = 0; k < 4; k++) {
    int cx = x + dx - 1 + (k & 1), cz = z + dz - 1 + (k >> 1);
    float h = liqH(c, cx, y, cz, id, water);
    if (h >= 0) {
      if (liqH(c, cx, y + 1, cz, id, water) >= 0) return 1;
      if (h >= 0.8f) { tot += h * 10; wsum += 10; } else { tot += h; wsum += 1; }
    } else if (!c.R.tSolid[c.at(cx, y, cz)]) wsum += 1;
  }
  return wsum ? tot / wsum : 0.875f;
}
bool flatPool(const MeshCtx& c, int x, int y, int z, BlockId id, bool water) {
  static const int NB8[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
  for (auto& o : NB8) {
    int nx = x + o[0], nz = z + o[1]; BlockId b = c.at(nx, y, nz);
    if (water ? !c.water(nx, y, nz) : b != id) return false;
    if (c.R.tLiquid[b] && (c.metaAt(nx, y, nz) & 15) != 0) return false;
    if (water ? c.water(nx, y + 1, nz) : c.at(nx, y + 1, nz) == id) return false;
  }
  return true;
}
int waterOpen(const MeshCtx& c, int x, int y, int z, int cx, int cz) {
  int tot = 0;
  for (int dz = cz - 1; dz <= cz; dz++) for (int dx = cx - 1; dx <= cx; dx++) {
    int px = x + dx, pz = z + dz, py = y;
    if (!c.water(px, py, pz)) continue;
    int d = 1;
    while (d < 12 && py - 1 >= MeshInput::Y0 && c.water(px, py - 1, pz)) { d++; py--; }
    tot += d;
  }
  return std::min(15, (int)std::lround(tot / 4.0 * 1.25));
}
// how much shore there is at a corner of the water's surface (foam)
float shoreAt(const MeshCtx& c, int x, int y, int z, int cx, int cz) {
  int n = 0;
  for (int dz = cz - 1; dz <= cz; dz++) for (int dx = cx - 1; dx <= cx; dx++) {
    int px = x + dx, pz = z + dz; BlockId b = c.at(px, y, pz);
    if (!c.water(px, y, pz) && (c.R.tSolid[b] || c.R.tOpaque[b])) n++;
  }
  return std::min(1.f, n * 0.5f);
}

void emitLiquid(MeshCtx& c, std::vector<ChunkVertex>& dst, BlockId id, int x, int y, int z, u8 meta) {
  const Registry& R = c.R;
  bool water = R.tLiquid[id] == 1;
  bool isPortal = R.tPortal[id];
  int stillT = R.block(id).faces[0], flowT = stillT;
  if (water) { int t = R.tile("water_flow"); if (t >= 0) flowT = t; }
  else if (R.tLiquid[id] == 2) { int t = R.tile("lava_flow"); if (t >= 0) flowT = t; }
  BlockId above = c.at(x, y + 1, z);
  bool full = isPortal || (water ? c.water(x, y + 1, z) : above == id);
  float ch[4];
  if (full) ch[0] = ch[1] = ch[2] = ch[3] = 1;
  else if ((meta & 15) == 0 && flatPool(c, x, y, z, id, water)) ch[0] = ch[1] = ch[2] = ch[3] = 0.875f;
  else for (int k = 0; k < 4; k++) ch[k] = cornerH(c, x, y, z, id, water, k & 1, k >> 1);
  // which way it runs: from the high corners to the low ones
  float fx = (ch[0] + ch[2]) - (ch[1] + ch[3]), fz = (ch[0] + ch[1]) - (ch[2] + ch[3]);
  bool runs = !full && !isPortal && (std::fabs(fx) + std::fabs(fz) > 0.03f);
  float cosA = 1, sinA = 0, scl = 1;
  if (runs) {
    float a = std::round(std::atan2(fx, fz) / (PI / 4)) * (PI / 4);
    cosA = std::round(std::cos(a) * 1e6f) / 1e6f; sinA = std::round(std::sin(a) * 1e6f) / 1e6f;
    scl = std::fabs(cosA) > 0.01f && std::fabs(sinA) > 0.01f ? 0.7071f : 1;
  }
  bool falling = water && (meta & 8);
  u8 base = Mat::NoRelief | (isPortal ? Mat::Translucent : R.tLiquid[id] == 2 ? (Mat::Liquid | Mat::Lava) : (Mat::Liquid | Mat::Translucent));
  if (water) { c.tint = c.cornerTint((float)x + 1, (float)z + 1, 2); c.extraMat = Mat::TintAll; }
  for (int f = 0; f < 6; f++) {
    const FaceDef& F = FACES[f];
    int ox = x + F.n[0], oy = y + F.n[1], oz = z + F.n[2];
    BlockId nb = c.at(ox, oy, oz);
    if (nb == id && !water) continue;
    if (water && c.water(ox, oy, oz)) continue;
    if (R.tOpaque[nb] && !(f == 2 && !full)) continue;
    if (!isPortal && R.tLiquid[nb] && f != 2) continue;
    float sk = R.tOpaque[nb] ? c.skyAt(x, y, z) : c.skyAt(ox, oy, oz);
    float bl = std::max<float>(R.tOpaque[nb] ? c.blAt(x, y, z) : c.blAt(ox, oy, oz), R.tLight[id]);
    bool top = f == 2, side = f != 2 && f != 3;
    int tile = isPortal ? stillT : top ? (runs ? flowT : stillT) : side ? flowT : stillT;
    u8 mat = base | (((top && runs) || (side && falling)) ? Mat::Flowing : 0);
    MV v[4];
    for (int k = 0; k < 4; k++) {
      const int* cc = F.c[k]; const int* t = FACE_UV[k];
      float hc = cc[1] ? ch[cc[0] + cc[2] * 2] : 0;
      u8 sway = water && cc[1] && hc < 1 ? (u8)(32 + waterOpen(c, x, y, z, cc[0], cc[2])) : 0;
      float u, vv;
      if (top) {
        // turn the picture so its ripples run downstream
        float ox2 = cc[0] - 0.5f, oz2 = cc[2] - 0.5f;
        u = (cosA * ox2 - sinA * oz2) * scl + 0.5f; vv = (sinA * ox2 + cosA * oz2) * scl + 0.5f;
      } else if (side) { u = (float)t[0]; vv = cc[1] ? (1 - hc) : 1.f; }
      else { u = (float)t[0]; vv = (float)t[1]; }
      float shade = top && !isPortal ? (water ? shoreAt(c, x, y, z, cc[0], cc[2]) : 0.f) : F.shade;
      v[k] = {vec3(x + cc[0], y + hc, z + cc[2]), vec2(u, vv), sk, bl, shade, sway};
    }
    c.quad(dst, v, tile, f, mat);
    if (top || isPortal) { MV r[4] = {v[1], v[0], v[3], v[2]}; c.quad(dst, r, tile, f, mat); }   // visible from below / inside
  }
  c.tint = 0xFFFFFF; c.extraMat = 0;
}

// ---------------------------------------------------------------- torches
void emitTorch(MeshCtx& c, std::vector<ChunkVertex>& dst, BlockId id, int x, int y, int z, u8 meta) {
  int tile = c.R.block(id).faces[0];
  const float P = 1 / 16.f, hgt = 10 * P, w = P, LEAN = 0.4f;
  int m = meta & 7;
  float wx = 0, wz = 0;
  if (m == 1) wx = 1; else if (m == 2) wx = -1; else if (m == 3) wz = 1; else if (m == 4) wz = -1;
  float bx = x + 0.5f, by = (float)y, bz = z + 0.5f;
  if (m) {
    MeshShapeNb nb(c, x, y, z);
    float back = torchSetBack((u8)m, nb);
    bx = x + 0.5f - wx * (0.5f + back - 1.6f * P); bz = z + 0.5f - wz * (0.5f + back - 1.6f * P); by = y + 3.5f * P;
  } else if (c.R.tShape[c.at(x, y - 1, z)] == ShapeKind::Slab && (c.metaAt(x, y - 1, z) & 7) == 0) by -= 0.5f;
  float ca = std::cos(m ? LEAN : 0.f), sa = std::sin(m ? LEAN : 0.f);
  auto V = [&](float lx, float ly, float lz) { return vec3(bx + lx + wx * ly * sa, by + ly * ca, bz + lz + wz * ly * sa); };
  float sk = c.skyAt(x, y, z), bl = (float)c.R.tLight[id];
  float u0 = 7 / 16.f, u1 = 9 / 16.f, v0 = 6 / 16.f, v1 = 1;
  vec3 C[8] = {V(-w, 0, -w), V(w, 0, -w), V(w, 0, w), V(-w, 0, w), V(-w, hgt, -w), V(w, hgt, -w), V(w, hgt, w), V(-w, hgt, w)};
  u8 mat = Mat::Cutout | Mat::NoRelief;
  auto q = [&](int a, int b, int cc, int d, float ua, float ub, float va, float vb, float sh) {
    MV v[4] = {{C[a], {ua, vb}, sk, bl, sh, 0}, {C[b], {ub, vb}, sk, bl, sh, 0}, {C[cc], {ub, va}, sk, bl, sh, 0}, {C[d], {ua, va}, sk, bl, sh, 0}};
    c.quad(dst, v, tile, NORMAL_NONE, mat);
  };
  q(2, 1, 5, 6, u0, u1, v0, v1, 220 / 255.f);
  q(0, 3, 7, 4, u0, u1, v0, v1, 220 / 255.f);
  q(3, 2, 6, 7, u0, u1, v0, v1, 200 / 255.f);
  q(1, 0, 4, 5, u0, u1, v0, v1, 200 / 255.f);
  q(7, 6, 5, 4, u0, u1, 6 / 16.f, 8 / 16.f, 1.f);
  q(0, 1, 2, 3, u0, u1, 14 / 16.f, 1.f, 160 / 255.f);
}
} // namespace

// ---------------------------------------------------------------- the section (worker threads)
void buildSectionMesh(const MeshInput& in, MeshOutput& out, const MesherOptions& opt) {
  out.opaque.clear(); out.cutout.clear(); out.trans.clear(); out.fx = MeshFx(); out.detail = opt.detail;
  if (in.empty) return;
  double t0 = nowSeconds();
  initF6Rot();
  const Registry& R = reg();
  auto tiles = meshTilesSnapshot();
  MeshCtx c(in, out, opt, R, tiles.get());
  // the masks are left clean by flushGreedy (every entry it reads it clears), so one per worker will do
  thread_local std::unique_ptr<GreedyMasks> tlMasks;
  GreedyMasks* gm = nullptr;
  if (opt.greedy) { if (!tlMasks) tlMasks = std::make_unique<GreedyMasks>(); gm = tlMasks.get(); for (bool& a : gm->any) a = false; }
  BlockId waterId = R.blockId("water"), endPool = R.blockId("end_portal"), grassBlock = R.blockId("grass_block");
  bool tufts = opt.detail && meshGrassTufts() > 0 && grassBlock;
  for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    BlockId id = c.at(x, y, z);
    if (!id) continue;
    // portal blocks (Ember, Skylands, the End's pool) are drawn by the portal layer (render/portal_view.cpp):
    // a live window to the far side, or the portal's own picture while it isn't one. Meshing their tile
    // here would lay the pack's swirl over the window (game/portalview.js draws the window instead of it).
    if (R.tPortal[id] || (id == endPool && endPool)) { if (traits(id).emitsParticles) out.fx.emitters.push_back({ivec3(c.wx0 + x, c.wy0 + y, c.wz0 + z), id}); continue; }
    u8 meta = c.metaAt(x, y, z);
    ivec3 wp(c.wx0 + x, c.wy0 + y, c.wz0 + z);
    Render rt = (Render)R.tRender[id];
    const BlockTraits& T = traits(id);
    switch (rt) {
      case Render::Cube: case Render::Cutout: {
        if (chestHiddenAt(wp.x, wp.y, wp.z)) break;   // the open chest is drawn by render/chest_layer.cpp
        if (c.opaque(x + 1, y, z) && c.opaque(x - 1, y, z) && c.opaque(x, y, z + 1) && c.opaque(x, y, z - 1) && c.opaque(x, y + 1, z) && c.opaque(x, y - 1, z)) break;
        int pass = R.tTrans[id] ? 2 : rt == Render::Cutout ? 1 : 0;
        emitCube(c, gm, pass, pass == 2 ? out.trans : pass == 1 ? out.cutout : out.opaque, id, x, y, z, meta);
        if (tufts && id == grassBlock && c.at(x, y + 1, z) == AIR) emitGrassTufts(c, out.cutout, id, x, y, z);
        break;
      }
      case Render::Cross: emitCross(c, out.cutout, id, x, y, z, meta); break;
      case Render::Liquid: {
        bool deep = R.tLiquid[id] == 1 && c.water(x, y + 1, z) && c.water(x + 1, y, z) && c.water(x - 1, y, z) && c.water(x, y, z + 1) && c.water(x, y, z - 1) && (c.water(x, y - 1, z) || c.opaque(x, y - 1, z));
        if (!deep) emitLiquid(c, R.tLiquid[id] == 2 ? out.opaque : out.trans, id, x, y, z, meta);
        if (R.tLiquid[id] == 1 && (meta & 8)) {
          bool belowFalls = c.water(x, y - 1, z) && (c.metaAt(x, y - 1, z) & 8) && R.tLiquid[c.at(x, y - 1, z)] == 1;
          if (!belowFalls && c.at(x, y - 1, z) != AIR) out.fx.waterfalls.push_back(wp);
        }
        break;
      }
      case Render::Torch: emitTorch(c, out.cutout, id, x, y, z, meta); break;
      case Render::Model: emitModel(c, out.cutout, id, x, y, z, meta); break;
      case Render::Shape: emitShapeBlock(c, R.tTrans[id] || isPuddle(R, id) ? out.trans : out.cutout, id, x, y, z, meta); break;
      default: break;
    }
    if ((meta & WLOG) && R.tWlog[id] && waterId) emitLiquid(c, out.trans, waterId, x, y, z, 0);
    if (T.emitsParticles) out.fx.emitters.push_back({wp, id});
    // where drips form: under a block with water or lava on it, and under leaves (rain comes through)
    if (c.at(x, y - 1, z) == AIR) {
      if (T.hive) { if ((meta & 0x0c) == 0x0c) out.fx.drips.push_back(ivec4(wp, 4)); }
      else if (R.tSolid[id] && R.tOpaque[id]) { int a = R.tLiquid[c.at(x, y + 1, z)]; if (a) out.fx.drips.push_back(ivec4(wp, a)); }
      else if (T.leaves) out.fx.drips.push_back(ivec4(wp, 3));
    }
  }
  if (gm) flushGreedy(c, *gm);
  out.ms = (nowSeconds() - t0) * 1000;
}

} // namespace dq
