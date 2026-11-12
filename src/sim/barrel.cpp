// sim/barrel.cpp — see barrel.h
#include "sim/barrel.h"
#include "sim/firepit.h"
#include "common/fs.h"
#include "core/registry.h"
#include <mutex>

namespace dq {

namespace {
struct BRecipe { std::string liquid, item, out, outLiquid; int use = 0; double hours = 0; bool instant = false; };
const std::vector<BRecipe>& recipes() {
  static std::vector<BRecipe> v; static std::once_flag once;
  std::call_once(once, [] {
    auto j = readJson(dataDir() / "barrel.json");
    if (j && (*j)["recipes"].is_array()) for (auto& r : (*j)["recipes"]) v.push_back({r.value("liquid", ""), r.value("item", ""), r.value("out", ""), r.value("outLiquid", ""), r.value("use", 0), r.value("hours", 0.0), r.value("instant", false)});
  });
  return v;
}
bool itemIs(const std::string& id, const std::string& want) { return want == id || (!want.empty() && want[0] == '#' && reg().itemMatches(id, want)); }
json* ent(World& w, int x, int y, int z) {
  static BlockId B = 0; if (!B) B = reg().blockId("sealed_barrel");
  if (!B || w.getBlock(x, y, z) != B) return nullptr;
  json* e = w.blockEntity(x, y, z, true);
  if (!e->is_object() || e->value("type", "") != "sbarrel") *e = json{{"type", "sbarrel"}, {"liquid", ""}, {"amount", 0}, {"items", json::array({nullptr, nullptr, nullptr, nullptr})}, {"sealed", false}, {"t", 0.0}};
  return e;
}
json& sealedList(Level& lv) { json& l = lv.info.data["barrels"]; if (!l.is_array()) l = json::array(); return l; }
// the mixes that happen at once
void mixNow(json& e) {
  for (auto& it : e["items"]) {
    if (it.is_null()) continue;
    ItemStack s = ItemStack::fromJson(it);
    for (auto& r : recipes()) {
      if (!r.instant || r.liquid != e.value("liquid", "") || !itemIs(s.id, r.item) || e.value("amount", 0) <= 0) continue;
      if (!r.outLiquid.empty()) e["liquid"] = r.outLiquid;
      s.n -= std::max(1, r.use);
      it = s.n > 0 ? s.toJson() : json();
      break;
    }
  }
}
}

bool barrelPour(World& w, int x, int y, int z, ItemStack& bucket) {
  json* e = ent(w, x, y, z);
  if (!e || e->value("sealed", false)) return false;
  const std::string liq = e->value("liquid", ""); const int n = e->value("amount", 0);
  if (bucket.id == "water_bucket") {
    if ((!liq.empty() && liq != "water") || n >= BARREL_BUCKETS) return false;
    (*e)["liquid"] = "water"; (*e)["amount"] = n + 1; bucket = ItemStack("bucket", 1);
    mixNow(*e);
  } else if (bucket.id == "bucket") {
    if (liq != "water" || n <= 0) return false;
    (*e)["amount"] = n - 1; if (n - 1 <= 0) (*e)["liquid"] = ""; bucket = ItemStack("water_bucket", 1);
  } else return false;
  w.blockEntityChanged(x, y, z);
  return true;
}
bool barrelPut(World& w, int x, int y, int z, ItemStack& held) {
  json* e = ent(w, x, y, z);
  if (!e || held.empty() || e->value("sealed", false)) return false;
  for (auto& it : (*e)["items"]) if (it.is_null()) { it = held.toJson(); held = ItemStack(); mixNow(*e); w.blockEntityChanged(x, y, z); return true; }
  return false;
}
ItemStack barrelTake(World& w, int x, int y, int z) {
  json* e = ent(w, x, y, z);
  if (!e || e->value("sealed", false)) return {};
  auto& items = (*e)["items"];
  for (int i = (int)items.size() - 1; i >= 0; i--) if (!items[i].is_null()) { ItemStack s = ItemStack::fromJson(items[i]); items[i] = nullptr; w.blockEntityChanged(x, y, z); return s; }
  return {};
}
bool barrelSeal(Level& lv, World& w, int x, int y, int z, bool seal) {
  json* e = ent(w, x, y, z);
  if (!e || e->value("sealed", false) == seal) return false;
  (*e)["sealed"] = seal; (*e)["t"] = 0.0;
  if (seal) sealedList(lv).push_back({w.realm, x, y, z});
  w.blockEntityChanged(x, y, z);
  return true;
}
std::string barrelLiquid(World& w, int x, int y, int z) { json* e = ent(w, x, y, z); return e ? e->value("liquid", "") : ""; }
ItemStack barrelItem(World& w, int x, int y, int z, int i) { json* e = ent(w, x, y, z); return e && i >= 0 && i < BARREL_SLOTS ? ItemStack::fromJson((*e)["items"][i]) : ItemStack(); }

void barrelTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  json& l = sealedList(lv);
  const double hour = gameHourSecs(lv);
  for (size_t i = 0; i < l.size();) {
    World* w = lv.worldIfOpen(l[i][0].get<int>());
    const int x = l[i][1], y = l[i][2], z = l[i][3];
    if (!w || !w->isLoaded(x, y, z)) { i++; continue; }
    json* e = ent(*w, x, y, z);
    if (!e || !e->value("sealed", false)) { l.erase(l.begin() + i); continue; }
    const double t = e->value("t", 0.0) + dt;
    (*e)["t"] = t;
    bool changed = false;
    for (auto& r : recipes()) {
      if (r.instant || r.liquid != e->value("liquid", "") || e->value("amount", 0) <= 0 || t < r.hours * hour) continue;
      for (auto& it : (*e)["items"]) {
        if (it.is_null()) continue;
        ItemStack s = ItemStack::fromJson(it);
        if (!itemIs(s.id, r.item)) continue;
        if (!r.outLiquid.empty()) { (*e)["liquid"] = r.outLiquid; s.n -= std::max(1, r.use); it = s.n > 0 ? s.toJson() : json(); }
        if (!r.out.empty()) { s.id = r.out; it = s.toJson(); }
        changed = true;
        break;
      }
      if (changed) break;
    }
    if (changed) { (*e)["t"] = 0.0; w->blockEntityChanged(x, y, z); }
    i++;
  }
}

bool scrapeHide(ItemStack& hide, ItemStack& knife) {
  const ItemDef* k = knife.def();
  if (hide.id != "soaked_hide" || !k || !k->has("knife")) return false;
  hide.id = "scraped_hide"; knife.d++;
  if (k->hasTool && knife.d >= k->tool.durability) knife = ItemStack();
  return true;
}
std::string hideFor(const std::string& drop, bool immersiveWorld) { return immersiveWorld && drop == "leather" ? "raw_hide" : drop; }

} // namespace dq
