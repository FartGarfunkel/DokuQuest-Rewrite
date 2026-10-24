// core/registry.h — blocks, items, recipes, smelting and tags.
// Loaded from data/*.json (generated from the original JS by tools/port/extract_all.py), then
// extended by data packs and mods (mod/modloader). Numeric ids are runtime-only: saves store keys.
#pragma once
#include "common/fs.h"
#include <unordered_map>
#include <array>

namespace dq {

enum class Render : u8 { None = 0, Cube = 1, Cutout = 2, Cross = 3, Liquid = 4, Torch = 5, Model = 6, Shape = 7 };
namespace ShapeKind { enum : u8 { None = 0, Slab = 1, Stairs, Fence, Gate, Wall, Pane, Door, Trapdoor, Ladder, Carpet, Bed, Layer, Candle, Cake, Rod,
  Scaffold, Path, Frame, Composter, Cocoa, Petals, Wire, Lever, Button, Plate, Repeater, Comparator, Rail, PistonBase, PistonHead, Hopper,
  Daylight, Hook, Tripwire, Cactus, Chorus, Count }; }
enum Face : u8 { FACE_PX = 0, FACE_NX = 1, FACE_PY = 2, FACE_NY = 3, FACE_PZ = 4, FACE_NZ = 5 };  // +x -x +y -y +z -z (JS BLOCK_FACES order)

constexpr int MAX_BLOCKS = 4096;
using BlockId = u16;
constexpr BlockId AIR = 0;

struct BlockDef {
  BlockId id = 0;
  std::string key, name, tex, drop, sound = "stone", tool;   // tool: "", pickaxe, axe, shovel, hoe, shears, sword
  Render render = Render::Cube;
  u8 shapeKind = 0, light = 0, lightAtt = 0, tier = 0;
  float hardness = 1;
  bool solid = true, opaque = true, replaceable = false;
  std::array<u16, 6> faces{};      // tile layer per face (+x,-x,+y,-y,+z,-z)
  std::string mod = "dokuquest";   // owning mod id
  json props;                      // every original field (crop, door, furniture, tint, ...)
  bool has(const char* k) const { return props.contains(k) && !props[k].is_null() && props[k] != false; }
  std::string str(const char* k, const std::string& def = "") const { return props.contains(k) && props[k].is_string() ? props[k].get<std::string>() : def; }
  double num(const char* k, double def = 0) const { return props.contains(k) && props[k].is_number() ? props[k].get<double>() : def; }
};

struct ToolInfo { std::string type; int tier = 0; float speed = 1, damage = 1; int durability = 0; };
struct ItemDef {
  u16 id = 0;
  std::string key, name, icon, block;    // block = key of the block it places ("" = none)
  int maxStack = 64;
  bool hasTool = false; ToolInfo tool;
  std::string mod = "dokuquest";
  json props;   // armor, food, fuel, potion, desc, rarity, ...
  bool has(const char* k) const { return props.contains(k) && !props[k].is_null() && props[k] != false; }
  std::string str(const char* k, const std::string& def = "") const { return props.contains(k) && props[k].is_string() ? props[k].get<std::string>() : def; }
  double num(const char* k, double def = 0) const { return props.contains(k) && props[k].is_number() ? props[k].get<double>() : def; }
};

struct Recipe {
  std::string out; int n = 1;
  int w = 0, h = 0;
  std::vector<std::string> shape;                 // rows (shaped)
  std::unordered_map<char, std::string> key;      // char -> item key or #tag
  std::vector<std::string> items;                 // shapeless ingredients
  std::string group;
  json props;
  bool shaped() const { return !shape.empty(); }
};

class Registry {
public:
  std::vector<BlockDef> blocks;
  std::vector<ItemDef> items;
  std::unordered_map<std::string, BlockId> blockIds;
  std::unordered_map<std::string, u16> itemIds;
  std::unordered_map<std::string, std::string> blockAliases;
  std::vector<Recipe> recipes;
  std::unordered_map<std::string, std::string> smelting;           // input item -> output item
  std::unordered_map<std::string, std::vector<std::string>> tags;  // "#log" -> item keys
  std::unordered_map<std::string, int> tileIndex;                  // tile name -> atlas layer
  json mobs;      // MOB_TYPES (data/mobs.json)
  json models;    // MODEL_DEFS (rigs)

  // hot-loop tables (indexed by block id)
  u8 tOpaque[MAX_BLOCKS]{}, tSolid[MAX_BLOCKS]{}, tRender[MAX_BLOCKS]{}, tLight[MAX_BLOCKS]{}, tAtt[MAX_BLOCKS]{},
     tReplaceable[MAX_BLOCKS]{}, tLiquid[MAX_BLOCKS]{}, tPortal[MAX_BLOCKS]{}, tShape[MAX_BLOCKS]{}, tWlog[MAX_BLOCKS]{},
     tSink[MAX_BLOCKS]{}, tTrans[MAX_BLOCKS]{}, tLMeta[MAX_BLOCKS]{}, tTick[MAX_BLOCKS]{}, tGrassy[MAX_BLOCKS]{}, tLeaves[MAX_BLOCKS]{};

  bool loadBase(const fs::path& dataDir);                    // data/blocks.json, items.json, recipes.json, mobs.json, models.json, tiles.json
  bool loadDataPack(const fs::path& dir, const std::string& modId);   // same file names, merged in
  BlockId registerBlock(BlockDef def);                       // returns id (existing key -> replaced)
  u16 registerItem(ItemDef def);
  void addRecipe(Recipe r) { recipes.push_back(std::move(r)); }
  void finalize();                                           // rebuild tables, resolve faces/drops

  BlockId blockId(const std::string& key) const;             // aliases resolved; unknown -> AIR
  const BlockDef& block(BlockId id) const { return blocks[id < blocks.size() ? id : 0]; }
  const ItemDef* item(const std::string& key) const { auto it = itemIds.find(key); return it == itemIds.end() ? nullptr : &items[it->second]; }
  const ItemDef* itemOfBlock(BlockId id) const;
  int tile(const std::string& name) const { auto it = tileIndex.find(name); return it == tileIndex.end() ? -1 : it->second; }
  bool itemMatches(const std::string& itemKey, const std::string& pattern) const;  // pattern = key or #tag
  u8 metaLight(BlockId id, u8 meta) const;
};

Registry& reg();   // the global registry (filled before any world opens)

} // namespace dq
