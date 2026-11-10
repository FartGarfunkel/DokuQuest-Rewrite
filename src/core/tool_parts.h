// core/tool_parts.h — tools whose heads are made of more than one material (the idea is Cubyz's procedural tools; our
// own design and code, nothing taken from it). At an ordinary crafting table, a tool's pattern whose head holds two or
// three different materials (say iron, diamond and copper across a pickaxe's head) makes a mixed tool: one item per kind
// (mixed_pickaxe, mixed_axe, mixed_shovel, mixed_sword, mixed_hoe) whose stack carries what it is made of, tag "pt":
//   {"t": kind, "m": [material per head section, in the grid's order], "h": haft item,
//    "tier", "speed", "dmg", "dur", "el": element, "gem": a gem set in it at the Tool Forge}
// Its stats mix the materials' own tools' stats, weighted per section the way each part of the head does its work (a
// pickaxe's tips do the digging, its middle takes the wear), with a little more toughness for each extra metal; the haft
// changes speed and wear as the forge's hafts do. Its model takes each material's colour on its section of the head
// (assets/item_models.cpp, key partToolModelKey). Gear levels (sim/skills.h "gx") and repairs work on them like any tool;
// one of its own materials beside a worn one at the crafting table mends a quarter of it.
#pragma once
#include "core/itemstack.h"
#include "core/registry.h"

namespace dq {

// A material with no tool or armour set of its own (tin, quartz, amethyst, mese...) brings its own stats ("stats" in
// data/tool_materials.json): a pickaxe's tier, speed, damage and wear, which the other kinds take in proportion to iron's;
// and its armour against iron's (armorK points, armorDurK wear). Such a material makes a tool or a piece all of itself too.
struct ToolMaterial {
  std::string key, name, prefix, element; std::vector<std::string> items; vec3 rgb{1};
  bool stats = false, tools = true;
  int sTier = 0; float sSpeed = 0, sDamage = 0, sDur = 0, armorK = 0, armorDurK = 0, fireResist = 0;
};
const std::vector<ToolMaterial>& toolMaterials();                       // data/tool_materials.json
const ToolMaterial* toolMaterial(const std::string& key);
const ToolMaterial* toolMaterialOfItem(const std::string& itemKey);     // which material an item is (tags too)

bool isPartTool(const ItemStack& s);                                    // a mixed tool (tag "pt")
ToolInfo toolOf(const ItemStack& s);                                    // its stats: the item's own, or the mix's (+ its gem)
int toolMaxDurability(const ItemStack& s);                              // 0 for things that don't wear
std::string stackName(const ItemStack& s);                              // "Iron, Diamond & Copper Pickaxe"
std::string partToolModelKey(const ItemStack& s);                       // "pt:pickaxe:iron,diamond,copper:stick:" ("" if plain)

// The heads' patterns: for a kind, the grid cells (x, y in a w x h box) of its head sections and of its haft.
struct ToolPattern { std::string kind; int w, h; std::vector<ivec2> head, haft; };
const std::vector<ToolPattern>& toolPatterns();
// A mixed tool from these head materials (one per section) and haft item. false if they don't make one.
bool makePartTool(const std::string& kind, const std::vector<std::string>& mats, const std::string& haft, ItemStack& out);
// Mixed armour: the same at the crafting table for a helmet, chestplate, leggings or boots whose cells hold more than one
// material (each must have a piece of its own). Stack tag "pa": {"p": piece, "m": [material per cell, in the pattern's
// order], "pts", "dur", "fr"}; each cell's material colours its part of the piece's model (the cells laid over the picture).
struct ArmorPattern { std::string piece; int slot, w, h; std::vector<ivec2> cells; };
const std::vector<ArmorPattern>& armorPatterns();
const ArmorPattern* armorPattern(const std::string& piece);
bool isPartArmor(const ItemStack& s);
bool makePartArmor(const std::string& piece, const std::vector<std::string>& mats, ItemStack& out);
float armorPointsOf(const ItemStack& s);                                // worn: what it turns aside
float armorFireResistOf(const ItemStack& s);
std::string partArmorWornLook(const ItemStack& s);                      // what armorMeshes draws: its id, or a mixed piece's "pa:..." key

// Set a gem in a tool (the Tool Forge): what each gem does, and the tool with it (false: not a gem, or it already has one)
const char* gemEffect(const std::string& gemItem);
bool setToolGem(ItemStack& tool, const std::string& gemItem);

} // namespace dq
