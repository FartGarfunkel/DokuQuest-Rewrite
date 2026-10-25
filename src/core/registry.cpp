#include "core/registry.h"

namespace dq {

Registry& reg() { static Registry r; return r; }

static BlockDef blockFromJson(const json& d) {
  BlockDef b;
  b.props = d;
  b.key = d.value("key", "");
  b.name = d.value("name", b.key);
  if (d.contains("tex") && d["tex"].is_string()) b.tex = d["tex"];
  b.render = (Render)d.value("render", 1);
  b.solid = d.value("solid", true);
  b.opaque = d.value("opaque", b.render == Render::Cube);
  b.replaceable = d.value("replaceable", false);
  b.hardness = d.value("hardness", 1.0f);
  if (d.contains("tool") && d["tool"].is_string()) b.tool = d["tool"];
  b.tier = (u8)d.value("tier", 0);
  b.drop = d.contains("drop") && d["drop"].is_string() ? d["drop"].get<std::string>() : (d.contains("drop") && d["drop"].is_null() ? "" : b.key);
  b.light = (u8)d.value("light", 0);
  b.lightAtt = (u8)d.value("lightAtt", 0);
  if (d.contains("sound") && d["sound"].is_string()) b.sound = d["sound"];
  b.shapeKind = (u8)d.value("shapeKind", 0);
  return b;
}

static ItemDef itemFromJson(const json& d) {
  ItemDef it;
  it.props = d;
  it.key = d.value("key", "");
  it.name = d.value("name", it.key);
  it.icon = d.value("icon", it.key);
  if (d.contains("block") && d["block"].is_string()) it.block = d["block"];
  it.maxStack = d.value("maxStack", 64);
  if (d.contains("tool") && d["tool"].is_object()) {
    auto& t = d["tool"]; it.hasTool = true;
    it.tool.type = t.value("type", ""); it.tool.tier = t.value("tier", 0); it.tool.speed = t.value("speed", 1.0f);
    it.tool.damage = t.value("damage", 1.0f); it.tool.durability = t.value("durability", 0);
  }
  return it;
}

static Recipe recipeFromJson(const json& d) {
  Recipe r; r.props = d;
  r.out = d.value("out", ""); r.n = d.value("n", 1);
  if (d.contains("shape")) for (auto& row : d["shape"]) r.shape.push_back(row.get<std::string>());
  if (d.contains("key")) for (auto& [k, v] : d["key"].items()) if (!k.empty() && v.is_string()) r.key[k[0]] = v.get<std::string>();
  if (d.contains("items")) for (auto& v : d["items"]) if (v.is_string()) r.items.push_back(v.get<std::string>());
  r.h = (int)r.shape.size();
  for (auto& row : r.shape) r.w = std::max(r.w, (int)row.size());
  r.group = d.value("group", "");
  return r;
}

bool Registry::loadBase(const fs::path& dir) {
  auto tj = readJson(dir / "tiles.json");
  if (tj) for (auto& [k, v] : (*tj)["names"].items()) tileIndex[k] = v.get<int>();
  auto bj = readJson(dir / "blocks.json");
  auto ij = readJson(dir / "items.json");
  auto rj = readJson(dir / "recipes.json");
  if (!bj || !ij || !rj) { DQ_ERR("game data missing in %s", dir.string().c_str()); return false; }
  const json& bl = (*bj)["blocks"];
  const json& faces = (*bj)["faces"];
  for (size_t i = 0; i < bl.size(); i++) {
    if (bl[i].is_null()) continue;
    BlockDef b = blockFromJson(bl[i]);
    if (i < faces.size() && faces[i].is_array()) for (int f = 0; f < 6 && f < (int)faces[i].size(); f++) b.faces[f] = faces[i][f].get<u16>();
    registerBlock(std::move(b));
  }
  if ((*bj).contains("aliases")) for (auto& [k, v] : (*bj)["aliases"].items()) if (v.is_string()) blockAliases[k] = v;
  for (auto& [k, v] : ij->items()) registerItem(itemFromJson(v));
  for (auto& r : (*rj)["recipes"]) recipes.push_back(recipeFromJson(r));
  for (auto& [k, v] : (*rj)["smelting"].items()) if (v.is_string()) smelting[k] = v;
  for (auto& [k, v] : (*rj)["tags"].items()) for (auto& s : v) tags[k].push_back(s.get<std::string>());
  if (auto m = readJson(dir / "mobs.json")) mobs = *m;
  if (auto m = readJson(dir / "models.json")) models = *m;
  finalize();
  DQ_LOG("registry: %zu blocks, %zu items, %zu recipes, %zu tiles", blocks.size(), items.size(), recipes.size(), tileIndex.size());
  return true;
}

bool Registry::loadDataPack(const fs::path& dir, const std::string& modId) {
  // A data pack may hold any of: blocks.json ([{key,...}] with "tex" naming a tile or a png in textures/),
  // items.json ({key:{...}} or [...]), recipes.json ({recipes:[], smelting:{}, tags:{}}), mobs.json
  bool any = false;
  if (auto bj = readJson(dir / "blocks.json")) {
    const json& arr = bj->is_array() ? *bj : (*bj)["blocks"];
    for (auto& d : arr) { BlockDef b = blockFromJson(d); b.mod = modId;
      int t = tile(b.tex); if (t >= 0) b.faces.fill((u16)t);
      registerBlock(std::move(b)); any = true; }
  }
  if (auto ij = readJson(dir / "items.json")) {
    auto add = [&](const json& d) { ItemDef it = itemFromJson(d); it.mod = modId; registerItem(std::move(it)); any = true; };
    if (ij->is_array()) for (auto& d : *ij) add(d); else for (auto& [k, d] : ij->items()) add(d);
  }
  if (auto rj = readJson(dir / "recipes.json")) {
    if (rj->contains("recipes")) for (auto& r : (*rj)["recipes"]) recipes.push_back(recipeFromJson(r));
    if (rj->contains("smelting")) for (auto& [k, v] : (*rj)["smelting"].items()) smelting[k] = v;
    if (rj->contains("tags")) for (auto& [k, v] : (*rj)["tags"].items()) for (auto& s : v) tags[k].push_back(s);
    any = true;
  }
  if (auto mj = readJson(dir / "mobs.json")) { for (auto& [k, v] : mj->items()) mobs[k] = v; any = true; }
  return any;
}

BlockId Registry::registerBlock(BlockDef def) {
  auto it = blockIds.find(def.key);
  if (it != blockIds.end()) { def.id = it->second; blocks[def.id] = std::move(def); return it->second; }
  if (blocks.size() >= MAX_BLOCKS) { DQ_ERR("too many block kinds, %s ignored", def.key.c_str()); return AIR; }
  def.id = (BlockId)blocks.size();
  blockIds[def.key] = def.id;
  blocks.push_back(std::move(def));
  return blocks.back().id;
}
u16 Registry::registerItem(ItemDef def) {
  auto it = itemIds.find(def.key);
  if (it != itemIds.end()) { def.id = it->second; items[def.id] = std::move(def); return it->second; }
  def.id = (u16)items.size();
  itemIds[def.key] = def.id;
  items.push_back(std::move(def));
  return items.back().id;
}

void Registry::finalize() {
  // shaped blocks inherit textures from what they are made of
  for (auto& b : blocks) if (b.tex.empty() && b.has("base")) { BlockId p = blockId(b.str("base")); if (p) b.tex = blocks[p].tex; }
  static const char* tickKeys[] = {"berry_bush_empty", "sugar_cane", "cactus", "kelp", "bamboo", "twisting_vines", "weeping_vines", "cave_vines",
                                   "budding_amethyst", "redstone_ore_lit", "moving_block", "puddle", "snow_layer"};
  for (auto& b : blocks) {
    u16 i = b.id;
    tOpaque[i] = b.opaque; tSolid[i] = b.solid; tRender[i] = (u8)b.render; tLight[i] = b.light; tAtt[i] = b.lightAtt;
    tReplaceable[i] = b.replaceable;
    std::string liq = b.str("liquid");
    tLiquid[i] = liq == "water" ? 1 : liq == "lava" ? 2 : 0;
    tPortal[i] = (u8)b.num("portal", 0);
    tShape[i] = b.shapeKind; tWlog[i] = b.has("wlog"); tSink[i] = b.has("sink"); tTrans[i] = b.has("translucent") || b.key == "ice";   // ice is see-through too (its tile carries the alpha); the JS data only flags the glass family
    tLMeta[i] = b.has("lightMeta"); tLeaves[i] = b.has("isLeaves");
    bool tick = b.has("tree") || b.has("crop") || b.has("fire") || b.has("oxidizes") || b.has("hive");
    for (auto* k : tickKeys) if (b.key == k) tick = true;
    tTick[i] = tick;
  }
  for (const char* k : {"grass", "sky_grass", "mycelium", "dirt_with_dry_grass"}) { BlockId id = blockId(k); if (id) tGrassy[id] = 1; }
  for (auto& [a, b] : std::vector<std::pair<const char*, const char*>>{{"copper_block", "exposed_copper"}, {"exposed_copper", "weathered_copper"},
       {"weathered_copper", "oxidized_copper"}, {"cut_copper", "exposed_cut_copper"}, {"exposed_cut_copper", "weathered_cut_copper"}, {"weathered_cut_copper", "oxidized_cut_copper"}}) {
    BlockId id = blockId(a); if (id) { blocks[id].props["oxidizes"] = b; tTick[id] = 1; }
  }
}

BlockId Registry::blockId(const std::string& key) const {
  auto it = blockIds.find(key);
  if (it != blockIds.end()) return it->second;
  auto al = blockAliases.find(key);
  if (al != blockAliases.end()) { auto it2 = blockIds.find(al->second); if (it2 != blockIds.end()) return it2->second; }
  return AIR;
}
const ItemDef* Registry::itemOfBlock(BlockId id) const {
  const auto& b = block(id);
  if (auto* it = item(b.key)) return it;
  return nullptr;
}
bool Registry::itemMatches(const std::string& itemKey, const std::string& pattern) const {
  if (pattern.empty()) return false;
  if (pattern[0] != '#') return itemKey == pattern;
  auto it = tags.find(pattern);
  if (it == tags.end()) return false;
  return std::find(it->second.begin(), it->second.end(), itemKey) != it->second.end();
}
u8 Registry::metaLight(BlockId id, u8 meta) const {
  if (!tLMeta[id]) return tLight[id];
  const auto& b = blocks[id]; int n = (meta & 3) + 1;
  std::string lm = b.str("lightMeta");
  if (lm == "candle") return (meta & 4) ? 3 * n : 0;
  if (lm == "pickle") return (meta & 0x80) ? 3 + 3 * n : 0;
  if (lm == "campfire") return (meta & 8) ? 0 : b.light;   // camping.js: a fire that's out gives no light
  return b.light;
}

} // namespace dq
