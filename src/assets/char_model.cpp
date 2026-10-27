// assets/char_model.cpp — skins built into voxel bodies (render/skins.js buildSkinParts / addSkinVoxels), the
// gear look's shapes (render/gear3d.js gearMeshes), worn armour (skins.js armorMeshes, render/packarmor.js),
// capes (skins.js capeMesh / capeSegMesh) and the worn backpack (render/items3d.js buildPack3D).
#include "assets/char_model.h"
#include "assets/cape_tex.h"
#include "assets/voxel_paint.h"
#include "assets/pack_armor.h"
#include "assets/tileset.h"
#include "assets/pack_fs.h"
#include <list>

namespace dq {

const char* charPartName(int p) {
  static const char* N[CP_COUNT] = {"head", "body", "armR_u", "armR_d", "armL_u", "armL_d", "legR_u", "legR_d", "legL_u", "legL_d"};
  return p >= 0 && p < CP_COUNT ? N[p] : "";
}

namespace {
void upload(CharMesh& m, const VoxelMeshData& md, vec3 origin) { m.gl.upload(md); m.origin = origin; m.voxel = 1.f / CHAR_VPP; }

// the inner layer of one part, between model heights y0..y1 (a limb half), interior filled; round = bevelled edges
void buildBase(const SkinImage& s, const SkinPartDef& d, float y0, float y1, bool round, CharMesh& out, vec3 pivot) {
  const int V = CHAR_VPP;
  ivec3 n = d.sz * V;
  int j0 = (int)std::lround((y0 - d.mn.y) * V), j1 = (int)std::lround((y1 - d.mn.y) * V);
  VoxelGrid g(ivec3(n.x, j1 - j0, n.z));
  auto R = skinRects(d.uv.x, d.uv.y, d.sz.x, d.sz.y, d.sz.z);
  auto pix = [&](int f, int i, int jj, int k) -> u32 {
    ivec2 p = skinFacePixel(R, f, vec3((i + 0.5f) / n.x, (jj + 0.5f) / n.y, (k + 0.5f) / n.z));
    const u8* q = s.at(p.x, p.y);
    return voxRGBA(q[0], q[1], q[2]);   // the inner layer is solid: a clear pixel there is its colour
  };
  auto edge = [&](int i, int jj, int k) { return ((i == 0 || i == n.x - 1) ? 1 : 0) + ((jj == 0 || jj == n.y - 1) ? 1 : 0) + ((k == 0 || k == n.z - 1) ? 1 : 0) >= 2; };
  for (int k = 0; k < n.z; k++) for (int j = 0; j < j1 - j0; j++) for (int i = 0; i < n.x; i++) {
    int jj = j + j0;
    if (round && edge(i, jj, k)) continue;
    int dist[6] = {n.x - 1 - i, i, n.y - 1 - jj, jj, n.z - 1 - k, k};
    int f = 5; for (int q : {4, 0, 1, 2, 3}) if (dist[q] < dist[f]) f = q;
    g.set(i, j, k, pix(f, i, jj, k));
  }
  VoxelFaceColor fc = [&](int x, int y, int z, int face, u32 v) -> u32 {
    int jj = y + j0;
    bool outer = face == 0 ? x == n.x - 1 : face == 1 ? x == 0 : face == 2 ? jj == n.y - 1 : face == 3 ? jj == 0 : face == 4 ? z == n.z - 1 : z == 0;
    return outer ? pix(face, x, jj, z) : voxShade(v, 0.82f);
  };
  VoxelMeshData md; buildVoxelMesh(g, md, &fc);
  upload(out, md, vec3(d.mn.x, d.mn.y + (float)j0 / V, d.mn.z) - pivot);
}

// the outer layer as a raised shell t voxels thick, over model heights y0..y1 (+ the shell beyond a real end)
void buildOverlay(const SkinImage& s, const SkinPartDef& d, float y0, float y1, bool topEnd, bool botEnd, int t, bool round, CharMesh& out, vec3 pivot) {
  const int V = CHAR_VPP;
  ivec3 n = d.sz * V;
  int jA = (int)std::lround((y0 - d.mn.y) * V) - (botEnd ? t : 0), jB = (int)std::lround((y1 - d.mn.y) * V) + (topEnd ? t : 0);
  VoxelGrid g(ivec3(n.x + 2 * t, jB - jA, n.z + 2 * t));
  auto R = skinRects(d.ov.x, d.ov.y, d.sz.x, d.sz.y, d.sz.z);
  size_t cnt = 0;
  for (int gk = 0; gk < g.size.z; gk++) for (int gj = 0; gj < g.size.y; gj++) for (int gi = 0; gi < g.size.x; gi++) {
    int bi = gi - t, bj = gj + jA, bk = gk - t;
    bool ox = bi < 0 || bi >= n.x, oy = bj < 0 || bj >= n.y, oz = bk < 0 || bk >= n.z;
    if (!ox && !oy && !oz) continue;
    if (round && (ox ? 1 : 0) + (oy ? 1 : 0) + (oz ? 1 : 0) >= 2) continue;
    // which face owns it: front/back reach round the sides, sides (and front/back) reach up and down
    int f = oz ? (bk < 0 ? 5 : 4) : ox ? (bi < 0 ? 1 : 0) : (bj < 0 ? 3 : 2);
    vec3 c((clampv(bi, 0, n.x - 1) + 0.5f) / n.x, (clampv(bj, 0, n.y - 1) + 0.5f) / n.y, (clampv(bk, 0, n.z - 1) + 0.5f) / n.z);
    ivec2 p = skinFacePixel(R, f, c);
    const u8* q = s.at(p.x, p.y);
    if (q[3] < 100) continue;
    g.set(gi, gj, gk, voxRGBA(q[0], q[1], q[2])); cnt++;
  }
  if (!cnt) { out.gl.release(); return; }
  VoxelMeshData md; buildVoxelMesh(g, md);
  upload(out, md, vec3(d.mn.x - (float)t / V, d.mn.y + (float)jA / V, d.mn.z - (float)t / V) - pivot);
}

// voxelise a list of gear3d shapes (model pixels) into one mesh, origin relative to pivot
bool buildShapes(const std::vector<const GearShape*>& S, const std::function<u32(const GearShape&, const PaintPt&)>& col, CharMesh& out, vec3 pivot) {
  if (S.empty()) return false;
  vec3 lo(1e9f), hi(-1e9f);
  for (auto* s : S) for (auto& p : s->pts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
  float v = 1.f / CHAR_VPP;
  vec3 o = glm::floor(lo / v) * v - vec3(v);
  ivec3 size = ivec3(glm::ceil((hi - o) / v)) + ivec3(2);
  if (size.x * size.y * size.z > 4000000) return false;
  VoxelPainter P(size, o, v);
  for (auto* s : S) P.hexa(s->pts, [&](const PaintPt& pt) { return col(*s, pt); });
  if (!P.g.count()) return false;
  VoxelMeshData md; buildVoxelMesh(P.g, md);
  upload(out, md, o - pivot);
  return out.ready();
}

// skins.js skinFace: the eye row (a villager's is known; anyone else's is the row of the face that stands out most from the
// skin), the lid over it and the mouth. Columns the hat layer covers are left out (the lid sits behind that layer).
// herobrine.js eyes: the white of a classic skin's face stands out from every colour around it (hair, shirt, any
// pupil), so the eye row is the face row with the most such pixels and the eye columns are its runs. The stock rig draws
// those pixels unlit; a skinned mob gets this band back here (drawSkinMob) — players keep their shaded faces.
bool buildEyeBand(const SkinImage& s, CharMesh& out) {
  auto col = [&](int i, int j) { return s.at(8 + i, 8 + j); };
  auto diff = [](const u8* a, const u8* b) { return (int)std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]); };
  int eye = -1, best = 0;
  for (int j = 2; j <= 6; j++) {
    int n = 0;
    for (int i = 0; i < 8; i++) {
      if (col(i, j)[3] < 100) continue;
      bool standout = true;
      for (int dj : {-1, 1}) for (int di : {-1, 0, 1}) {
        int jj = j + dj, ii = i + di;
        if (jj < 0 || jj > 7 || col(ii, jj)[3] < 100) continue;   // out of the face or transparent: no vote
        if (diff(col(i, j), col(ii, jj)) <= 90) standout = false;
      }
      n += standout ? 1 : 0;
    }
    if (n > best) { best = n; eye = j; }
  }
  if (best < 2) return false;
  int run[8], rn = 0, cnt[8] = {}, wsum[8] = {};
  auto lumAt = [&](int i) { const u8* c = col(i, eye); return c[0] * 0.299f + c[1] * 0.587f + c[2] * 0.114f; };
  float top = 0; for (int i = 0; i < 8; i++) if (col(i, eye)[3] >= 100) top = std::max(top, lumAt(i));
  for (int i = 0; i < 8; i++) {
    // the eye pixels are the brightest of the row (a dark skin's mid greys around them are not eyes)
    bool white = col(i, eye)[3] >= 100 && top > 120 && lumAt(i) >= top * 0.8f;
    run[i] = white ? rn - 1 >= 0 && run[i - 1] == rn - 1 ? run[i - 1] : (rn++, rn - 1) : -1;
    if (run[i] < 0) continue;
    cnt[run[i]]++;
    for (int k = 0; k < 3; k++) wsum[run[i]] += col(i, eye)[k];
  }
  VoxelGrid g(ivec3(16, 2, 1));
  bool built = false;
  for (int r = 0; r < rn; r++) {
    if (cnt[r] < 1 || cnt[r] > 3) continue;   // the eyes are at most three pixels wide
    u32 c = voxRGBA(wsum[r] / cnt[r], wsum[r] / cnt[r], wsum[r] / cnt[r]);
    for (int i = 0; i < 8; i++) {
      if (run[i] != r) continue;
      int gi = (7 - i) * 2;
      g.set(gi, 0, 0, c), g.set(gi + 1, 0, 0, c);
      g.set(gi, 1, 0, c), g.set(gi + 1, 1, 0, c);
      built = true;
    }
  }
  if (!built) return false;
  VoxelMeshData md; buildVoxelMesh(g, md);
  upload(out, md, vec3(-4, 32 - eye - 1, -4.06f) - vec3(0, 24, 0));
  return out.ready();
}

void buildFace(const SkinImage& s, const std::string& ref, CharModel& m) {
  int eye = 4, mouth = -1, mxa = 2, mxb = 6;
  auto col = [&](int i, int j) { return s.at(8 + i, 8 + j); };
  if (ref.rfind("npc:", 0) == 0) { eye = 3; mouth = 5; }
  else {
    const u8* sk = col(0, 6); int bs = -1;
    for (int j : {3, 4, 5}) {
      int n = 0;
      for (int i = 0; i < 8; i++) { const u8* c = col(i, j); n += std::abs(c[0] - sk[0]) + std::abs(c[1] - sk[1]) + std::abs(c[2] - sk[2]) > 90 ? 1 : 0; }
      if (n > bs) { bs = n; eye = j; }
    }
  }
  auto covered = [&](int c, int row) { return s.at(40 + c, 8 + row)[3] >= 100; };
  const vec3 pivot(0, 24, 0);
  VoxelGrid g(ivec3(16, 2, 1));
  for (int gi = 0; gi < 16; gi++) {
    int c = 7 - gi / 2;   // the face's column, counted from the viewer's left (the character's right is +x)
    if (covered(c, eye)) continue;
    const u8* q = col(c, std::min(eye + 1, 7));
    for (int gj = 0; gj < 2; gj++) g.set(gi, gj, 0, voxRGBA(q[0], q[1], q[2]));
  }
  if (g.count()) {
    VoxelMeshData md; buildVoxelMesh(g, md);
    upload(m.face.lid, md, vec3(-4, 32 - eye - 1, -4.5f) - pivot);
  }
  if (mouth >= 0) {
    int w = (mxb - mxa) * 2;
    VoxelGrid mg(ivec3(w, 3, 1));
    const u8* q = col(3, mouth);
    u32 mc = voxShade(voxRGBA(q[0], q[1], q[2]), 0.5f);
    for (int gi = 0; gi < w; gi++) {
      int c = 7 - (gi + 2 * (8 - mxb)) / 2;
      if (covered(c, mouth)) continue;
      for (int gj = 0; gj < 3; gj++) mg.set(gi, gj, 0, mc);
    }
    if (mg.count()) {
      VoxelMeshData md; buildVoxelMesh(mg, md);
      upload(m.face.mouth, md, vec3(4 - mxb, 32 - 8 + 6 - mouth, -4.5f) - pivot + vec3(0, 0, 0));
    }
  }
  std::string key = ref.empty() ? "default" : ref;
  m.face.seed = std::fmod((float)key.size() * 1.7f, 5.f);
  m.face.period = 3.4f + (float)((key.size() > 5 ? (unsigned char)key[5] : 0) % 5) * 0.4f;
  m.face.has = m.face.lid.ready();
}

struct Cache {
  std::map<u64, std::unique_ptr<CharModel>> models;
  std::list<u64> lru;
  std::map<std::string, std::unique_ptr<ArmorSet>> armor;
  std::map<std::string, std::unique_ptr<CharMesh>> misc;   // capes, packs
  std::map<u64, std::unique_ptr<VoxelModel>> arms;
};
Cache& C() { static Cache* c = new Cache(); return *c; }   // kept past exit (no GL calls after the context is gone)
}

void charModelsReset() {
  Cache& c = C();
  c.armor.clear(); c.misc.clear();
  for (auto& [h, m] : c.models) for (int i = 0; i < 4; i++) { m->gearSets[i].clear(); m->gearBuilt[i] = false; }
}

// ------------------------------------------------------------------ the body
const CharModel& charModel(const CharacterSpec& spec) {
  Cache& c = C();
  auto it = c.models.find(spec.hash);
  if (it != c.models.end()) {
    c.lru.remove(spec.hash); c.lru.push_back(spec.hash);
    return *it->second;
  }
  while (c.models.size() >= 48 && !c.lru.empty()) { c.models.erase(c.lru.front()); c.lru.pop_front(); }
  auto m = std::make_unique<CharModel>();
  const SkinImage& s = spec.skin;
  m->hash = spec.hash; m->slim = s.slim; m->aw = s.slim ? 3.f : 4.f; m->body = spec.body;
  if (!s.legacy) m->look = gearRead(s.px.data());
  vec2 st = m->look ? gearStature(m->look->species) : vec2(1);
  m->stature = vec2(st.x * spec.body.height, st.y * spec.body.build);
  float aw = m->aw, ax = 4 + aw / 2;
  const vec3 P[CP_COUNT] = {{0, 24, 0}, {0, 12, 0}, {ax, 22, 0}, {ax, 18, 0}, {-ax, 22, 0}, {-ax, 18, 0}, {2, 12, 0}, {2, 6, 0}, {-2, 12, 0}, {-2, 6, 0}};
  for (int i = 0; i < CP_COUNT; i++) m->pivot[i] = P[i];
  m->gearPivot = gearPivots(aw);
  auto& D = skinParts(s.slim);
  bool round = spec.body.headShape == "round";
  buildBase(s, D[0], 24, 32, round, m->base[CP_HEAD], P[CP_HEAD]);
  buildOverlay(s, D[0], 24, 32, true, true, 2, round, m->ovl[CP_HEAD], P[CP_HEAD]);
  buildBase(s, D[1], 12, 24, false, m->base[CP_BODY], P[CP_BODY]);
  buildOverlay(s, D[1], 12, 24, true, true, 1, false, m->ovl[CP_BODY], P[CP_BODY]);
  // limbs in two halves overlapping by two pixels at the joint (so a bent elbow or knee never opens a gap);
  // the outer layer splits at the joint without overlap
  struct L { int def, up, dn; float mid; } limbs[4] = {{2, CP_ARMR_U, CP_ARMR_D, 18}, {3, CP_ARML_U, CP_ARML_D, 18}, {4, CP_LEGR_U, CP_LEGR_D, 6}, {5, CP_LEGL_U, CP_LEGL_D, 6}};
  for (auto& l : limbs) {
    const SkinPartDef& d = D[l.def];
    buildBase(s, d, l.mid - 2, d.mx.y, false, m->base[l.up], P[l.up]);
    buildBase(s, d, d.mn.y, l.mid + 2, false, m->base[l.dn], P[l.dn]);
    buildOverlay(s, d, l.mid, d.mx.y, true, false, 1, false, m->ovl[l.up], P[l.up]);
    buildOverlay(s, d, d.mn.y, l.mid, false, true, 1, false, m->ovl[l.dn], P[l.dn]);
  }
  buildFace(s, spec.ref, *m);
  buildEyeBand(s, m->eyesBand);   // the face is at the same place on 64x32 and 64x64 nets; no standout eye pixels = no band
  m->skinColor = skinAverage(s, 44, 29, (int)aw, 3);
  m->sleeveColor = skinAverage(s, 44, 20, (int)aw, 3);
  c.lru.push_back(spec.hash);
  return *(c.models[spec.hash] = std::move(m));
}

const std::map<std::string, CharMesh>& CharModel::gear(bool helm, bool chest, const TileSet* ts) const {
  int k = (helm ? 1 : 0) | (chest ? 2 : 0);
  auto& out = gearSets[k];
  if (gearBuilt[k] || !look) return out;
  gearBuilt[k] = true;
  auto S = gearShapes(*look, aw, helm, chest);
  std::map<std::string, std::vector<const GearShape*>> byPart;
  for (auto& s : S) byPart[s.part].push_back(&s);
  const GearLook& L = *look;
  for (auto& [part, list] : byPart) {
    auto pv = gearPivot.find(part);
    if (pv == gearPivot.end()) continue;
    CharMesh& cm = out[part];
    if (!buildShapes(list, [&](const GearShape& s, const PaintPt& p) { return gearColor(L, s.mat, p.p, ts); }, cm, pv->second)) out.erase(part);
  }
  return out;
}

// ------------------------------------------------------------------ armour
namespace {
// a box shell coloured with a tile across each face (models.js addBox with tilesOf)
void tileShell(VoxelGrid& g, ivec3 n, const TileSet& ts, const std::array<int, 6>& tiles, vec3 dye, int holeTile = -2) {
  for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
    if (!(x == 0 || y == 0 || z == 0 || x == n.x - 1 || y == n.y - 1 || z == n.z - 1)) continue;
    vec3 c((x + 0.5f) / n.x, (y + 0.5f) / n.y, (z + 0.5f) / n.z);
    float dist[6] = {(float)(n.x - 1 - x), (float)x, (float)(n.y - 1 - y), (float)y, (float)(n.z - 1 - z), (float)z};
    int f = 5; for (int q : {4, 0, 1, 2, 3}) if (dist[q] < dist[f]) f = q;
    float s, t;
    switch (f) { case 0: s = 1 - c.z; t = 1 - c.y; break; case 1: s = c.z; t = 1 - c.y; break; case 2: s = c.x; t = c.z; break;
                 case 3: s = 1 - c.x; t = c.z; break; case 4: s = c.x; t = 1 - c.y; break; default: s = 1 - c.x; t = 1 - c.y; }
    u32 col = tiles[f] >= 0 ? tileVoxel(ts, tiles[f], clampv((int)(s * ts.size), 0, ts.size - 1), clampv((int)(t * ts.size), 0, ts.size - 1)) : voxRGBA(168, 172, 180);
    if (!col && tiles[f] == holeTile) continue;   // the helmet's face opening (armor_<mat>_face is clear there): the face shows through
    if (!col) col = voxRGBA(150, 150, 156);
    if (dye.x >= 0) {
      float l = ((col & 255) * 0.3f + ((col >> 8) & 255) * 0.59f + ((col >> 16) & 255) * 0.11f) / 160.f;
      col = voxRGBA((int)(dye.r * l), (int)(dye.g * l), (int)(dye.b * l));
    }
    g.set(x, y, z, col | 0xFF000000u);
  }
}
vec3 hexDye(const std::string& h) {
  if (h.size() != 6) return vec3(-1);
  unsigned v = (unsigned)std::strtoul(h.c_str(), nullptr, 16);
  return vec3((v >> 16) & 255, (v >> 8) & 255, v & 255);
}
int tileOf(const TileSet& ts, const std::string& n) { auto it = ts.index.find(n); return it == ts.index.end() ? -1 : it->second; }
}

const ArmorSet& armorMeshes(const std::string& itemKey, const std::string& dye, int slot, bool slim, bool look3d, const TileSet& ts) {
  std::string key = itemKey + "#" + dye + "|" + std::to_string(slot) + (slim ? "s" : "c") + (look3d ? "3" : "");
  Cache& c = C();
  auto it = c.armor.find(key);
  if (it != c.armor.end()) return *it->second;
  auto set = std::make_unique<ArmorSet>();
  std::string mat = armorMatOf(itemKey);
  if (tileOf(ts, "armor_" + mat) < 0 && !packArmorHas(mat)) mat = "iron";
  bool pack = packArmorHas(mat), bare = pack && packArmorBareArms(mat);
  set->pack = pack;
  static const std::map<std::string, std::pair<int, std::string>> PIECE = {
    {"helmet", {0, "head"}}, {"chest", {1, "body"}}, {"pad", {1, "armR_u"}}, {"padL", {1, "armL_u"}}, {"cuff", {1, "armR_d"}}, {"cuffL", {1, "armL_d"}},
    {"waist", {2, "body"}}, {"thigh", {2, "legR_u"}}, {"thighL", {2, "legL_u"}}, {"knee", {2, "legR_d"}}, {"kneeL", {2, "legL_d"}},
    {"boot", {3, "legR_d"}}, {"bootL", {3, "legL_d"}}};
  int tMain = tileOf(ts, "armor_" + mat), tFace = tileOf(ts, "armor_" + mat + "_face");
  vec3 dyeC = mat == "leather" ? hexDye(dye) : vec3(-1);
  float aw = slim ? 3.f : 4.f;
  for (const ArmorPiece& p : packArmorPieces(bare)) {
    auto pi = PIECE.find(p.name);
    if (pi == PIECE.end() || pi->second.first != slot) continue;
    vec3 lo = p.lo, hi = p.hi;
    if (slim && (p.name.rfind("pad", 0) == 0 || p.name.rfind("cuff", 0) == 0)) { lo.x += (4 - aw) / 2; hi.x -= (4 - aw) / 2; }
    ivec3 n = glm::max(ivec3(glm::round((hi - lo) * (float)CHAR_VPP)), ivec3(1));
    VoxelGrid g(n);
    if (pack) {
      const ArmorLayer* A = packArmorLayer(mat, p.layer, mat == "leather" ? dye : "");
      if (A) paintSkinBox(g, ivec3(0), p, A->rgba.data(), A->w, A->h, 64, 32, CHAR_VPP);
    } else {
      std::array<int, 6> tl{tMain, tMain, tMain, tMain, tMain, p.name == "helmet" && tFace >= 0 ? tFace : tMain};
      tileShell(g, n, ts, tl, dyeC, p.name == "helmet" && tFace >= 0 ? tFace : -2);
    }
    if (!g.count()) continue;
    auto cm = std::make_unique<CharMesh>();
    VoxelMeshData md; buildVoxelMesh(g, md);
    upload(*cm, md, lo);
    if (!cm->ready()) continue;
    set->byPart[pi->second.second].push_back(cm.get());
    set->meshes.push_back(std::move(cm));
  }
  // a 3D character's armour grows its own pieces over the shell (pauldrons, a crest, a gorget, tassets, cuffs)
  if (look3d) {
    u32 avg = tMain >= 0 ? ts.avgColor(tMain) : voxRGBA(168, 172, 180);
    vec3 am((avg & 255), (avg >> 8) & 255, (avg >> 16) & 255);
    if (pack) if (const ArmorLayer* A = packArmorLayer(mat, 1, mat == "leather" ? dye : "")) {   // the pack's chestplate front
      float k = A->w / 64.f; vec3 sum(0); int cnt = 0;
      for (int y = (int)(21 * k); y < (int)(31 * k); y++) for (int x = (int)(20 * k); x < (int)(28 * k); x++) {
        const u8* q = A->rgba.data() + ((size_t)y * A->w + x) * 4; if (q[3] < 100) continue; sum += vec3(q[0], q[1], q[2]); cnt++;
      }
      if (cnt) am = sum / (float)cnt;
    }
    if (dyeC.x >= 0 && !pack) am = dyeC;
    auto S = gearArmorShapes(gearArmorClass(mat), slot, aw);
    std::map<std::string, std::vector<const GearShape*>> byPart;
    for (auto& s : S) byPart[s.part].push_back(&s);
    auto pivots = gearPivots(aw);
    static const std::map<std::string, std::string> TO = {{"head", "head"}, {"neck", "head"}, {"body", "body"}, {"skirtF", "body"}, {"skirtB", "body"}, {"armR", "armR_u"}, {"foreR", "armR_d"},
      {"armL", "armL_u"}, {"foreL", "armL_d"}, {"legR", "legR_u"}, {"shinR", "legR_d"}, {"legL", "legL_u"}, {"shinL", "legL_d"}};
    for (auto& [part, list] : byPart) {
      auto to = TO.find(part); auto pv = pivots.find(part);
      if (to == TO.end() || pv == pivots.end()) continue;
      auto cm = std::make_unique<CharMesh>();
      auto col = [&](const GearShape& s, const PaintPt& p) {
        u32 h = (u32)(std::floor(p.p.x * 2) * 73856093) ^ (u32)(std::floor(p.p.y * 2) * 19349663) ^ (u32)(std::floor(p.p.z * 2) * 83492791);
        float k = (s.mat == "at" ? 0.74f : 1.f) * (0.94f + (h % 1000) / 1000.f * 0.12f);
        return voxRGBA((int)(am.r * k), (int)(am.g * k), (int)(am.b * k));
      };
      // gear armour parts turn about their own pivot, which is the skin part's pivot for these
      if (!buildShapes(list, col, *cm, pv->second)) continue;
      set->byPart[to->second].push_back(cm.get());
      set->meshes.push_back(std::move(cm));
    }
  }
  return *(c.armor[key] = std::move(set));
}

// ------------------------------------------------------------------ capes
void capeMeshInvalidate(const std::string& capeKey) {
  Cache& c = C();
  for (int seg = 0; seg < 3; seg++) c.misc.erase("cape:" + capeKey + ":" + std::to_string(seg));
}

const CharMesh* capeMesh(const std::string& capeKey, int seg, const TileSet& ts) {
  std::string key = "cape:" + capeKey + ":" + std::to_string(seg);
  Cache& c = C();
  auto it = c.misc.find(key);
  if (it != c.misc.end()) return it->second && it->second->ready() ? it->second.get() : nullptr;
  // a cape you painted: the Minecraft layout in a 64x32 picture (cape_tex.h); else the game's cape tiles (+ "_in" for the inside)
  CapePixels paint; bool custom = isCustomCape(capeKey) && loadCustomCape(capeKey, paint);
  int tOut = custom ? -1 : tileOf(ts, capeKey), tIn = custom ? -1 : tileOf(ts, capeKey + "_in");
  if (!custom && tOut < 0) { c.misc[key] = nullptr; return nullptr; }
  if (tIn < 0) tIn = tOut;
  int top = seg == 2 ? 11 : 0, bot = seg == 1 ? 11 : 16, thick = seg == 0 ? 2 : 1;   // rows; voxels
  const int V = CHAR_VPP;
  VoxelGrid g(ivec3(10 * V, (bot - top) * V, thick));
  for (int z = 0; z < thick; z++) for (int y = 0; y < g.size.y; y++) for (int x = 0; x < g.size.x; x++) g.set(x, y, z, VOXEL_SOLID << 24 | 1);
  float rows = (float)(bot - top) * V;
  int pk = custom ? std::max(1, paint.w / 64) : 1;
  VoxelFaceColor fc = [&](int x, int y, int z, int face, u32) -> u32 {
    float cx = (x + 0.5f) / g.size.x, row = (top + (rows - 1 - y + 0.5f) / V) / 16.f;   // 0 at the top of the cape
    float s = face == 5 ? 1 - cx : face == 4 ? cx : face == 0 ? 0.95f : face == 1 ? 0.05f : cx;
    float t = face == 2 ? 0.02f : face == 3 ? 0.98f : row;
    if (custom) {   // outside = (1,1) 10x16, inside = (12,1) 10x16 (in 64x32 units)
      int px = ((face == 5 ? 12 : 1) * pk) + clampv((int)(s * 10 * pk), 0, 10 * pk - 1), py = pk + clampv((int)(t * 16 * pk), 0, 16 * pk - 1);
      const u8* q = paint.at(px, py);
      return q[3] ? voxRGBA(q[0], q[1], q[2]) : voxRGBA(120, 30, 30);
    }
    int tile = face == 5 ? tIn : tOut;
    u32 col = tileVoxel(ts, tile, clampv((int)(s * ts.size), 0, ts.size - 1), clampv((int)(t * ts.size), 0, ts.size - 1));
    return col ? col | 0xFF000000u : voxRGBA(120, 30, 30);
  };
  VoxelMeshData md; buildVoxelMesh(g, md, &fc);
  auto cm = std::make_unique<CharMesh>();
  upload(*cm, md, vec3(-5, -(float)(bot - top), 0));
  const CharMesh* r = cm->ready() ? cm.get() : nullptr;
  c.misc[key] = std::move(cm);
  return r;
}

// ------------------------------------------------------------------ elytra
const CharMesh* elytraWingMesh(int side, const TileSet& ts) {
  std::string key = "elytra:" + std::to_string(side);
  Cache& c = C();
  auto it = c.misc.find(key);
  if (it != c.misc.end()) return it->second && it->second->ready() ? it->second.get() : nullptr;
  // a resource pack's elytra picture (textures/entity/elytra.png, the vanilla 10 x 20 x 2 wing at u 22): cut up like a skin box
  if (PackFS* P = activePack()) if (P->hasTextures()) if (auto b = P->read(P->texRoot + "entity/elytra.png")) {
    int w = 0, h = 0; std::vector<u8> rgba;
    if (loadPngMem(b->data(), b->size(), w, h, rgba) && w >= 64 && w == h * 2) {
      ArmorPiece ap{"elytra", 0, vec3(0), vec3(10, 20, 2), 22, 0, 10, 20, 2, side == 0};
      VoxelGrid pg(ivec3(10 * CHAR_VPP, 20 * CHAR_VPP, 2 * CHAR_VPP));
      paintSkinBox(pg, ivec3(0), ap, rgba.data(), w, h, 64, 32, CHAR_VPP);
      if (pg.count()) {
        VoxelMeshData md; buildVoxelMesh(pg, md);
        auto cm = std::make_unique<CharMesh>();
        upload(*cm, md, vec3(side == 0 ? 0.f : -10.f, -20.f, 0.f));
        const CharMesh* r = cm->ready() ? cm.get() : nullptr;
        c.misc[key] = std::move(cm);
        return r;
      }
    }
  }
  int tOut = tileOf(ts, "elytra"), tIn = tileOf(ts, "elytra_in");
  if (tOut < 0) { c.misc[key] = nullptr; return nullptr; }
  if (tIn < 0) tIn = tOut;
  const int V = CHAR_VPP, W = 9 * V, L = 21 * V, T = 3;   // 3 voxels deep; the caller scales z by 0.8 (1.2 px)
  VoxelGrid g(ivec3(W, L, T));
  for (int z = 0; z < T; z++) for (int y = 0; y < L; y++) for (int x = 0; x < W; x++) g.set(x, y, z, VOXEL_SOLID << 24 | 1);
  VoxelFaceColor fc = [&](int x, int y, int z, int face, u32) -> u32 {
    float u = (x + 0.5f) / W; if (side) u = 1 - u;   // how far out from the shoulder, either wing the same picture mirrored
    float row = 1 - (y + 0.5f) / L;
    float s = face == 5 ? 1 - u : face == 4 ? u : 0.5f;
    float t = face == 2 ? 0.02f : face == 3 ? 0.98f : row;
    u32 col = tileVoxel(ts, face == 5 ? tIn : tOut, clampv((int)(s * ts.size), 0, ts.size - 1), clampv((int)(t * ts.size), 0, ts.size - 1));
    return col ? col | 0xFF000000u : voxRGBA(150, 150, 170);
  };
  VoxelMeshData md; buildVoxelMesh(g, md, &fc);
  auto cm = std::make_unique<CharMesh>();
  upload(*cm, md, vec3(side == 0 ? 0.f : -9.f, -21.f, 0.f));
  const CharMesh* r = cm->ready() ? cm.get() : nullptr;
  c.misc[key] = std::move(cm);
  return r;
}

// ------------------------------------------------------------------ backpacks
PackOver gearPackOver(const GearLook* L, const std::string& chestKey, const std::string& helmKey, bool cape) {
  static const std::map<std::string, float> SH = {{"vestments", 0.75f}, {"leafrobe", 0.95f}, {"ranger", 0.85f}, {"shadow", 0.85f}, {"traveller", 0.65f}, {"furs", 1.45f},
                                                  {"noble", 1.15f}, {"archrobe", 0.35f}, {"pact", 0.35f}, {"silks", 0.35f}};
  PackOver o;
  std::string chest = !chestKey.empty() && !packArmorHas(armorMatOf(chestKey)) ? armorMatOf(chestKey) : "";
  std::string cls = chest.empty() ? "" : gearArmorClass(chest);
  bool coif = !helmKey.empty() && gearArmorClass(armorMatOf(helmKey)) == "mail";
  bool dressed = L && (!L->outfit.empty() || L->robe || L->tabard || L->apron);
  float po = 0, lift = 0; bool fzSet = false; float fz = -2.32f;
  if (dressed) { po = L->robe || L->outfit == "furs" ? 1.2f : 1.0f; auto it = SH.find(L->outfit); lift = it == SH.end() ? 0 : it->second; fz = -3.0f; fzSet = true; }
  if (!chest.empty()) { po = std::max(po, 1.0f); lift = std::max(lift, cls == "plate" ? 1.1f : 0.45f); fz = -3.0f; fzSet = true; }
  if (coif) lift = std::max(lift, 0.95f);
  if (cape) { o.capeZ = 2.03f + (po ? po - 0.25f : 0); po = o.capeZ + 0.55f - 2.0f; }
  else if (L && L->cloak) { po = std::max(po, 1.75f); lift = std::max(lift, 1.1f); if (!fzSet) { fz = -2.95f; fzSet = true; } }
  o.any = po || lift || fzSet;
  o.po = std::round(po * 100) / 100; o.lift = std::round(lift * 100) / 100; o.fz = fz;
  return o;
}

const CharMesh* packWornMesh(const std::string& keyIn, bool straps, const PackOver& ov, const TileSet& ts) {
  std::string key = keyIn == "reinforced_backpack" || keyIn == "explorer_backpack" ? keyIn : "leather_backpack";
  std::string ck = strFormat("pack:%s:%d:%.2f:%.2f:%.2f", key.c_str(), straps ? 1 : 0, ov.po, ov.lift, ov.fz);
  Cache& c = C();
  auto it = c.misc.find(ck);
  if (it != c.misc.end()) return it->second && it->second->ready() ? it->second.get() : nullptr;
  bool big = key != "leather_backpack", ex = key == "explorer_backpack";
  int body = tileOf(ts, ex ? "pack_body_e" : big ? "pack_body_r" : "pack_body_l"), flap = tileOf(ts, ex ? "pack_flap_e" : big ? "pack_flap_r" : "pack_flap_l");
  int metal = tileOf(ts, big ? "pack_iron" : "pack_brass"), roll = tileOf(ts, ex ? "pack_roll_e" : big ? "pack_roll_r" : "pack_roll_l"), strap = tileOf(ts, "pack_strap"), iron = tileOf(ts, "pack_iron");
  float po = ov.po, lift = ov.lift, fz = ov.fz;
  float v = 1.f / CHAR_VPP;
  vec3 o(-6.5f, -1.f, -3.6f);
  VoxelPainter P(ivec3(glm::ceil(vec3(13, 17, 13 + po) / v)), o, v);
  auto tf = [&](vec3 p) { if (po && p.z >= 1.0f) p.z += po; return p; };
  auto box = [&](vec3 a, vec3 b, std::array<int, 6> t) {
    vec3 p = tf(a), q = tf(b), lo = glm::min(p, q), hi = glm::max(p, q);
    P.box(lo, hi, [&ts, lo, hi, t](const PaintPt& pt) -> u32 {
      vec3 n = glm::clamp((pt.p - lo) / glm::max(hi - lo, vec3(1e-5f)), vec3(0), vec3(0.999f));
      float d[6] = {hi.x - pt.p.x, pt.p.x - lo.x, hi.y - pt.p.y, pt.p.y - lo.y, hi.z - pt.p.z, pt.p.z - lo.z};
      int f = 4; for (int i : {5, 0, 1, 2, 3}) if (d[i] < d[f] - 1e-5f) f = i;
      float s = f == 0 ? 1 - n.z : f == 1 ? n.z : f == 2 ? n.x : f == 3 ? 1 - n.x : f == 4 ? n.x : 1 - n.x;
      float tt = f == 2 || f == 3 ? n.z : 1 - n.y;
      u32 col = t[f] >= 0 ? tileVoxel(ts, t[f], (int)(s * ts.size), (int)(tt * ts.size)) : 0;
      return col ? col | 0xFF000000u : voxRGBA(110, 70, 40);
    });
  };
  auto all = [](int t) { return std::array<int, 6>{t, t, t, t, t, t}; };
  if (straps) {
    for (float sx : {-2.3f, 2.3f}) box({sx - 0.6f, 3.6f, fz}, {sx + 0.6f, 12.25f + lift, fz + 0.5f}, all(strap));
  } else {
    float w = 3.5f + big * 0.3f, top = 10.5f + big * 0.4f, d = 5.2f + big * 0.4f;
    box({-w, 1.4f, 2.0f}, {w, top, d}, all(body));
    box({-w - 0.2f, top - 0.9f, 1.9f}, {w + 0.2f, top + 0.4f, d + 0.4f}, {body, body, flap, body, flap, body});
    box({-3.0f, top - 4.2f, d}, {3.0f, top + 0.4f, d + 0.5f}, all(flap));
    box({-0.7f, top - 4.0f, d + 0.4f}, {0.7f, top - 3.0f, d + 0.9f}, all(metal));
    box({-2.6f, 2.1f, d - 0.2f}, {2.6f, 5.8f, d + 1.4f}, all(body));
    box({-2.7f, 4.7f, d - 0.2f}, {2.7f, 6.0f, d + 1.55f}, {flap, flap, flap, body, flap, flap});
    box({-0.45f, 4.6f, d + 1.5f}, {0.45f, 5.3f, d + 2.0f}, all(metal));
    for (int s : {-1, 1}) {
      box({s * w, 2.4f, 2.6f}, {s * (w + 1.1f), 6.6f, 4.8f}, all(body));
      box({s * w, 6.0f, 2.5f}, {s * (w + 1.2f), 6.7f, 4.9f}, all(flap));
    }
    box({-4.4f, top + 0.4f, 2.5f}, {4.4f, top + 2.1f, 4.6f}, all(roll));
    for (float sx : {-2.6f, 2.6f}) box({sx - 0.35f, top + 0.3f, 2.4f}, {sx + 0.35f, top + 2.2f, 4.7f}, all(strap));
    if (big) for (float yy : {3.2f, 8.0f}) box({-w - 0.12f, yy, 1.95f}, {w + 0.12f, yy + 0.6f, d + 0.1f}, all(iron));
    for (float sx : {-2.3f, 2.3f}) {
      box({sx - 0.6f, 11.95f + lift, fz + 0.02f}, {sx + 0.6f, 12.5f + lift, 2.0f}, all(strap));   // over the shoulder
      box({sx - 0.6f, 3.6f, fz}, {sx + 0.6f, 12.3f + lift, fz + 0.5f}, all(strap));                // down the chest
      if (po) box({sx - 0.6f, 10.4f, 1.6f}, {sx + 0.6f, 12.3f + lift, 2.1f}, all(strap));          // and down the back to the bag
    }
    box({-3.6f, 3.4f, fz - 0.04f}, {3.6f, 3.9f, fz + 0.5f}, all(strap));                           // a waist strap
  }
  auto cm = std::make_unique<CharMesh>();
  VoxelMeshData md; buildVoxelMesh(P.g, md);
  upload(*cm, md, o);
  const CharMesh* r = cm->ready() ? cm.get() : nullptr;
  c.misc[ck] = std::move(cm);
  return r;
}

// ------------------------------------------------------------------ the first-person arm
const VoxelModel* skinArmModel(const CharacterSpec& spec) {
  Cache& c = C();
  auto it = c.arms.find(spec.hash);
  if (it != c.arms.end()) return it->second->ready() ? it->second.get() : nullptr;
  const SkinImage& s = spec.skin;
  const SkinPartDef& d = skinParts(s.slim)[2];
  const int V = CHAR_VPP;
  ivec3 n = d.sz * V;
  VoxelGrid g(n + ivec3(2));
  auto R = skinRects(d.uv.x, d.uv.y, d.sz.x, d.sz.y, d.sz.z), Ro = skinRects(d.ov.x, d.ov.y, d.sz.x, d.sz.y, d.sz.z);
  for (int z = 0; z < g.size.z; z++) for (int y = 0; y < g.size.y; y++) for (int x = 0; x < g.size.x; x++) {
    int bi = x - 1, bj = y - 1, bk = z - 1;
    bool ox = bi < 0 || bi >= n.x, oy = bj < 0 || bj >= n.y, oz = bk < 0 || bk >= n.z;
    vec3 cc((clampv(bi, 0, n.x - 1) + 0.5f) / n.x, (clampv(bj, 0, n.y - 1) + 0.5f) / n.y, (clampv(bk, 0, n.z - 1) + 0.5f) / n.z);
    if (!ox && !oy && !oz) {
      int dist[6] = {n.x - 1 - bi, bi, n.y - 1 - bj, bj, n.z - 1 - bk, bk};
      int f = 5; for (int q : {4, 0, 1, 2, 3}) if (dist[q] < dist[f]) f = q;
      ivec2 p = skinFacePixel(R, f, cc); const u8* q = s.at(p.x, p.y);
      g.set(x, y, z, voxRGBA(q[0], q[1], q[2]));
    } else {
      int f = oz ? (bk < 0 ? 5 : 4) : ox ? (bi < 0 ? 1 : 0) : (bj < 0 ? 3 : 2);
      ivec2 p = skinFacePixel(Ro, f, cc); const u8* q = s.at(p.x, p.y);
      if (q[3] >= 100) g.set(x, y, z, voxRGBA(q[0], q[1], q[2]));
    }
  }
  VoxelFaceColor fc = [&](int x, int y, int z, int face, u32 v) -> u32 {
    int bi = x - 1, bj = y - 1, bk = z - 1;
    if (bi < 0 || bj < 0 || bk < 0 || bi >= n.x || bj >= n.y || bk >= n.z) return v;   // the sleeve layer: its own pixel
    vec3 cc((bi + 0.5f) / n.x, (bj + 0.5f) / n.y, (bk + 0.5f) / n.z);
    ivec2 p = skinFacePixel(R, face, cc); const u8* q = s.at(p.x, p.y);
    return voxRGBA(q[0], q[1], q[2]);
  };
  auto vm = std::make_unique<VoxelModel>();
  VoxelMeshData md; buildVoxelMesh(g, md, &fc);
  vm->gl.upload(md);
  // the same frame as the plain first-person arm (item_layers firstPersonArm): 1/32 block voxels, the hand at -0.34
  vm->frame.voxel = 1.f / 32; vm->frame.origin = vec3(-g.size.x / 64.f, -0.34375f - 1.f / 32, -g.size.z / 64.f);
  vm->size = g.size; vm->kind = VoxelModel::Block; vm->built = true; vm->key = "player_arm";
  const VoxelModel* r = vm->ready() ? vm.get() : nullptr;
  c.arms[spec.hash] = std::move(vm);
  return r;
}

} // namespace dq
