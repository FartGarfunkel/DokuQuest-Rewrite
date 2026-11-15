// assets/pack_armor.cpp — the pack's armour pictures and boxes (render/packarmor.js).
#include "assets/pack_armor.h"
#include "assets/pack_fs.h"
#include "assets/armor3d.h"
#include "assets/tileset.h"
#include "common/fs.h"
#include <map>

namespace dq {

namespace {
struct ArmorSet { std::optional<ArmorLayer> l1, l2, o1, o2; int bare = -1; };
std::map<std::string, ArmorSet> g_sets;
std::map<std::string, ArmorLayer> g_dyed;
// the game's own materials last: no pack paints those, the local overlay pack does (tools/port/armor_art.py)
const std::vector<std::pair<std::string, std::vector<std::string>>> NAMES = {
  {"leather", {"leather"}}, {"iron", {"iron"}}, {"gold", {"gold", "golden"}}, {"diamond", {"diamond"}}, {"cinderite", {"netherite"}}, {"copper", {"copper"}},
  {"bronze", {"bronze"}}, {"silver", {"silver"}}, {"steel", {"steel"}}, {"mithril", {"mithril"}}, {"adamantite", {"adamantite"}}, {"emerald", {"emerald"}},
  {"zanite", {"zanite"}}, {"gravitite", {"gravitite"}}, {"thallasium", {"thallasium"}}};
const int LEATHER_UNDYED[3] = {160, 101, 64};
}

// armour trims (Minecraft's): a pattern laid out as the armour's own picture, its grey shades the trim palette's eight,
// each swapped for the trim material's own eight
std::map<std::string, ArmorLayer> g_trimPat;                    // pattern -> its picture (both layers' layout)
std::vector<u32> g_trimKey;                                      // the palette's eight greys
std::map<std::string, std::vector<u32>> g_trimMat;               // material -> its eight colours
std::map<std::string, ArmorLayer> g_trimDone;                    // "pattern:material" -> coloured
void resetPackArmor() { g_sets.clear(); g_dyed.clear(); g_trimPat.clear(); g_trimKey.clear(); g_trimMat.clear(); g_trimDone.clear(); }

int loadPackArmor(const PackFS& P) {
  resetPackArmor();
  // the chosen pack first, then the local overlay pack (Mod Art (local): the armour of the pack's style for the
  // materials it has no picture of, tools/port/armor_art.py)
  const std::unique_ptr<PackFS> ov = PackFS::open(userDir() / "resourcepacks" / "Mod Art (local)");
  auto firstIn = [&](const PackFS& Q, const std::vector<std::string>& rels) -> std::optional<ArmorLayer> {
    if (!Q.hasTextures()) return std::nullopt;
    for (auto& r : rels) {
      auto b = Q.read(Q.texRoot + r); if (!b) continue;
      ArmorLayer L;
      if (loadPngMem(b->data(), b->size(), L.w, L.h, L.rgba) && L.w >= 64 && L.w == L.h * 2) return L;
    }
    return std::nullopt;
  };
  auto first = [&](const std::vector<std::string>& rels) -> std::optional<ArmorLayer> {
    if (auto L = firstIn(P, rels)) return L;
    return ov ? firstIn(*ov, rels) : std::nullopt;
  };
  int n = 0;
  for (auto& [mat, names] : NAMES) {
    ArmorSet S;
    for (auto& nm : names) {
      if (!S.l1) S.l1 = first({"entity/equipment/humanoid/" + nm + ".png", "models/armor/" + nm + "_layer_1.png"});
      if (!S.l2) S.l2 = first({"entity/equipment/humanoid_leggings/" + nm + ".png", "models/armor/" + nm + "_layer_2.png"});
      if (mat == "leather") {
        if (!S.o1) S.o1 = first({"entity/equipment/humanoid/" + nm + "_overlay.png", "models/armor/" + nm + "_layer_1_overlay.png"});
        if (!S.o2) S.o2 = first({"entity/equipment/humanoid_leggings/" + nm + "_overlay.png", "models/armor/" + nm + "_layer_2_overlay.png"});
      }
    }
    if (S.l1 || S.l2) { g_sets[mat] = std::move(S); n++; }
  }
  {   // the trims: the chosen pack's, else the game's own base pack's
    std::unique_ptr<PackFS> base = PackFS::open(dataDir() / "resourcepacks" / "DokuQuest Base");
    auto readPng = [&](const std::vector<std::string>& rels, ArmorLayer& L) {
      for (const PackFS* Q : std::initializer_list<const PackFS*>{&P, base.get(), ov.get()}) {
        if (!Q || !Q->hasTextures()) continue;
        for (auto& r : rels) { auto b = Q->read(Q->texRoot + r); if (b && loadPngMem(b->data(), b->size(), L.w, L.h, L.rgba)) return true; }
      }
      return false;
    };
    auto palette = [&](const std::string& name, std::vector<u32>& out) {
      ArmorLayer L;
      if (!readPng({"trims/color_palettes/" + name + ".png", "trims/" + name + ".png"}, L) || L.w < 8) return false;
      out.clear(); for (int i = 0; i < 8; i++) { const u8* q = &L.rgba[(size_t)i * 4]; out.push_back((u32)q[0] | (u32)q[1] << 8 | (u32)q[2] << 16 | 0xFF000000u); }
      return true;
    };
    palette("trim_palette", g_trimKey);
    for (const char* m : {"iron", "gold", "copper", "diamond", "emerald", "lapis", "quartz", "amethyst", "redstone", "netherite", "resin", "iron_darker", "gold_darker", "diamond_darker", "netherite_darker"}) { std::vector<u32> c; if (palette(m, c)) g_trimMat[m] = c; }
    for (const char* pt : {"sentry", "vex", "wild", "coast", "dune", "wayfinder", "raiser", "shaper", "host", "ward", "silence", "tide", "snout", "rib", "eye", "spire", "flow", "bolt"}) {
      ArmorLayer L;
      if (readPng({"trims/entity/humanoid/" + std::string(pt) + ".png", "trims/models/armor/" + std::string(pt) + ".png", "trims/" + std::string(pt) + ".png"}, L) && L.w >= 64 && L.w == L.h * 2) g_trimPat[pt] = std::move(L);
    }
  }
  loadArmor3d(P);   // the 3D sets (assets/armor3d.h)
  return n;
}

bool packArmorHas(const std::string& mat) { return g_sets.count(mat) > 0; }

const ArmorLayer* trimLayer(const std::string& pattern, const std::string& material) {
  const std::string key = pattern + ":" + material;
  if (auto it = g_trimDone.find(key); it != g_trimDone.end()) return &it->second;
  auto pt = g_trimPat.find(pattern); auto mt = g_trimMat.find(material);
  if (pt == g_trimPat.end() || mt == g_trimMat.end() || g_trimKey.size() < 8) return nullptr;
  ArmorLayer L = pt->second;
  for (size_t i = 0; i < L.rgba.size(); i += 4) {
    if (L.rgba[i + 3] < 8) { L.rgba[i + 3] = 0; continue; }
    int best = 0, bd = 1 << 30;   // (its grey: the nearest of the palette's eight)
    for (int k = 0; k < 8; k++) { const u32 c = g_trimKey[k]; const int d = std::abs((int)(c & 255) - L.rgba[i]) + std::abs((int)(c >> 8 & 255) - L.rgba[i + 1]) + std::abs((int)(c >> 16 & 255) - L.rgba[i + 2]); if (d < bd) { bd = d; best = k; } }
    const u32 c = mt->second[best];
    L.rgba[i] = c & 255; L.rgba[i + 1] = c >> 8 & 255; L.rgba[i + 2] = c >> 16 & 255; L.rgba[i + 3] = 255;
  }
  return &(g_trimDone[key] = std::move(L));
}

void stampTrim(VoxelGrid& g, vec3 lo, int vpp, const ArmorPiece& p, const ArmorLayer& T) {
  const vec3 sz = glm::max(p.hi - p.lo, vec3(1e-3f));
  static const ivec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int z = 0; z < g.size.z; z++) for (int y = 0; y < g.size.y; y++) for (int x = 0; x < g.size.x; x++) {
    if (!g.solid(x, y, z)) continue;
    // where it is on the box Minecraft lays the picture over (the piece's), 0..1 each way
    const vec3 q = glm::clamp((lo + (vec3(x, y, z) + 0.5f) / (float)vpp - p.lo) / sz, vec3(0), vec3(1));
    const float s[6][2] = {{1 - q.z, 1 - q.y}, {q.z, 1 - q.y}, {1 - q.x, 1 - q.z}, {1 - q.x, q.z}, {q.x, 1 - q.y}, {1 - q.x, 1 - q.y}};
    for (int k : {4, 5, 0, 1, 2, 3}) {   // (an open face, front/back first as the picture's are painted)
      const ivec3 nb = ivec3(x, y, z) + N[k];
      if (g.solid(nb.x, nb.y, nb.z)) continue;
      const u32 c = skinBoxColor(p, T.rgba.data(), T.w, T.h, 64, 32, k, s[k][0], s[k][1]);
      if (c) { g.set(x, y, z, (c & 0x00FFFFFFu) | (g.at(x, y, z) & 0xFF000000u)); }
      break;
    }
  }
}
std::string armorMatOf(const std::string& key) { auto p = key.find('_'); return p == std::string::npos ? key : key.substr(0, p); }

const ArmorLayer* packArmorLayer(const std::string& mat, int layer, const std::string& dye) {
  auto it = g_sets.find(mat);
  if (it == g_sets.end()) return nullptr;
  auto& S = it->second;
  const std::optional<ArmorLayer>& base = layer == 2 ? S.l2 : S.l1;
  if (!base) return nullptr;
  if (mat != "leather") return &*base;
  int c3[3] = {LEATHER_UNDYED[0], LEATHER_UNDYED[1], LEATHER_UNDYED[2]};
  if (dye.size() >= 6) { unsigned v = (unsigned)std::stoul(dye.substr(dye.size() - 6), nullptr, 16); c3[0] = (v >> 16) & 255; c3[1] = (v >> 8) & 255; c3[2] = v & 255; }
  std::string key = std::to_string(layer) + ":" + std::to_string(c3[0]) + "," + std::to_string(c3[1]) + "," + std::to_string(c3[2]);
  auto d = g_dyed.find(key);
  if (d != g_dyed.end()) return &d->second;
  ArmorLayer L = *base;
  for (size_t i = 0; i < L.rgba.size(); i += 4) for (int k = 0; k < 3; k++) L.rgba[i + k] = (u8)(L.rgba[i + k] * c3[k] / 255);
  const std::optional<ArmorLayer>& ov = layer == 2 ? S.o2 : S.o1;
  if (ov && ov->w == L.w && ov->h == L.h) for (size_t i = 0; i < L.rgba.size(); i += 4) {   // drawn over (source-over)
    float a = ov->rgba[i + 3] / 255.f;
    for (int k = 0; k < 3; k++) L.rgba[i + k] = (u8)(ov->rgba[i + k] * a + L.rgba[i + k] * (1 - a));
    L.rgba[i + 3] = (u8)std::max<int>(L.rgba[i + 3], ov->rgba[i + 3]);
  }
  return &(g_dyed[key] = std::move(L));
}

bool packArmorBareArms(const std::string& mat) {
  auto it = g_sets.find(mat);
  if (it == g_sets.end() || !it->second.l1) return false;
  auto& S = it->second;
  if (S.bare < 0) {
    const ArmorLayer& c = *S.l1; float s = c.w / 64.f;
    int x0 = (int)std::lround(40 * s), y0 = (int)std::lround(26 * s), w = (int)std::lround(16 * s), h = (int)std::lround(6 * s), n = 0;
    for (int y = y0; y < y0 + h && y < c.h; y++) for (int x = x0; x < x0 + w && x < c.w; x++) if (c.rgba[((size_t)y * c.w + x) * 4 + 3] >= 128) n++;
    S.bare = n < w * h * 0.25f ? 1 : 0;
  }
  return S.bare == 1;
}

const std::vector<ArmorPiece>& packArmorPieces(bool bareArms) {
  static std::vector<ArmorPiece> M[2];
  auto& out = M[bareArms ? 1 : 0];
  if (!out.empty()) return out;
  auto mk = [&](const char* name, int layer, vec3 lo, vec3 hi, int u, int v, int w, int h, int d, bool mirror, vec2 slice = vec2(0, 1)) {
    out.push_back({name, layer, lo, hi, u, v, w, h, d, mirror, slice});
  };
  mk("helmet", 1, {-5, -1, -5}, {5, 9, 5}, 0, 0, 8, 8, 8, false);
  mk("chest", 1, {-5, -1, -3}, {5, 13, 3}, 16, 16, 8, 12, 4, false);
  mk("waist", 2, {-4.5f, -0.5f, -2.5f}, {4.5f, 12.5f, 2.5f}, 16, 16, 8, 12, 4, false);
  for (int m = 0; m < 2; m++) {
    std::string S = m ? "L" : ""; bool mir = m == 1;
    mk(("pad" + S).c_str(), 1, {-3, -4, -3}, {3, 3, 3}, 40, 16, 4, 12, 4, mir, {0, 0.5f});
    if (bareArms) mk(("cuff" + S).c_str(), 1, {-2.9f, -7, -2.9f}, {2.9f, 0, 2.9f}, 16, 16, 4, 12, 4, mir, {0.5f, 1});
    else mk(("cuff" + S).c_str(), 1, {-3, -7, -3}, {3, 0, 3}, 40, 16, 4, 12, 4, mir, {0.5f, 1});
    mk(("thigh" + S).c_str(), 2, {-2.5f, -6, -2.5f}, {2.5f, 0.5f, 2.5f}, 0, 16, 4, 12, 4, mir, {0, 0.5f});
    mk(("knee" + S).c_str(), 2, {-2.5f, -6.5f, -2.5f}, {2.5f, 0, 2.5f}, 0, 16, 4, 12, 4, mir, {0.5f, 1});
    mk(("boot" + S).c_str(), 1, {-3, -7, -3}, {3, 0.02f, 3}, 0, 16, 4, 12, 4, mir, {0.5f, 1});
  }
  return out;
}

u32 skinBoxColor(const ArmorPiece& p, const u8* rgba, int W, int H, int TW, int TH, int face, float s, float t) {
  const float u = (float)p.u, v = (float)p.v, w = (float)p.w, h = (float)p.h, d = (float)p.d;
  vec4 top(u + d, v, w, d), bottom(u + d + w, v, w, d), right(u, v + d, d, h), front(u + d, v + d, w, h), left(u + d + w, v + d, d, h), back(u + d + w + d, v + d, w, h);
  if (p.mirror) std::swap(right, left);
  for (vec4* r : {&right, &front, &left, &back}) { r->y += p.slice.x * h; r->w = (p.slice.y - p.slice.x) * h; }
  const vec4* R = face == 0 ? &right : face == 1 ? &left : face == 2 ? &top : face == 3 ? &bottom : face == 4 ? &back : &front;
  if (p.mirror) s = 1 - s;
  float px = (R->x + clampv(s, 0.f, 0.999f) * R->z) / TW * W, py = (R->y + clampv(t, 0.f, 0.999f) * R->w) / TH * H;
  int x = clampv((int)px, 0, W - 1), y = clampv((int)py, 0, H - 1);
  const u8* c = rgba + ((size_t)y * W + x) * 4;
  return c[3] < 128 ? 0 : voxRGBA(c[0], c[1], c[2]);
}

void paintSkinBox(VoxelGrid& g, ivec3 at, const ArmorPiece& p, const u8* rgba, int W, int H, int TW, int TH, int vpp) {
  ivec3 n = glm::max(ivec3(glm::round((p.hi - p.lo) * (float)vpp)), ivec3(1));
  for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
    // the faces this voxel lies on, nearest first
    float c[3] = {(x + 0.5f) / n.x, (y + 0.5f) / n.y, (z + 0.5f) / n.z};
    bool shell = x == 0 || y == 0 || z == 0 || x == n.x - 1 || y == n.y - 1 || z == n.z - 1;
    if (!shell) continue;
    // per face: (s, t) from the normalised corner position (skins.js addSkinBox)
    struct F { int f; float s, t; bool on; } fs[6] = {
      {0, 1 - c[2], 1 - c[1], x == n.x - 1}, {1, c[2], 1 - c[1], x == 0}, {2, 1 - c[0], 1 - c[2], y == n.y - 1},
      {3, 1 - c[0], c[2], y == 0}, {4, c[0], 1 - c[1], z == n.z - 1}, {5, 1 - c[0], 1 - c[1], z == 0}};
    bool slicedTop = p.slice.x > 0, slicedBottom = p.slice.y < 1;   // the cut faces are inside the limb
    u32 col = 0;
    for (int k : {4, 5, 0, 1, 2, 3}) {   // front/back, then sides, then top/bottom win on edges
      if (!fs[k].on || (k == 2 && slicedTop) || (k == 3 && slicedBottom)) continue;
      col = skinBoxColor(p, rgba, W, H, TW, TH, fs[k].f, fs[k].s, fs[k].t);
      if (col) break;
    }
    if (col) g.set(at.x + x, at.y + y, at.z + z, col);
  }
}

} // namespace dq
