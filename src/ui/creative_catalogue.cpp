// ui/creative_catalogue.cpp — see creative_catalogue.h.
#include "ui/creative_catalogue.h"
#include "core/registry.h"
#include "sim/crafting.h"
#include <algorithm>
#include <cstring>
#include <regex>
#include <unordered_map>

namespace dq {

const std::vector<CreativeTab>& creativeTabs() {
  static const std::vector<CreativeTab> T = {
    {"all", "All items", "compass"}, {"build", "Building blocks", "bricks"}, {"colour", "Colours", "lime_wool"},
    {"nature", "Nature", "grass"}, {"func", "Functional", "crafting_table"}, {"redstone", "Redstone & rails", "redstone"},
    {"tools", "Tools & gear", "iron_pickaxe"}, {"combat", "Combat", "iron_sword"}, {"food", "Food", "apple"},
    {"magic", "Magic & potions", "apprentice_staff"}, {"mat", "Materials", "iron_ingot"}, {"nether", "The Nether", "netherrack"},
    {"sky", "The Skylands", "aether_grass_block"}, {"end", "The End", "end_stone"}, {"eggs", "Spawn eggs", "sheep_spawn_egg"},
    {"forged", "Forged & mixed", "steel_hafted_iron_pickaxe"},
  };
  return T;
}

namespace {
bool ends(const std::string& s, const char* e) { size_t n = std::strlen(e); return s.size() >= n && s.compare(s.size() - n, n, e) == 0; }
bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
const char* COLOURS[] = {"white", "light_gray", "gray", "black", "brown", "red", "orange", "yellow", "lime", "green", "cyan", "light_blue", "blue", "purple", "magenta", "pink"};
int colourOf(const std::string& k, std::string* rest) {
  for (int i = 15; i >= 0; i--) {   // (light_gray before gray, light_blue before blue)
    std::string c = std::string(COLOURS[i]) + "_";
    if (starts(k, c.c_str())) { if (rest) *rest = k.substr(c.size()); return i; }
  }
  for (int i = 0; i < 16; i++) { std::string c = std::string("_") + COLOURS[i]; if (ends(k, c.c_str())) { if (rest) *rest = k.substr(0, k.size() - c.size()); return i; } }
  return -1;
}
// a shape after its block: the block itself first, then these
const std::pair<const char*, int> SHAPES[] = {
  {"_planks", 1}, {"_log", 0}, {"_wood", 2}, {"_stairs", 4}, {"_vertical_slab", 6}, {"_slab", 5}, {"_wall", 7}, {"_fence_gate", 9}, {"_fence", 8},
  {"_door", 10}, {"_trapdoor", 11}, {"_pressure_plate", 12}, {"_button", 13}, {"_hanging_sign", 15}, {"_sign", 14}, {"_ladder", 16},
  {"_leaves", 3}, {"_sapling", 3}, {"_bookshelf", 17}, {"_chest", 18}, {"_barrel", 18}, {"_table", 19}, {"_chair", 19}, {"_stool", 19}, {"_bench", 19},
  {"_bricks", 1}, {"_brick", 1}, {"_tiles", 2}, {"_pillar", 3}, {"_lantern", 20},
};
const char* TOOLS[] = {"_sword", "_pickaxe", "_axe", "_shovel", "_hoe", "_helmet", "_chestplate", "_leggings", "_boots"};

struct Key { std::string group; int rank = 0; std::string name; };
Key sortKey(const ItemDef& d, const BlockDef* b) {
  Key k; k.name = d.name;
  std::string s = d.key;
  // a metal's tools and armour: by the tool's tier, then sword, pickaxe, axe, shovel, hoe, helmet ... boots
  for (int i = 0; i < 9; i++) if (ends(s, TOOLS[i]) && (d.hasTool || d.has("armor") || i == 0)) {
    k.group = std::string(1, (char)('0' + std::min(9, d.hasTool ? d.tool.tier : (int)(d.props.contains("armor") && d.props["armor"].is_object() ? d.props["armor"].value("points", 0) / 2 : 0)))) + s.substr(0, s.size() - std::strlen(TOOLS[i]));
    k.rank = i; return k;
  }
  if (b) { std::string base = b->str("base"), po = b->str("paintOf"); if (!po.empty()) s = po; else if (!base.empty()) s = base; }
  int c = colourOf(s, &s);
  if (c >= 0) { k.group = s; k.rank = c; return k; }
  if (starts(s, "stripped_")) { s = s.substr(9); k.rank = 1; }
  for (auto& [suf, r] : SHAPES) if (ends(s, suf)) { s = s.substr(0, s.size() - std::strlen(suf)); k.rank = r; break; }
  if (s == "planks" || s == "log" || s == "sapling" || s == "leaves") s = "oak";   // the game's oak things are plain
  // the variants of an item follow it (stairs, slabs ... of the same stem): stem the item's own key too
  if (d.key != s && k.rank == 0 && b && b->has("shapeKind")) k.rank = 4;
  k.group = s;
  return k;
}
}

std::string creativeTabOf(const std::string& key) {
  const ItemDef* dp = reg().item(key);
  if (!dp) return "mat";
  const ItemDef& d = *dp;
  const std::string& k = d.key;
  if (d.has("spawnEgg")) return "eggs";
  if (d.has("forged") || d.has("mixedTool") || d.has("mixedArmor") || d.has("forgeHead")) return "forged";
  if (d.has("tome") || d.has("staff") || d.has("charm") || d.has("potion") || d.has("splash") || d.has("school") || d.has("drink") || starts(k, "enchanted_book")) return "magic";
  if (!d.block.empty()) {
    const BlockDef& b = reg().block(reg().blockId(d.block));
    if (b.has("rs") || isRedstoneItem(&d) || k.find("rail") != std::string::npos) return "redstone";
    if (b.has("furnace") || b.has("container") || b.has("furniture") || b.has("bed") || b.has("seat") || b.has("shelf") || b.has("door") || b.has("trapdoor") ||
        b.has("station") || b.has("machine") || (b.light > 0 && !b.solid) || k.find("sign") != std::string::npos || k.find("ladder") != std::string::npos ||
        k.find("banner") != std::string::npos || k == "crafting_table" || k == "anvil" || k == "enchanting_table" || k == "brewing_stand" || k == "cauldron" ||
        k == "loom" || k == "smithing_table" || k == "fletching_table" || k == "cartography_table" || k == "stonecutter" || k == "grindstone" || k == "lectern" ||
        k == "composter" || k == "bell" || k == "beacon" || k == "jukebox" || k == "campfire" || k == "flower_pot" || k == "scaffolding") return "func";
    if (b.has("aether")) return "sky";
    if (b.has("betterEnd")) return "end";
    static const std::regex NETHER("(^|_)(nether|netherrack|crimson|warped|soul|blackstone|magma|glowstone|shroomlight|ancient_debris|brimsand|brimthaw|ember|rubyglass|gloomstone|cinderite|nylium|wart)(_|$)");
    if (b.has("bta") || std::regex_search(k, NETHER)) return "nether";
    static const std::regex COLOURED("(^|_)(wool|carpet|concrete|concrete_powder|stained_glass|stained_glass_pane|glazed_terracotta|terracotta|candle|shulker_box|flag)$");
    if (b.has("color") || b.has("cloth") || b.has("painted") || b.has("paintOf") || std::regex_search(k, COLOURED)) return "colour";
    static const std::regex NATURE("(^|_)(dirt|grass|sand|red_sand|gravel|clay|mud|snow|ice|stone|granite|diorite|andesite|deepslate|tuff|calcite|basalt|obsidian|mycelium|podzol|moss|vine|vines|lily_pad|kelp|seagrass|coral|mushroom|cactus|bamboo|sugar_cane|fern|bush|roots|log|stem|leaves|sapling|ore|limestone|permafrost|sandstone|end_stone|flower|tulip|orchid|poppy|dandelion|allium|bluet|daisy|cornflower|rose|lilac|peony|pumpkin|melon|hay_block|farmland|path|sponge|dripstone|amethyst_block|budding_amethyst|turf|litter)$");
    if (b.has("isLeaves") || b.has("flower") || b.has("sapling") || b.has("crop") || b.has("plants") || b.has("isLog") ||
        (std::regex_search(k, NATURE) && !b.has("shapeKind") && k.find("brick") == std::string::npos && k.find("polished") == std::string::npos &&
         k.find("chiseled") == std::string::npos && k.find("smooth") == std::string::npos && k.find("cut_") == std::string::npos)) return "nature";
    return "build";
  }
  if (d.has("armor") || ends(k, "_sword") || d.has("bow") || k.find("arrow") != std::string::npos || k.find("shield") != std::string::npos || ends(k, "_spear") || ends(k, "_lance")) return "combat";
  if (d.has("cart") || k.find("minecart") != std::string::npos) return "redstone";
  if (d.hasTool || d.has("bucket") || d.has("compass") || d.has("boat") || d.has("backpack") || d.has("hook") || d.has("hat") || d.has("cape") || d.has("disc") ||
      d.has("firework") || k == "shears" || k == "fishing_rod" || k == "flint_and_steel" || k == "clock" || k == "map" || k == "filled_map" || k == "spyglass" ||
      k == "lead" || k == "name_tag" || k == "saddle" || k == "whistle" || k == "paintbrush" || k == "book" || k == "writable_book" || k == "travel_journal" ||
      k == "ember_igniter" || k == "hand_sieve" || k == "watering_can" || k == "saddle_bags") return "tools";
  if (d.has("food") || d.has("bowl")) return "food";
  return "mat";
}

const std::vector<std::string>& creativeItems(const std::string& tab) {
  static std::unordered_map<std::string, std::vector<std::string>> cache;
  static size_t builtFor = 0;
  if (builtFor != reg().items.size()) { cache.clear(); builtFor = reg().items.size(); }
  auto it = cache.find(tab);
  if (it != cache.end()) return it->second;
  struct E { std::string key, tab; Key k; size_t order; };
  std::vector<E> all;
  size_t n = 0;
  for (auto& d : reg().items) {
    if (d.key.empty() || d.has("hidden") || d.has("hideCreative")) continue;
    const BlockDef* b = d.block.empty() ? nullptr : &reg().block(reg().blockId(d.block));
    all.push_back({d.key, creativeTabOf(d.key), sortKey(d, b), n++});
  }
  // tabs in their own order; families together; registry order breaks ties (so related things the data lists
  // together stay together)
  std::unordered_map<std::string, size_t> tabRank, groupFirst;
  for (size_t i = 0; i < creativeTabs().size(); i++) tabRank[creativeTabs()[i].key] = i;
  for (auto& e : all) { auto g = e.tab + "|" + e.k.group; if (!groupFirst.count(g)) groupFirst[g] = e.order; }
  std::sort(all.begin(), all.end(), [&](const E& a, const E& b) {
    if (a.tab != b.tab) return tabRank[a.tab] < tabRank[b.tab];
    size_t ga = groupFirst[a.tab + "|" + a.k.group], gb = groupFirst[b.tab + "|" + b.k.group];
    if (a.tab == "combat" || a.tab == "tools") { if (a.k.group != b.k.group) return a.k.group < b.k.group; }
    else if (ga != gb) return ga < gb;
    if (a.k.rank != b.k.rank) return a.k.rank < b.k.rank;
    return a.order < b.order;
  });
  for (auto& t : creativeTabs()) cache[t.key];
  for (auto& e : all) { cache[e.tab].push_back(e.key); cache["all"].push_back(e.key); }
  return cache[tab];
}

} // namespace dq
