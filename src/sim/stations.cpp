// sim/stations.cpp — see stations.h. Line-by-line ports of stationResult (game/blocks2.js), its enchanting.js
// wrapper (books onto gear, the grindstone taking enchantments off, joins keeping both enchantments) and
// stonecutterOptions (core/items.js).
#include "sim/stations.h"

namespace dq {

int maxDurabilityOf(const ItemDef* d) {
  if (!d) return 0;
  if (d->hasTool && d->tool.durability > 0) return d->tool.durability;
  if (d->props.contains("armor") && d->props["armor"].is_object()) return d->props["armor"].value("durability", 0);
  return 0;
}

std::string repairMaterial(const std::string& key) {
  // core/items.js TIERS × TOOL_KINDS, ARMOR_MATS × ARMOR_PIECES
  static const std::pair<const char*, const char*> TIERS[] = {{"wood", "#planks"}, {"stone", "#stone_tool"}, {"copper", "copper_ingot"}, {"bronze", "bronze_ingot"},
    {"iron", "iron_ingot"}, {"gold", "gold_ingot"}, {"diamond", "diamond"}, {"cinderite", "cinderite_shard"}};
  static const char* KINDS[] = {"pickaxe", "axe", "shovel", "sword", "hoe"};
  static const std::pair<const char*, const char*> ARMOR[] = {{"leather", "leather"}, {"copper", "copper_ingot"}, {"bronze", "bronze_ingot"}, {"iron", "iron_ingot"},
    {"gold", "gold_ingot"}, {"diamond", "diamond"}, {"cinderite", "cinderite_shard"}};
  static const char* PIECES[] = {"helmet", "chestplate", "leggings", "boots"};
  for (auto& [t, m] : TIERS) for (auto* k : KINDS) if (key == std::string(t) + "_" + k) return m;
  for (auto& [t, m] : ARMOR) for (auto* k : PIECES) if (key == std::string(t) + "_" + k) return m;
  return "";
}

// game/blocks2.js stationResult
static StationResult baseResult(const std::string& kind, const ItemStack& a, const ItemStack& b) {
  StationResult r;
  const ItemDef* da = a.empty() ? nullptr : reg().item(a.id);
  const ItemDef* db = b.empty() ? nullptr : reg().item(b.id);
  if (kind == "anvil" || kind == "grindstone") {
    if (a.empty() || b.empty()) return r;
    int max = maxDurabilityOf(da);
    if (!max || !(a.d > 0 || (b.id == a.id && b.d > 0))) return r;
    if (a.id == b.id) {
      int left = (max - a.d) + (max - b.d) + (int)std::floor(max * (kind == "anvil" ? 0.12 : 0.05));
      r.out = a; r.out.n = 1; r.out.d = std::max(0, max - left);
      r.useA = 1; r.useB = 1; r.note = "Joins the two into one"; r.ok = true;
      return r;
    }
    if (kind == "anvil") {
      std::string mat = repairMaterial(a.id);
      if (mat.empty() || !tagMatch(mat, b.id)) return r;
      int per = (int)std::ceil(max / 4.0), need = std::min(b.n, (int)std::ceil(a.d / (double)per));
      if (need <= 0) return r;
      r.out = a; r.out.n = 1; r.out.d = std::max(0, a.d - per * need);
      r.useA = 1; r.useB = need; r.ok = true;
      r.note = "Uses " + std::to_string(need) + " " + (db ? db->name : b.id);
    }
    return r;
  }
  if (kind == "smithing") {
    if (a.empty() || b.empty()) return r;
    std::string to;
    if (startsWith(a.id, "diamond_") && b.id == "cinderite_shard") to = "cinderite_" + a.id.substr(8);
    else if (startsWith(a.id, "copper_") && b.id == "tin_ingot") to = "bronze_" + a.id.substr(7);
    const ItemDef* dt = to.empty() ? nullptr : reg().item(to);
    if (!dt) return r;
    int maxA = maxDurabilityOf(da), maxT = maxDurabilityOf(dt);
    r.out = ItemStack(to, 1);
    if (a.d && maxA) r.out.d = (int)std::lround(a.d / (double)maxA * maxT);
    if (a.tag.is_object() && a.tag.contains("gx")) r.out.tag = json{{"gx", a.tag["gx"]}};
    r.useA = 1; r.useB = 1; r.note = "Upgraded, wear and all"; r.ok = true;
    return r;
  }
  if (kind == "loom") {
    if (a.empty() || b.empty() || !db || !db->has("dye")) return r;
    std::string to = recolorKey(a.id, db->str("dye"));
    if (to.empty()) return r;
    int n = std::min(a.n, 8);
    r.out = ItemStack(to, n); r.useA = n; r.useB = 1; r.note = "Up to eight blocks per dye"; r.ok = true;
    return r;
  }
  if (kind == "fletching") {
    bool flint = a.id == "flint" || b.id == "flint", feather = a.id == "feather" || b.id == "feather";
    if (!flint || !feather) return r;
    r.out = ItemStack("arrow", 6); r.useA = 1; r.useB = 1; r.note = "Six arrows, no sticks needed"; r.ok = true;
    return r;
  }
  return r;
}

StationResult stationResult(const std::string& kind, const ItemStack& a0, const ItemStack& b0) {
  ItemStack a = a0, b = b0;   // enchOf may settle what a treasure book holds
  // enchanting.js: a book (or the same item) puts its enchantments on
  if (kind == "anvil" && !a.empty() && !b.empty()) {
    std::string ka = enchKind(a.id);
    json eb = enchOf(b);
    bool same = a.id == b.id;
    if (!ka.empty() && eb.is_object() && !eb.empty() && (b.id == "enchanted_book" || same)) {
      ItemStack A = a, Bk = b;
      StationResult m = anvilEnchant(A, Bk);
      if (m.ok) {
        if (same && b.id != "enchanted_book") {
          StationResult base = baseResult(kind, a, b);
          if (base.ok) { json en = m.out.tag["en"]; m.out = base.out; m.out.n = 1; if (!m.out.tag.is_object()) m.out.tag = json::object(); m.out.tag["en"] = en; }
        }
        return m;
      }
      if (b.id == "enchanted_book") return StationResult();
    }
  }
  if (kind == "grindstone") {
    ItemStack* one = !a.empty() && b.empty() ? &a : !b.empty() && a.empty() ? &b : nullptr;
    if (one) { json en = enchOf(*one); if (en.is_object() && !en.empty()) return grindstoneDisenchant(&a, &b); }
  }
  StationResult r = baseResult(kind, a, b);
  // joining two worn tools keeps the enchantments of both (where they fit)
  if (r.ok && (kind == "anvil" || kind == "grindstone") && !a.empty() && !b.empty() && a.id == b.id) {
    json ea = enchOf(a), ebb = enchOf(b);
    bool any = (ea.is_object() && !ea.empty()) || (ebb.is_object() && !ebb.empty());
    if (any) {
      if (kind == "grindstone") { if (r.out.tag.is_object()) { r.out.tag.erase("en"); if (r.out.tag.empty()) r.out.tag = nullptr; } }
      else {
        ItemStack A = a, Bk = b;
        StationResult m = anvilEnchant(A, Bk);
        if (m.ok) {
          if (!r.out.tag.is_object()) r.out.tag = json::object();
          r.out.tag["en"] = m.out.tag["en"]; r.lv = m.lv;
          r.note += " \xC2\xB7 costs " + std::to_string(m.lv) + " level" + (m.lv > 1 ? "s" : "");
        }
      }
    }
  }
  return r;
}

std::vector<std::pair<std::string, int>> stonecutterOptions(const std::string& id) {
  std::vector<std::pair<std::string, int>> out;
  if (id.empty()) return out;
  for (auto& b : reg().blocks) {
    std::string fam = b.str("family");
    if (b.str("base") == id && !fam.empty() && fam != "fence" && fam != "gate") out.push_back({b.key, fam == "slab" ? 2 : 1});
  }
  for (auto& r : reg().recipes) {
    if (!r.shaped() || r.n != 4 || r.w != 2 || r.h != 2 || r.key.size() != 1 || r.key.begin()->second != id) continue;
    if (reg().blockId(r.out)) out.push_back({r.out, 1});
  }
  return out;
}

const char* stationHelp(const std::string& kind) {
  if (kind == "anvil") return "Put in a worn tool and its own material (ingots, planks, stones\xE2\x80\xA6), or two of the same tool.";
  if (kind == "grindstone") return "Put in two worn tools of the same kind to join them.";
  if (kind == "smithing") return "Diamond gear + a cinderite shard \xE2\x86\x92 cinderite gear. Copper gear + a tin ingot \xE2\x86\x92 bronze gear.";
  if (kind == "loom") return "Wool, carpet, glass, terracotta, concrete powder, beds or candles + a dye: recolours up to eight.";
  if (kind == "fletching") return "A flint and a feather make six arrows.";
  return "";
}
std::pair<const char*, const char*> stationPlaceholders(const std::string& kind) {
  if (kind == "anvil") return {"iron_pickaxe", "iron_ingot"};
  if (kind == "grindstone") return {"iron_sword", "iron_sword"};
  if (kind == "smithing") return {"diamond_pickaxe", "cinderite_shard"};
  if (kind == "loom") return {"wool", "red_dye"};
  if (kind == "fletching") return {"flint", "feather"};
  return {nullptr, nullptr};
}

} // namespace dq
