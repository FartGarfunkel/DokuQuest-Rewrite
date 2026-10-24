// assets/packs.cpp — resource packs, the way Minecraft does them (port of render/packs.js).
// A pack (folder or .zip, Java Edition layout: assets/minecraft/textures/block and /item, or the old
// textures/blocks and /items) in userDir()/resourcepacks/ replaces the built-in tiles wherever it has a
// matching picture — pixel-for-voxel, since every tile is a face of 32x32 micro-voxels. Grass and leaves
// stay biome-coloured (the 253 alpha marker), animated strips animate (TileSet::anims), 16x, 32x and
// 64x all work. Also: chests from the chest model picture, colormaps (packTint), the pack's armour
// (assets/pack_armor) and GUI pictures (assets/gui_textures). Sounds and creature skins are read by
// their own modules through activePack().
#include "assets/tileset.h"
#include "assets/pack_fs.h"
#include "assets/pack_armor.h"
#include "assets/gui_textures.h"
#include <unordered_set>
#include <atomic>
#include <cstring>

namespace dq {

namespace {
// Older packs (1.12 and before) used different names in textures/blocks.
const std::unordered_map<std::string, std::string> PACK_LEGACY = {
  {"grass_block_top", "grass_top"}, {"grass_block_side", "grass_side"}, {"grass_block_side_overlay", "grass_side_overlay"}, {"grass_block_snow", "grass_side_snowed"},
  {"oak_planks", "planks_oak"}, {"spruce_planks", "planks_spruce"}, {"birch_planks", "planks_birch"}, {"jungle_planks", "planks_jungle"}, {"acacia_planks", "planks_acacia"}, {"dark_oak_planks", "planks_big_oak"},
  {"oak_log", "log_oak"}, {"oak_log_top", "log_oak_top"}, {"spruce_log", "log_spruce"}, {"spruce_log_top", "log_spruce_top"}, {"birch_log", "log_birch"}, {"birch_log_top", "log_birch_top"},
  {"jungle_log", "log_jungle"}, {"jungle_log_top", "log_jungle_top"}, {"acacia_log", "log_acacia"}, {"acacia_log_top", "log_acacia_top"}, {"dark_oak_log", "log_big_oak"}, {"dark_oak_log_top", "log_big_oak_top"},
  {"oak_leaves", "leaves_oak"}, {"spruce_leaves", "leaves_spruce"}, {"birch_leaves", "leaves_birch"}, {"jungle_leaves", "leaves_jungle"}, {"acacia_leaves", "leaves_acacia"}, {"dark_oak_leaves", "leaves_big_oak"},
  {"stone_bricks", "stonebrick"}, {"mossy_stone_bricks", "stonebrick_mossy"}, {"cracked_stone_bricks", "stonebrick_cracked"}, {"chiseled_stone_bricks", "stonebrick_carved"},
  {"mossy_cobblestone", "cobblestone_mossy"}, {"bricks", "brick"}, {"water_still", "water_still"}, {"lava_still", "lava_still"}, {"nether_portal", "portal"},
  {"crafting_table_top", "crafting_table_top"}, {"crafting_table_front", "crafting_table_front"}, {"furnace_front_on", "furnace_front_on"},
  {"poppy", "flower_rose"}, {"dandelion", "flower_dandelion"}, {"short_grass", "tallgrass"}, {"grass", "tallgrass"}, {"oak_sapling", "sapling_oak"}, {"fern", "fern"},
  {"white_wool", "wool_colored_white"}, {"netherrack", "netherrack"}, {"smooth_stone", "stone_slab_top"}, {"magma", "magma"}, {"granite", "stone_granite"}, {"diorite", "stone_diorite"}, {"andesite", "stone_andesite"},
  {"red_sand", "red_sand"}, {"sandstone", "sandstone_normal"}, {"sandstone_top", "sandstone_top"}, {"tnt_side", "tnt_side"}, {"bookshelf", "bookshelf"},
};
// Pictures whose layout in a pack doesn't match how this game draws the block: leave them be.
const std::unordered_set<std::string> PACK_SKIP = {"lantern", "soul_lantern", "lantern_cap", "chain", "bell", "bell_frame", "chest_top", "chest_side", "chest_front", "trapped_chest_top", "trapped_chest_side", "trapped_chest_front", "ender_chest_top", "ender_chest_side", "ender_chest_front", "cauldron_side", "cauldron_top", "cauldron_inner",
  "campfire_log", "campfire_log_end", "campfire_embers", "compass", "clock", "flower_pot", "flower_pot_top"};
// Tinted by the biome in this game (the pack draws them in grey, as Minecraft's are)
const std::unordered_set<std::string> PACK_TINTED_SRC = {"grass_block_top", "grass_block_side_overlay", "short_grass", "grass", "fern", "tall_grass_top", "tall_grass_bottom", "large_fern_top", "large_fern_bottom",
  "oak_leaves", "jungle_leaves", "acacia_leaves", "dark_oak_leaves", "mangrove_leaves", "birch_leaves", "spruce_leaves", "vine", "lily_pad", "sugar_cane",
  "grass_top", "grass_side_overlay", "tallgrass", "leaves_oak", "leaves_jungle", "leaves_acacia", "leaves_big_oak", "leaves_birch", "leaves_spruce", "waterlily", "reeds"};
// Minecraft's fixed colours for the leaves it doesn't tint by biome
const std::unordered_map<std::string, std::array<int, 3>> PACK_FIXED = {{"spruce_leaves", {97, 153, 97}}, {"birch_leaves", {128, 167, 85}}, {"leaves_spruce", {97, 153, 97}}, {"leaves_birch", {128, 167, 85}},
  {"leather_helmet", {160, 101, 64}}, {"leather_chestplate", {160, 101, 64}}, {"leather_leggings", {160, 101, 64}}, {"leather_boots", {160, 101, 64}}};

struct Img { int w = 0, h = 0; std::vector<u8> px; };

// one square frame of a picture (frame f of a vertical strip), cropped to `crop` of its width, brought to
// `res` pixels: smaller ones doubled up, bigger ones averaged down (packs.js packFrame)
std::vector<u8> packFrame(const Img& img, int f, int res, float crop = 1) {
  const int W = img.w, S = (int)std::lround(W * crop);
  const double k = (double)S / res;
  const u8* d = img.px.data();
  int soft = 0, seen = 0;
  for (size_t i = 3; i < (size_t)W * W * 4 && i < img.px.size(); i += 28) { seen++; if (d[i] > 8 && d[i] < 247) soft++; }
  bool cut = soft < seen * 0.03;
  std::vector<u8> out((size_t)res * res * 4, 0);
  for (int y = 0; y < res; y++) for (int x = 0; x < res; x++) {
    int x0 = (int)std::floor(x * k), y0 = (int)std::floor(y * k), x1 = std::max(x0 + 1, (int)std::floor((x + 1) * k)), y1 = std::max(y0 + 1, (int)std::floor((y + 1) * k));
    double r = 0, g = 0, b = 0, a = 0; int n = 0;
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) {
      size_t i = ((size_t)(yy + f * W) * W + xx) * 4;
      if (i + 3 >= img.px.size()) continue;
      double al = d[i + 3];
      n++; a += al; r += d[i] * al; g += d[i + 1] * al; b += d[i + 2] * al;
    }
    u8* o = &out[((size_t)y * res + x) * 4];
    if (a > 0) { o[0] = (u8)(r / a); o[1] = (u8)(g / a); o[2] = (u8)(b / a); }
    double A = n ? a / n : 0;
    o[3] = cut ? (A >= 128 ? 255 : 0) : (u8)A;
  }
  return out;
}
// doubled up to the tile size (textures.js toTileSize)
void toTile(const std::vector<u8>& px, int res, int T, u8* dst) {
  for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
    size_t o = ((size_t)(y * res / T) * res + x * res / T) * 4;
    std::memcpy(dst + ((size_t)y * T + x) * 4, &px[o], 4);
  }
}

struct Colormaps { Img grass, foliage; };
std::shared_ptr<const Colormaps> g_colormaps;

std::optional<Img> loadImg(const PackFS& P, const std::string& rel) {
  auto b = P.read(rel);
  if (!b) return std::nullopt;
  Img im;
  if (!loadPngMem(b->data(), b->size(), im.w, im.h, im.px)) return std::nullopt;
  return im;
}

// Minecraft draws chests as a model with a 64x64 picture (entity/chest/normal.png): a lid box 14x5x14
// above a base box 14x10x14, and a little latch. This game's chest is an ordinary block, so its top,
// side and front pictures are put together from the pieces. Packs for 1.15 and later store those
// sides upside down. (packs.js packChests)
int packChests(TileSet& ts, const PackFS& P) {
  bool flip = !P.legacy && (P.packFormat == 0 || P.packFormat >= 5);
  int n = 0, T = ts.size;
  const char* pairs[3][2] = {{"normal", "chest"}, {"trapped", "trapped_chest"}, {"ender", "ender_chest"}};
  for (auto& pr : pairs) {
    auto ti = [&](const char* suf) { auto it = ts.index.find(std::string(pr[1]) + suf); return it == ts.index.end() ? -1 : it->second; };
    int tTop = ti("_top"), tSide = ti("_side"), tFront = ti("_front");
    if (tTop < 0 && tSide < 0) continue;
    auto img = loadImg(P, P.texRoot + "entity/chest/" + pr[0] + ".png");
    if (!img || img->w < 64 || img->h < 64) continue;
    double k = img->w / 64.0;
    auto get = [&](double u, double v) {
      int sx = std::min(img->w - 1, (int)std::floor(u * k)), sy = std::min(img->h - 1, (int)std::floor(v * k));
      const u8* p = &img->px[((size_t)sy * img->w + sx) * 4];
      return std::array<u8, 4>{p[0], p[1], p[2], p[3]};
    };
    auto put = [&](int idx, auto fn) {
      if (idx < 0) return;
      u8* d = ts.tile(idx);
      for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) { auto c = fn((x + 0.5) / T, (y + 0.5) / T); u8* o = d + ((size_t)y * T + x) * 4; o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255; }
      ts.res[idx] = (u8)T;
    };
    auto strip = [&](double u, double v, double w, double h, double fx, double fy) { return get(u + fx * w, v + (flip ? 1 - fy : fy) * h); };
    auto side = [&](double fu, double fx, double fy) { return fy < 5.0 / 15 ? strip(fu, 14, 14, 5, fx, fy * 3) : strip(fu, 33, 14, 10, fx, (fy - 5.0 / 15) * 1.5); };
    put(tTop, [&](double fx, double fy) { return get(14 + fx * 14, fy * 14); });
    put(tSide, [&](double fx, double fy) { return side(0, fx, fy); });
    put(tFront, [&](double fx, double fy) {
      if (fx > 7.0 / 16 && fx < 9.0 / 16 && fy > 3.0 / 16 && fy < 7.5 / 16) { auto c = get(1 + (fx - 7.0 / 16) * 16, 1 + (fy - 3.0 / 16) / (4.5 / 16) * 4); if (c[3] > 128) return c; }
      return side(14, fx, fy);
    });
    n++;
  }
  return n;
}
} // namespace

u32 packTint(const char* kind, u8 temp, u8 humid, bool& ok) {
  auto cm = std::atomic_load(&g_colormaps);
  const Img* im = !cm ? nullptr : (kind && kind[0] == 'f') ? &cm->foliage : &cm->grass;
  if (!im || im->w < 2 || im->h < 2) { ok = false; return 0xFFFFFF; }
  float t = temp / 255.f, h = humid / 255.f * t;
  int x = clampv((int)((1 - t) * (im->w - 1)), 0, im->w - 1), y = clampv((int)((1 - h) * (im->h - 1)), 0, im->h - 1);
  const u8* p = &im->px[((size_t)y * im->w + x) * 4];
  ok = true;
  return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
}

std::vector<std::string> listResourcePacks() {
  std::vector<std::string> out;
  std::error_code ec;
  fs::path dir = userSub("resourcepacks");
  for (auto& e : fs::directory_iterator(dir, ec)) {
    std::string fn = e.path().filename().string();
    if (e.is_directory(ec)) { if (fs::exists(e.path() / "pack.mcmeta", ec) || fs::exists(e.path() / "assets", ec)) out.push_back(fn); }
    else if (endsWith(toLower(fn), ".zip")) out.push_back(fn.substr(0, fn.size() - 4));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

static fs::path packPath(const std::string& name) {
  fs::path dir = userSub("resourcepacks");
  std::error_code ec;
  if (fs::is_directory(dir / name, ec)) return dir / name;
  if (fs::exists(dir / (name + ".zip"), ec)) return dir / (name + ".zip");
  if (fs::exists(dir / name, ec)) return dir / name;
  return {};
}

std::string applyResourcePack(TileSet& ts, const std::string& choice) {
  setActivePack(nullptr);
  std::atomic_store(&g_colormaps, std::shared_ptr<const Colormaps>());
  resetPackArmor();
  resetGuiTextures();
  std::string name = choice;
  if (name == "auto") {
    name.clear();
    for (auto& n : listResourcePacks()) if (toLower(n).find("gerudoku") != std::string::npos) { name = n; break; }
  }
  if (name.empty() || name == "none") return "";
  fs::path path = packPath(name);
  auto P = path.empty() ? nullptr : PackFS::open(path);
  if (!P) { DQ_WARN("resource pack '%s' not found in resourcepacks/", name.c_str()); return ""; }
  if (!P->hasTextures()) { DQ_WARN("resource pack '%s': no textures (looked for assets/minecraft/textures)", name.c_str()); setActivePack(std::move(P)); return activePack()->name(); }

  // our tile names -> the pack's picture names (data/packmap.json: [name, as a block, as an item]),
  // and which tiles are item pictures (data/itemshapes.json itemTiles = the original ATLAS.items)
  auto pm = readJson(dataDir() / "packmap.json");
  auto is = readJson(dataDir() / "itemshapes.json");
  std::unordered_set<std::string> items;
  if (is && (*is)["itemTiles"].is_array()) for (auto& v : (*is)["itemTiles"]) items.insert(v.get<std::string>());
  const std::string R = P->texRoot;
  std::vector<std::string> blockDirs = P->legacy ? std::vector<std::string>{"blocks/"} : std::vector<std::string>{"block/", "blocks/"};
  std::vector<std::string> itemDirs = P->legacy ? std::vector<std::string>{"items/"} : std::vector<std::string>{"item/", "items/"};
  struct Hit { std::string path, base; };
  auto find = [&](const std::vector<std::string>& dirs, const std::string& base) -> std::optional<Hit> {
    for (auto& d : dirs) {
      std::string p = R + d + base + ".png";
      if (P->exists(p)) return Hit{p, base};
      auto lb = PACK_LEGACY.find(base);
      if (lb != PACK_LEGACY.end() && P->exists(R + d + lb->second + ".png")) return Hit{R + d + lb->second + ".png", lb->second};
    }
    return std::nullopt;
  };
  std::unordered_map<std::string, std::shared_ptr<Img>> cache;
  auto load = [&](const std::string& path) -> std::shared_ptr<Img> {
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    auto im = loadImg(*P, path);
    auto sp = im ? std::make_shared<Img>(std::move(*im)) : nullptr;
    cache[path] = sp;
    return sp;
  };
  // which tiles the game tints by biome (painted with the 253 marker)
  const int T = ts.size;
  auto tintedTile = [&](int idx) { const u8* p = ts.tile(idx); for (int i = 0; i < T * T; i++) if (p[i * 4 + 3] == 253) return true; return false; };
  int used = 0, nItems = 0, animated = 0, resSeen = 0;
  if (pm && pm->is_array()) for (auto& e : *pm) {
    if (!e.is_array() || e.size() < 3) continue;
    std::string tname = e[0];
    auto ii = ts.index.find(tname);
    if (ii == ts.index.end()) continue;
    int idx = ii->second;
    if (PACK_SKIP.count(tname) || startsWith(tname, "crack_")) continue;
    bool isItem = items.count(tname) > 0;
    std::optional<Hit> hit;
    for (auto& c : e[isItem ? 2 : 1]) { hit = find(isItem ? itemDirs : blockDirs, c.get<std::string>()); if (hit) break; }
    if (!hit) continue;
    auto img = load(hit->path);
    if (!img || img->w < 8 || img->h < img->w) continue;
    int res = clampv(img->w >= T ? T : img->w, 8, T);
    bool flow = endsWith(hit->base, "_flow");
    float crop = flow ? 0.5f : 1.f;
    int nFrames = img->h / img->w;
    bool tinted = tintedTile(idx);
    const std::array<int, 3>* fixed = nullptr;
    if (!tinted) { auto f = PACK_FIXED.find(hit->base); if (f != PACK_FIXED.end()) fixed = &f->second; }
    bool tintSrc = tinted && PACK_TINTED_SRC.count(hit->base);
    bool water = tname == "water" || tname == "water_flow";
    auto fix = [&](std::vector<u8>& px) {
      for (size_t i = 0; i < px.size(); i += 4) {
        if (px[i + 3] == 253) px[i + 3] = 252;               // 253 is the game's own "tint me" marker
        if (fixed && px[i + 3] > 0) { px[i] = (u8)(px[i] * (*fixed)[0] / 255); px[i + 1] = (u8)(px[i + 1] * (*fixed)[1] / 255); px[i + 2] = (u8)(px[i + 2] * (*fixed)[2] / 255); }
        if (tintSrc && px[i + 3] >= 128) px[i + 3] = 253;
        if (water && px[i + 3] == 255) px[i + 3] = 180;
      }
    };
    std::vector<u8> px = packFrame(*img, 0, res, crop);
    fix(px);
    // the grassy side: Minecraft draws a grey fringe over the dirt as a second layer
    if (tname == "grass_side") {
      auto ov = find(blockDirs, "grass_block_side_overlay");
      auto oimg = ov ? load(ov->path) : nullptr;
      if (oimg && oimg->w == img->w) {
        auto o = packFrame(*oimg, 0, res);
        for (size_t i = 0; i < o.size(); i += 4) if (o[i + 3] >= 128) { px[i] = o[i]; px[i + 1] = o[i + 1]; px[i + 2] = o[i + 2]; px[i + 3] = 253; }
      }
    }
    toTile(px, res, T, ts.tile(idx));
    ts.res[idx] = (u8)res;
    used++; if (isItem) nItems++;
    if (tname == "stone" || (!resSeen && !flow && nFrames == 1)) resSeen = img->w;
    // animation: the pack's frames replace the game's own (or stop it, if the pack's picture is still)
    auto ai = std::find_if(ts.anims.begin(), ts.anims.end(), [&](const TileAnim& a) { return a.layer == idx; });
    bool hadAnim = ai != ts.anims.end();
    if (nFrames > 1 || hadAnim) {
      if (hadAnim) ts.anims.erase(ai);
      if (nFrames > 1) {
        json an = json::object();
        if (auto mm = P->readJson(hit->path + ".mcmeta")) if ((*mm)["animation"].is_object()) an = (*mm)["animation"];
        std::vector<int> order;
        if (an.contains("frames") && an["frames"].is_array()) {
          for (auto& f : an["frames"]) { int fi = f.is_number() ? f.get<int>() : f.is_object() ? f.value("index", 0) : -1; if (fi >= 0 && fi < nFrames) order.push_back(fi); }
        } else for (int f = 0; f < nFrames; f++) order.push_back(f);
        TileAnim A{idx, {}, std::max(1, an.value("frametime", 1))};
        std::unordered_map<int, int> layerOf;
        std::vector<u8> full((size_t)T * T * 4);
        for (int f : order) {
          auto it = layerOf.find(f);
          if (it == layerOf.end()) {
            auto fp = packFrame(*img, f, res, crop); fix(fp);
            toTile(fp, res, T, full.data());
            it = layerOf.emplace(f, appendTile(ts, full.data())).first;
            ts.res.back() = (u8)res;
          }
          A.frames.push_back(it->second);
        }
        if (A.frames.size() > 1) { ts.anims.push_back(std::move(A)); animated++; }
      }
    }
  }
  int chests = packChests(ts, *P);
  // biome colours from the pack's colormaps (an engine addition: the original tinted with fixed colours)
  {
    auto cm = std::make_shared<Colormaps>();
    if (auto g = loadImg(*P, R + "colormap/grass.png")) cm->grass = std::move(*g);
    if (auto f = loadImg(*P, R + "colormap/foliage.png")) cm->foliage = std::move(*f);
    if (cm->grass.w || cm->foliage.w) std::atomic_store(&g_colormaps, std::shared_ptr<const Colormaps>(cm));
  }
  int armor = loadPackArmor(*P);
  deriveMicroVoxelHeights(ts);
  DQ_LOG("resource pack '%s': %d pictures (%d items, %d animated, %dpx), %d chests, %d armour sets", P->name().c_str(), used, nItems, animated, resSeen, chests, armor);
  std::string nm = P->name();
  setActivePack(std::move(P));
  return nm;
}

} // namespace dq
