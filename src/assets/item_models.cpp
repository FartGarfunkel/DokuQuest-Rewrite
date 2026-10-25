// assets/item_models.cpp — the item model cache, block items, the sculpt, and which builder each item gets
// (the original's itemMesh chain: models3d.js -> sculpt.js -> items3d.js -> models.js).
#include "assets/item_build.h"
#include "common/threadpool.h"
#include "world/shapes.h"
#include <regex>
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
  if (kind == "flat") return {0, 0.5f};
  return {0.8f, 3.5f};
}
std::string sculptKind(const std::string& key, const ItemDef* def) {
  if (!def) return "default";
  if (!def->block.empty()) return "thin";                                       // saplings, flowers, doors as items
  auto re = [&](const char* r) { return rxSearch(key, r); };
  if (re("paper|map$|^leather$|^pelt$|feather|^string$|_cape$|saddle|name_tag|pattern|^lore_")) return "flat";
  if (re("music_disc|compass|clock|charm$|_scale$|^turtle_scute$|minecart|boat$|ship$")) return "thin";
  if ((def->hasTool && def->tool.type == "sword") || re("dagger|blade$")) return "blade";
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
struct PxBox { float b[6]; int tile[6]; float tv; };

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

// ------------------------------------------------------------------ building one model
static void finishGrid(VoxelModel& out, VoxelGrid& g, const VoxelFrame& fr, const VoxelFaceColor* fc) {
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
bool shapedBlockBoxes(const ItemDef& def, std::vector<PxBox>& out) {
  static std::once_flag once; std::call_once(once, [] { initBlockTraits(); });
  const Registry& R = reg();
  BlockId id = R.blockId(def.block);
  if (!id) return false;
  const BlockDef& bd = R.block(id);
  if (bd.render == Render::Cross || bd.render == Render::Torch || bd.render == Render::Liquid || bd.render == Render::None) return false;
  const BlockTraits& T = traits(id);
  if (T.crop || T.flower || T.stackPlant || T.tripwire || T.chorus || T.rail || T.rs == RsRole::Wire || T.rs == RsRole::Hook || joinsPane(id)) return false;
  AloneNb nb;
  auto gather = [&](u8 meta, float dy, int side, int top, int bot) -> bool {
    std::vector<ShapeBox> sb;
    if (!shapeBoxes(id, meta, nb, ShapePurpose::Draw, sb)) return false;
    for (auto& b : sb) {
      PxBox p{}; for (int i = 0; i < 6; i++) p.b[i] = b.b[i];
      p.b[1] += dy; p.b[4] += dy;
      int st = b.side >= 0 ? b.side : side, tt = b.end >= 0 ? b.end : top, bt = b.end >= 0 ? b.end : bot;
      for (int f = 0; f < 6; f++) p.tile[f] = b.hasFaces && b.faces[f] >= 0 ? b.faces[f] : f == 2 ? tt : f == 3 ? bt : st;
      out.push_back(p);
    }
    return true;
  };
  int side = bd.faces[0], top = bd.faces[2], bot = bd.faces[3];
  if (T.door) top = bot = side;
  if (!gather(0, 0, side, top, bot) || out.empty()) { out.clear(); return false; }
  if (!T.door && out.size() == 1) {   // a single thin sheet (ladders, plates, signs): its flat picture shows it better than an edge-on slab
    const PxBox& b = out[0];
    float d[3] = {b.b[3] - b.b[0], b.b[4] - b.b[1], b.b[5] - b.b[2]};
    int thin = 0, wide = 0; for (float v : d) { if (v <= 2.5f) thin++; if (v >= 12) wide++; }
    if (thin == 1 && wide == 2) { out.clear(); return false; }
  }
  if (T.door && T.upperTile >= 0) gather(16, 16, T.upperTile, T.upperTile, T.upperTile);   // a door is two blocks tall
  return true;
}
}

namespace { std::shared_ptr<const TileSet>& tilesSnap() { static std::shared_ptr<const TileSet> t; return t; } std::mutex g_snapMx; }

bool buildItemModel(const std::string& key, VoxelModel& out) {
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

  // ---- block items
  if (def && !def->block.empty()) {
    const json& bi = blockItems();
    auto it = bi.find(key);
    std::string k = it != bi.end() ? (*it)["k"].get<std::string>() : "sprite";
    VoxelGrid g; VoxelFaceColor fc;
    {   // furniture and other shaped blocks that only had a flat picture: build them from their real shape
      std::vector<PxBox> sbx;
      if (k == "sprite" && shapedBlockBoxes(*def, sbx)) {
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
      buildTorch(ts, tileOf((*it)["tile"]), g, fc);
      VoxelFrame fr; fr.voxel = 0.05f; fr.origin = vec3(-0.1f, -0.8f, -0.1f);
      out.kind = VoxelModel::Block; out.torch3d = true; out.depth = 0.1f;
      finishGrid(out, g, fr, &fc);
      return true;
    }
    // a flat block item (saplings, flowers, doors): sculpted thin from its picture
    int t = it != bi.end() && (*it)["tile"].is_string() ? tileOf((*it)["tile"]) : tileOf(def->icon);
    SpriteInfo I = SpriteInfo::of(ts, t);
    VoxelPainter P(ivec3(34, 34, 16), vec3(-17.f / 32, -17.f / 32, -0.25f), 1.f / 32);
    out.depth = std::max(sculpt(I, "thin", P), 0.02f);
    out.kind = VoxelModel::Flat;
    finishGrid(out, P.g, P.frame, nullptr);
    return true;
  }

  // ---- everything else: built from parts, or sculpted from the picture
  int t = def ? tileOf(def->icon) : -1;
  SpriteInfo I = SpriteInfo::of(ts, t);
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
    b.depth = std::max(sculpt(I, sculptKind(base, def), P), 0.02f);
    b.kind = VoxelModel::Flat;
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
