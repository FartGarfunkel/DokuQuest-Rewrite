// sim/crafting.cpp — see crafting.h. Ported from core/items.js (matchGrid, recipeIngredients, recipeLayout,
// SMELTING rules), game/inventory.js (recipeMissing, ingredientLabel), game/dyeing.js, game/maps.js,
// game/blocks2.js (recolorKey), game/game.js (tickFurnace) and game/itemfix.js (smoker/blast classes).
#include "sim/crafting.h"
#include <unordered_set>

namespace dq {

CraftHooks& craftHooks() { static CraftHooks h; return h; }

bool tagMatch(const std::string& want, const std::string& id) {
  if (!want.empty() && want[0] == '#') {
    auto it = reg().tags.find(want);
    return it != reg().tags.end() && std::find(it->second.begin(), it->second.end(), id) != it->second.end();
  }
  return want == id;
}

ItemStack CraftMatch::result() const {
  ItemStack s(out, n);
  if (extra.is_object()) for (auto& [k, v] : extra.items()) {
    if (k == "d") s.d = v.get<int>();
    else { if (s.tag.is_null()) s.tag = json::object(); s.tag[k] = v; }
  }
  if (repair >= 0) s.d = repair;
  return s;
}

void recipeDims(const Recipe& r, int& w, int& h) {
  if (r.shaped()) { w = r.w; h = r.h; return; }
  w = r.props.value("w", (int)std::min<size_t>(3, std::max<size_t>(1, r.items.size())));
  h = r.props.value("h", 1);
}

// ---- dyes -----------------------------------------------------------------------------------------------
static const std::vector<std::pair<std::string, std::string>>& dyeTable() {
  static const std::vector<std::pair<std::string, std::string>> t = {
    {"white", "#e9ecec"}, {"orange", "#f07613"}, {"magenta", "#bd44b3"}, {"light_blue", "#3aafd9"}, {"yellow", "#f8c627"},
    {"lime", "#70b919"}, {"pink", "#ed8dac"}, {"gray", "#3e4447"}, {"light_gray", "#8e8e86"}, {"cyan", "#158991"},
    {"purple", "#792aac"}, {"blue", "#35399d"}, {"brown", "#724728"}, {"green", "#546d1b"}, {"red", "#a12722"}, {"black", "#141519"}};
  return t;
}
const std::string& dyeHex(const std::string& c) {
  static const std::string none;
  for (auto& [k, v] : dyeTable()) if (k == c) return v;
  return none;
}
static ivec3 hexRGB(const std::string& h) {
  unsigned v = 0; if (h.size() >= 7) v = (unsigned)std::strtoul(h.c_str() + 1, nullptr, 16);
  return ivec3((v >> 16) & 255, (v >> 8) & 255, v & 255);
}
std::string dyeMix(const std::vector<std::string>& cols) {
  if (cols.empty()) return "#ffffff";
  double r = 0, g = 0, b = 0, mx = 0;
  for (auto& h : cols) { ivec3 c = hexRGB(h); r += c.x; g += c.y; b += c.z; mx += std::max({c.x, c.y, c.z}); }
  double n = (double)cols.size(), avgMax = mx / n; r /= n; g /= n; b /= n;
  double k = avgMax / std::max(1.0, std::max({r, g, b}));   // keep it as bright as the dyes were
  auto to = [&](double v) { return (int)std::lround(clampv(v * k, 0.0, 255.0)); };
  return strFormat("#%02x%02x%02x", to(r), to(g), to(b));
}
std::string recolorKey(const std::string& key, const std::string& c) {
  auto bi = reg().blockIds.find(key);
  if (bi == reg().blockIds.end()) return "";
  const BlockDef& d = reg().block(bi->second);
  std::string base = key, col = d.str("color");
  if (key == "wool") { base = "white_wool"; col = "white"; }
  if (key == "bed") { base = "red_bed"; col = "red"; }
  static const std::map<std::string, std::string> plain = {{"candle", "candle"}, {"terracotta", "terracotta"}, {"glass", "stained_glass"}, {"glass_pane", "stained_glass_pane"}};
  std::string suffix;
  if (!col.empty()) { if (base.size() > col.size() + 1) suffix = base.substr(col.size() + 1); }
  else { auto it = plain.find(key); if (it != plain.end()) suffix = it->second; }
  if (suffix.empty()) return "";
  std::string out = c + "_" + suffix;
  if (out == "white_wool") out = "wool";
  if (out == "red_bed") out = "bed";
  return out != key && reg().item(out) ? out : "";
}

// ---- matching ---------------------------------------------------------------------------------------------
static CraftMatch special(const std::string& out, int n, int w = 1, int h = 1) { CraftMatch m; m.ok = true; m.out = out; m.n = n; m.w = w; m.h = h; return m; }

static CraftMatch matchBase(const std::vector<ItemStack>& slots, int W) {
  int x0 = W, y0 = W, x1 = -1, y1 = -1, count = 0;
  for (int i = 0; i < W * W && i < (int)slots.size(); i++) if (!slots[i].empty()) {
    int x = i % W, y = i / W; x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); count++;
  }
  if (!count) return {};
  // two worn tools of the same kind combine into one repaired tool (the classic way)
  if (count == 2) {
    std::vector<const ItemStack*> ts; for (auto& s : slots) if (!s.empty()) ts.push_back(&s);
    const ItemDef* d = ts[0]->def();
    if (ts[0]->id == ts[1]->id && d && d->hasTool && d->maxStack == 1) {
      int dur = d->tool.durability, left = (dur - ts[0]->d) + (dur - ts[1]->d) + (int)std::floor(dur * 0.05);
      auto gxOf = [](const ItemStack* s) { return s->tag.is_object() ? s->tag.value("gx", 0) : 0; };
      int gx = std::max(gxOf(ts[0]), gxOf(ts[1]));
      CraftMatch m = special(ts[0]->id, 1); m.repair = std::max(0, dur - left);
      if (gx) m.extra = {{"gx", gx}};
      return m;
    }
  }
  int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  auto at = [&](int x, int y) -> const ItemStack& { return slots[(y0 + y) * W + x0 + x]; };
  const auto& R = reg().recipes;
  for (size_t ri = 0; ri < R.size(); ri++) {
    const Recipe& r = R[ri];
    if (r.shaped()) {
      if (r.w != bw || r.h != bh) continue;
      for (int mirror = 0; mirror < 2; mirror++) {
        bool ok = true;
        for (int y = 0; y < bh && ok; y++) for (int x = 0; x < bw && ok; x++) {
          int cx = mirror ? bw - 1 - x : x;
          char ch = cx < (int)r.shape[y].size() ? r.shape[y][cx] : ' ';
          const ItemStack& s = at(x, y);
          if (ch == ' ') { if (!s.empty()) ok = false; }
          else { auto k = r.key.find(ch); if (s.empty() || k == r.key.end() || !tagMatch(k->second, s.id)) ok = false; }
        }
        if (ok) { CraftMatch m; m.ok = true; m.out = r.out; m.n = r.n; m.idx = (int)ri; m.recipe = &r; recipeDims(r, m.w, m.h); return m; }
      }
    } else if ((int)r.items.size() == count) {
      std::vector<std::string> left = r.items;
      bool ok = true;
      for (int i = 0; i < W * W && i < (int)slots.size() && ok; i++) {
        const ItemStack& s = slots[i]; if (s.empty()) continue;
        auto k = std::find_if(left.begin(), left.end(), [&](const std::string& want) { return tagMatch(want, s.id); });
        if (k == left.end()) ok = false; else left.erase(k);
      }
      if (ok && left.empty()) { CraftMatch m; m.ok = true; m.out = r.out; m.n = r.n; m.idx = (int)ri; m.recipe = &r; recipeDims(r, m.w, m.h); return m; }
    }
  }
  return {};
}

// game/dyeing.js: a dye and one block takes the colour; leather armour mixes any number of dyes
static CraftMatch matchDye(const std::vector<ItemStack>& slots, int W) {
  std::vector<const ItemStack*> dyes, rest;
  for (auto& s : slots) if (!s.empty()) { const ItemDef* d = s.def(); if (d && d->has("dye")) dyes.push_back(&s); else rest.push_back(&s); }
  if (!dyes.empty() && rest.size() == 1) {
    const ItemStack& a = *rest[0]; const ItemDef* da = a.def();
    if (da && da->has("armor") && startsWith(a.id, "leather_")) {
      std::vector<std::string> cols;
      for (auto* s : dyes) { const std::string& h = dyeHex(s->def()->str("dye")); if (!h.empty()) cols.push_back(h); }
      if (a.tag.is_object() && a.tag.contains("col") && a.tag["col"].is_string()) cols.push_back(a.tag["col"]);
      CraftMatch m = special(a.id, 1);
      m.extra = json::object(); if (a.d) m.extra["d"] = a.d;
      m.extra["col"] = dyeMix(cols);
      return m;
    }
    if (dyes.size() == 1) {
      std::string to = recolorKey(a.id, dyes[0]->def()->str("dye"));
      if (!to.empty()) return special(to, 1);
    }
  }
  return matchBase(slots, W);
}

// game/maps.js: copies of a map, or a map ringed with paper zoomed out
CraftMatch matchGrid(const std::vector<ItemStack>& slots, int W) {
  const ItemStack* fm = nullptr; int nf = 0, ne = 0, np = 0, other = 0, fi = -1;
  for (int i = 0; i < (int)slots.size(); i++) {
    const ItemStack& s = slots[i]; if (s.empty()) continue;
    if (s.id == "filled_map") { nf++; fm = &s; fi = i; } else if (s.id == "empty_map") ne++; else if (s.id == "paper") np++; else other++;
  }
  if (!nf) return matchDye(slots, W);
  if (nf != 1 || other) return {};
  std::string mapId = fm->tag.is_object() && fm->tag.contains("map") ? (fm->tag["map"].is_string() ? fm->tag["map"].get<std::string>() : fm->tag["map"].dump()) : "";
  if (ne && !np) { CraftMatch m = special("filled_map", ne + 1); if (!mapId.empty()) m.extra = {{"map", mapId}}; return m; }
  if (np == 8 && !ne && W == 3 && fi == 4) {
    if (mapId.empty() || !craftHooks().mapZoom) return {};
    std::string z = craftHooks().mapZoom(mapId);
    if (z.empty()) return {};
    CraftMatch m = special("filled_map", 1, 3, 3); m.extra = {{"map", z}};
    return m;
  }
  return {};
}

// ---- recipe book helpers -------------------------------------------------------------------------------
std::vector<std::pair<std::string, int>> recipeIngredients(const Recipe& r) {
  std::vector<std::pair<std::string, int>> c;
  auto add = [&](const std::string& k) { for (auto& p : c) if (p.first == k) { p.second++; return; } c.push_back({k, 1}); };
  if (r.shaped()) {
    // JS object order: keys appear in the order the shape first uses them
    for (auto& row : r.shape) for (char ch : row) if (ch != ' ') { auto it = r.key.find(ch); if (it != r.key.end()) add(it->second); }
  } else for (auto& k : r.items) add(k);
  return c;
}
std::vector<std::string> recipeLayout(const Recipe& r, int W) {
  std::vector<std::string> cells(W * W);
  if (r.shaped()) {
    for (int y = 0; y < r.h && y < W; y++) for (int x = 0; x < r.w && x < W; x++) {
      char ch = x < (int)r.shape[y].size() ? r.shape[y][x] : ' ';
      if (ch != ' ') { auto it = r.key.find(ch); if (it != r.key.end()) cells[y * W + x] = it->second; }
    }
  } else {
    int w, h; recipeDims(r, w, h); w = std::max(1, w);
    for (size_t i = 0; i < r.items.size(); i++) { int c = (int)(i / w) * W + (int)(i % w); if (c < W * W) cells[c] = r.items[i]; }
  }
  return cells;
}
std::string ingredientLabel(const std::string& key) {
  static const std::map<std::string, std::string> L = {{"#log", "Any Log"}, {"#planks", "Any Planks"}, {"#wood_slab", "Any Wooden Slab"}, {"#wool", "Any Wool"},
    {"#sand", "Any Sand"}, {"#stone_tool", "Cobblestone or Blackstone"}, {"#leaves", "Any Leaves"}, {"#coal", "Coal or Charcoal"}};
  auto it = L.find(key); if (it != L.end()) return it->second;
  const ItemDef* d = reg().item(key); return d ? d->name : key;
}
std::string ingredientIcon(const std::string& key) {
  if (!key.empty() && key[0] == '#') { auto it = reg().tags.find(key); return it != reg().tags.end() && !it->second.empty() ? it->second[0] : ""; }
  return key;
}
bool isRedstoneItem(const ItemDef* d) {
  if (!d) return false;
  if (d->has("cart") || d->key == "redstone") return true;
  if (d->block.empty() || d->key == "string") return false;
  auto bi = reg().blockIds.find(d->block); if (bi == reg().blockIds.end()) return false;
  const BlockDef& b = reg().block(bi->second);
  return b.has("rs") || b.has("rail") || b.key == "redstone_block" || b.has("noteBlock") || b.has("tnt");
}
std::string recipeCategory(const Recipe& r) {
  if (r.group == "tools") return "tools";
  if (r.group == "armor") return "armor";
  const ItemDef* d = reg().item(r.out);
  if (isRedstoneItem(d)) return "redstone";
  if (d && d->has("food")) return "food";
  if (d && !d->block.empty()) return "blocks";
  return "misc";
}

// ---- bags --------------------------------------------------------------------------------------------------
static int countIn(const std::vector<ItemStack>* v, const std::string& id) { int n = 0; if (v) for (auto& s : *v) if (!s.empty() && s.id == id) n += s.n; return n; }
int Bags::count(const std::string& id) const { return countIn(main, id) + countIn(bp, id); }
int Bags::countTag(const std::string& key) const {
  if (key.empty() || key[0] != '#') return count(key);
  int n = 0; auto it = reg().tags.find(key); if (it != reg().tags.end()) for (auto& k : it->second) n += count(k);
  return n;
}
static int removeIn(std::vector<ItemStack>* v, const std::string& id, int n) {
  if (!v) return n;
  for (int i = (int)v->size() - 1; i >= 0 && n > 0; i--) {
    ItemStack& s = (*v)[i];
    bool hasContents = s.tag.is_object() && s.tag.contains("c");
    if (!s.empty() && s.id == id && !hasContents) { int k = std::min(n, s.n); s.n -= k; n -= k; if (s.n <= 0) s.clear(); }
  }
  return n;
}
int Bags::remove(const std::string& id, int n) { n = removeIn(bp, id, n); return removeIn(main, id, n); }
int Bags::removeTag(const std::string& key, int n) {
  if (key.empty() || key[0] != '#') return remove(key, n);
  auto it = reg().tags.find(key); if (it == reg().tags.end()) return n;
  for (auto& k : it->second) { n = remove(k, n); if (!n) break; }
  return n;
}
std::map<std::string, int> recipeMissing(const Bags& b, const Recipe& r, const std::vector<ItemStack>* grid) {
  std::map<std::string, int> miss;
  for (auto& [k, n] : recipeIngredients(r)) {
    int have = b.countTag(k);
    if (grid) for (auto& s : *grid) if (!s.empty() && tagMatch(k, s.id)) have += s.n;
    if (have < n) miss[k] = n - have;
  }
  return miss;
}
bool canCraft(const Bags& b, const Recipe& r, const std::vector<ItemStack>* grid) { return recipeMissing(b, r, grid).empty(); }

// ---- furnaces ---------------------------------------------------------------------------------------------
int fuelValue(const std::string& key) { const ItemDef* d = reg().item(key); return d ? (int)d->num("fuel", 0) : 0; }

namespace {
struct SmeltClasses { std::unordered_set<std::string> food, ore; size_t seen = 0; };
SmeltClasses& smeltClasses() {
  static SmeltClasses c;
  if (c.seen == reg().smelting.size() && c.seen) return c;
  c.food = {"raw_meat", "venison", "rabbit", "potato", "kelp", "flour", "beef", "porkchop", "chicken", "mutton"};
  c.ore = {"iron_ore", "gold_ore", "copper_ore", "tin_ore", "crushed_iron", "crushed_gold", "crushed_copper", "crushed_tin", "cinderite_ore", "quartz_ore", "nether_gold_ore", "redstone_ore", "ancient_debris"};
  // itemfix.js: every ore smelts in a blast furnace and every raw food in a smoker
  auto oreLike = [](const std::string& k, const std::string& out) {
    return endsWith(k, "_ore") || startsWith(k, "crushed_") || startsWith(k, "raw_") || endsWith(k, "_blend") || endsWith(k, "_dust") || k == "ancient_debris"
      || endsWith(out, "_ingot") || endsWith(out, "_shard") || endsWith(out, "_nugget");
  };
  for (auto& [k, out] : reg().smelting) {
    const ItemDef* od = reg().item(out);
    if (od && od->has("food")) c.food.insert(k);
    else if (oreLike(k, out)) c.ore.insert(k);
  }
  c.seen = reg().smelting.size();
  return c;
}
}

std::string smeltResult(const std::string& kind, const std::string& input) {
  auto it = reg().smelting.find(input);
  if (it == reg().smelting.end()) return "";
  if (kind == "smoker" && !smeltClasses().food.count(input)) return "";
  if (kind == "blast" && !smeltClasses().ore.count(input)) return "";
  return it->second;
}

bool tickFurnace(json& e, double dt, const std::string& kind) {
  if (!e.is_object()) return false;
  if (!e.contains("items") || !e["items"].is_array()) e["items"] = json::array({nullptr, nullptr, nullptr});
  json& it = e["items"];
  while (it.size() < 3) it.push_back(nullptr);
  ItemStack inp = ItemStack::fromJson(it[0]), fuel = ItemStack::fromJson(it[1]), out = ItemStack::fromJson(it[2]);
  std::string result = inp.empty() ? "" : smeltResult(kind, inp.id);
  if (kind == "smoker" || kind == "blast") dt *= 2;
  bool canOut = !result.empty() && (out.empty() || (out.id == result && out.n < out.maxStack()));
  bool changed = false;
  double burn = e.value("burn", 0.0), cook = e.value("cook", 0.0), burnMax = e.value("burnMax", 0.0);
  if (burn <= 0 && canOut && !fuel.empty() && fuelValue(fuel.id) > 0) {
    burnMax = burn = fuelValue(fuel.id) * SMELT_TIME;
    if (fuel.id == "lava_bucket") fuel = ItemStack("bucket", 1);
    else if (--fuel.n <= 0) fuel.clear();
    changed = true;
  }
  if (burn > 0) {
    burn = std::max(0.0, burn - dt);
    if (canOut) {
      cook += dt;
      if (cook >= SMELT_TIME) {
        cook = 0; if (--inp.n <= 0) inp.clear();
        if (!out.empty()) out.n++; else out = ItemStack(result, 1);
        changed = true;
      }
    } else cook = 0;
    if (burn <= 0) changed = true;
  } else if (cook > 0) cook = std::max(0.0, cook - dt * 2);
  it[0] = inp.toJson(); it[1] = fuel.toJson(); it[2] = out.toJson();
  e["burn"] = burn; e["cook"] = cook; e["burnMax"] = burnMax;
  return changed;
}

} // namespace dq
