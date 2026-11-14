// gen/village_pieces.cpp — our villages' buildings: Better Village's structure pieces (gzip'd NBT templates in
// data/villages, tools/port/village_import.py), read once on first use, each Minecraft block state turned into our
// block and its meta for every quarter turn.
#include <regex>
#include <set>
#include "world/rug.h"
#include "gen/village_pieces.h"
#include "common/fs.h"
#include "common/common.h"
#include "world/carve_forms.h"
#include <miniz.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace dq {

namespace {

// ---- gzip + NBT ----
bool gunzip(const std::vector<u8>& in, std::vector<u8>& out) {
  if (in.size() < 18 || in[0] != 0x1f || in[1] != 0x8b) return false;
  const u8 fl = in[3]; size_t p = 10;
  if (fl & 4) { if (p + 2 > in.size()) return false; p += 2 + (in[p] | in[p + 1] << 8); }
  if (fl & 8) { while (p < in.size() && in[p]) p++; p++; }
  if (fl & 16) { while (p < in.size() && in[p]) p++; p++; }
  if (fl & 2) p += 2;
  if (p >= in.size() - 8) return false;
  size_t n = 0;
  void* d = tinfl_decompress_mem_to_heap(in.data() + p, in.size() - p - 8, &n, 0);
  if (!d) return false;
  out.assign((u8*)d, (u8*)d + n); mz_free(d);
  return true;
}

struct RawBlock { u8 x, y, z; u16 s; int nbt; };
struct Template { json size, palette; std::vector<RawBlock> blocks; std::vector<json> nbts; };

struct Nbt {
  const u8* p; const u8* e; bool bad = false;
  u8 u1() { if (p >= e) { bad = true; return 0; } return *p++; }
  u32 u4() { u32 v = 0; for (int i = 0; i < 4; i++) v = v << 8 | u1(); return v; }
  i32 i4() { return (i32)u4(); }
  i16 i2() { return (i16)(u1() << 8 | u1()); }
  i64 i8() { return (i64)((u64)u4() << 32 | u4()); }
  std::string str() { const u16 n = (u16)i2(); if (p + n > e) { bad = true; return {}; } std::string s((const char*)p, n); p += n; return s; }
  json val(u8 t) {
    switch (t) {
      case 1: return (int)(int8_t)u1();
      case 2: return (int)i2();
      case 3: return i4();
      case 4: return i8();
      case 5: { u32 v = u4(); float f; memcpy(&f, &v, 4); return f; }
      case 6: { u64 v = (u64)u4() << 32 | u4(); double d; memcpy(&d, &v, 8); return d; }
      case 7: { const i32 n = i4(); if (n < 0 || p + n > e) { bad = true; return nullptr; } p += n; return nullptr; }
      case 8: return str();
      case 9: { const u8 et = u1(); const i32 n = i4(); json a = json::array(); for (i32 i = 0; i < n && !bad; i++) a.push_back(val(et)); return a; }
      case 10: { json o = json::object(); for (;;) { const u8 tt = u1(); if (tt == 0 || bad) return o; std::string k = str(); o[k] = val(tt); } }
      case 11: { const i32 n = i4(); json a = json::array(); for (i32 i = 0; i < n && !bad; i++) a.push_back(i4()); return a; }
      case 12: { const i32 n = i4(); json a = json::array(); for (i32 i = 0; i < n && !bad; i++) a.push_back(i8()); return a; }
    }
    bad = true; return nullptr;
  }
  // the template's "blocks" list, read straight into RawBlocks (a piece has tens of thousands, mostly air)
  void blocks(Template& T) {
    const u8 et = u1(); const i32 n = i4();
    if (et != 10) { for (i32 i = 0; i < n && !bad; i++) val(et); return; }
    T.blocks.reserve(std::max(0, n));
    for (i32 i = 0; i < n && !bad; i++) {
      RawBlock b{0, 0, 0, 0, -1};
      for (;;) {
        const u8 tt = u1(); if (tt == 0 || bad) break;
        const std::string k = str();
        if (k == "pos" && tt == 9) { json a = val(tt); if (a.size() >= 3) { b.x = (u8)a[0].get<int>(); b.y = (u8)a[1].get<int>(); b.z = (u8)a[2].get<int>(); } }
        else if (k == "state" && tt == 3) b.s = (u16)i4();
        else if (k == "nbt" && tt == 10) { T.nbts.push_back(val(tt)); b.nbt = (int)T.nbts.size() - 1; }
        else val(tt);
      }
      T.blocks.push_back(b);
    }
  }
};

bool readTemplate(const std::vector<u8>& gz, Template& T) {
  std::vector<u8> raw;
  if (!gunzip(gz, raw)) return false;
  Nbt r{raw.data(), raw.data() + raw.size()};
  if (r.u1() != 10) return false;
  r.str();
  for (;;) {
    const u8 tt = r.u1(); if (tt == 0 || r.bad) break;
    const std::string k = r.str();
    if (k == "blocks" && tt == 9) r.blocks(T);
    else if (k == "size") T.size = r.val(tt);
    else if (k == "palette") T.palette = r.val(tt);
    else if (k == "palettes") { json ps = r.val(tt); if (ps.is_array() && !ps.empty()) T.palette = ps[0]; }
    else r.val(tt);
  }
  return !r.bad && T.size.is_array() && T.size.size() == 3 && T.palette.is_array();
}

// ---- where the pieces are: data/villages/<style>/<kind>/<name>.nbt, listed in data/villages/index.json ----
using PieceFn = std::function<void(const std::string& rel, const std::vector<u8>& bytes)>;   // rel: "plains/houses/x.nbt"
json& pieceRules() { static json r = json::object(); return r; }   // index.json "rules": rel -> the builder's swaps
bool eachPiece(const PieceFn& f) {
  auto idx = readJson(dataDir() / "villages" / "index.json");
  if (!idx || !idx->is_object() || !(*idx)["pieces"].is_array()) return false;
  if ((*idx).contains("rules") && (*idx)["rules"].is_object()) pieceRules() = (*idx)["rules"];
  int n = 0;
  for (auto& r : (*idx)["pieces"]) {
    if (!r.is_string()) continue;
    const std::string rel = r.get<std::string>();
    if (auto b = readBinary(dataDir() / "villages" / rel)) { f(rel, *b); n++; }
  }
  return n > 0;
}

// ---- Minecraft block states -> ours ----
const std::unordered_map<std::string, std::string>& NAMES() {
  static const std::unordered_map<std::string, std::string> M = {
    {"oak_planks", "planks"}, {"oak_log", "log"}, {"stripped_oak_log", "stripped_log"}, {"oak_wood", "oak_wood"}, {"oak_leaves", "leaves"},
    {"oak_sapling", "sapling"}, {"grass_block", "grass"}, {"grass", "tall_grass"}, {"short_grass", "tall_grass"}, {"tall_grass", "tall_grass_double"},
    {"dandelion", "flower_yellow"}, {"poppy", "flower_red"}, {"white_wool", "wool"}, {"hay_block", "hay_bale"}, {"snow", "snow_layer"},
    {"wheat", "wheat_crop"}, {"carrots", "carrot_crop"}, {"potatoes", "potato_crop"}, {"beetroots", "beetroot_crop"}, {"red_bed", "bed"},
    {"wall_torch", "torch"}, {"soul_wall_torch", "soul_torch"}, {"redstone_wall_torch", "redstone_torch"}, {"cave_air", "air"}, {"void_air", "air"},
    {"infested_stone", "stone"}, {"infested_cobblestone", "cobblestone"}, {"infested_stone_bricks", "stone_bricks"},
    {"infested_mossy_stone_bricks", "mossy_stone_bricks"}, {"infested_cracked_stone_bricks", "cracked_stone_bricks"},
    {"infested_chiseled_stone_bricks", "chiseled_stone_bricks"}, {"water_cauldron", "cauldron"}, {"petrified_oak_slab", "oak_slab"},
    {"attached_pumpkin_stem", "pumpkin_stem"}, {"attached_melon_stem", "melon_stem"}, {"tall_seagrass", "seagrass"},
    {"ice", "packed_ice"},   // (ours melts by lamplight and in rain: the snowy houses' ice walls flooded them)
    {"deepslate", "slate"}, {"cobbled_deepslate", "cobbled_slate"}, {"sweet_berry_bush", "berry_bush"},
    // (Towns and Towers' pieces: Minecraft's newer names)
    {"deepslate_bricks", "slate_bricks"}, {"deepslate_brick_stairs", "slate_brick_stairs"}, {"deepslate_brick_slab", "slate_brick_slab"},
    {"deepslate_brick_wall", "slate_brick_wall"}, {"deepslate_tiles", "slate_tiles"}, {"deepslate_tile_stairs", "slate_tile_stairs"},
    {"deepslate_tile_slab", "slate_tile_slab"}, {"deepslate_tile_wall", "slate_tile_wall"}, {"cracked_deepslate_tiles", "cracked_slate_tiles"},
    {"cobbled_deepslate_wall", "cobbled_slate_wall"}, {"cobbled_deepslate_stairs", "cobbled_slate_stairs"}, {"cobbled_deepslate_slab", "cobbled_slate_slab"},
    {"polished_deepslate", "polished_slate"}, {"deepslate_coal_ore", "coal_ore"}, {"deepslate_iron_ore", "iron_ore"}, {"deepslate_copper_ore", "copper_ore"},
    {"cave_vines_plant", "cave_vines"}, {"weeping_vines_plant", "weeping_vines"}, {"kelp_plant", "kelp"},
    {"grass_path", "dirt_path"}, {"raw_iron_block", "iron_block"}, {"raw_gold_block", "gold_block"}, {"mangrove_roots", "mangrove_wood"},
    {"lava_cauldron", "cauldron"}, {"twisting_vines_plant", "twisting_vines"}, {"trapped_chest", "chest"}, {"candle_cake", "cake"},
    {"dead_bubble_coral_block", "dead_coral_block"}, {"waxed_copper_block", "copper_block"}, {"waxed_exposed_copper", "exposed_copper"},
    {"waxed_weathered_copper", "weathered_copper"}, {"waxed_exposed_cut_copper", "exposed_cut_copper"}, {"waxed_exposed_cut_copper_slab", "exposed_cut_copper_slab"},
  };
  return M;
}
// blocks a village is better without here: traps and parts of machines we'd only half-build
bool dropped(const std::string& n) {
  static const std::vector<std::string> D = {"tnt", "moving_piston", "structure_block", "barrier", "light", "turtle_egg"};
  if (n == "piston_head") return false;   // (an extended piston keeps its head: village_pieces mapState)
  return std::find(D.begin(), D.end(), n) != D.end() || n.find("_skull") != std::string::npos || n.find("_head") != std::string::npos;
}
const BlockId NONE = 0xFFFF;
BlockId findBlock(const std::string& n) {
  const Registry& R = reg();
  if (n == "air") return AIR;
  if (R.blockIds.count(n) || R.blockAliases.count(n)) return R.blockId(n);
  return NONE;
}
// a block of another mod (Create, Quark, The Graveyard, Supplementaries...) or a newer Minecraft name: the nearest of ours, by
// its name without the mod, Minecraft's newer words in ours, the material words, and its shape (stairs, slab, wall...)
BlockId nearestBlock(std::string n, std::string& keyOut) {
  if (const size_t c = n.find(':'); c != std::string::npos) n = n.substr(c + 1);
  auto rep = [&](const char* a, const char* b) { for (size_t p; (p = n.find(a)) != std::string::npos;) n.replace(p, strlen(a), b); };
  rep("waxed_", ""); rep("deepslate", "slate"); rep("tg_", ""); rep("cut_slate_brick", "slate_brick"); rep("polished_cut_slate", "polished_slate");
  rep("cut_slate", "slate_tile"); rep("cobblestone_bricks", "stone_bricks"); rep("sandstone_bricks", "cut_sandstone");
  if (auto it = NAMES().find(n); it != NAMES().end()) n = it->second;
  if (findBlock(n) != NONE) { keyOut = n; return findBlock(n); }
  // a whole word it's known by
  static const std::vector<std::pair<const char*, const char*>> WORDS = {
    {"chest", "chest"}, {"barrel", "barrel"}, {"coffin", "chest"}, {"sarcophagus", "chiseled_stone_bricks"}, {"gravestone", "cobblestone_wall"},
    {"vase", "decorated_pot"}, {"lantern", "lantern"}, {"candle", "candle"}, {"rope", "chain"}, {"flower_box", "flower_pot"}, {"book_pile", "bookshelf"},
    {"hedge", "leaves"}, {"post", "oak_fence"}, {"pipe", "iron_bars"}, {"gearbox", "copper_block"}, {"shaft", "chain"}, {"cogwheel", "copper_block"},
    {"iron_bars", "iron_bars"}, {"bars", "iron_bars"}, {"grass_block", "grass"}, {"moss_block", "moss_block"}, {"leaves", "leaves"}, {"log", "log"},
    {"planks", "planks"}, {"glass", "glass"}, {"lamp", "glowstone"}, {"skull", "mob_head"}, {"head", "mob_head"}, {"spawner", "monster_spawner"}, {"sack", "hay_bale"}, {"jar", "glass"},
    {"pedestal", "chiseled_stone_bricks"}, {"urn", "decorated_pot"}, {"trapdoor", "oak_trapdoor"}, {"door", "oak_door"}, {"button", "oak_button"},
    {"window", "glass_pane"}, {"pane", "glass_pane"}, {"shingles", "bricks"}, {"casing", "andesite"}, {"girder", "iron_bars"}, {"shelf", "bookshelf"},
    {"notice_board", "oak_planks"}, {"doormat", "brown_carpet"}, {"clock", "copper_block"}, {"dripleaf", "fern"},
    {"shroom", "brown_mushroom_block"}, {"spikes", "bamboo"}, {"faucet", "iron_bars"}, {"belt", "black_wool"}, {"crank", "lever"}, {"link", "lever"},
    // (other mods' woods, signs and flowers: they had been left out, holes in the builds that used them)
    {"_wood", "log"}, {"bundle", "log"}, {"sign", "oak_sign"}, {"begonia", "red_tulip"}, {"bleeding_heart", "red_tulip"}};
  static const char* SHAPES[] = {"_stairs", "_slab", "_wall", "_fence", "_fence_gate", "_trapdoor", "_door", "_pillar", "_vertical_slab", "_button", "_pressure_plate", "_bars"};
  std::string shape;
  for (const char* sh : SHAPES) { const size_t L = strlen(sh); if (n.size() > L && n.compare(n.size() - L, L, sh) == 0) { shape = sh; break; } }
  if (shape == "_vertical_slab") shape = "_slab";
  if (shape == "_pillar") shape = "";
  if (!shape.empty()) {   // the material: its words dropped from the left until one of ours has that shape ("polished_cut_scoria_wall" -> "..._wall")
    std::string mat = n.substr(0, n.rfind(shape == "_slab" && n.find("_vertical_slab") != std::string::npos ? "_vertical_slab" : shape));
    for (;;) {
      for (const std::string& c : {mat + shape, mat + "s" + shape, mat + "_brick" + shape}) if (findBlock(c) != NONE) { keyOut = c; return findBlock(c); }
      const size_t u = mat.find('_'); if (u == std::string::npos) break; mat = mat.substr(u + 1);
    }
    for (const char* m : {"stone_brick", "cobblestone", "planks"}) { std::string c = std::string(m) + shape; if (shape == "_slab" || shape == "_stairs") { if (std::string(m) == "planks") c = "oak" + shape; } if (findBlock(c) != NONE) { keyOut = c; return findBlock(c); } }
  }
  for (auto& [w, to] : WORDS) if (n.find(w) != std::string::npos && findBlock(to) != NONE) { keyOut = to; return findBlock(to); }
  // a plain block of a material we know: drop words from the left
  for (std::string m = n;;) { if (findBlock(m) != NONE) { keyOut = m; return findBlock(m); } const size_t u = m.find('_'); if (u == std::string::npos) break; m = m.substr(u + 1); }
  return NONE;
}

std::string wallFree(const std::string& n, const char* wall, const char* plain) {   // "oak_wall_sign" -> "oak_sign"
  const size_t p = n.find(wall);
  return p == std::string::npos ? std::string() : n.substr(0, p) + plain + n.substr(p + std::string(wall).size());
}
int dirOf(const std::string& f) { return f == "north" ? 0 : f == "east" ? 1 : f == "south" ? 2 : f == "west" ? 3 : f == "up" ? 4 : f == "down" ? 5 : -1; }
std::string prop(const json& P, const char* k) { return P.is_object() && P.contains(k) && P[k].is_string() ? P[k].get<std::string>() : std::string(); }
int propI(const json& P, const char* k, int def) { const std::string s = prop(P, k); return s.empty() ? def : std::atoi(s.c_str()); }

std::unordered_map<std::string, int>& missing() { static std::unordered_map<std::string, int> m; return m; }
int furnN[2] = {0, 0};
int emptyFurnished = 0;   // houses that came empty, furnished (logged)   // chairs and tables of ours put in for the builders' stand-ins (logged)
std::map<std::string, std::array<int, 2>> furnBy;   // (by style)

// one palette entry -> our block (id NONE: nothing placed) with its meta at each quarter turn
VPState mapState(const json& e) {
  VPState S; S.id = NONE;
  std::string n = e.value("Name", std::string());
  if (n.rfind("minecraft:", 0) == 0) n = n.substr(10);
  const json P = e.contains("Properties") ? e["Properties"] : json::object();
  if (n == "air" || n == "cave_air" || n == "void_air") { S.id = AIR; S.kind = VPK_AIR; return S; }
  if (n == "structure_void" || n == "jigsaw" || dropped(n)) return S;
  // water and lava as they were, running parts and all (our levels are Minecraft's: 0 still, 1-7 running, 8 falling); the
  // farms' irrigation runs down their terraces in it (left out, the stepped channels dried up and the crops with them)
  const int liquidLevel = (n == "water" || n == "lava") ? std::min(8, propI(P, "level", 0)) : 0;
  const Registry& R = reg();
  std::string key = n;
  bool wallSign = false, wallBanner = false;
  if (auto s = wallFree(n, "_wall_sign", "_sign"); !s.empty()) { key = s; wallSign = true; }
  else if (auto b = wallFree(n, "_wall_banner", "_banner"); !b.empty()) { key = b; wallBanner = true; }
  if (key.rfind("potted_", 0) == 0) { S.plant = key.substr(7); key = "flower_pot"; S.kind = VPK_POT; }
  // a piston left pushed out in the template: ours pushed out too, its head in front (it had come in drawn back, a gap where
  // its head was, and the hidden door it closes fired as the chunk woke)
  if (key == "redstone_torch" && prop(P, "lit") == "false" && findBlock("redstone_torch_off") != NONE) key = "redstone_torch_off";   // (one a circuit keeps off)
  if ((key == "piston" || key == "sticky_piston") && prop(P, "extended") == "true" && findBlock(key + "_extended") != NONE) key += "_extended";
  // (another mod's lantern hung on the wall: a lantern stood on nothing beside the wall; ours there is a wall torch, its facing kept)
  if (key.find("wall_lantern") != std::string::npos) key = "torch";
  if (auto it = NAMES().find(key); it != NAMES().end()) key = it->second;
  if (key == "grass" && prop(P, "snowy") == "true" && findBlock("snowy_grass") != NONE) key = "snowy_grass";
  if (key == "cave_vines" && prop(P, "berries") == "true" && findBlock("cave_vines_lit") != NONE) key = "cave_vines_lit";   // (glow berries on it)
  // a block we don't have (another mod's, or a Minecraft one we lack): ours from the table (tools/port/block_match.py, matched by
  // its texture and shape), "air" leaving it empty. Before any guess by name: a dead coral block the builder used as weathered
  // stone had become brain coral by its name
  static const json SUBS = [] { auto j = readJson(dataDir() / "block_substitutes.json"); return j && j->is_object() ? *j : json::object(); }();
  if (auto it = SUBS.find(n); it != SUBS.end() && it->is_string() && findBlock(n) == NONE) {
    key = it->get<std::string>();
    if (key == "air") { S.id = AIR; S.kind = VPK_AIR; return S; }
  }
  BlockId id = findBlock(key);
  if (id == NONE) id = nearestBlock(key, key);   // (another mod's block, or a newer name: the nearest of ours)
  if (id == NONE) { missing()[n]++; if (std::getenv("DQ_POI_AUDIT")) DQ_LOG("poi audit missing-block %s", n.c_str()); return S; }
  if (!S.plant.empty()) { auto it = NAMES().find(S.plant); if (it != NAMES().end()) S.plant = it->second; if (findBlock(S.plant) == NONE) S.plant.clear(); }
  S.id = id;
  const BlockDef& d = R.block(id);
  const int shape = R.tShape[id];
  const int f = dirOf(prop(P, "facing"));
  const bool top = prop(P, "half") == "top" || prop(P, "half") == "upper";
  if (key == "chest" || key == "trapped_chest" || key == "barrel") S.kind = VPK_CHEST;
  if (key.find("_cabinet") != std::string::npos) S.kind = VPK_CHEST;   // a cupboard: a chest of the house's things (unless stocked below)
  if (const std::string it = prop(P, "items"); !it.empty()) {   // our pieces' wares: what lies on a table, stands on a shelf or in a
    // bookcase, hangs in a frame or on a weapon rack, is kept in a cupboard
    const char* et = key == "item_pile" ? "pile" : key.find("bookcase") != std::string::npos ? "bookcase" : key.find("shelf") != std::string::npos ? "shelf"
                   : key == "item_frame" ? "frame" : key.find("weapon_rack") != std::string::npos ? "rack" : key.find("_cabinet") != std::string::npos ? "cabinet"
                   : key == "armor_stand" ? "stand" : key == "feeding_trough" ? "trough" : nullptr;   // (a stand: helmet, chest, legs, boots, hand; a trough: its food, sim/trough.h)
    if (et) { S.kind = VPK_WARES; S.items = it; S.etype = et; }
  }
  if (const std::string rg = prop(P, "rug"); !rg.empty()) { const BlockId c = findBlock(rg); if (c != NONE && rugCodeOf(c)) S.rug = c; }   // our pieces: a rug on this floor
  if (d.has("bed") && prop(P, "part") == "foot") S.kind = VPK_BED_FOOT;
  if (d.has("bellBlock")) S.kind = VPK_BELL;
  static const std::vector<std::string> JOB = {"composter", "smoker", "blast_furnace", "barrel", "lectern", "cartography_table", "fletching_table",
                                               "smithing_table", "stonecutter", "loom", "grindstone", "brewing_stand", "cauldron", "anvil", "crafting_table"};
  if (S.kind == 0 && std::find(JOB.begin(), JOB.end(), key) != JOB.end()) S.kind = VPK_JOB;
  for (int r = 0; r < 4; r++) {
    const int rf = f >= 0 && f < 4 ? (f + r) & 3 : -1;
    int m = 0;
    // redstone parts (sim/rsplace.cpp's placing rules): a piston, observer, dispenser, dropper or hopper keeps Minecraft's facing as a
    // six-way direction (down, up, north, south, west, east); a repeater or comparator faces the way its signal goes (the
    // opposite of Minecraft's "facing"), a repeater's delay above it. Without these every hidden door's piston pushed north.
    if (const std::string rs = d.str("rs"); rs == "piston" || rs == "observer" || rs == "dispenser" || rs == "dropper" || rs == "hopper" || rs == "repeater" || rs == "comparator") {
      static const int H2D6x[4] = {2, 5, 3, 4};   // north east south west -> world/shapes.h D6
      const int f6 = f == 4 ? 1 : f == 5 ? 0 : f >= 0 ? H2D6x[(f + r) & 3] : 0;
      if (rs == "repeater") m = (rf < 0 ? 0 : (rf + 2) & 3) | ((std::clamp(propI(P, "delay", 1), 1, 4) - 1) << 2) | (prop(P, "locked") == "true" ? 32 : 0);
      else if (rs == "comparator") m = (rf < 0 ? 0 : (rf + 2) & 3) | (prop(P, "mode") == "subtract" ? 4 : 0);
      else m = (rs == "hopper" && f < 0 ? 0 : f6) | (rs == "piston" && key.find("_extended") != std::string::npos ? 8 : 0);
      S.meta[r] = (u8)m; continue;
    }
    if (key == "piston_head") {   // its facing (six ways) and whether it's a sticky one's
      static const int H2D6h[4] = {2, 5, 3, 4};
      S.meta[r] = (u8)((f == 4 ? 1 : f == 5 ? 0 : f >= 0 ? H2D6h[(f + r) & 3] : 0) | (prop(P, "type") == "sticky" ? 8 : 0));
      continue;
    }
    if (shape == ShapeKind::Rail) {   // its course (Minecraft's RailShape order is ours, 0-9), turned with the piece; a powered one's power
      // (each shape as its two ends: 0 north 1 east 2 south 3 west, an ascending one up toward its first; every rail had lain north-south)
      static const char* SH[10] = {"north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south", "south_east", "south_west", "north_west", "north_east"};
      static const int A[10] = {0, 1, 1, 3, 0, 2, 2, 2, 0, 0}, B[10] = {2, 3, -1, -1, -1, -1, 1, 3, 3, 1};
      const std::string sh = prop(P, "shape");
      int k = 0; for (int i = 0; i < 10; i++) if (sh == SH[i]) k = i;
      const int a = (A[k] + r) & 3, b = B[k] < 0 ? -1 : (B[k] + r) & 3;
      int nk = k;
      if (b < 0) { static const int ASC[4] = {4, 2, 5, 3}; nk = ASC[a]; }   // (up toward north, east, south, west)
      else if ((a & 1) == (b & 1)) nk = a & 1 ? 1 : 0;
      else { const int lo = std::min(a, b), hi = std::max(a, b);   // a curve: its two ends
        nk = lo == 1 && hi == 2 ? 6 : lo == 2 && hi == 3 ? 7 : lo == 0 && hi == 3 ? 8 : 9; }
      const bool special = key != "rail";
      if (special && nk > 5) nk = 0;   // (powered, detector and activator rails don't turn corners)
      m = nk | (special && prop(P, "powered") == "true" ? 8 : 0);
      S.meta[r] = (u8)m; continue;
    }
    if (d.has("cauldron")) m = n == "water_cauldron" ? std::clamp(propI(P, "level", 3), 1, 3) : 0;   // (its water: meta & 3, sim/heat.cpp)
    else if (shape == ShapeKind::Stairs) {   // (a corner: world/carve_forms.h STAIR_CORNER bits, so a roof's corners are whole)
      m = (rf < 0 ? 0 : rf) | (top ? 4 : 0);
      const std::string sh = prop(P, "shape");
      if (sh == "inner_left") m |= STAIR_CORNER; else if (sh == "inner_right") m |= STAIR_CORNER | STAIR_RIGHT;
      else if (sh == "outer_left") m |= STAIR_CORNER | STAIR_OUTER; else if (sh == "outer_right") m |= STAIR_CORNER | STAIR_OUTER | STAIR_RIGHT;
    }
    else if (shape == ShapeKind::Slab) { const std::string t = prop(P, "type"); m = t == "top" ? 1 : t == "double" ? 6 : 0; }
    else if (shape == ShapeKind::Door) m = (rf < 0 ? 0 : rf) | (prop(P, "open") == "true" ? 4 : 0) | (prop(P, "hinge") == "right" ? 8 : 0) | (top ? 16 : 0);
    else if (shape == ShapeKind::Trapdoor) m = (rf < 0 ? 0 : (rf + 2) & 3) | (prop(P, "open") == "true" ? 4 : 0) | (top ? 8 : 0);
    else if (shape == ShapeKind::Gate) m = (rf < 0 ? 0 : rf) | (prop(P, "open") == "true" ? 4 : 0);
    else if (shape == ShapeKind::Bed || d.has("bed")) m = (rf < 0 ? 0 : rf) | (prop(P, "part") == "head" ? 4 : 0);
    else if (shape == ShapeKind::Ladder && n == "vine") {   // ours cling to one wall: the first side Minecraft's has
      int s = -1; for (const char* k : {"north", "east", "south", "west"}) if (prop(P, k) == "true") { s = dirOf(k); break; }
      if (s < 0) { S.id = NONE; return S; }
      m = (s + r) & 3;
    }
    else if (shape == ShapeKind::Ladder) m = rf < 0 ? 0 : (rf + 2) & 3;
    else if (shape == ShapeKind::Layer) m = std::clamp(propI(P, "layers", 1) - 1, 0, 7);
    else if (shape == ShapeKind::Candle) m = std::clamp(propI(P, d.has("pickle") ? "pickles" : "candles", 1) - 1, 0, 3);
    else if (shape == ShapeKind::Cake) m = std::clamp(propI(P, "bites", 0), 0, 6);
    else if (shape == ShapeKind::Button || shape == ShapeKind::Lever) {   // on the floor, the ceiling, or fixed to the wall behind it
      const std::string fc = prop(P, "face");
      m = fc == "floor" ? 0 : fc == "ceiling" ? 1 : 2 + (rf < 0 ? 0 : (rf + 2) & 3);
      if (fc != "wall" && rf >= 0 && (rf & 1)) m |= 16;
      if (shape == ShapeKind::Lever && prop(P, "powered") == "true") m |= 8;   // (a lever left pulled: sim/redstone.cpp reads m & 8)
    }
    else if (shape == ShapeKind::Hook) m = (rf < 0 ? 0 : (rf + 2) & 3) | (prop(P, "attached") == "true" ? 4 : 0);   // (strung: its wire across)
    else if (key == "tripwire") m = prop(P, "attached") == "true" ? 2 : 0;
    else if (key == "daylight_detector") m = prop(P, "inverted") == "true" ? 16 : 0;
    else if (shape == ShapeKind::Frame) m = rf < 0 ? 0 : (rf + 2) & 3;   // (it had no rule: every frame hung the same way, off its wall)
    else if (d.str("shape") == "campfire" || d.str("shape") == "soul_campfire") m = prop(P, "lit") == "false" ? 8 : 0;   // (CAMPFIRE_OUT)
    else if (d.render == Render::Torch) { static const int T[4] = {4, 1, 3, 2}; m = rf < 0 ? 0 : T[rf]; }
    else if (d.str("shape") == "lantern") m = prop(P, "hanging") == "true" ? 1 : 0;
    else if (d.has("sign")) m = wallSign ? 16 | (rf < 0 ? 0 : (rf + 2) & 3) : (propI(P, "rotation", 0) + 4 * r) & 15;
    else if (d.has("facingModel")) {
      if (wallBanner || d.str("cloth") == "banner") { S.banner = wallBanner ? 1 : 2; m = wallBanner ? (rf < 0 ? 0 : rf) | 8 : ((propI(P, "rotation", 0) + 2) / 4 + r + 2) & 3; }   // (8: flat to the wall behind it)
      else m = rf < 0 ? 0 : d.str("station") == "anvil" ? rf : (rf + 2) & 3;
    }
    else if (d.str("mech") == "axle" || d.str("mech") == "windmill") {   // a mill's shaft and its sails (big: the village mills' great sails)
      std::string a = prop(P, "axis");
      if (a.empty()) { const std::string fc = prop(P, "facing"); a = fc == "north" || fc == "south" ? "z" : fc == "east" || fc == "west" ? "x" : ""; }   // (Create's windmill bearing faces a way)
      int ax = a == "z" ? 2 : 1;
      if (r & 1) ax = 3 - ax;
      m = ax | (d.str("mech") == "windmill" && (prop(P, "big") == "true" || n.find("windmill_bearing") != std::string::npos) ? 4 : 0);
    }
    else if (d.has("axisBlock") || shape == ShapeKind::Rod || d.str("shape") == "chain") {
      const std::string a = prop(P, "axis"); int ax = a == "x" ? 1 : a == "z" ? 2 : 0;
      if ((r & 1) && ax) ax = 3 - ax;
      m = ax;
    }
    else if (d.has("crop")) {
      const int st = std::max(1, (int)d.props["crop"].value("stages", 1)), mx = n == "beetroots" || n == "cocoa" ? (n == "cocoa" ? 2 : 3) : 7;
      m = std::clamp((int)std::lround(propI(P, "age", 0) * (st - 1) / (double)mx), 0, st - 1);
      if (shape == ShapeKind::Cocoa) m |= (rf < 0 ? 0 : (rf + 2) & 3) << 4;
    }
    else if (key == "pointed_dripstone") m = prop(P, "vertical_direction") == "down" ? 1 : 0;   // (hanging from a ceiling: sim/build.cpp's meta)
    else if (d.has("double")) m = top ? 1 : 0;
    else if (R.tRender[id] == (u8)Render::Cube && (d.props.contains("tex") && d.props["tex"].is_object() && d.props["tex"].contains("front"))) m = rf < 0 ? 0 : (2 - rf) & 3;
    else if (d.has("facingBlock")) m = rf < 0 ? 0 : (2 - rf) & 3;
    if (prop(P, "waterlogged") == "true" && R.tWlog[id]) m |= 0x80;
    if (R.tLiquid[id]) m = liquidLevel;
    S.meta[r] = (u8)m;
  }
  return S;
}

const std::map<std::string, int>& WEIGHTS() {   // roughly vanilla's pool weights, by what the piece is
  static const std::map<std::string, int> W = {{"farm", 4}, {"straight", 4}, {"library", 3}, {"animal_pen", 2}, {"cartographer", 1}, {"accessory", 1}};
  return W;
}

std::unique_ptr<VillagePieces> finishPack(std::unique_ptr<VillagePieces> pack, int pieces);
std::unique_ptr<VillagePieces> loadPieces() {
  auto pack = std::make_unique<VillagePieces>();
  int pieces = 0; missing().clear();
  eachPiece([&](const std::string& rel, const std::vector<u8>& bytes) {
    const size_t a = rel.find('/'), b = a == std::string::npos ? a : rel.find('/', a + 1);
    if (b == std::string::npos) return;
    const std::string style = rel.substr(0, a), kind = rel.substr(a + 1, b - a - 1);
    if (kind.find("zombie") != std::string::npos || (kind != "houses" && kind != "streets" && kind != "town_centers" && kind != "terminators" && kind != "decor")) return;
    VPPiece P;
    const json* rl = pieceRules().contains(rel) ? &pieceRules()[rel] : nullptr;
    if (!makePiece(style, kind, fs::path(rel).stem().string(), bytes, P, rl)) { DQ_WARN("village pieces: %s isn't a structure template", rel.c_str()); return; }
    pack->pools[style + "/" + kind].push_back(std::move(P));
    pieces++;
  });
  return finishPack(std::move(pack), pieces);
}

}  // namespace

BlockId mcBlockState(const json& state) { const VPState s = mapState(state); return s.id; }

// one structure template (.nbt bytes) as a piece: its blocks in our states, its joints, our furniture (village_pieces.h)
std::string piecesMissing() { std::vector<std::pair<int, std::string>> v; for (auto& [k, c] : missing()) v.push_back({c, k}); std::sort(v.rbegin(), v.rend()); std::string o; for (size_t i = 0; i < v.size() && i < 40; i++) o += (o.empty() ? "" : ", ") + v[i].second + " " + std::to_string(v[i].first); return o; }

// a builder's spawner mob (Minecraft's or another mod's) as ours: the same where we have it, else the nearest kind. Never a
// phantom (there are none); a skeleton when there's no telling
static std::string spawnerMob(const std::string& id) {
  static const std::map<std::string, std::string> NEAR = {
    {"zombie", "ghoul"}, {"zombie_villager", "ghoul"}, {"pillager", "raider"}, {"vindicator", "raider"}, {"ravager", "raider"},
    {"evoker", "warlock"}, {"acolyte", "warlock"}, {"blaze", "cinder_wisp"}, {"magma_cube", "magma_slime"},
    {"piglin", "charred_ghoul"}, {"piglin_brute", "charred_ghoul"}, {"hoglin", "charred_ghoul"}, {"zombified_piglin", "charred_ghoul"},
    {"skeleton_creeper", "fuseling"}, {"creeper", "fuseling"}, {"forgotten", "skeleton"}, {"skelewag", "skeleton"},
    {"rocky_roller", "stone_mite"}, {"centipede_head", "scorpion"}, {"anaconda", "spider"}, {"silverfish", "stone_mite"}};
  std::string n = id.substr(id.find(':') == std::string::npos ? 0 : id.find(':') + 1);
  if (auto it = NEAR.find(n); it != NEAR.end()) n = it->second;
  if (n == "phantom" || !reg().mobs.contains(n)) n = "skeleton";
  return n;
}

// A house its builder left empty inside (our mushroom houses, savanna huts and classic cottages were shells): furnished
// here, as a villager would have it. On the main floor inside its walls: a bed with its head to a wall, a chest by
// another, the work block its name asks for (a bakery's smoker, a library's lectern...), a table in the room with a chair
// at it and a lantern on it. Nothing goes in a doorway or the cell before one.
static void furnishEmpty(VPPiece& P) {
  const int SX = P.sx, SY = P.sy, SZ = P.sz;
  if (SX < 4 || SZ < 4 || SY < 3) return;
  std::vector<int> g((size_t)SX * SY * SZ, -1);
  auto in = [&](int x, int y, int z) { return x >= 0 && y >= 0 && z >= 0 && x < SX && y < SY && z < SZ; };
  auto at = [&](int x, int y, int z) { return in(x, y, z) ? g[((size_t)y * SZ + z) * SX + x] : -1; };
  for (int i = 0; i < (int)P.blocks.size(); i++) g[((size_t)P.blocks[i].y * SZ + P.blocks[i].z) * SX + P.blocks[i].x] = i;
  auto keyAt = [&](int x, int y, int z) -> std::string { const int i = at(x, y, z); return i < 0 ? std::string() : reg().block(P.states[P.blocks[i].s].id).key; };
  auto solid = [&](int x, int y, int z) { const int i = at(x, y, z); return i >= 0 && reg().tSolid[P.states[P.blocks[i].s].id]; };
  auto free = [&](int x, int y, int z) { return in(x, y, z) && at(x, y, z) < 0; };
  static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {-1, 0, 1, 0};   // north east south west
  static const char* FN[4] = {"north", "east", "south", "west"};
  // the floor cells inside: free with free over them, standing on something solid, roofed, walled every way within 8
  struct C { int x, y, z; };
  std::map<int, std::vector<C>> byY;
  for (int y = 1; y < SY - 1; y++) for (int z = 0; z < SZ; z++) for (int x = 0; x < SX; x++) {
    if (!free(x, y, z) || !free(x, y + 1, z) || !solid(x, y - 1, z)) continue;
    bool roof = false; for (int k = y + 2; k < SY && k <= y + 9 && !roof; k++) roof = at(x, k, z) >= 0;
    if (!roof) continue;
    bool walled = true;
    for (int d = 0; d < 4 && walled; d++) { bool w = false; for (int k = 1; k <= 8 && !w; k++) w = solid(x + DX[d] * k, y, z + DZ[d] * k) || solid(x + DX[d] * k, y + 1, z + DZ[d] * k); walled = w; }
    if (!walled) continue;
    bool byDoor = false;
    for (int d = 0; d < 4; d++) for (int k = 0; k <= 1; k++) if (keyAt(x + DX[d], y + k, z + DZ[d]).find("door") != std::string::npos) byDoor = true;
    if (keyAt(x, y, z).find("door") != std::string::npos) byDoor = true;
    if (!byDoor) byY[y].push_back({x, y, z});
  }
  if (byY.empty()) return;
  int fy = byY.begin()->first; for (auto& [y, v] : byY) if (v.size() > byY[fy].size()) fy = y;
  std::vector<C>& cells = byY[fy];
  if (cells.size() < 4) return;
  std::set<long> used;
  auto ck = [](int x, int z) { return (long)x * 4096 + z; };
  auto isCell = [&](int x, int z) { for (auto& c : cells) if (c.x == x && c.z == z) return !used.count(ck(x, z)); return false; };
  auto wallDir = [&](const C& c) { for (int d = 0; d < 4; d++) if (solid(c.x + DX[d], fy, c.z + DZ[d])) return d; return -1; };
  auto put = [&](int x, int y, int z, const json& e) {
    VPState st = mapState(e); if (st.id == NONE || st.id == AIR) return false;
    P.states.push_back(st); P.blocks.push_back({(u8)x, (u8)y, (u8)z, (u16)(P.states.size() - 1)});
    g[((size_t)y * SZ + z) * SX + x] = (int)P.blocks.size() - 1; used.insert(ck(x, z)); return true;
  };
  auto mc = [](const std::string& n, json props = json::object()) { json e = {{"Name", "minecraft:" + n}}; if (!props.empty()) e["Properties"] = props; return e; };
  u32 h = (u32)std::hash<std::string>()(P.name);
  auto rnd = [&]() { h = h * 1103515245u + 12345u; return (h >> 8) & 0xFFFF; };
  // 1. the bed: its head to a wall, its foot into the room
  for (int t = 0; t < (int)cells.size(); t++) {
    const C& c = cells[(t + rnd()) % cells.size()];
    const int w = wallDir(c); if (w < 0 || !isCell(c.x, c.z)) continue;
    const int fx = c.x - DX[w], fz = c.z - DZ[w];
    if (!isCell(fx, fz)) continue;
    put(c.x, fy, c.z, mc("red_bed", {{"facing", FN[w]}, {"part", "head"}}));
    put(fx, fy, fz, mc("red_bed", {{"facing", FN[w]}, {"part", "foot"}}));
    break;
  }
  // 2. the work block the house's name asks for, then 3. a chest, each against a wall facing the room
  static const std::pair<const char*, const char*> JOBS[] = {{"bakery", "smoker"}, {"butcher", "smoker"}, {"armorer", "blast_furnace"}, {"cartographer", "cartography_table"},
    {"fletcher", "fletching_table"}, {"librar", "lectern"}, {"mason", "stonecutter"}, {"shepherd", "loom"}, {"tanner", "cauldron"}, {"leather", "cauldron"},
    {"toolsmith", "smithing_table"}, {"weaponsmith", "grindstone"}, {"temple", "brewing_stand"}, {"church", "brewing_stand"}, {"cleric", "brewing_stand"},
    {"fisher", "barrel"}, {"farm", "composter"}, {"jewel", "crafting_table"}, {"alchemist", "brewing_stand"}};
  std::vector<std::string> wallThings;
  for (auto& [k, b] : JOBS) if (P.name.find(k) != std::string::npos) { wallThings.push_back(b); break; }
  if (wallThings.empty()) wallThings.push_back("crafting_table");
  wallThings.push_back("chest");
  for (const std::string& what : wallThings) for (int t = 0; t < (int)cells.size(); t++) {
    const C& c = cells[(t + rnd()) % cells.size()];
    const int w = wallDir(c); if (w < 0 || !isCell(c.x, c.z)) continue;
    put(c.x, fy, c.z, mc(what, {{"facing", FN[(w + 2) & 3]}}));
    break;
  }
  // 4. a table in the room (not against a wall when there's room for that) with a chair at it and a lantern on it
  std::string wood = "oak";
  { std::map<std::string, int> wc; for (auto& b : P.blocks) { const std::string k = reg().block(P.states[b.s].id).key;
      for (const char* wd : {"spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry"}) if (k.rfind(std::string(wd) + "_", 0) == 0) wc[wd]++; }
    int best = 0; for (auto& [k, n] : wc) if (n > best) { best = n; wood = k; } }
  const std::string tableKey = findBlock(wood + "_table") != NONE ? wood + "_table" : "oak_table", chairKey = findBlock(wood + "_chair") != NONE ? wood + "_chair" : "oak_chair";
  for (int pass = 0; pass < 2; pass++) {
    bool done = false;
    for (int t = 0; t < (int)cells.size() && !done; t++) {
      const C& c = cells[(t + rnd()) % cells.size()];
      if (!isCell(c.x, c.z) || (pass == 0 && wallDir(c) >= 0)) continue;
      int sd = -1; for (int d = 0; d < 4 && sd < 0; d++) if (isCell(c.x + DX[d], c.z + DZ[d])) sd = d;
      if (sd < 0 || findBlock(tableKey) == NONE) continue;
      VPState tb; tb.id = findBlock(tableKey); tb.meta = {0, 1, 2, 3}; P.states.push_back(tb);
      P.blocks.push_back({(u8)c.x, (u8)fy, (u8)c.z, (u16)(P.states.size() - 1)}); used.insert(ck(c.x, c.z));
      if (findBlock(chairKey) != NONE) {   // (a chair keeps a stair's meta: its back on the far side from the table)
        const int f = sd;   // the chair's back away from the table
        VPState ch; ch.id = findBlock(chairKey); for (int r = 0; r < 4; r++) ch.meta[r] = (u8)((f + r) & 3); P.states.push_back(ch);
        P.blocks.push_back({(u8)(c.x + DX[sd]), (u8)fy, (u8)(c.z + DZ[sd]), (u16)(P.states.size() - 1)}); used.insert(ck(c.x + DX[sd], c.z + DZ[sd]));
      }
      if (fy + 1 < SY && free(c.x, fy + 1, c.z)) put(c.x, fy + 1, c.z, mc("lantern"));
      done = true;
    }
    if (done) break;
  }
}

bool makePiece(const std::string& style, const std::string& kind, const std::string& name, const std::vector<u8>& bytes, VPPiece& P, const json* rules) {
  {
    Template T;
    if (!readTemplate(bytes, T)) return false;
    // the builder's processors (tools/port/processors.py: [[input, probability, output, properties]...], tried in order):
    // placeholder colours become paths, stone mossy... each block decided by its place (the same every time)
    if (rules && rules->is_array() && !rules->empty()) {
      std::vector<int> outIdx;
      for (const json& r : *rules) {
        json o = json{{"Name", r[2]}}; if (r.size() > 3 && r[3].is_object() && !r[3].empty()) o["Properties"] = r[3];
        T.palette.push_back(o); outIdx.push_back((int)T.palette.size() - 1);
      }
      for (RawBlock& rb : T.blocks) {
        if (rb.s >= T.palette.size()) continue;
        const std::string nm = T.palette[rb.s].value("Name", std::string());
        u32 h = (u32)rb.x * 73856093u ^ (u32)rb.y * 19349663u ^ (u32)rb.z * 83492791u ^ (u32)std::hash<std::string>()(name);
        for (size_t k = 0; k < rules->size(); k++) {
          const json& r = (*rules)[k];
          if (!r[0].is_string() || r[0].get_ref<const std::string&>() != nm) continue;
          h = h * 1103515245u + 12345u;
          if (((h >> 8) & 0xFFFF) / 65536.0 < r[1].get<double>()) { rb.s = (decltype(rb.s))outIdx[k]; break; }
        }
      }
    }
    P.style = style; P.kind = kind;
    P.name = name;
    P.sx = T.size[0]; P.sy = T.size[1]; P.sz = T.size[2];
    P.terrain = kind == "streets" || kind == "terminators";
    for (auto& [k, w] : WEIGHTS()) if (P.name.find(k) != std::string::npos) P.weight = std::max(P.weight, w);
    if (P.weight == 1 && kind == "houses") P.weight = 2;
    // the palette, plus the final states the jigsaw blocks leave behind
    std::unordered_map<std::string, int> seen;
    auto addState = [&](VPState s) -> int {
      if (s.id == NONE) return -1;
      const std::string k = std::to_string(s.id) + ":" + std::to_string(s.meta[0]) + ":" + std::to_string(s.meta[1]) + ":" + std::to_string(s.kind) + ":" + s.plant
                          + ":" + std::to_string(s.banner) + ":" + std::to_string(s.rug) + ":" + s.etype + ":" + s.items;   // (a rug or a shelf's goods make a state of its own)
      auto it = seen.find(k); if (it != seen.end()) return it->second;
      P.states.push_back(std::move(s)); return seen[k] = (int)P.states.size() - 1;
    };
    std::vector<int> pal; bool jig = false; int jigIdx = -1;
    for (int i = 0; i < (int)T.palette.size(); i++) {
      const std::string nm = T.palette[i].value("Name", std::string());
      if (nm == "minecraft:jigsaw") { jig = true; jigIdx = i; }
      pal.push_back(addState(mapState(T.palette[i])));
      if (static const char* dm = std::getenv("DQ_PIECE_MAP"); dm && name.find(dm) != std::string::npos && pal.back() >= 0)   // (what each of a piece's blocks became)
        DQ_LOG("piece map %s: %s -> %s", name.c_str(), nm.c_str(), reg().block(P.states[pal.back()].id).key.c_str());
    }
    (void)jig;
    P.air.assign(((size_t)P.sx * P.sy * P.sz + 7) / 8, 0);
    auto markAir = [&](const RawBlock& rb) { const size_t i = ((size_t)rb.y * P.sz + rb.z) * P.sx + rb.x; if ((i >> 3) < P.air.size()) P.air[i >> 3] |= (u8)(1u << (i & 7)); };
    for (const RawBlock& rb : T.blocks) {
      if (rb.s >= T.palette.size()) continue;
      const json& pe = T.palette[rb.s];
      if (pe.value("Name", std::string()) == "minecraft:jigsaw") {
        VPJoint J; J.x = rb.x; J.y = rb.y; J.z = rb.z;
        const std::string o = pe.contains("Properties") ? prop(pe["Properties"], "orientation") : std::string();
        J.front = dirOf(o.substr(0, o.find('_')));
        if (o.find('_') != std::string::npos) J.top = dirOf(o.substr(o.find('_') + 1));
        if (rb.nbt >= 0) {
          const json& nb = T.nbts[rb.nbt];
          J.name = nb.value("name", std::string()); J.target = nb.value("target", std::string()); J.pool = nb.value("pool", std::string());
          // Minecraft's joint: "aligned" (the pieces' tops must agree, so an upper floor sits square on the one below) or "rollable" (any turn);
          // without one, a side joint is aligned and an up/down one rollable
          const std::string jt = nb.value("joint", std::string());
          J.aligned = jt.empty() ? J.front < 4 : jt == "aligned";
          std::string fs = nb.value("final_state", std::string("minecraft:air"));
          // "minecraft:oak_planks[axis=y]" -> a palette entry
          json fe{{"Name", fs.substr(0, fs.find('['))}};
          if (const size_t q = fs.find('['); q != std::string::npos) {
            json pr = json::object(); std::string body = fs.substr(q + 1, fs.size() - q - 2);
            for (size_t s = 0; s < body.size();) { size_t c = body.find(',', s); if (c == std::string::npos) c = body.size(); std::string kv = body.substr(s, c - s); size_t eq = kv.find('='); if (eq != std::string::npos) pr[kv.substr(0, eq)] = kv.substr(eq + 1); s = c + 1; }
            fe["Properties"] = pr;
          }
          J.fin = addState(mapState(fe));
          if (J.fin >= 0 && P.states[J.fin].kind == VPK_AIR) J.fin = -1;
          if (J.pool.find("common/sheep") != std::string::npos) P.animals = "sheep";
          else if (P.animals.empty() && (J.pool.find("common/animals") != std::string::npos || J.pool.find("butcher_animals") != std::string::npos)) P.animals = "stock";
        }
        if (J.fin >= 0) P.blocks.push_back({rb.x, rb.y, rb.z, (u16)J.fin});
        else markAir(rb);   // (a joint left as air)
        P.joints.push_back(std::move(J));
        continue;
      }
      int s = pal[rb.s];
      // a monster cage: what its builder put in it (SpawnData), as our nearest mob (an empty cage spawns nothing)
      if (s >= 0 && pe.value("Name", std::string()) == "minecraft:spawner") {
        std::string mc;
        if (rb.nbt >= 0 && T.nbts[rb.nbt].contains("SpawnData") && T.nbts[rb.nbt]["SpawnData"].is_object()) {
          const json& sd = T.nbts[rb.nbt]["SpawnData"];
          const json& en = sd.contains("entity") && sd["entity"].is_object() ? sd["entity"] : sd;
          mc = en.value("id", std::string());
        }
        VPState sp = P.states[s]; sp.kind = VPK_SPAWNER; sp.items = spawnerMob(mc);
        s = addState(std::move(sp));
      }
      if (s >= 0 && P.states[s].kind == VPK_AIR) { markAir(rb); continue; }
      if (s < 0) continue;
      P.blocks.push_back({rb.x, rb.y, rb.z, (u16)s});
      P.top = std::max(P.top, (int)rb.y);
    }
    (void)jigIdx;
    // Water: a building's is kept as its builder made it (our water runs and settles as Minecraft's does, so what held
    // there holds here). A street's puddles go (it lies on whatever ground there is).
    {
      const Registry& R = reg();
      const int SX = P.sx, SY = P.sy, SZ = P.sz;
      std::vector<int> grid((size_t)SX * SY * SZ, -1);
      auto cell = [&](int x, int y, int z) -> int& { return grid[((size_t)y * SZ + z) * SX + x]; };
      for (int i = 0; i < (int)P.blocks.size(); i++) cell(P.blocks[i].x, P.blocks[i].y, P.blocks[i].z) = i;
      std::vector<char> gone(P.blocks.size(), 0);
      auto wetAt = [&](int i) { const VPState& s = P.states[P.blocks[i].s]; return R.tLiquid[s.id] == 1 || (s.meta[0] & 0x80); };
      static const int ND[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
      auto dryAt = [&](int i) {
        VPState dry = P.states[P.blocks[i].s]; for (auto& m : dry.meta) m &= 0x7F;
        int di = -1; for (int k = 0; k < (int)P.states.size(); k++) if (P.states[k].id == dry.id && P.states[k].meta == dry.meta && P.states[k].kind == dry.kind && P.states[k].plant == dry.plant && P.states[k].rug == dry.rug) { di = k; break; }
        if (di < 0) { P.states.push_back(dry); di = (int)P.states.size() - 1; }
        P.blocks[i].s = (u16)di;
      };
      if (!P.terrain) {   // a rigid piece's waterlogged stairs and slabs with no water to them (a builder's leftovers): dry. Each had
        // stood a cube of water in the room, glassy sides to the air, and leaked
        std::vector<char> seen(P.blocks.size(), 0); std::vector<int> q;
        for (int i = 0; i < (int)P.blocks.size(); i++) if (R.tLiquid[P.states[P.blocks[i].s].id] == 1) { seen[i] = 1; q.push_back(i); }
        static const int N6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        while (!q.empty()) {
          const VPBlock b = P.blocks[q.back()]; q.pop_back();
          for (auto& d6 : N6) {
            const int nx = b.x + d6[0], ny = b.y + d6[1], nz = b.z + d6[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= SX || ny >= SY || nz >= SZ) continue;
            const int j = cell(nx, ny, nz);
            if (j >= 0 && !seen[j] && wetAt(j)) { seen[j] = 1; q.push_back(j); }
          }
        }
        for (int i = 0; i < (int)P.blocks.size(); i++) if (!seen[i] && wetAt(i) && R.tLiquid[P.states[P.blocks[i].s].id] != 1) dryAt(i);
        // flowing water caught mid-run in the template (levels 1-7) with nothing feeding it: no source and no higher water
        // beside it, nothing falling into it from above. It was never ticked, so it stood at its odd partial height for
        // good: taken out, a pass at a time, so a whole stranded run goes
        auto lvlOf = [&](int i) -> int { const VPState& st = P.states[P.blocks[i].s]; return R.tLiquid[st.id] == 1 ? (st.meta[0] & 15) : -1; };
        for (int pass = 0; pass < 8; pass++) {
          std::vector<int> drop;
          for (int i = 0; i < (int)P.blocks.size(); i++) {
            const int lv = lvlOf(i);
            if (lv <= 0 || lv >= 8) continue;   // (a source, or falling water: kept)
            const VPBlock& b = P.blocks[i];
            bool fed = false;
            for (auto& d6 : N6) {
              const int nx = b.x + d6[0], ny = b.y + d6[1], nz = b.z + d6[2];
              if (nx < 0 || ny < 0 || nz < 0 || nx >= SX || ny >= SY || nz >= SZ) continue;
              const int j = cell(nx, ny, nz); if (j < 0) continue;
              const int lj = lvlOf(j);
              if (lj < 0) continue;
              if (d6[1] == 1 || lj == 0 || lj >= 8 || lj < lv) { fed = true; break; }
            }
            if (!fed) drop.push_back(i);
          }
          if (drop.empty()) break;
          for (int i : drop) { const VPBlock b = P.blocks[i]; cell(b.x, b.y, b.z) = -1; P.blocks[i].s = (u16)0xFFFF; }
          std::vector<VPBlock> kept2; std::vector<int> remap(P.blocks.size(), -1);
          for (int i = 0; i < (int)P.blocks.size(); i++) if (P.blocks[i].s != 0xFFFF) { remap[i] = (int)kept2.size(); kept2.push_back(P.blocks[i]); }
          P.blocks.swap(kept2);
          for (auto& g : grid) if (g >= 0) g = remap[g];
        }
      }
      for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < (int)P.blocks.size(); i++) {
          if (gone[i] || !wetAt(i)) continue;
          const VPBlock& b = P.blocks[i];
          if (!P.terrain) continue;   // (rigid pieces: kept)
          // a ground piece's water: dried (open farm channels and the like ran out over the land and flooded the fields),
          // but our own fountains, wells and pools keep theirs where it's walled in, below and on every side
          const bool pool = P.name.find("fountain") != std::string::npos || P.name.find("wishing_well") != std::string::npos;
          bool held = pool;
          for (int k = 0; k < 5 && held; k++) {
            const int nx = b.x + ND[k][0], ny = b.y + ND[k][1], nz = b.z + ND[k][2];
            const int j = nx < 0 || ny < 0 || nz < 0 || nx >= SX || ny >= SY || nz >= SZ ? -1 : cell(nx, ny, nz);
            if (j < 0 || gone[j]) { held = ny <= 0; continue; }   // the ground there, or nothing at all
            const BlockId n = P.states[P.blocks[j].s].id;
            held = wetAt(j) || (R.tSolid[n] && !R.tReplaceable[n]);
          }
          if (held) continue;
          const VPState& s = P.states[b.s];
          if (R.tLiquid[s.id]) { gone[i] = 1; changed = true; continue; }
          dryAt(i); changed = true;
        }
      }
      std::vector<VPBlock> kept; kept.reserve(P.blocks.size());
      for (int i = 0; i < (int)P.blocks.size(); i++) if (!gone[i]) kept.push_back(P.blocks[i]);
      P.blocks.swap(kept);
      // a building's water with an open side at its own level (a farm's terrace channel left open at its end): that side
      // closed with what's under it, so it doesn't run out over the ground beside. A waterfall's column (falling water)
      // keeps its open sides: it doesn't spread while it falls.
      if (!P.terrain) {
        std::vector<int> g2((size_t)SX * SY * SZ, -1);
        for (int i = 0; i < (int)P.blocks.size(); i++) g2[((size_t)P.blocks[i].y * SZ + P.blocks[i].z) * SX + P.blocks[i].x] = i;
        auto at3 = [&](int x, int y, int z) { return x < 0 || y < 0 || z < 0 || x >= SX || y >= SY || z >= SZ ? -2 : g2[((size_t)y * SZ + z) * SX + x]; };
        const int nB = (int)P.blocks.size();   // (indices past this are plugs made just now: filled, not blocks to look at)
        std::vector<VPBlock> plugs;
        for (int i = 0; i < (int)P.blocks.size(); i++) {
          const VPBlock& b = P.blocks[i]; const VPState& st = P.states[b.s];
          if (R.tLiquid[st.id] != 1 || (st.meta[0] & 8) || b.y == 0) continue;   // (layer 0: the ground is round it)
          // only where our water does run on sideways (worldsys's FluidSystem::update): not at its last level, not over an
          // open cell (it falls there), not running water resting on water (that only feeds what's below: a fountain's tiers)
          const int lvl = st.meta[0] & 7, bl = at3(b.x, b.y - 1, b.z);
          bool spreads = lvl < 7;
          if (bl >= nB) spreads = false;   // (a plug under it: held)
          else if (bl < 0) spreads = spreads && b.y == 1;   // (layer 0's empty cells are the ground)
          else if (R.tLiquid[P.states[P.blocks[bl].s].id] == 1) spreads = spreads && lvl == 0 && (P.states[P.blocks[bl].s].meta[0] & 15) == 0;
          else spreads = spreads && R.tSolid[P.states[P.blocks[bl].s].id];
          if (!spreads) continue;
          auto plug = [&](int nx, int nz, int fx, int fz) {   // (fx,fz: the wet cell it closes)
            if (at3(nx, b.y, nz) != -1) return;   // a block there, or outside the piece
            int under = at3(nx, b.y - 1, nz); if (under >= nB) under = -1;
            if (under < 0 || R.tLiquid[P.states[P.blocks[under].s].id]) { under = at3(fx, b.y - 1, fz); if (under >= nB) under = -1; }
            u16 s2 = under >= 0 && R.tSolid[P.states[P.blocks[under].s].id] && !R.tLiquid[P.states[P.blocks[under].s].id] ? P.blocks[under].s : (u16)0xFFFF;
            if (s2 == 0xFFFF) { VPState dirt; dirt.id = findBlock("dirt"); P.states.push_back(dirt); s2 = (u16)(P.states.size() - 1); }
            if (std::getenv("DQ_PLUG_DEBUG")) DQ_LOG("plug %s %d,%d,%d from %d,%d,%d water/%d", P.name.c_str(), nx, b.y, nz, b.x, b.y, b.z, st.meta[0]);
            plugs.push_back({(u8)nx, b.y, (u8)nz, s2});
            g2[((size_t)b.y * SZ + nz) * SX + nx] = (int)P.blocks.size() + (int)plugs.size();   // (filled: not plugged twice)
          };
          const bool source = lvl == 0;
          for (int d = 0; d < 4; d++) {
            const int nx = b.x + ND[d][0], nz = b.z + ND[d][2];
            plug(nx, nz, b.x, b.z);
            // a slab or stair rim beside still water: Minecraft's stays dry and holds the pond, ours takes the water into its
            // empty half and runs it on out of its far side (taiga_small_farm_1's pond spilled 48 blocks over the grass)
            const int j = at3(nx, b.y, nz);
            if (!source || j < 0 || j >= nB) continue;
            const VPState& ns = P.states[P.blocks[j].s];
            if (R.tLiquid[ns.id] || (ns.meta[0] & 0x80) || carveFormOf(ns.id, ns.meta[0]).base == 0) continue;
            for (int e = 0; e < 4; e++) plug(nx + ND[e][0], nz + ND[e][2], nx, nz);
          }
        }
        for (auto& pl : plugs) P.blocks.push_back(pl);
      }
      // a banner is two blocks of ours: Minecraft's hangs from the wall down into the block below, or stands up into the
      // one above; the other half goes there when it's free
      auto withMeta = [&](int si, bool low) {
        VPState t = P.states[si]; for (auto& m : t.meta) m = (u8)(low ? m | 4 : m & ~4);
        for (int k = 0; k < (int)P.states.size(); k++) if (P.states[k].id == t.id && P.states[k].meta == t.meta && P.states[k].kind == t.kind && P.states[k].plant == t.plant && P.states[k].banner == t.banner && P.states[k].rug == t.rug) return k;
        P.states.push_back(t); return (int)P.states.size() - 1;
      };
      std::vector<VPBlock> more;
      for (VPBlock& b : P.blocks) {
        const u8 bn = P.states[b.s].banner;
        if (!bn) continue;
        const int oy = bn == 1 ? b.y - 1 : b.y + 1;
        if (oy < 0 || oy >= SY || cell(b.x, oy, b.z) >= 0) continue;
        if (bn == 1) more.push_back({b.x, (u8)oy, b.z, (u16)withMeta(b.s, true)});
        else { const u16 up = (u16)withMeta(b.s, false); b.s = (u16)withMeta(b.s, true); more.push_back({b.x, (u8)oy, b.z, up}); }
      }
      for (auto& m : more) P.blocks.push_back(m);
      // our furniture for the builders' stand-ins: a fence under a pressure plate or a carpet is a table (ours, of its wood),
      // a wooden stair at a table, or with armrests (trapdoors, wall signs) both sides, is a chair (ours, facing out).
      // Only in our own villages' pieces (made for it: tools/port/village_builder.py): other builders' stairs are steps
      static const std::set<std::string> OURS = {"plains", "savanna", "taiga", "snowy", "desert", "beach", "jungle", "swamp", "mountain", "alpine", "mesa",
        "oasis", "savanna_hut", "mushroom", "igloo", "christmas", "halloween", "jungle_tree"};
      // (every village's: Towns and Towers' styles had kept their stair-and-sign chairs and fence tables. Theirs are only
      //  taken for seats indoors, a roof over them, so a stair out in the street stays a stair)
      const bool ours = OURS.count(P.style) > 0;
      if (P.kind != "poi") {
        std::vector<int> grid2((size_t)SX * SY * SZ, -1);
        auto at2 = [&](int x, int y, int z) -> int { return x < 0 || y < 0 || z < 0 || x >= SX || y >= SY || z >= SZ ? -1 : grid2[((size_t)y * SZ + z) * SX + x]; };
        for (int i = 0; i < (int)P.blocks.size(); i++) grid2[((size_t)P.blocks[i].y * SZ + P.blocks[i].z) * SX + P.blocks[i].x] = i;
        auto keyOf = [&](int i) -> std::string { return i < 0 ? std::string() : reg().block(P.states[P.blocks[i].s].id).key; };
        auto woodOf = [](const std::string& k, const char* suf) -> std::string {
          static const char* W[] = {"dark_oak", "oak", "spruce", "birch", "jungle", "acacia", "mangrove", "cherry", "crimson", "warped", "pine", "aspen"};
          for (const char* w : W) if (k == std::string(w) + suf) return w;
          return "";
        };
        // the wood a block is of (our keys: oak's planks and logs are plain "planks", "log"), "" if it isn't wood
        auto woodAny = [&](const std::string& k) -> std::string {
          if (k == "planks" || k == "log" || k == "stripped_log" || k == "oak_wood" || k == "stripped_oak_wood") return "oak";
          for (const char* suf : {"_planks", "_log", "_wood", "_slab", "_stairs", "_trapdoor", "_fence"}) {
            std::string w = woodOf(k, suf); if (!w.empty()) return w;
            if (k.rfind("stripped_", 0) == 0) { w = woodOf(k.substr(9), suf); if (!w.empty()) return w; }
          }
          return "";
        };
        // furniture of stone (seats of sandstone, cobble, andesite...) is the land's own wood: the desert's birch...
        const std::string styleWood = P.style == "savanna" ? "acacia" : P.style == "snowy" || P.style == "taiga" ? "spruce" : P.style == "desert" ? "birch" : "oak";
        auto stateOf = [&](const std::string& key, std::array<u8, 4> meta) {
          const BlockId id = findBlock(key);
          for (int k = 0; k < (int)P.states.size(); k++) if (P.states[k].id == id && P.states[k].meta == meta && P.states[k].kind == 0) return k;
          VPState t; t.id = id; t.meta = meta; P.states.push_back(t); return (int)P.states.size() - 1;
        };
        std::vector<char> drop(P.blocks.size(), 0);
        // ---- what Better Village's builders made furniture of, made ours ----
        const Registry& RR = reg();
        static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {-1, 0, 1, 0};
        auto isAir = [&](int x, int y, int z) { return at2(x, y, z) < 0; };
        auto idOf = [&](int j) -> BlockId { return j < 0 ? (BlockId)0 : P.states[P.blocks[j].s].id; };
        auto shapeOf = [&](int j) { return j < 0 ? -1 : (int)RR.tShape[idOf(j)]; };
        auto m0 = [&](int j) { return j < 0 ? 0 : (int)P.states[P.blocks[j].s].meta[0]; };
        auto full = [&](int j) { return j >= 0 && RR.tSolid[idOf(j)] && !RR.tShape[idOf(j)]; };   // a whole block
        auto has = [](const std::string& k, const char* w) { return k.find(w) != std::string::npos; };
        auto isTrap = [&](int j) { return shapeOf(j) == ShapeKind::Trapdoor; };
        auto trapOpen = [&](int j) { return isTrap(j) && (m0(j) & 4); };
        auto isArm = [&](int j) { const std::string q = keyOf(j); return j >= 0 && (trapOpen(j) || has(q, "_sign")); };
        // a cell on a room's (or a yard's) floor: a whole block under it
        // (not on top of a counter: the block under it isn't one that stands up out of a floor beside it)
        auto floorBlock = [&](int j) {   // something to stand on that fills its cell's top: a whole block, a top or double slab, a stair upside down
          if (full(j)) return true;
          const int sh = shapeOf(j);
          return (sh == ShapeKind::Slab && ((m0(j) & 7) == 1 || (m0(j) & 7) == 6)) || (sh == ShapeKind::Stairs && (m0(j) & 4));
        };
        auto onFloor = [&](int x, int y, int z) {
          if (!floorBlock(at2(x, y - 1, z))) return false;
          for (int d = 0; d < 4; d++) { const int nx = x + DX[d], nz = z + DZ[d]; if (isAir(nx, y - 1, nz) && full(at2(nx, y - 2, nz))) return false; }
          return true;
        };
        // its stair's meta for each quarter turn, facing direction d (rises toward d)
        auto turned = [&](int d, int extra) { std::array<u8, 4> m{}; for (int r = 0; r < 4; r++) m[r] = (u8)(((d + r) & 3) | extra); return m; };
        auto woodFor = [&](int j) { const std::string w = woodAny(keyOf(j)); return w.empty() ? styleWood : w; };
        auto furn = [&](const std::string& wood, const char* kind) { const std::string k = wood + "_" + kind; return findBlock(k) != NONE ? k : styleWood + "_" + kind; };
        // 1. cupboards: unlit campfires behind doors (a pantry, two wide and two high, trapdoors round it): our cabinets,
        //    facing out through where the doors were; the doors and the trapdoor frame gone. (Villagers took those doors
        //    for the house's: they walked in and were lost in the cupboard.)
        {
          const BlockId CF = findBlock("campfire");
          std::vector<int> cup; std::vector<int> face;
          for (int i = 0; i < (int)P.blocks.size(); i++) {
            const VPBlock& b = P.blocks[i];
            if (idOf(i) != CF || !(m0(i) & 8)) continue;
            int f = -1;
            for (int dy = 0; dy >= -1 && f < 0; dy--) for (int d = 0; d < 4; d++) if (RR.tShape[idOf(at2(b.x + DX[d], b.y + dy, b.z + DZ[d]))] == ShapeKind::Door) { f = d; break; }
            if (f < 0) { const int up = at2(b.x, b.y + 1, b.z), dn = at2(b.x, b.y - 1, b.z);   // the other of a stack: its partner's front
              for (int d = 0; d < 4 && f < 0; d++) for (int q : {up, dn}) if (q >= 0 && idOf(q) == CF && RR.tShape[idOf(at2(P.blocks[q].x + DX[d], P.blocks[q].y, P.blocks[q].z + DZ[d]))] == ShapeKind::Door) { f = d; break; } }
            if (f < 0) continue;
            cup.push_back(i); face.push_back(f);
          }
          for (size_t c = 0; c < cup.size(); c++) {
            const VPBlock b = P.blocks[cup[c]]; const int f = face[c];
            std::string wood = styleWood;
            for (int dy = -1; dy <= 1; dy++) { const int dj = at2(b.x + DX[f], b.y + dy, b.z + DZ[f]); if (RR.tShape[idOf(dj)] == ShapeKind::Door) { drop[dj] = 1; const std::string w = woodAny(keyOf(dj).substr(0, keyOf(dj).size() - 5) + "_planks"); if (!w.empty()) wood = w; } }
            for (int d = 0; d < 4; d++) { const int t = at2(b.x + DX[d], b.y, b.z + DZ[d]); if (isTrap(t)) drop[t] = 1; }
            const int lid = at2(b.x, b.y + 1, b.z); if (isTrap(lid)) drop[lid] = 1;
            P.blocks[cup[c]].s = (u16)stateOf(furn(wood, "cabinet"), turned((f + 2) & 3, 0));
            furnN[1]++; furnBy[P.style][1]++;
          }
        }
        // 2. unlit campfires laid side by side (or over nothing) are a roof of logs in Minecraft (the stables', the snowy
        //    pens'): ours look like campfires, so oak slabs; a lone one in a kitchen stays a campfire
        {
          const BlockId CF = findBlock("campfire"), OS = findBlock("oak_slab");
          if (CF != NONE && OS != NONE) {
            std::vector<int> roof;
            for (int i = 0; i < (int)P.blocks.size(); i++) {
              const VPBlock& b = P.blocks[i];
              if (idOf(i) != CF || !(m0(i) & 8)) continue;
              int n = 0; for (int d = 0; d < 4; d++) if (idOf(at2(b.x + DX[d], b.y, b.z + DZ[d])) == CF) n++;
              if (n == 0 && at2(b.x, b.y - 1, b.z) >= 0) continue;
              roof.push_back(i);
            }
            if (!roof.empty()) { const int slab = stateOf("oak_slab", {0, 0, 0, 0}); for (int i : roof) P.blocks[i].s = (u16)slab; }
          }
        }
        // 2b. water let into a floor indoors with open trapdoors round its rim (the fisherman's fish tank, Minecraft's cod and
        //     salmon in it): glass round it instead, a tank you can see is one
        {
          const BlockId GP = findBlock("glass_pane");
          for (int i = 0; i < (int)P.blocks.size(); i++) {
            if (RR.tLiquid[idOf(i)] != 1 || GP == NONE) continue;
            const VPBlock& b = P.blocks[i];
            bool roofed = false; for (int u = 2; u <= 7 && !roofed; u++) if (at2(b.x, b.y + u, b.z) >= 0) roofed = true;
            if (!roofed) continue;
            for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
              const int t = at2(b.x + dx, b.y + 1, b.z + dz);
              if (trapOpen(t)) P.blocks[t].s = (u16)stateOf("glass_pane", {0, 0, 0, 0});
            }
          }
        }
        // 2c. a block of leaves set on a flower pot (Minecraft's potted shrub): ours floats over the pot's little plant, so it goes
        for (int i = 0; i < (int)P.blocks.size(); i++) {
          if (keyOf(i) != "flower_pot") continue;
          const VPBlock& b = P.blocks[i];
          for (int u = 1; u <= 2; u++) { const int j = at2(b.x, b.y + u, b.z); if (j < 0 || !has(keyOf(j), "leaves")) break; drop[j] = 1; }
        }
        // 3. staircases of half slabs a whole block apart (you had to jump each): a stair where the next one is a block up
        for (int i = 0; i < (int)P.blocks.size(); i++) {
          if (drop[i] || shapeOf(i) != ShapeKind::Slab || (m0(i) & 7) != 0) continue;
          const VPBlock& b = P.blocks[i];
          for (int d = 0; d < 4; d++) {
            const int n = at2(b.x + DX[d], b.y + 1, b.z + DZ[d]);
            if (!((shapeOf(n) == ShapeKind::Slab && (m0(n) & 7) == 0) || shapeOf(n) == ShapeKind::Stairs)) continue;
            if (!isAir(b.x, b.y + 1, b.z)) continue;
            std::string k = keyOf(i); k = k.substr(0, k.size() - 5) + "_stairs";
            if (k == "smooth_stone_stairs") k = "stone_stairs";
            if (findBlock(k) == NONE) { const std::string w = woodAny(keyOf(i)); k = w.empty() ? std::string() : w + "_stairs"; }
            if (k.empty() || findBlock(k) == NONE) break;
            P.blocks[i].s = (u16)stateOf(k, turned(d, 0));
            break;
          }
        }
        // 4. tables
        std::map<long long, std::string> table;   // the cells that become tables, of what wood
        std::map<long long, int> weak;            // cells that make a table when someone sits at them (index)
        auto ck = [](int x, int y, int z) { return (long long)(x & 0xFFFF) | ((long long)(y & 0xFFFF) << 16) | ((long long)(z & 0xFFFF) << 32); };
        auto makeTable = [&](int j, const std::string& wood) { table[ck(P.blocks[j].x, P.blocks[j].y, P.blocks[j].z)] = wood; };
        for (int i = 0; i < (int)P.blocks.size(); i++) {
          if (drop[i]) continue;
          const VPBlock& b = P.blocks[i];
          const std::string k = keyOf(i); const int shp = shapeOf(i);
          if (!onFloor(b.x, b.y, b.z)) continue;
          const int up = at2(b.x, b.y + 1, b.z); const std::string uk = keyOf(up);
          const bool piled = up >= 0 && uk == "item_pile";   // (wares laid on it: a stall's counter)
          const bool cloth = up >= 0 && (has(uk, "carpet") || has(uk, "pressure_plate") || piled);
          const bool lid = isTrap(up) && !trapOpen(up);
          // a post with a top: a fence, a wall, a scaffold under a carpet, a plate or a closed trapdoor
          const bool post = has(k, "_fence") && !has(k, "gate") ? true : (k == "scaffolding" || (has(k, "_wall") && shp == ShapeKind::Wall));
          // (and a lone fence post under a wooden bottom slab: the commonest table of all; a railing's capped posts aren't lone)
          const bool slabTop = up >= 0 && shapeOf(up) == ShapeKind::Slab && !(m0(up) & 1) && !woodAny(uk).empty() && has(k, "_fence");
          if (post && (cloth || lid || slabTop)) {
            bool lone = true; for (int d = 0; d < 4; d++) if (has(keyOf(at2(b.x + DX[d], b.y, b.z + DZ[d])), "_fence")) lone = false;
            if (slabTop && !lone) continue;
            if (cloth || lone || k == "scaffolding") { makeTable(i, woodAny(k).empty() ? (woodAny(uk).empty() ? styleWood : woodAny(uk)) : woodAny(k)); if (!piled) drop[up] = 1; continue; }
          }
          // a stair turned upside down: a little table (with a cloth on it, always)
          if (shp == ShapeKind::Stairs && (m0(i) & 4)) {
            if (cloth) { makeTable(i, woodFor(i)); drop[up] = 1; } else if (up < 0) weak[ck(b.x, b.y, b.z)] = i;
            continue;
          }
          // a closed trapdoor at the top of its cell (a tabletop) with open trapdoors for legs at its ends: a desk
          if (shp == ShapeKind::Trapdoor && !trapOpen(i) && (m0(i) & 8)) {
            int legs = 0; std::vector<int> lj;
            for (int d = 0; d < 4; d++) { int x = b.x, z = b.z; while (isTrap(at2(x + DX[d], b.y, z + DZ[d])) && !trapOpen(at2(x + DX[d], b.y, z + DZ[d])) && (m0(at2(x + DX[d], b.y, z + DZ[d])) & 8)) { x += DX[d]; z += DZ[d]; }
              const int e = at2(x + DX[d], b.y, z + DZ[d]); if (trapOpen(e)) { legs++; lj.push_back(e); } }
            if (legs >= 1) { makeTable(i, woodFor(i)); for (int e : lj) drop[e] = 1; } else weak[ck(b.x, b.y, b.z)] = i;
            continue;
          }
          if (up >= 0) continue;
          if (k == "scaffolding" && keyOf(at2(b.x, b.y - 1, b.z)) != "scaffolding") weak[ck(b.x, b.y, b.z)] = i;
          else if (shp == ShapeKind::Slab && (m0(i) & 7) == 1) {   // a top slab (a counter, a little table)
            int n = 0; for (int d = 0; d < 4; d++) if (shapeOf(at2(b.x + DX[d], b.y, b.z + DZ[d])) == ShapeKind::Slab) n++;
            if (n <= 1) weak[ck(b.x, b.y, b.z)] = i;
          } else if (!shp && RR.tSolid[idOf(i)] && !woodAny(k).empty()) {   // a lone block of wood
            int n = 0; for (int d = 0; d < 4; d++) if (RR.tSolid[idOf(at2(b.x + DX[d], b.y, b.z + DZ[d]))]) n++;
            if (n == 0) weak[ck(b.x, b.y, b.z)] = i;
          }
        }
        // 5. seats: a stair (the right way up) on a floor, open above, that isn't a step of a staircase, at a table, with
        //    a backrest (open trapdoors behind it), or with armrests (trapdoors, signs) both sides. A run of two to four
        //    side by side with armrests at its ends is a bench. Backrests and armrests go.
        auto seatOk = [&](int j) {
          if (j < 0 || drop[j] || shapeOf(j) != ShapeKind::Stairs || (m0(j) & 4)) return false;
          const VPBlock& b = P.blocks[j];
          if (!isAir(b.x, b.y + 1, b.z) || !onFloor(b.x, b.y, b.z)) return false;
          if (!ours) { bool roof = false; for (int yy = b.y + 2; yy < std::min(SY, b.y + 9) && !roof; yy++) roof = at2(b.x, yy, b.z) >= 0 && !isAir(b.x, yy, b.z); if (!roof) return false; }
          const int rf = m0(j) & 3, bx = b.x + DX[rf], bz = b.z + DZ[rf];
          const int up1 = at2(bx, b.y + 1, bz);
          if (shapeOf(up1) == ShapeKind::Stairs || shapeOf(up1) == ShapeKind::Slab) return false;   // a staircase going on up
          if (full(at2(bx, b.y, bz)) && isAir(bx, b.y + 1, bz) && full(at2(bx + DX[rf], b.y, bz + DZ[rf])) && isAir(bx + DX[rf], b.y + 1, bz + DZ[rf])) return false;   // a step up onto a raised floor (a lone block behind, a target or a barrel, isn't one)
          const int fr = at2(b.x - DX[rf], b.y - 1, b.z - DZ[rf]);
          if (shapeOf(fr) == ShapeKind::Stairs) return false;   // a step down from something
          return true;
        };
        std::vector<char> seated(P.blocks.size(), 0);
        auto backrest = [&](const VPBlock& b, int rf, bool take) {
          bool any = false;
          for (int dy = 0; dy <= 1; dy++) { const int t = at2(b.x + DX[rf], b.y + dy, b.z + DZ[rf]); if (trapOpen(t) || (dy == 0 && has(keyOf(t), "_sign"))) { any = true; if (take) drop[t] = 1; } }
          return any;
        };
        // rows of seats side by side, the same way round (one alone is a row of one): seating when it has an armrest at
        // either end, a backrest behind one of them, or a table in front of one. One alone: a chair; two or more: benches.
        const bool dbg = std::getenv("DQ_FURN_DEBUG") && P.name == std::getenv("DQ_FURN_DEBUG");
        // a block that serves for a tabletop: a top slab or a plain whole block (planks, stone, quartz...; not a chest, a
        // furnace, a bookshelf, a crafting bench), standing on a floor, open above
        auto plainTop = [&](int t) {
          if (t < 0 || !isAir(P.blocks[t].x, P.blocks[t].y + 1, P.blocks[t].z) || !full(at2(P.blocks[t].x, P.blocks[t].y - 1, P.blocks[t].z))) return false;
          const BlockId id = idOf(t); const BlockDef& d = RR.block(id); const std::string& k = d.key;
          if (d.has("container") || d.has("station") || d.has("furnace") || d.has("table") || d.has("furniture") || has(k, "bookshelf") || has(k, "crafting") || has(k, "log") || has(k, "leaves") || has(k, "glass")) return false;
          if (has(k, "snow") || has(k, "ice") || has(k, "dirt") || k == "grass" || (has(k, "sand") && !has(k, "sandstone")) || has(k, "gravel") || has(k, "hay") || has(k, "wool")) return false;   // ground, drifts, bales
          if (shapeOf(t) == ShapeKind::Slab) return (m0(t) & 7) == 1;
          return full(t) && RR.tRender[id] == (u8)Render::Cube;
        };
        if (dbg) for (int i = 0; i < (int)P.blocks.size(); i++) if (shapeOf(i) == ShapeKind::Stairs && !(m0(i) & 4)) {
          const VPBlock& b = P.blocks[i];
          DQ_LOG("  stair %d,%d,%d %s rf %d: seatOk %d air-above %d onFloor %d seated %d drop %d", b.x, b.y, b.z, keyOf(i).c_str(), m0(i) & 3, (int)seatOk(i), (int)isAir(b.x, b.y + 1, b.z), (int)onFloor(b.x, b.y, b.z), (int)seated[i], (int)drop[i]);
        }
        for (int i = 0; i < (int)P.blocks.size(); i++) {
          if (seated[i] || !seatOk(i)) continue;
          const VPBlock& b = P.blocks[i]; const int rf = m0(i) & 3, front = (rf + 2) & 3, lx = DX[(rf + 1) & 3], lz = DZ[(rf + 1) & 3];
          auto same = [&](int j) { return j >= 0 && !seated[j] && seatOk(j) && (m0(j) & 3) == rf; };
          int x0 = b.x, z0 = b.z; while (same(at2(x0 - lx, b.y, z0 - lz))) { x0 -= lx; z0 -= lz; }
          std::vector<int> run; int x = x0, z = z0;
          while (run.size() < 6 && same(at2(x, b.y, z))) { run.push_back(at2(x, b.y, z)); x += lx; z += lz; }
          if (run.empty()) continue;
          const int ea = at2(x0 - lx, b.y, z0 - lz), eb = at2(x, b.y, z);
          bool back = false, tbl = false, roofed = false, stone = false;
          std::vector<long long> tks;
          for (int j : run) {
            const VPBlock& c = P.blocks[j];
            if (backrest(c, rf, false)) back = true;
            for (int d = 1; d <= 2; d++) { const long long q = ck(c.x + d * DX[front], c.y, c.z + d * DZ[front]); if (table.count(q) || weak.count(q)) { tbl = true; tks.push_back(q); break; } }
            // (a long table of plain blocks or top slabs at seat height, open above, on the floor: each cell of it a table)
            if (!tbl) {
              const int t = at2(c.x + DX[front], c.y, c.z + DZ[front]);
              if (t >= 0 && !drop[t] && plainTop(t)) { const long long q = ck(P.blocks[t].x, P.blocks[t].y, P.blocks[t].z); weak[q] = t; tbl = true; tks.push_back(q); }
            }
            for (int u = 2; u <= 6 && !roofed; u++) if (at2(c.x, c.y + u, c.z) >= 0) roofed = true;
            if (woodOf(keyOf(j), "_stairs").empty()) stone = true;
          }
          const bool arms = isArm(ea) || isArm(eb);
          // a window seat: a wooden row with its back to the wall under a window (glass over the back, a wall below it)
          bool window = false;
          if (!stone) for (int j : run) {
            const VPBlock& c = P.blocks[j]; const int bx = c.x + DX[rf], bz = c.z + DZ[rf];
            const std::string over = keyOf(at2(bx, c.y + 1, bz));
            if (full(at2(bx, c.y, bz)) && (has(over, "glass") || has(over, "pane"))) window = true;
          }
          if (window) back = true;
          if (dbg) DQ_LOG("  row at %d,%d,%d of %zu: arms %d back %d table %d roofed %d stone %d", b.x, b.y, b.z, run.size(), (int)arms, (int)back, (int)tbl, (int)roofed, (int)stone);
          // a sofa: two to four wooden seats together, closed in at both ends (a log, a wall, a hedge), no door behind it
          bool sofa = false;
          if (!stone && run.size() >= 2 && run.size() <= 4) {
            auto closed = [&](int j) { const int sh = shapeOf(j); return j >= 0 && (RR.tSolid[idOf(j)] || has(keyOf(j), "leaves")) && sh != ShapeKind::Stairs && sh != ShapeKind::Layer && sh != ShapeKind::Slab && !has(keyOf(j), "snow") && keyOf(j) != "grass" && keyOf(j) != "dirt"; };
            bool bad = !roofed;   // (indoors only: a row out of doors is a step, a plinth, a roof's foot)
            for (int j : run) {
              const VPBlock& c = P.blocks[j];
              for (int dy = 0; dy <= 1; dy++) { const int sh = shapeOf(at2(c.x + DX[rf], c.y + dy, c.z + DZ[rf])); if (sh == ShapeKind::Door || sh == ShapeKind::Stairs || sh == ShapeKind::Slab) bad = true; }   // a door behind, or a roof stepping up behind it
            }
            sofa = closed(ea) && closed(eb) && !bad;
          }
          // pews: rows of wooden seats indoors, one row behind another the same way round (a chapel's, a hall's): each a
          // bench of ours (they were left as stairs, a church full of steps)
          bool pews = false;
          if (!stone && roofed && run.size() >= 2) for (int j : run) {
            const VPBlock& c = P.blocks[j];
            for (int d = 1; d <= 2 && !pews; d++) for (int sgn : {1, -1}) {
              const int q = at2(c.x + sgn * d * DX[rf], c.y, c.z + sgn * d * DZ[rf]);
              if (q >= 0 && q != j && shapeOf(q) == ShapeKind::Stairs && (m0(q) & 3) == rf && !(m0(q) & 4) && seatOk(q)) { pews = true; break; }
            }
            if (pews) break;
          }
          if (!arms && !back && !tbl && !sofa && !pews) continue;
          if (!arms && !back) {   // a table alone doesn't make a seat of a stair in a roof's stepped foot, or one out in the snow
            const VPBlock& c = P.blocks[run[0]];
            const int sb = shapeOf(at2(c.x + DX[rf], c.y, c.z + DZ[rf])), sf = shapeOf(at2(c.x + DX[front], c.y, c.z + DZ[front]));
            if (sb == ShapeKind::Stairs || sb == ShapeKind::Slab || sf == ShapeKind::Layer) continue;
          }
          if (std::getenv("DQ_FURN_LEFT") && sofa && !arms && !back && !tbl) {
            auto nm = [&](int j) { std::string k = keyOf(j); return k.empty() ? std::string("air") : k; };
            const VPBlock& c = P.blocks[run[0]];
            DQ_LOG("sofa %s %d,%d,%d x%zu %s ends %s,%s back %s/%s front %s roofed %d", P.name.c_str(), c.x, c.y, c.z, run.size(), keyOf(run[0]).c_str(), nm(ea).c_str(), nm(eb).c_str(),
                   nm(at2(c.x + DX[rf], c.y, c.z + DZ[rf])).c_str(), nm(at2(c.x + DX[rf], c.y + 1, c.z + DZ[rf])).c_str(), nm(at2(c.x + DX[front], c.y, c.z + DZ[front])).c_str(), (int)roofed);
          }
          if (stone && !tbl && !roofed) continue;   // (stone stairs out of doors with no table: steps, a wall's coping)
          if (run.size() > 4 && !tbl && !pews) continue;     // (a long row of stairs: a roof's edge or a terrace)
          for (long long q : tks) if (!table.count(q)) { const int wj = weak[q]; table[q] = woodAny(keyOf(wj)).empty() ? woodFor(run[0]) : woodAny(keyOf(wj)); }
          if (std::getenv("DQ_FURN_ALL")) {
            auto nm = [&](int j) { std::string k = keyOf(j); if (k.empty()) return std::string("air"); const size_t u = k.rfind('_'); return u == std::string::npos ? k : k.substr(u + 1); };
            const VPBlock& c = P.blocks[run[0]];
            DQ_LOG("seat %s %s x%zu why %s%s%s%s%s | back %s/%s front %s ends %s,%s", P.style.c_str(), P.name.c_str(), run.size(), arms ? "arms " : "", back && !window ? "back " : "", window ? "window " : "", tbl ? "table " : "", sofa ? "sofa" : "",
                   nm(at2(c.x + DX[rf], c.y, c.z + DZ[rf])).c_str(), nm(at2(c.x + DX[rf], c.y + 1, c.z + DZ[rf])).c_str(), nm(at2(c.x + DX[front], c.y, c.z + DZ[front])).c_str(), nm(ea).c_str(), nm(eb).c_str());
          }
          if (isArm(ea)) drop[ea] = 1;
          if (isArm(eb)) drop[eb] = 1;
          for (int j : run) {
            const VPBlock& c = P.blocks[j];
            backrest(c, rf, true);
            const std::string w = woodFor(j);
            P.blocks[j].s = (u16)stateOf(furn(w, run.size() == 1 ? "chair" : "bench"), P.states[P.blocks[j].s].meta);   // (their meta is the stair's: the back to the same side)
            seated[j] = 1; furnN[0]++; furnBy[P.style][0]++;
          }
        }
        if (std::getenv("DQ_FURN_LEFT")) for (int i = 0; i < (int)P.blocks.size(); i++) {   // the stair seats still left: what's round them
          if (seated[i] || !seatOk(i)) continue;
          const VPBlock& b = P.blocks[i]; const int rf = m0(i) & 3, fr = (rf + 2) & 3;
          bool roofed = false; for (int u = 2; u <= 6 && !roofed; u++) if (at2(b.x, b.y + u, b.z) >= 0) roofed = true;
          if (!roofed) continue;
          auto nm = [&](int j) { std::string k = keyOf(j); if (k.empty()) return std::string("air"); const size_t u = k.rfind('_'); return u == std::string::npos ? k : k.substr(u + 1); };
          DQ_LOG("left %s %d,%d,%d %s | back %s/%s sides %s,%s front %s,%s", P.name.c_str(), b.x, b.y, b.z, keyOf(i).c_str(),
                 nm(at2(b.x + DX[rf], b.y, b.z + DZ[rf])).c_str(), nm(at2(b.x + DX[rf], b.y + 1, b.z + DZ[rf])).c_str(),
                 nm(at2(b.x + DX[(rf + 1) & 3], b.y, b.z + DZ[(rf + 1) & 3])).c_str(), nm(at2(b.x + DX[(rf + 3) & 3], b.y, b.z + DZ[(rf + 3) & 3])).c_str(),
                 nm(at2(b.x + DX[fr], b.y, b.z + DZ[fr])).c_str(), nm(at2(b.x + 2 * DX[fr], b.y, b.z + 2 * DZ[fr])).c_str());
        }
        for (auto& [k, w] : table) {
          const int x = (int)(k & 0xFFFF), y = (int)((k >> 16) & 0xFFFF), z = (int)((k >> 32) & 0xFFFF), j = at2(x, y, z);
          if (j < 0 || drop[j]) continue;
          if (std::getenv("DQ_FURN_ALL")) DQ_LOG("tbl %s %s was %s/%d", P.style.c_str(), P.name.c_str(), keyOf(j).c_str(), m0(j));
          P.blocks[j].s = (u16)stateOf(furn(w, "table"), {0, 1, 2, 3});
          furnN[1]++; furnBy[P.style][1]++;
        }
        // Better Village's weaponsmith yard "machine" (a grindstone hung under an anvil, an observer and trapdoors over it, a
        // hopper on a cobble wall each side, carpet on the hoppers: Minecraft decoration, it does nothing): made a smith's
        // corner that works: the grindstone on the ground, an anvil beside it, a barrel the other side, the rest gone
        std::vector<VPBlock> added;
        for (int i = 0; i < (int)P.blocks.size(); i++) {
          if (keyOf(i) != "grindstone") continue;
          const VPBlock g = P.blocks[i];
          const int an = at2(g.x, g.y + 1, g.z);
          if (an < 0 || keyOf(an).find("anvil") == std::string::npos) continue;
          int ax = 1, az = 0;
          if (at2(g.x + 1, g.y + 1, g.z) < 0 && at2(g.x - 1, g.y + 1, g.z) < 0) { ax = 0; az = 1; }   // the side the walls stood
          for (int dy = 1; dy <= 3; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            const int j = at2(g.x + dx, g.y + dy, g.z + dz); if (j < 0) continue;
            const std::string q = keyOf(j);
            if (q.find("anvil") != std::string::npos || q == "observer" || q == "hopper" || q.find("trapdoor") != std::string::npos || q.find("carpet") != std::string::npos || q.find("_wall") != std::string::npos) drop[j] = 1;
          }
          for (int sd : {-1, 1}) {
            const int x = g.x + ax * sd, z = g.z + az * sd, j = at2(x, g.y, z);
            const std::string key = sd < 0 ? "anvil" : "barrel";
            if (findBlock(key) == NONE) continue;
            const int st = stateOf(key, sd < 0 ? std::array<u8, 4>{0, 1, 2, 3} : std::array<u8, 4>{0, 0, 0, 0});
            if (j < 0) added.push_back({(u8)x, (u8)g.y, (u8)z, (u16)st});
            else if (!reg().tSolid[P.states[P.blocks[j].s].id]) P.blocks[j].s = (u16)st;   // (a flower there)
          }
        }
        std::vector<VPBlock> kept2; for (int i = 0; i < (int)P.blocks.size(); i++) if (!drop[i]) kept2.push_back(P.blocks[i]);
        for (auto& a2 : added) kept2.push_back(a2);
        P.blocks.swap(kept2);
      }
    }
  }
  // a torch in a roof's eaves (our builder's pieces hung some in the row of stairs along the wall: a hole in the roof with
  // a torch in it): the eaves made whole, the stair either side carried on through it
  {
    std::map<long long, int> at;
    auto ck = [](int x, int y, int z) { return ((long long)x << 40) | ((long long)y << 20) | (long long)z; };
    for (int i = 0; i < (int)P.blocks.size(); i++) at[ck(P.blocks[i].x, P.blocks[i].y, P.blocks[i].z)] = i;
    const BlockId torch = findBlock("torch");
    auto stairAt = [&](int x, int y, int z) -> int { auto it = at.find(ck(x, y, z)); if (it == at.end()) return -1; const BlockId id = P.states[P.blocks[it->second].s].id; return reg().tShape[id] == ShapeKind::Stairs || reg().block(id).key.find("stairs") != std::string::npos ? it->second : -1; };
    for (VPBlock& b : P.blocks) {
      if (torch == NONE || P.states[b.s].id != torch) continue;
      for (auto [dx, dz] : {std::pair{1, 0}, {0, 1}}) {
        const int a1 = stairAt(b.x - dx, b.y, b.z - dz), a2 = stairAt(b.x + dx, b.y, b.z + dz);
        if (a1 >= 0 && a2 >= 0 && P.blocks[a1].s == P.blocks[a2].s) { b.s = P.blocks[a1].s; break; }
      }
    }
  }
  // a house left empty inside: furnished (furnishEmpty)
  if (kind == "houses" && !std::regex_search(name, std::regex("stable|farm|pen|ruin|well|lamp|decor|garden|field|pasture|pond|fountain|statue|tree|graveyard|camp|tent|market|gazebo"))) {
    int furn = 0;
    for (const VPBlock& b : P.blocks) { const VPState& st = P.states[b.s]; const BlockDef& d = reg().block(st.id);
      if (st.kind == VPK_CHEST || st.kind == VPK_BED_FOOT || st.kind == VPK_JOB || d.has("furniture") || d.has("table") || d.has("chair")) furn++; }
    if (furn <= 1) { const size_t before = P.blocks.size(); furnishEmpty(P); emptyFurnished += P.blocks.size() > before;
      if (std::getenv("DQ_FURN_LOG")) { std::string o; for (size_t i = before; i < P.blocks.size(); i++) o += " " + reg().block(P.states[P.blocks[i].s].id).key + "@" + std::to_string(P.blocks[i].x) + "," + std::to_string(P.blocks[i].y) + "," + std::to_string(P.blocks[i].z);
        DQ_LOG("furnished %s/%s (%dx%dx%d):%s", P.style.c_str(), P.name.c_str(), P.sx, P.sy, P.sz, o.c_str()); } }
  }
  return true;
}

namespace {
std::unique_ptr<VillagePieces> finishPack(std::unique_ptr<VillagePieces> pack, int pieces) {
  if (!pieces) return nullptr;
  // Towns and Towers' styles have no market: each takes the stalls of the style of ours nearest it (the baker's, butcher's,
  // farmer's, fisherman's, jeweller's and merchant's), a pool of its own the town plan draws on round the square (terrain 21)
  {
    static const std::map<std::string, std::string> NEAR = {{"tudor", "plains"}, {"classic", "plains"}, {"rustic", "plains"}, {"iberian", "plains"},
      {"flower_forest_japanese", "plains"}, {"mediterranean", "plains"}, {"birch_forest_romanian", "plains"}, {"swedish", "taiga"}, {"meadow_swiss", "alpine"},
      {"old_growth_taiga_polish", "taiga"}, {"snowy_taiga_viking", "snowy"}, {"sparse_jungle_polynesian", "jungle"}, {"jungle_tribal", "jungle"},
      {"badlands_pueblo", "mesa"}, {"swamp_boat", "swamp"}, {"mushroom_fields_fantasy", "mushroom"}, {"nilotic", "desert"}};
    int n = 0;
    for (auto& [st, near] : NEAR) {
      auto src = pack->pools.find(near + "/houses");
      if (src == pack->pools.end() || !pack->pools.count(st + "/houses")) continue;
      std::vector<VPPiece> stalls;
      for (const VPPiece& p : src->second) if (p.name.find("market_stall") != std::string::npos) { stalls.push_back(p); n++; }
      if (!stalls.empty()) pack->pools[st + "/market"] = std::move(stalls);
    }
    DQ_LOG("village pieces: %d market stalls lent to Towns and Towers' styles", n);
  }
  for (auto& [k, v] : pack->pools) std::sort(v.begin(), v.end(), [](const VPPiece& a, const VPPiece& b) { return a.name < b.name; });   // the same order on every machine
  { std::string by; for (auto& [st, n] : furnBy) by += " " + st + " " + std::to_string(n[0]) + "/" + std::to_string(n[1]); DQ_LOG("village pieces: chairs/tables by style:%s", by.c_str()); }
  DQ_LOG("village pieces: %d empty houses furnished", emptyFurnished);
  std::string miss; int shown = 0;
  for (auto& [k, c] : missing()) if (shown++ < 40) miss += (miss.empty() ? "" : ", ") + k;
  DQ_LOG("village pieces: %d in %d pools, %d chairs and %d tables of ours%s%s", pieces, (int)pack->pools.size(), furnN[0], furnN[1], miss.empty() ? "" : "; left out (no block of ours): ", miss.c_str());
  return pack;
}

} // namespace

const std::vector<VPPiece>* VillagePieces::pool(const std::string& name) const {
  std::string n = name;
  if (const size_t c = n.find(':'); c != std::string::npos) n = n.substr(c + 1);   // (any namespace: minecraft:, kaisyn: (Towns and Towers))
  if (n.rfind("village/", 0) == 0) n = n.substr(8);
  if (n.rfind("exclusives/", 0) == 0) n = n.substr(11);   // (Towns and Towers' exclusive styles: their own folder)
  auto it = pools.find(n);
  return it == pools.end() || it->second.empty() ? nullptr : &it->second;
}

const VillagePieces* villagePieces() {
  static const std::unique_ptr<VillagePieces> P = [] {
    auto p = loadPieces();
    if (!p) DQ_WARN("village pieces: none in %s/villages, villages are built the classic way", dataDir().string().c_str());
    return p;
  }();
  return P.get();
}

} // namespace dq
