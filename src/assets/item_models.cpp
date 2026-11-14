// assets/item_models.cpp — the item model cache, block items, the sculpt, and which builder each item gets
// (the original's itemMesh chain: models3d.js -> sculpt.js -> items3d.js -> models.js).
#include "assets/item_build.h"
#include "assets/pack_models.h"
#include "assets/pottery3d.h"
#include "assets/shield3d.h"
#include <cstring>
#include <cstdlib>
#include "core/tool_parts.h"
#include "render/creature_models.h"
#include "render/vehicle_parts.h"
#include "common/threadpool.h"
#include "world/shapes.h"
#include <regex>
#include <map>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace dq {

// ------------------------------------------------------------------ tables (sculpt.js, skins.js)
namespace {
struct Rx { std::regex re; float v; };
const std::vector<Rx>& sizeTable() {
  static const std::vector<Rx> T = [] {
    std::vector<std::pair<const char*, float>> src = {
      {"^ender_pearl$|^ender_eye$", 0.5f},
      {"chorus_fruit$", 0.6f},
      {"nugget$|seeds$|^cocoa_beans$|^nether_wart_item$|berries$", 0.55f},
      {"^(diamond|emerald|ruby|sapphire|quartz|lapis_lazuli|amethyst_shard|frost_shard|cinderite_shard|aether_crystal|mese_crystal|heart_crystal|turtle_scute|dragon_scale|croc_scale)$", 0.6f},
      {"ball$|^snowball$|^egg$|^cinder_core$|^magma_cream$|^ink_sac$|^glow_ink_sac$|^venom_sac$|^spider_eye$|^fermented_spider_eye$|^ectoplasm$|^gust_puff$|^rabbit_foot$|^shark_tooth$", 0.6f},
      {"^(golden_)?apple$|potato$|^beetroot$|^cookie$|^aechor_petal$", 0.62f},
      {"_dye$|dust$|powder$|^sugar$|^flour$|^bone_meal$|^gunpowder$|^redstone$|^crushed_", 0.68f},
      {"^(coal|charcoal|flint|clay_ball|honeycomb)$", 0.65f},
      {"compass$|^clock$|charm$|^whistle$", 0.62f},
      {"potion_|bottle$|^hex_flask$", 0.7f},
      {"bucket$|^bowl$|_stew$|_soup$", 0.8f},
      {"^book$|^enchanted_book$|^tome_|^written_book$|^writable_book$|^lore_", 0.8f},
      {"^(raw_meat|cooked_meat|beef|cooked_beef|porkchop|cooked_porkchop|mutton|cooked_mutton|venison|cooked_venison|rabbit|cooked_rabbit|chicken|cooked_chicken|rotten_flesh)$", 0.78f},
      {"^(cod|cooked_cod|salmon|cooked_salmon|tropical_fish|pufferfish)$", 0.82f},
      {"carrot$|^bread$|melon_slice$|^pumpkin_pie$|^dried_kelp$|^wheat$", 0.8f},
      {"^(flint_and_steel|spyglass|shears|bone|feather|paper|leather|string)$", 0.8f},
      {"music_disc|^empty_map$|^filled_map$", 0.85f},
    };
    std::vector<Rx> out;
    for (auto& [r, v] : src) out.push_back({std::regex(r), v});
    return out;
  }();
  return T;
}
// how thick each kind of thing is: [scale, most half-thickness in pixels]
struct Profile { float scale, cap; };
Profile profileOf(const std::string& kind) {
  if (kind == "round") return {1.0f, 6};
  if (kind == "tool") return {0.9f, 2};
  if (kind == "blade") return {0.55f, 1.25f};
  if (kind == "thin") return {0.45f, 1};
  if (kind == "flat") return {0.25f, 0.75f};   // still a little body to them (the user's ask: nothing goes paper-flat)
  return {0.8f, 3.5f};
}
std::string sculptKind(const std::string& key, const ItemDef* def) {
  if (!def) return "default";
  if (!def->block.empty()) return "thin";                                       // saplings, flowers, doors as items
  auto re = [&](const char* r) { return rxSearch(key, r); };
  if (re("paper|map$|feather|^string$|_cape$|name_tag|pattern|^lore_")) return "flat";
  if (re("^leather$|^pelt$|hide$|saddle|music_disc|compass|clock|charm$|_scale$|^turtle_scute$|minecart|boat$|ship$")) return "thin";
  if ((def->hasTool && def->tool.type == "sword" && key != "smith_hammer") || re("dagger|blade$")) return "blade";
  if (def->hasTool || def->has("staff") || re("stick|rod$|bone$|^cinder_rod$|spyglass|horn$|whistle|tooth$|shard$")) return "tool";
  if (def->has("food") || re("ball$|egg|snowball|pearl|eye|seeds|berries|nugget|dust|powder|diamond|emerald|ruby|sapphire|quartz|crystal|lapis|coal|flint|bone_meal|_dye$|beans|wart|slime|cream|sac$|core$|heart|sugar|flour|crushed_|gunpowder|honeycomb|puff$|petal|ectoplasm")) return "round";
  return "default";
}
}

float itemSize(const std::string& key) {
  for (auto& r : sizeTable()) if (std::regex_search(key, r.re)) return r.v;
  return 1;
}

bool isHandheld(const std::string& key) {
  const ItemDef* d = reg().item(key);
  if (!d) return false;
  static const std::unordered_set<std::string> TYPES = {"pickaxe", "axe", "shovel", "sword", "hoe"};
  if (d->hasTool && TYPES.count(d->tool.type)) return true;
  static const std::regex re("stick|rod|wand|spear|staff|trident|mace");
  return std::regex_search(d->key, re);
}

// ------------------------------------------------------------------ the sculpt (sculpt.js extrudeTile)
// Every pixel stands as thick as the part of the picture it belongs to is wide: the "local thickness"
// (the biggest circle inside the shape covering the pixel), rounded off like a pebble, stepped in half pixels.
static float sculpt(const SpriteInfo& I, const std::string& kind, VoxelPainter& P) {
  const int R = I.R;
  if (I.empty()) return 0;
  auto A = [&](int i, int j) { return i >= 0 && j >= 0 && i < R && j < R && I.grid[j * R + i]; };
  Profile pr = profileOf(kind);
  const int RAD = 9;
  std::vector<float> rad(R * R, 0), loc(R * R, 0), H(R * R, 0);
  for (int j = 0; j < R; j++) for (int i = 0; i < R; i++) {
    if (!A(i, j)) continue;
    float best = RAD;
    for (int dj = -RAD; dj <= RAD; dj++) for (int di = -RAD; di <= RAD; di++) {
      float d = std::hypot((float)di, (float)dj);
      if (d >= best || (!di && !dj)) continue;
      if (!A(i + di, j + dj)) best = d;
    }
    rad[j * R + i] = std::max(0.65f, best - 0.35f);
  }
  for (int j = 0; j < R; j++) for (int i = 0; i < R; i++) {
    float r = rad[j * R + i]; if (!r) continue;
    int e = (int)std::ceil(r + 0.5f);
    for (int dj = -e; dj <= e; dj++) for (int di = -e; di <= e; di++) {
      int ii = i + di, jj = j + dj;
      if (ii < 0 || jj < 0 || ii >= R || jj >= R || !rad[jj * R + ii] || std::hypot((float)di, (float)dj) > r + 0.5f) continue;
      loc[jj * R + ii] = std::max(loc[jj * R + ii], r);
    }
  }
  float deepest = 0;
  for (int k = 0; k < R * R; k++) {
    if (!rad[k]) continue;
    float L = loc[k], r = rad[k];
    float h = pr.scale ? std::sqrt(std::max(0.f, L * L - (L - r) * (L - r))) * pr.scale : 0.5f;
    h = std::round(clampv(h, 0.5f, pr.cap) * 2) / 2;
    H[k] = h / R; deepest = std::max(deepest, H[k]);
  }
  // each pixel a column of voxels, its colour the picture's
  const float px = 1.f / R;
  for (int j = 0; j < R; j++) for (int i = 0; i < R; i++) {
    float h = H[j * R + i]; if (!h) continue;
    u32 c = I.raw[j * R + i]; if (!c) continue;
    float x0 = -0.5f + i * px, y1 = 0.5f - j * px;
    P.box(vec3(x0, y1 - px, -h), vec3(x0 + px, y1, h), [c](const PaintPt&) { return c; });
  }
  return deepest;
}

// ------------------------------------------------------------------ block items (data/itemshapes.json)
namespace {
const json& blockItems() {
  static json j = [] { auto d = readJson(dataDir() / "itemshapes.json"); return d && d->contains("blockItems") ? (*d)["blockItems"] : json::object(); }();
  return j;
}
// world/mesher.js SHAPE_ST: where a point of a box face (in block pixels) lands on its tile
inline vec2 shapeST(int f, float X, float Y, float Z) {
  switch (f) {
    case 0: return {16 - Z, 16 - Y};
    case 1: return {Z, 16 - Y};
    case 2: return {X, Z};
    case 3: return {16 - X, Z};
    case 4: return {X, 16 - Y};
    default: return {16 - X, 16 - Y};
  }
}
struct PxBox {
  float b[6]; int tile[6]; float tv;
  float fu[6] = {0, 0, 0, 0, 0, 0}, fv[6] = {0, 0, 0, 0, 0, 0};   // per face: the picture shifted (a shape's tu/tv: rods' sheets)
  u8 swapUV = 0;                                                  // per face (bit): the picture's axes swapped (uvT)
};

// Boxes in block pixels -> a voxel grid at 2 voxels per pixel. Opaque boxes are filled; a box whose
// faces show see-through pixels (glass, leaves, panes) keeps only the shell voxels under opaque
// pixels, so you look into it. Voxel value = box index + 1 | home face << 16; faces are coloured from
// their box's tiles (buildVoxelMesh faceColor).
void buildBoxes(const TileSet& ts, const std::vector<PxBox>& boxes, VoxelGrid& g, VoxelFaceColor& fc) {
  ivec3 lo(1 << 20), hi(-(1 << 20));
  for (auto& b : boxes) { lo = glm::min(lo, ivec3(glm::floor(vec3(b.b[0], b.b[1], b.b[2]) * 2.f))); hi = glm::max(hi, ivec3(glm::ceil(vec3(b.b[3], b.b[4], b.b[5]) * 2.f))); }
  // the grid covers 0..32 voxels (and more for shapes that reach out, like hanging lanterns' chains)
  lo = glm::min(lo, ivec3(0)); hi = glm::max(hi, ivec3(32));
  ivec3 off = -lo;
  g.resize(hi - lo);
  const int S = ts.size;
  auto pix = [&ts, S](const PxBox& b, int f, vec3 p) -> u32 {   // p in block pixels, on face f
    vec2 st = shapeST(f, p.x, p.y, p.z);
    if ((b.swapUV >> f) & 1) std::swap(st.x, st.y);
    st += vec2(b.fu[f], b.fv[f]);
    if (f != 2 && f != 3) st.y += b.tv;
    return tileVoxel(ts, b.tile[f], clampv((int)std::floor(st.x * S / 16), 0, S - 1), clampv((int)std::floor(st.y * S / 16), 0, S - 1));
  };
  for (size_t bi = 0; bi < boxes.size(); bi++) {
    const PxBox& b = boxes[bi];
    ivec3 a = ivec3(glm::round(vec3(b.b[0], b.b[1], b.b[2]) * 2.f)), c = ivec3(glm::round(vec3(b.b[3], b.b[4], b.b[5]) * 2.f));
    c = glm::max(c, a + 1);
    // does any face show a see-through pixel?
    bool hollow = false;
    for (int f = 0; f < 6 && !hollow; f++) {
      int ax = f / 2;
      int ua = (ax + 1) % 3, va = (ax + 2) % 3;
      for (int v = a[va]; v < c[va] && !hollow; v++) for (int u = a[ua]; u < c[ua]; u++) {
        vec3 p; p[ax] = (f & 1) ? a[ax] * 0.5f : c[ax] * 0.5f; p[ua] = (u + 0.5f) * 0.5f; p[va] = (v + 0.5f) * 0.5f;
        if (!pix(b, f, p)) { hollow = true; break; }
      }
    }
    for (int z = a.z; z < c.z; z++) for (int y = a.y; y < c.y; y++) for (int x = a.x; x < c.x; x++) {
      int on[6] = {x == c.x - 1, x == a.x, y == c.y - 1, y == a.y, z == c.z - 1, z == a.z};
      int home = -1;
      vec3 p((x + 0.5f) * 0.5f, (y + 0.5f) * 0.5f, (z + 0.5f) * 0.5f);
      for (int f : {2, 3, 4, 5, 0, 1}) if (on[f]) { vec3 q = p; q[f / 2] = (f & 1) ? a[f / 2] * 0.5f : c[f / 2] * 0.5f; if (pix(b, f, q)) { home = f; break; } }
      bool shell = on[0] || on[1] || on[2] || on[3] || on[4] || on[5];
      if (shell && home < 0) { if (hollow) continue; home = on[2] ? 2 : on[3] ? 3 : on[4] ? 4 : on[5] ? 5 : on[0] ? 0 : 1; }
      if (!shell) { if (hollow) continue; home = 2; }
      g.set(x + off.x, y + off.y, z + off.z, (u32)(bi + 1) | (u32)home << 16);
    }
  }
  fc = [&ts, boxes, off, pix](int x, int y, int z, int f, u32 v) -> u32 {
    int bi = (int)(v & 0xFFFF) - 1, home = (int)(v >> 16);
    if (bi < 0 || bi >= (int)boxes.size()) return 0;
    const PxBox& b = boxes[bi];
    ivec3 a = ivec3(glm::round(vec3(b.b[0], b.b[1], b.b[2]) * 2.f)), c = glm::max(ivec3(glm::round(vec3(b.b[3], b.b[4], b.b[5]) * 2.f)), a + 1);
    ivec3 q(x - off.x, y - off.y, z - off.z);
    vec3 p((q.x + 0.5f) * 0.5f, (q.y + 0.5f) * 0.5f, (q.z + 0.5f) * 0.5f);
    bool onFace = (f & 1) ? q[f / 2] == a[f / 2] : q[f / 2] == c[f / 2] - 1;
    int use = onFace ? f : home;
    vec3 s = p; s[use / 2] = (use & 1) ? a[use / 2] * 0.5f : c[use / 2] * 0.5f;
    u32 col = pix(b, use, s);
    if (!col && use != home) { s = p; s[home / 2] = (home & 1) ? a[home / 2] * 0.5f : c[home / 2] * 0.5f; col = pix(b, home, s); }
    return col;
  };
}

// the torch exactly as it stands on the ground: a stick two pixels square and ten tall, its picture's
// middle wrapped round it, the flame on top (items3d.js)
void buildTorch(const TileSet& ts, int tile, VoxelGrid& g, VoxelFaceColor& fc) {
  g.resize(ivec3(4, 20, 4));
  for (int z = 0; z < 4; z++) for (int y = 0; y < 20; y++) for (int x = 0; x < 4; x++) g.set(x, y, z, 1);
  const int S = ts.size, k = std::max(1, S / 16);
  // the stick: columns 7..9 of the picture, rows 6..16 down the sides, 6..8 on top, 14..16 underneath
  fc = [&ts, tile, S, k](int x, int y, int z, int f, u32) -> u32 {
    int col = 0, row = 0; bool flame = false;
    switch (f) {
      case 0: col = 3 - z; row = 19 - y; break;
      case 1: col = z; row = 19 - y; break;
      case 4: col = x; row = 19 - y; break;
      case 5: col = 3 - x; row = 19 - y; break;
      case 2: col = x; row = z; flame = true; break;
      default: col = x; row = 16 + z; break;
    }
    if (f != 2 && f != 3) flame = row < 4;
    u32 c = tileVoxel(ts, tile, clampv(7 * k + col * k / 2, 0, S - 1), clampv(6 * k + row * k / 2, 0, S - 1));
    if (c && flame) c = (c & 0xFFFFFFu) | (u32)VOXEL_GLOW << 24;   // the flame glows
    return c;
  };
}
} // namespace

// ------------------------------------------------------------------ mixed tools (core/tool_parts.h): the iron tool's model with
// each head section in its material's colour, along the head from one end to the other (the grid's order: a pickaxe's left,
// middle and right; a sword's tip, then the base), the light and shade of each voxel kept; a haft other than a stick in its
// colour; and a gem set in the middle of the head.
static thread_local const std::function<void(VoxelGrid&)>* g_recolour = nullptr;
static thread_local int g_flameFrame = 0;   // a torch's flame: which frame of the pack's animated flame ("flame<n>:")
// a mix's flecks come in clumps of 2x2x2 voxels (streaks of each metal, not single-voxel noise)
static u32 clumpHash(int x, int y, int z) { return (u32)(x >> 1) * 73856093u ^ (u32)(y >> 1) * 19349663u ^ (u32)(z >> 1) * 83492791u; }
static void recolourPartTool(VoxelGrid& g, const std::string& kind, const std::vector<std::string>& mats, const std::string& haft, const std::string& gem) {
  struct V { size_t i; vec2 p; float lum; bool head; u32 clump; };
  std::vector<V> vs; vs.reserve(4096);
  auto lumOf = [](u32 c) { return ((c & 255) * 0.299f + ((c >> 8) & 255) * 0.587f + ((c >> 16) & 255) * 0.114f) / 255.f; };
  vec2 hm(0), sm(0); int nh = 0, ns = 0; float lh = 0, ls = 0;
  for (int z = 0; z < g.size.z; z++) for (int y = 0; y < g.size.y; y++) for (int x = 0; x < g.size.x; x++) {
    const u32 c = g.at(x, y, z); if (!c) continue;
    const float r = (c & 255) / 255.f, gg = ((c >> 8) & 255) / 255.f, b = ((c >> 16) & 255) / 255.f;
    const float mx = std::max({r, gg, b}), mn = std::min({r, gg, b}), sat = mx > 0 ? (mx - mn) / mx : 0;
    float hue = 0;
    if (mx > mn) { if (mx == r) hue = std::fmod((gg - b) / (mx - mn), 6.f) / 6.f; else if (mx == gg) hue = ((b - r) / (mx - mn) + 2) / 6.f; else hue = ((r - gg) / (mx - mn) + 4) / 6.f; if (hue < 0) hue += 1; }
    const bool handle = hue > 0.02f && hue < 0.16f && sat > 0.22f && mx < 0.85f;   // the wooden stick's browns
    V v{g.idx(x, y, z), vec2((float)x, (float)y), lumOf(c), !handle, clumpHash(x, y, z)};
    if (v.head) { hm += v.p; nh++; lh += v.lum; } else { sm += v.p; ns++; ls += v.lum; }
    vs.push_back(v);
  }
  if (!nh) return;
  hm /= (float)nh; lh /= nh; if (ns) { sm /= (float)ns; ls /= ns; }
  // the head's long axis
  float cxx = 0, cxy = 0, cyy = 0;
  for (auto& v : vs) if (v.head) { vec2 d = v.p - hm; cxx += d.x * d.x; cxy += d.x * d.y; cyy += d.y * d.y; }
  const float ang = 0.5f * std::atan2(2 * cxy, cxx - cyy);
  vec2 e(std::cos(ang), std::sin(ang));
  const bool fromHandle = kind == "sword" || kind == "axe" || kind == "hoe";
  if (fromHandle ? (ns && glm::dot(e, hm - sm) < 0) : e.x < 0) e = -e;
  float t0 = 1e9f, t1 = -1e9f;
  for (auto& v : vs) if (v.head) { float t = glm::dot(v.p - hm, e); t0 = std::min(t0, t); t1 = std::max(t1, t); }
  const int n = (int)mats.size();
  auto colour = [](vec3 rgb, float k, u32 old) { return voxRGBA((int)(rgb.x * 255 * k), (int)(rgb.y * 255 * k), (int)(rgb.z * 255 * k), (u8)(old >> 24)); };
  std::vector<MaterialPaint> paint; for (auto& m : mats) paint.push_back(materialPaint(m));
  if (n) for (auto& v : vs) if (v.head) {
    int sec = (int)((glm::dot(v.p - hm, e) - t0) / std::max(1e-3f, t1 - t0) * n); sec = clampv(sec, 0, n - 1);
    if (kind == "sword") sec = n - 1 - sec;   // the grid's first is the sword's tip, the far end from the hilt
    if (toolMaterial(mats[sec])) g.c[v.i] = colour(paint[sec].at(v.clump), clampv(v.lum / std::max(lh, 0.05f), 0.35f, 1.7f), g.c[v.i]);
  }
  static const std::map<std::string, vec3> HAFT = {{"bone", vec3(0.91f, 0.89f, 0.8f)}, {"cinder_rod", vec3(0.82f, 0.36f, 0.16f)}, {"steel_ingot", vec3(0.59f, 0.63f, 0.7f)}};
  if (auto hi = HAFT.find(haft); hi != HAFT.end() && ns) for (auto& v : vs) if (!v.head) g.c[v.i] = colour(hi->second, clampv(v.lum / std::max(ls, 0.05f), 0.4f, 1.6f), g.c[v.i]);
  static const std::map<std::string, vec3> GEM = {{"diamond", vec3(0.4f, 0.95f, 0.9f)}, {"emerald", vec3(0.2f, 0.88f, 0.42f)}, {"amethyst_shard", vec3(0.68f, 0.44f, 0.92f)},
    {"ruby", vec3(0.92f, 0.16f, 0.24f)}, {"sapphire", vec3(0.22f, 0.36f, 0.92f)}, {"quartz", vec3(0.96f, 0.94f, 0.9f)}, {"red_crystal", vec3(0.88f, 0.2f, 0.24f)},
    {"charged_crystal", vec3(1.f, 0.5f, 0.3f)}, {"cinderite_shard", vec3(0.95f, 0.45f, 0.16f)}};
  if (auto gi = GEM.find(gem); gi != GEM.end()) {   // the gem: set in the middle of the head
    // (set in the middle of a tool's head; a sword's or a dagger's at its guard, where the blade meets the crossguard: the
    //  ricasso, as a jeweller sets one)
    float tm = (t0 + t1) * 0.5f;
    if (kind == "sword" || kind == "dagger") {   // the crossguard: where the head is widest across; the gem just past it, on the blade
      const int nb = std::max(4, (int)std::ceil(t1 - t0) + 1);
      std::vector<float> lo(nb, 1e9f), hi(nb, -1e9f);
      for (auto& v : vs) if (v.head) {
        const vec2 d = v.p - hm; const int b = clampv((int)std::floor(glm::dot(d, e) - t0), 0, nb - 1);
        const float ac = glm::dot(d, vec2(-e.y, e.x)); lo[b] = std::min(lo[b], ac); hi[b] = std::max(hi[b], ac);
      }
      int gb = 0; float gw = -1;
      for (int b = 0; b < nb; b++) if (hi[b] - lo[b] > gw) { gw = hi[b] - lo[b]; gb = b; }
      // (the blade's side of the guard: the longer way from it)
      const float dir = (t1 - (t0 + gb)) > ((t0 + gb) - t0) ? 1.f : -1.f;
      tm = t0 + gb + 0.5f + dir * 2.5f;
    }
    for (auto& v : vs) if (v.head) {
      const vec2 d = v.p - hm; const float along = glm::dot(d, e) - tm, across = glm::dot(d, vec2(-e.y, e.x));
      if (along * along + across * across <= 9.f) g.c[v.i] = colour(gi->second, 0.85f + 0.3f * (v.lum > lh ? 1.f : 0.f), gem == "charged_crystal" ? (u32)VOXEL_GLOW << 24 : g.c[v.i]);
    }
  }
}

// mixed armour: the piece's pattern laid over its model (front view), each voxel in the colour of the cell it falls in (the
// nearest cell of the pattern where the pattern has a gap), its light and shade kept
// the wooden haft's voxels (the browns recolourPartTool leaves as the handle) taken away: a loose head
static void stripHaft(VoxelGrid& g) {
  for (int z = 0; z < g.size.z; z++) for (int y = 0; y < g.size.y; y++) for (int x = 0; x < g.size.x; x++) {
    const u32 c = g.at(x, y, z); if (!c) continue;
    const float r = (c & 255) / 255.f, gg = ((c >> 8) & 255) / 255.f, b = ((c >> 16) & 255) / 255.f;
    const float mx = std::max({r, gg, b}), mn = std::min({r, gg, b}), sat = mx > 0 ? (mx - mn) / mx : 0;
    float hue = 0;
    if (mx > mn) { if (mx == r) hue = std::fmod((gg - b) / (mx - mn), 6.f) / 6.f; else if (mx == gg) hue = ((b - r) / (mx - mn) + 2) / 6.f; else hue = ((r - gg) / (mx - mn) + 4) / 6.f; if (hue < 0) hue += 1; }
    if (hue > 0.02f && hue < 0.16f && sat > 0.22f && mx < 0.85f) g.set(x, y, z, 0);
  }
}
static void recolourPartArmor(VoxelGrid& g, const std::string& piece, const std::vector<std::string>& mats) {
  const ArmorPattern* P = armorPattern(piece);
  if (!P || mats.size() != P->cells.size()) return;
  ivec3 lo, hi; if (!g.bounds(lo, hi)) return;
  auto lumOf = [](u32 c) { return ((c & 255) * 0.299f + ((c >> 8) & 255) * 0.587f + ((c >> 16) & 255) * 0.114f) / 255.f; };
  float mean = 0; int n = 0;
  for (u32 c : g.c) if (c) { mean += lumOf(c); n++; }
  if (!n) return;
  mean /= n;
  std::vector<MaterialPaint> paint; for (auto& m : mats) paint.push_back(materialPaint(m));
  for (int z = lo.z; z < hi.z; z++) for (int y = lo.y; y < hi.y; y++) for (int x = lo.x; x < hi.x; x++) {
    u32& c = g.c[g.idx(x, y, z)]; if (!c) continue;
    const float u = (x - lo.x + 0.5f) / (hi.x - lo.x) * P->w, v = (1 - (y - lo.y + 0.5f) / (hi.y - lo.y)) * P->h;
    int best = 0; float bd = 1e9f;
    for (size_t i = 0; i < P->cells.size(); i++) { const float dx = P->cells[i].x + 0.5f - u, dy = P->cells[i].y + 0.5f - v, d = dx * dx + dy * dy; if (d < bd) { bd = d; best = (int)i; } }
    if (!toolMaterial(mats[best])) continue;
    const vec3 rgb = paint[best].at(clumpHash(x, y, z));
    const float k = clampv(lumOf(c) / std::max(mean, 0.05f), 0.35f, 1.7f);
    c = voxRGBA((int)std::min(255.f, rgb.x * 255 * k), (int)std::min(255.f, rgb.y * 255 * k), (int)std::min(255.f, rgb.z * 255 * k), (u8)(c >> 24));
  }
}

// ------------------------------------------------------------------ building one model
static void finishGrid(VoxelModel& out, VoxelGrid& g, const VoxelFrame& fr, const VoxelFaceColor* fc) {
  if (g_recolour && !fc) (*g_recolour)(g);
  ivec3 lo, hi;
  out.frame = fr;
  if (!g.bounds(lo, hi)) { out.size = ivec3(0); out.data.verts.clear(); return; }
  if (!fc && (lo != ivec3(0) || hi != g.size)) {   // crop to what's there (positions stay small)
    VoxelGrid c(hi - lo);
    for (int z = lo.z; z < hi.z; z++) for (int y = lo.y; y < hi.y; y++) for (int x = lo.x; x < hi.x; x++) c.set(x - lo.x, y - lo.y, z - lo.z, g.at(x, y, z));
    g = std::move(c);
    out.frame.origin += vec3(lo) * fr.voxel;
  }
  out.size = g.size;
  buildVoxelMesh(g, out.data, fc);
}


// A block item whose block isn't a plain cube (furniture, beds, hoppers, levers, lanterns, doors...) and has no entry of its own in
// itemshapes.json: its real shape, the boxes the mesher draws it with (world/shapes.h), tiles picked the way the mesher picks them.
// false = leave it flat (plants, panes, rails, dust, crops).
namespace {
struct AloneNb : ShapeNb {
  BlockId id(int, int, int) const override { return 0; }
  u8 meta(int, int, int) const override { return 0; }
};
struct PaneRunNb : ShapeNb {   // a pane or bars in a run, east to west (alone they're only the post)
  BlockId self = 0;
  BlockId id(int dx, int dy, int dz) const override { return dy == 0 && dz == 0 && dx != 0 ? self : 0; }
  u8 meta(int, int, int) const override { return 0; }
};
bool shapedBlockBoxes(const ItemDef& def, std::vector<PxBox>& out) {
  static std::once_flag once; std::call_once(once, [] { initBlockTraits(); });
  const Registry& R = reg();
  BlockId id = R.blockId(def.block);
  if (!id) return false;
  const BlockDef& bd = R.block(id);
  if (bd.render == Render::Cross || bd.render == Render::Torch || bd.render == Render::Liquid || bd.render == Render::None) return false;
  if (R.tShape[id] == ShapeKind::Wall) {   // a wall on its own: a post and an arm through it, as it stands between two others
    const int side = bd.faces[0], top = bd.faces[2], bot = bd.faces[3];
    for (const PxBox& b0 : {PxBox{{4, 0, 4, 12, 16, 12}, {}, 0}, PxBox{{0, 0, 5, 16, 13, 11}, {}, 0}}) {
      PxBox b = b0; for (int f = 0; f < 6; f++) b.tile[f] = f == 2 ? top : f == 3 ? bot : side; out.push_back(b);
    }
    return true;
  }
  const BlockTraits& T = traits(id);
  if (T.crop || T.flower || T.stackPlant || T.tripwire || T.chorus || T.rs == RsRole::Wire || T.rs == RsRole::Hook) return false;   // (rails and panes: their own shape now)
  AloneNb alone; PaneRunNb run; run.self = id;
  const ShapeNb& nb = joinsPane(id) ? static_cast<const ShapeNb&>(run) : static_cast<const ShapeNb&>(alone);
  BlockId gid = id;   // the block gathered (a two-block statue's top too)
  auto gather = [&](u8 meta, float dy, int side, int top, int bot, float dz = 0) -> bool {
    std::vector<ShapeBox> sb;
    if (!shapeBoxes(gid, meta, nb, ShapePurpose::Draw, sb)) return false;
    for (auto& b : sb) {
      PxBox p{}; for (int i = 0; i < 6; i++) p.b[i] = b.b[i];
      p.b[1] += dy; p.b[4] += dy; p.b[2] += dz; p.b[5] += dz;
      int st = b.side >= 0 ? b.side : side, tt = b.end >= 0 ? b.end : top, bt = b.end >= 0 ? b.end : bot;
      for (int f = 0; f < 6; f++) p.tile[f] = b.hasFaces && b.faces[f] >= 0 ? b.faces[f] : f == 2 ? tt : f == 3 ? bt : st;
      if (b.only == 63 && !b.tu && !b.uvT) { out.push_back(p); continue; }
      // a part drawing some faces only, its picture shifted (the rods: one box, its sides and ends from different
      // corners of the sheet): its faces go onto the box of the same size, made once
      PxBox* q = nullptr;
      for (auto& o : out) if (std::equal(o.b, o.b + 6, p.b)) { q = &o; break; }
      if (!q) { out.push_back(p); q = &out.back(); }
      for (int f = 0; f < 6; f++) if ((b.only >> f) & 1) {
        q->tile[f] = p.tile[f]; q->fu[f] = b.tu; q->fv[f] = b.tv;
        if (b.uvT) q->swapUV |= (u8)(1 << f); else q->swapUV &= (u8)~(1 << f);
      }
    }
    return true;
  };
  int side = bd.faces[0], top = bd.faces[2], bot = bd.faces[3];
  if (T.door) top = bot = side;
  const bool bed = R.tShape[id] == ShapeKind::Bed || bd.has("bed");
  if (!gather(bed ? 4 : 0, 0, side, top, bot) || out.empty()) { out.clear(); return false; }   // (a bed: its head half first)
  if (bed) gather(0, 0, side, top, bot, 16);   // ...and its foot behind it: the whole bed, two blocks long
  bool tall = false;   // two blocks tall (a door, a waystone, a statue): the whole thing, brought down to a block's height in the hand
  if (T.door && T.upperTile >= 0) { gather(16, 16, T.upperTile, T.upperTile, T.upperTile); tall = true; }
  if (const std::string up = bd.str("statueLower"); !up.empty()) if (const BlockId uid = R.blockId(up)) {
    const BlockDef& ub = R.block(uid);
    gid = uid; gather(0, 16, ub.faces[0], ub.faces[2], ub.faces[3]); gid = id; tall = true;
  }
  if (tall) for (auto& b : out) for (int i = 0; i < 6; i++) b.b[i] = i % 3 == 1 ? b.b[i] * 0.5f : 8 + (b.b[i] - 8) * 0.5f;
  if (bed) for (auto& b : out) for (int i = 0; i < 6; i++) b.b[i] = i % 3 == 2 ? b.b[i] * 0.5f : i % 3 == 0 ? 8 + (b.b[i] - 8) * 0.5f : b.b[i] * 0.5f;   // (the whole bed in a block's room)
  return true;
}
}

namespace { std::shared_ptr<const TileSet>& tilesSnap() { static std::shared_ptr<const TileSet> t; return t; } std::mutex g_snapMx; }

// Eating Animation's bitten frames (its "eat" predicate picks <food>_eating_0..2 by use progress), carved from our own
// model instead of drawn: bite n takes n round bites out of the top corner, the scalloped edge of each showing the paler
// inside of the food
void biteVoxelGrid(VoxelGrid& g, int n) {
  ivec3 lo, hi;
  if (!g.bounds(lo, hi)) return;
  const float w = float(hi.x - lo.x), h = float(hi.y - lo.y), r = 0.27f * std::max(w, h);
  struct Bite { float x, y, r; };
  const Bite bites[3] = {{hi.x - 0.04f * w, hi.y - 0.30f * h, r}, {hi.x - 0.36f * w, hi.y - 0.02f * h, r * 1.05f}, {hi.x - 0.22f * w, hi.y - 0.52f * h, r * 1.3f}};
  auto bitten = [&](float x, float y) {
    for (int i = 0; i < n && i < 3; i++) {
      const float dx = x - bites[i].x, dy = y - bites[i].y, a = std::atan2(dy, dx);
      if (dx * dx + dy * dy < bites[i].r * bites[i].r * (1 + 0.1f * std::sin(a * 9 + i)) * (1 + 0.1f * std::sin(a * 9 + i))) return true;   // (teeth marks)
    }
    return false;
  };
  std::vector<u8> gone(g.c.size(), 0);
  for (int z = 0; z < g.size.z; z++) for (int y = 0; y < g.size.y; y++) for (int x = 0; x < g.size.x; x++)
    if (g.solid(x, y, z) && bitten(x + 0.5f, y + 0.5f)) { gone[g.idx(x, y, z)] = 1; g.set(x, y, z, 0); }
  for (int z = 0; z < g.size.z; z++) for (int y = 0; y < g.size.y; y++) for (int x = 0; x < g.size.x; x++) {
    u32& c = g.c[g.idx(x, y, z)];
    if (!c) continue;
    bool edge = false;
    for (ivec3 d : {ivec3(1, 0, 0), ivec3(-1, 0, 0), ivec3(0, 1, 0), ivec3(0, -1, 0)})
      if (g.in(x + d.x, y + d.y, z + d.z) && gone[g.idx(x + d.x, y + d.y, z + d.z)]) edge = true;
    if (!edge) continue;   // the bitten face: the food's own colour, lighter (an apple's flesh, bread's crumb)
    const int r = c & 255, gg = (c >> 8) & 255, b = (c >> 16) & 255;
    c = voxRGBA(r + (238 - r) * 45 / 100, gg + (226 - gg) * 45 / 100, b + (196 - b) * 45 / 100, (u8)(c >> 24));
  }
}

std::string eatModelKey(const std::string& model, float progress) {
  if (model.empty() || progress <= 0) return model;
  const int n = std::min(3, 1 + (int)(progress * 3));
  return "eat" + std::to_string(n) + ":" + model;
}

bool buildItemModel(const std::string& key, VoxelModel& out) {
  if (key.size() > 5 && startsWith(key, "eat") && key[4] == ':' && key[3] >= '1' && key[3] <= '3') {   // "eat<n>:<model>": bitten n times
    const int n = key[3] - '0';
    const std::function<void(VoxelGrid&)> fn = [n](VoxelGrid& g) { biteVoxelGrid(g, n); };
    const auto* prev = g_recolour; g_recolour = &fn;
    const bool ok = buildItemModel(key.substr(5), out);
    g_recolour = prev;
    out.key = key;
    return ok;
  }
  if (startsWith(key, "flame") && key.find(':') != std::string::npos) {   // "flame<n>:<torch>": its flame at frame n
    const size_t c = key.find(':');
    const int prevF = g_flameFrame; g_flameFrame = std::atoi(key.substr(5, c - 5).c_str());
    const bool ok = buildItemModel(key.substr(c + 1), out);
    g_flameFrame = prevF;
    out.key = key;
    return ok;
  }
  if (startsWith(key, "pa:")) {   // mixed armour: "pa:<piece>:<m1;m2;...>"
    const size_t c1 = key.find(':', 3);
    const std::string piece = key.substr(3, c1 == std::string::npos ? std::string::npos : c1 - 3), list = c1 == std::string::npos ? "" : key.substr(c1 + 1);
    const std::vector<std::string> mats = modelKeyMaterials(list);
    const std::function<void(VoxelGrid&)> fn = [&](VoxelGrid& g) { recolourPartArmor(g, piece, mats); };
    const auto* prev = g_recolour; g_recolour = &fn;
    const bool ok = buildItemModel("iron_" + piece, out);
    g_recolour = prev;
    out.key = key;
    return ok;
  }
  if (startsWith(key, "mc:")) {   // a cast or forged head, or an ingot, of some makeup: "mc:<item>:<material>" (core/metals.h), its metal recoloured
    const size_t a = key.find(':', 3);
    if (a == std::string::npos) return false;
    const std::string item = key.substr(3, a - 3), mat = key.substr(a + 1);
    const std::function<void(VoxelGrid&)> fn = [&](VoxelGrid& g) { recolourPartTool(g, "", {mat}, "", ""); };
    const auto* prev = g_recolour; g_recolour = &fn;
    const bool ok = buildItemModel(item, out);
    g_recolour = prev;
    out.key = key;
    return ok;
  }
  if (startsWith(key, "pt:")) {   // a mixed tool, or a tool with a gem: "pt:<kind>:<m1;m2;..>:<haft>:<gem>" / "pt:=<item>:::<gem>"
    std::vector<std::string> f; size_t a = 3;
    for (size_t b; (b = key.find(':', a)) != std::string::npos; a = b + 1) f.push_back(key.substr(a, b - a));
    f.push_back(key.substr(a));
    while (f.size() < 4) f.push_back("");
    const std::vector<std::string> mats = modelKeyMaterials(f[1]);
    const bool plain = !f[0].empty() && f[0][0] == '=';
    const std::string baseKey = plain ? f[0].substr(1) : partToolBaseItem(f[0]), kind = plain ? (reg().item(baseKey) && reg().item(baseKey)->hasTool ? reg().item(baseKey)->tool.type : "") : partToolBaseKind(f[0]);
    const std::function<void(VoxelGrid&)> fn = [&](VoxelGrid& g) { recolourPartTool(g, kind, mats, f[2], f[3]); };
    const auto* prev = g_recolour; g_recolour = &fn;
    const bool ok = buildItemModel(baseKey, out);
    g_recolour = prev;
    out.key = key;
    return ok;
  }
  if (startsWith(key, "mf:")) {   // a filled mold: "mf:<item>:<material>", its metal recoloured to that makeup (assets/pottery3d.h)
    const size_t a = key.find(':', 3);
    if (a == std::string::npos) return false;
    const std::string item = key.substr(3, a - 3), mat = key.substr(a + 1);
    VoxelGrid g; VoxelFrame fr; float depth; bool stand; const vec3 grey(186, 186, 192);
    if (!buildPotteryGrid(item, g, fr, depth, stand, &grey)) return false;
    recolourPartTool(g, "", {mat}, "", "");   // (the fired clay's browns read as a haft: only the metal takes the colour)
    out.key = key; out.built = true; out.kind = VoxelModel::Flat; out.stand = stand; out.depth = depth;
    const auto* prev = g_recolour; g_recolour = nullptr;
    finishGrid(out, g, fr, nullptr);
    g_recolour = prev;
    return true;
  }
  // a loose tool head (knapped, cast or forged): its tool's own model with the wooden haft taken off
  {
    std::string tool, headMat;
    auto ends = [&](const char* e) { const size_t n = std::strlen(e); return key.size() > n && key.compare(key.size() - n, n, e) == 0; };
    if (key == "crude_pickaxe_head") tool = "crude_pickaxe";
    else if (ends("_head") && (startsWith(key, "stone_") || startsWith(key, "flint_") || startsWith(key, "metal_"))) {
      const std::string mat = key.substr(0, key.find('_')), kind = key.substr(mat.size() + 1, key.size() - mat.size() - 1 - 5);
      tool = mat == "metal" ? partToolBaseItem(kind) : mat + "_" + kind;
      if (!reg().item(tool)) tool = partToolBaseItem(kind);
    } else if (ends("_head") && reg().item(key.substr(0, key.size() - 5)) && reg().item(key.substr(0, key.size() - 5))->hasTool) {
      // the Tool Forge's heads (copper_pickaxe_head...): the iron tool's head (grey, so it can't be taken for the wooden haft),
      // coloured as the material's own (core/tool_parts.h materials)
      const std::string t = key.substr(0, key.size() - 5), kind = t.substr(t.rfind('_') + 1), mat = t.substr(0, t.rfind('_'));
      tool = partToolBaseItem(kind);
      if (!reg().item(tool)) tool = t;
      else if (toolMaterial(mat)) headMat = mat;
    }
    if (!tool.empty() && reg().item(tool)) {
      const auto* prev = g_recolour;
      const std::function<void(VoxelGrid&)> fn = [&](VoxelGrid& g) { stripHaft(g); if (!headMat.empty()) recolourPartTool(g, "", {headMat}, "", ""); if (prev) (*prev)(g); };
      g_recolour = &fn;
      const bool ok = buildItemModel(tool, out);
      g_recolour = prev;
      out.key = key;
      return ok;
    }
  }
  if (const ItemDef* sd = reg().item(key); key == "shield" || (sd && sd->has("shield"))) {   // modelled, straps and all (assets/shield3d.h)
    VoxelGrid g; VoxelFrame fr; float depth;
    if (buildShieldGrid(g, fr, depth)) {
      out.key = key; out.built = true; out.kind = VoxelModel::Flat; out.stand = false; out.depth = depth;
      finishGrid(out, g, fr, nullptr);
      return true;
    }
  }
  {   // the immersive pottery, molds and firestarter: modelled (assets/pottery3d.h), not sculpted from a picture
    VoxelGrid g; VoxelFrame fr; float depth; bool stand;
    if (buildPotteryGrid(key, g, fr, depth, stand)) {
      out.key = key; out.built = true; out.kind = VoxelModel::Flat; out.stand = stand; out.depth = depth;
      finishGrid(out, g, fr, nullptr);
      return true;
    }
  }
  std::shared_ptr<const TileSet> tsp;
  { std::lock_guard<std::mutex> lk(g_snapMx); tsp = tilesSnap(); }
  if (!tsp) return false;
  const TileSet& ts = *tsp;
  out.key = key; out.built = true;
  std::string base = key;
  if (startsWith(key, "bow_pulling_") || startsWith(key, "bow_pq")) base = "bow";   // bow_pqN: drawn N/8 of the way (first person)
  if (key == "fishing_rod_cast" || key == "fishing_rod_tp") base = "fishing_rod";
  const ItemDef* def = reg().item(base);
  auto tileOf = [&](const std::string& n) { auto it = ts.index.find(n); return it == ts.index.end() ? -1 : it->second; };

  // ---- mob heads and skulls: the creature's own head (they have a helmet slot to be worn, but aren't helmets)
  if (def && def->has("mobHead")) {
    VoxelGrid g; ivec3 lo32;
    if (creatureHeadGrid(def->str("mobHead"), ts, g, lo32)) {
      VoxelFrame fr; fr.voxel = 1.f / 32; fr.origin = vec3(-0.5f) + vec3(lo32) / 32.f;
      out.kind = VoxelModel::Block; out.depth = 0.5f;
      finishGrid(out, g, fr, nullptr);
      return true;
    }
  }
  // ---- a chain: links of iron, each turned a quarter from the last, hanging in a line
  if (base == "chain") {
    const int ct = tileOf("chain");
    const u32 avg = ct >= 0 ? (ts.avgColor(ct) | 0xFF000000u) : voxRGBA(70, 74, 86);
    const int ar = ((avg & 255) + 140) / 2, ag = (((avg >> 8) & 255) + 146) / 2, ab = (((avg >> 16) & 255) + 158) / 2;   // (iron grey: the tile is mostly its dark gaps)
    VoxelGrid g(ivec3(32, 32, 32));
    const float a = 5.f, bh = 6.8f, wire = 1.7f;   // a link's half width, half height, the iron's half thickness (voxels)
    for (int k = 0; k < 3; k++) {
      const float yc = 7.f + k * 9.f;   // three links, each through the next
      for (int z = 0; z < 32; z++) for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        const float u = (k & 1 ? z : x) + 0.5f - 16, v = y + 0.5f - yc, w = (k & 1 ? x : z) + 0.5f - 16;
        // distance to the oval (a stadium: two half circles and straight sides)
        const float sy = std::max(0.f, std::fabs(v) - (bh - a));
        const float d = std::fabs(std::hypot(u, sy) - a);
        if (std::hypot(d, w) > wire) continue;
        const float lit = 0.75f + 0.45f * clampv((v + u * 0.5f) / bh * 0.5f + 0.5f, 0.f, 1.f) - (std::fabs(w) > wire * 0.6f ? 0.12f : 0.f);
        g.set(x, y, z, voxRGBA((int)(ar * lit), (int)(ag * lit), (int)(ab * lit)));
      }
    }
    VoxelFrame fr; fr.voxel = 1.f / 32; fr.origin = vec3(-0.5f);
    out.kind = VoxelModel::Block; out.depth = 0.5f;
    finishGrid(out, g, fr, nullptr);
    return true;
  }
  // ---- vehicles and machines (minecarts, boats, the ship, the waterwheel, the windmill): the very model that rides the
  // rails or the water (render/vehicle_parts.h), brought down to an item's size
  if (std::vector<ModelPart> parts = vehicleItemParts(base); !parts.empty()) {
    vec3 lo(1e9f), hi(-1e9f);
    for (auto& p : parts) for (int k = 0; k < 8; k++) { const vec3 c = p.c + p.R * (vec3(k & 4 ? 1 : -1, k & 2 ? 1 : -1, k & 1 ? 1 : -1) * p.h); lo = glm::min(lo, c); hi = glm::max(hi, c); }
    const float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 0.1f}), vox = std::max(1.f / 32, ext / 56);   // (a big ship at a coarser grain)
    VoxelGrid g; vec3 glo; VoxelFaceColor fc;
    if (partsGrid(parts, vox, ts, g, glo, fc)) {
      const float s = 1.f / ext; const vec3 mid = (lo + hi) * 0.5f;
      VoxelFrame fr; fr.voxel = vox * s; fr.origin = (glo - mid) * s;
      out.kind = VoxelModel::Block; out.depth = 0.5f;
      finishGrid(out, g, fr, &fc);
      return true;
    }
  }
  // ---- block items
  if (def && !def->block.empty()) {
    const BlockId cb = reg().blockId(def->block);
    if (cb && reg().block(cb).render == Render::Cutout && key.find("crystal") != std::string::npos && !reg().tLeaves[cb]) {   // crystal blocks: a cluster of crystals, not a see-through cube
      struct GlassOn { bool prev; GlassOn() : prev(g_tileVoxelGlass) { g_tileVoxelGlass = true; } ~GlassOn() { g_tileVoxelGlass = prev; } } glassOn;   // (their pictures are mostly see-through glass)
      SpriteInfo I = SpriteInfo::of(ts, reg().block(cb).faces[0]);
      VoxelPainter P(ivec3(52, 52, 52), vec3(-26.f / 32), 1.f / 32);
      ItemBuild b{ts, P, I, key, def};
      if (!I.empty() && buildModel3d("crystal", b)) {
        out.kind = b.kind; out.stand = b.stand; out.depth = b.depth;
        finishGrid(out, P.g, P.frame, nullptr);
        return true;
      }
    }
  }
  if (def && !def->block.empty()) {
    const BlockId gb = reg().blockId(def->block);
    struct GlassOn { bool prev; GlassOn(bool on) : prev(g_tileVoxelGlass) { g_tileVoxelGlass = on; } ~GlassOn() { g_tileVoxelGlass = prev; } } glassOn(gb && reg().tTrans[gb] && reg().block(gb).has("color"));   // stained glass (and its panes): the tint shows
    const json& bi = blockItems();
    auto it = bi.find(key);
    std::string k = it != bi.end() ? (*it)["k"].get<std::string>() : "sprite";
    VoxelGrid g; VoxelFaceColor fc;
    if (it == bi.end()) {   // a block added since itemshapes.json was made (the Tool Forge, the BTA blocks...): a plain cube is drawn as one
      BlockId bid = reg().blockId(def->block);
      const BlockDef* bd = bid ? &reg().block(bid) : nullptr;
      if (bd && (bd->render == Render::Cube || (bd->render == Render::Cutout && bd->solid)) && !bd->shapeKind && !bd->has("shape")) {
        PxBox b{{0, 0, 0, 16, 16, 16}, {}, 0};
        for (int f = 0; f < 6; f++) b.tile[f] = bd->faces[f];
        std::vector<PxBox> boxes{b};
        buildBoxes(ts, boxes, g, fc);
        VoxelFrame fr; fr.voxel = 1.f / 32; fr.origin = vec3(-0.5f);
        out.kind = VoxelModel::Block; out.depth = 0.5f;
        finishGrid(out, g, fr, &fc);
        return true;
      }
    }
    {   // furniture and other shaped blocks that only had a flat picture: build them from their real shape
      std::vector<PxBox> sbx;
      const BlockId rb = reg().blockId(def->block);
      const bool rod = rb && reg().tShape[rb] == ShapeKind::Rod;   // (their pictures are sheets: itemshapes.json's boxes read the wrong part)
      if ((k == "sprite" || rod) && shapedBlockBoxes(*def, sbx)) {
        buildBoxes(ts, sbx, g, fc);
        ivec3 lo(0);
        for (auto& b : sbx) lo = glm::min(lo, ivec3(glm::floor(vec3(b.b[0], b.b[1], b.b[2]) * 2.f)));
        VoxelFrame fr; fr.voxel = 1.f / 32; fr.origin = vec3(-0.5f) + vec3(lo) / 32.f;
        out.kind = VoxelModel::Block; out.depth = 0.5f;
        finishGrid(out, g, fr, &fc);
        return true;
      }
    }
    if (k == "cube" || k == "shape" || k == "model") {
      std::vector<PxBox> boxes;
      if (k == "cube") {
        PxBox b{{0, 0, 0, 16, 16, 16}, {}, 0};
        const json& F = (*it)["faces"];
        for (int f = 0; f < 6; f++) b.tile[f] = F[f].is_string() ? tileOf(F[f]) : 0;
        boxes.push_back(b);
      } else for (auto& jb : (*it)["boxes"]) {
        PxBox b; for (int i = 0; i < 6; i++) b.b[i] = jb[i].get<float>();
        for (int f = 0; f < 6; f++) b.tile[f] = jb[6 + f].is_string() ? tileOf(jb[6 + f]) : 0;
        b.tv = jb.size() > 12 && jb[12].is_number() ? jb[12].get<float>() : 0;
        boxes.push_back(b);
      }
      buildBoxes(ts, boxes, g, fc);
      // the grid's corner: cubes and shapes fill the block (-0.5..0.5); placed models are drawn larger
      // in the hand (models.js: -0.8, -0.6, -0.8 and 1.6/16 per pixel)
      ivec3 lo(0);
      for (auto& b : boxes) lo = glm::min(lo, ivec3(glm::floor(vec3(b.b[0], b.b[1], b.b[2]) * 2.f)));
      VoxelFrame fr;
      if (k == "model") { fr.voxel = 0.05f; fr.origin = vec3(-0.8f, -0.6f, -0.8f) + vec3(lo) * 0.05f; }
      else { fr.voxel = 1.f / 32; fr.origin = vec3(-0.5f) + vec3(lo) / 32.f; }
      out.kind = VoxelModel::Block; out.depth = 0.5f;
      finishGrid(out, g, fr, &fc);
      return true;
    }
    if (k == "torch") {
      // the pack's own torch (assets/pack_models.h: the one the mesher places), so the one in your hand is the one on the
      // wall: its boxes filled two voxels to a pack pixel, each voxel the colour of its box's nearest face there
      std::string pm = def->block == "old_torch" ? "torch" : def->block;
      if (const PackModel* PM = packModel(pm)) {
        constexpr int K = 2;   // (voxels per pack pixel)
        float top = 0; for (auto& bx : PM->boxes) top = std::max(top, bx.to.y);
        VoxelGrid tg(ivec3(16 * K, (int)std::ceil(top) * K + 1, 16 * K));
        const int S = ts.size;
        for (auto& bx : PM->boxes) {
          const vec3 a0 = bx.from, a1 = bx.to;
          const bool thinX = a1.x - a0.x < 0.5f, thinZ = a1.z - a0.z < 0.5f;   // (a flame's crossed planes)
          // the flame is not made solid: in the hand it burns as flickering flame particles at the head
          // (render/held_light_fx.cpp), on the wall as the pack's animated picture (render/mesher.cpp)
          int flameTile = -1;
          if (bx.flame) {   // the flame: this frame of the pack's animated picture (a plane a voxel thick, as on the wall)
            int ft = -1; for (int f = 0; f < 4; f++) if (bx.face[f].tile >= 0) { ft = bx.face[f].tile; break; }
            for (auto& an : ts.anims) if (an.layer == ft && !an.frames.empty()) {
              flameTile = an.frames[(size_t)g_flameFrame % an.frames.size()];
              out.flameFrames = (u8)std::min<size_t>(an.frames.size(), 255); out.flameMs = (u16)std::clamp(an.frameTime, 1, 65535);
            }
          }
          const float c = std::cos(glm::radians(bx.rotAngle)), sn = std::sin(glm::radians(bx.rotAngle));
          for (int z = 0; z < tg.size.z; z++) for (int y = 0; y < tg.size.y; y++) for (int x = 0; x < tg.size.x; x++) {
            vec3 p = (vec3(x, y, z) + 0.5f) / (float)K;
            if (bx.rotAxis >= 0 && bx.rotAngle != 0) {   // into the box's own frame
              vec3 d = p - bx.rotOrigin;
              if (bx.rotAxis == 0) d = vec3(d.x, d.y * c + d.z * sn, -d.y * sn + d.z * c);
              else if (bx.rotAxis == 1) d = vec3(d.x * c - d.z * sn, d.y, d.x * sn + d.z * c);
              else d = vec3(d.x * c + d.y * sn, -d.x * sn + d.y * c, d.z);
              p = bx.rotOrigin + d;
            }
            const float hx = thinX ? 0.5f / K : 0.f, hz = thinZ ? 0.5f / K : 0.f;
            if (p.x < a0.x - hx || p.x > a1.x + hx || p.y < a0.y || p.y > a1.y || p.z < a0.z - hz || p.z > a1.z + hz) continue;
            // the nearest face that has a picture: north, south, west, east, up, down
            const float dist[6] = {p.z - a0.z, a1.z - p.z, p.x - a0.x, a1.x - p.x, a1.y - p.y, p.y - a0.y};
            int f = -1; for (int i = 0; i < 6; i++) if (bx.face[i].tile >= 0 && (f < 0 || dist[i] < dist[f])) f = i;
            if (f < 0) continue;
            PackModelFace fcx = bx.face[f];
            if (flameTile >= 0) fcx.tile = flameTile;
            // (across the box's own face: its uv spans the box, not the block)
            auto fr = [](float q, float lo, float hi) { return hi - lo > 1e-3f ? clampv((q - lo) / (hi - lo), 0.f, 0.999f) : 0.5f; };
            const float fx = fr(p.x, a0.x, a1.x), fy = fr(p.y, a0.y, a1.y), fz = fr(p.z, a0.z, a1.z);
            const float u = f <= 1 ? (f == 0 ? 1 - fx : fx) : f <= 3 ? (f == 2 ? fz : 1 - fz) : fx;
            const float v = f <= 3 ? 1 - fy : (f == 4 ? fz : 1 - fz);
            const float uu = fcx.uv.x + (fcx.uv.z - fcx.uv.x) * u, vv = fcx.uv.y + (fcx.uv.w - fcx.uv.y) * v;
            const u32 col = tileVoxel(ts, fcx.tile, clampv((int)(uu / 16 * S), 0, S - 1), clampv((int)(vv / 16 * S), 0, S - 1));
            if ((col >> 24) < 128) continue;
            tg.set(x, y, z, flameTile >= 0 || bx.flame ? (col & 0xFFFFFFu) | (u32)VOXEL_GLOW << 24 : col | 0xFF000000u);   // (the flame glows, as on the wall)
          }
        }
        // the same span as the game's own held torch (0.8 tall, its top at the origin), so the hand view's grip fits it
        const float px = 0.8f / std::max(1.f, top);
        VoxelFrame fr; fr.voxel = px / K; fr.origin = vec3(-8 * px, -0.8f, -8 * px);
        out.kind = VoxelModel::Block; out.torch3d = true; out.depth = 0.1f;
        finishGrid(out, tg, fr, nullptr);
        return true;
      }
      buildTorch(ts, tileOf((*it)["tile"]), g, fc);
      VoxelFrame fr; fr.voxel = 0.05f; fr.origin = vec3(-0.1f, -0.8f, -0.1f);
      out.kind = VoxelModel::Block; out.torch3d = true; out.depth = 0.1f;
      finishGrid(out, g, fr, &fc);
      return true;
    }
    // a flat block item (saplings, flowers, doors): sculpted thin from its picture
    if (std::getenv("DQ_DEV_FLAT")) DQ_LOG("flat item: %s (block)", key.c_str());
    int t = it != bi.end() && (*it)["tile"].is_string() ? tileOf((*it)["tile"]) : tileOf(def->icon);
    SpriteInfo I = SpriteInfo::of(ts, t);
    {   // a plant (a flower, a sapling, grass, a mushroom, coral): two crossed planes of its picture, as it stands in the world
      const BlockId pb = reg().blockId(def->block);
      if (pb && reg().block(pb).render == Render::Cross && !traits(pb).crop && !I.empty()) {
        const int R = I.R, k = 32 / R;   // voxels a pixel
        VoxelPainter C(ivec3(34, 34, 34), vec3(-17.f / 32), 1.f / 32);
        for (int j = 0; j < R; j++) for (int i = 0; i < R; i++) {
          const u32 c = I.raw[j * R + i]; if (!c || !I.grid[j * R + i]) continue;
          const float u0 = -0.5f + (float)i / R, u1 = u0 + 1.f / R, v1 = 0.5f - (float)j / R, v0 = v1 - 1.f / R, th = 1.f / 32;
          C.box(vec3(u0, v0, -th), vec3(u1, v1, th), [c](const PaintPt&) { return c; });   // across
          C.box(vec3(-th, v0, -u1), vec3(th, v1, -u0), [c](const PaintPt&) { return c; });   // and along (mirrored: it reads the right way round)
        }
        (void)k;
        if (def->block == "sunflower") {   // its head (the picture is the stalk: in the world the head is a face of its own)
          for (int z = 0; z < 34; z++) for (int y = 0; y < 34; y++) for (int x = 0; x < 34; x++) {
            const vec3 p = C.frame.origin + (vec3(x, y, z) + 0.5f) * C.frame.voxel - vec3(0, 0.26f, 0.05f);
            const float ty = 0.35f, cy = std::cos(ty), sy = std::sin(ty);   // tipped back a little to the sun
            const vec3 q(p.x, p.y * cy - p.z * sy, p.y * sy + p.z * cy);
            const float r = std::hypot(q.x, q.y), a = std::atan2(q.y, q.x);
            if (std::fabs(q.z) > 0.04f || r > 0.22f) continue;
            if (r > 0.1f && std::cos(a * 12) < -0.3f && r > 0.16f) continue;   // between the petals' tips
            const u32 c = r < 0.1f ? (((x + y + z) & 1) ? voxRGBA(96, 52, 18) : voxRGBA(120, 70, 26)) : voxRGBA(250, 206, 40);
            C.g.set(x, y, z, c);
          }
        }
        out.kind = VoxelModel::Block; out.depth = 0.5f;
        finishGrid(out, C.g, C.frame, nullptr);
        return true;
      }
    }
    VoxelPainter P(ivec3(34, 34, 16), vec3(-17.f / 32, -17.f / 32, -0.25f), 1.f / 32);
    out.depth = std::max(sculpt(I, "thin", P), 0.02f);
    out.kind = VoxelModel::Flat;
    finishGrid(out, P.g, P.frame, nullptr);
    return true;
  }

  // ---- everything else: built from parts, or sculpted from the picture
  int t = def ? tileOf(def->icon) : -1;
  SpriteInfo I = SpriteInfo::of(ts, t);
  if (def && !I.empty() && model3dKind(base, def) == "hammer") {
    // a hammer, a real one: a straight haft with a leather grip, the squared head across its top (the smith's with a
    // cross-peen), coloured from its own picture (the wood of its handle, the stone, flint or iron of its head). Built
    // upright like the torch and held as the torch is, in the fist at the haft's foot, the head up (the picture's
    // diagonal, voxelised, had come out a stepped slab standing in the hand)
    double wr = 0, wg = 0, wb = 0, hr = 0, hg = 0, hb = 0; int wn = 0, hn = 0;
    for (u32 c : I.raw) {
      if (!c) continue;
      const int r = c & 255, g = (c >> 8) & 255, bb = (c >> 16) & 255, mx = std::max({r, g, bb}), mn = std::min({r, g, bb});
      if (mx < 45) continue;   // (the outline)
      if (r > bb + 25 && r >= g && mx - mn > 30) { wr += r; wg += g; wb += bb; wn++; } else { hr += r; hg += g; hb += bb; hn++; }
    }
    const vec3 wood = wn ? vec3((float)(wr / wn), (float)(wg / wn), (float)(wb / wn)) : vec3(120, 84, 50);
    const vec3 head = hn ? vec3((float)(hr / hn), (float)(hg / hn), (float)(hb / hn)) : vec3(140, 140, 146);
    const bool peen = false;   // (the smith's hammer as the stone one: a plain squared head)
    VoxelGrid g(ivec3(32, 34, 32));
    auto col = [&](vec3 c, float k, int x, int y, int z) {
      const float n = ((float)((x * 73856093u ^ y * 19349663u ^ z * 83492791u) % 13u) - 6.f) * 1.6f;   // (a little grain)
      return voxRGBA((int)(c.r * k + n), (int)(c.g * k + n), (int)(c.b * k + n));
    };
    for (int y = 0; y < 28; y++) for (int z = 15; z < 18; z++) for (int x = 15; x < 18; x++) {   // the haft, its grip wrapped
      const bool grip = y >= 3 && y < 11, band = y == 3 || y == 10;
      g.set(x, y, z, grip ? col(wood, band ? 0.45f : 0.62f, x, y, z) : col(wood, 1.f, x, y, z));
    }
    // (the head runs front to back, as a hammer is held to strike: its face forward (-z, away from you), the smith's peen
    //  toward you; not across the haft side to side)
    for (int y = 27; y < 34; y++) for (int x = 13; x < 20; x++) for (int z = 8; z < 25; z++) {   // the head
      if (peen && z > 20) {   // (the cross-peen: narrowing to a wedge)
        const int t2 = z - 20;   // 1..4
        if (y < 27 + t2 || y > 33 - t2 / 2) continue;
      }
      const bool face = z == 8 || z == 24, top = y == 33, eye = z >= 14 && z < 19 && y < 29;
      g.set(x, y, z, col(head, face ? 1.25f : top ? 1.1f : eye ? 0.8f : 0.95f, x, y, z));
    }
    const float px = 0.8f / 17.f;
    VoxelFrame fr; fr.voxel = px / 2; fr.origin = vec3(-8 * px, -0.8f, -8 * px);
    out.kind = VoxelModel::Block; out.torch3d = true; out.depth = 0.1f;
    finishGrid(out, g, fr, nullptr);
    return true;
  }
  VoxelPainter P(ivec3(52, 52, 52), vec3(-26.f / 32), 1.f / 32);
  ItemBuild b{ts, P, I, key, def};
  float sz = 1;
  bool done = false;
  if (def && !I.empty()) {
    std::string mk = model3dKind(base, def);
    if (!mk.empty() && buildModel3d(mk, b)) done = true;
    else if (!(mk = modelKind(base, def)).empty() && buildModelM3d(mk, b)) done = true;
    if (done) sz = itemSize(base);
  }
  if (!done && buildItems3d(b)) done = true;
  if (!done) {
    // sculpted (sculpt.js): the picture, each pixel as thick as its part of the picture is wide
    sz = def ? itemSize(base) : 1;
    static const bool devFlat = std::getenv("DQ_DEV_FLAT") != nullptr;   // dev: list the items drawn as a flat picture
    if (devFlat) DQ_LOG("flat item: %s (%s)", key.c_str(), sculptKind(base, def).c_str());
    b.depth = std::max(sculpt(I, sculptKind(base, def), P), 0.02f);
    b.kind = VoxelModel::Flat;
    if (rxSearch(base, "_cape$|^cape$|banner$")) {   // cloth hangs in folds: a wave runs across it
      VoxelGrid& G = P.g; VoxelGrid W(G.size);
      for (int z = 0; z < G.size.z; z++) for (int y = 0; y < G.size.y; y++) for (int x = 0; x < G.size.x; x++) if (u32 c = G.at(x, y, z)) {
        const float ph = (float)x / G.size.x * TAU * 1.5f;
        const int dz = (int)std::lround(std::sin(ph) * 3.f);
        const float shade = 0.9f + 0.18f * std::cos(ph);   // (the folds catch the light)
        W.set(x, y, z + dz, voxRGBA((int)((c & 255) * shade), (int)(((c >> 8) & 255) * shade), (int)(((c >> 16) & 255) * shade), (u8)(c >> 24)));
      }
      G = std::move(W); b.depth += 3.f / 32;
    }
  }
  VoxelFrame fr = P.frame;
  // scaleMB: shrink about the middle, or (standing things) keeping the foot on the ground
  if (sz != 1) {
    fr.voxel *= sz;
    fr.origin.x *= sz; fr.origin.z *= sz;
    fr.origin.y = b.stand ? -0.5f + (fr.origin.y + 0.5f) * sz : fr.origin.y * sz;
  }
  out.kind = b.kind; out.stand = b.stand; out.depth = b.depth * sz;
  finishGrid(out, P.g, fr, nullptr);
  return true;
}

// ------------------------------------------------------------------ the cache (GL thread) + workers
namespace {
struct Cache {
  std::unordered_map<std::string, std::unique_ptr<VoxelModel>> models;
  std::unordered_map<std::string, u64> lastUse;
  std::mutex mx;
  std::vector<std::unique_ptr<VoxelModel>> done;    // built by workers, waiting for upload
  std::unordered_set<std::string> pending;
  ThreadPool* pool = nullptr;
  u64 gen = 0, tick = 0;
};
Cache& cache() { static Cache* c = new Cache(); return *c; }   // never destroyed: its GL meshes must not outlive the context at exit
const size_t MAX_MODELS = 384;
}

void itemModelsReset(const TileSet& ts, ThreadPool* pool) {
  Cache& C = cache();
  { std::lock_guard<std::mutex> lk(g_snapMx); tilesSnap() = std::make_shared<TileSet>(ts); }
  std::lock_guard<std::mutex> lk(C.mx);
  C.models.clear(); C.lastUse.clear(); C.done.clear(); C.pending.clear();
  C.pool = pool; C.gen++;
}

void itemModelsUpdate() {
  Cache& C = cache();
  std::vector<std::unique_ptr<VoxelModel>> ready;
  { std::lock_guard<std::mutex> lk(C.mx); ready.swap(C.done); }
  for (auto& m : ready) {
    m->gl.upload(m->data);
    m->data.verts.clear(); m->data.verts.shrink_to_fit();
    std::string k = m->key;
    C.pending.erase(k);
    C.models[k] = std::move(m);
    C.lastUse[k] = C.tick;
  }
  // keep the GPU tidy: forget the models not drawn for the longest
  if (C.models.size() > MAX_MODELS) {
    std::vector<std::pair<u64, std::string>> v;
    for (auto& [k, t] : C.lastUse) v.push_back({t, k});
    std::sort(v.begin(), v.end());
    for (size_t i = 0; i < v.size() && C.models.size() > MAX_MODELS * 3 / 4; i++) { C.models.erase(v[i].second); C.lastUse.erase(v[i].second); }
  }
}

std::string torchFlameKey(const std::string& key, const VoxelModel& plain, double t) {
  if (!plain.torch3d || plain.flameFrames < 2) return key;
  const long long f = (long long)(t * 1000.0 / std::max<int>(1, plain.flameMs)) % plain.flameFrames;   // (as render/tile_anim.cpp times the wall's)
  return f == 0 ? key : "flame" + std::to_string(f) + ":" + key;
}

const VoxelModel& itemModel(const std::string& key) {
  static const VoxelModel* empty = new VoxelModel();
  Cache& C = cache();
  C.tick++;
  itemModelsUpdate();
  auto it = C.models.find(key);
  if (it != C.models.end()) { C.lastUse[key] = C.tick; return *it->second; }
  if (C.pending.count(key)) return *empty;
  C.pending.insert(key);
  u64 gen = C.gen;
  auto job = [key, gen]() {
    auto m = std::make_unique<VoxelModel>();
    buildItemModel(key, *m);
    Cache& C = cache();
    std::lock_guard<std::mutex> lk(C.mx);
    if (gen == C.gen) C.done.push_back(std::move(m));
  };
  if (C.pool) C.pool->submit(job, -10.f);
  else job();
  return *empty;
}

} // namespace dq
