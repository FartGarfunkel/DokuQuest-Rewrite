// sim/trommel.cpp — the Trommel (see trommel.h; BTA 8.0.1 TileEntityTrommel.tick / sieveItem).
#include "sim/trommel.h"
#include "sim/creature.h"
#include "sim/inventory.h"
#include "sim/level.h"
#include "sim/magic_combat.h"
#include "sim/redstone.h"
#include "sim/worldsys.h"
#include "core/registry.h"
#include "common/fs.h"
#include "world/world.h"
#include <random>

namespace dq {

namespace {
struct Row { float w; std::string item; int lo, hi; };
struct Table { std::unordered_map<std::string, std::vector<Row>> sift; std::unordered_map<std::string, std::string> alias; };
const Table& table() {
  static Table t; static bool loaded = false;
  if (loaded) return t;
  loaded = true;
  if (auto j = readJson(dataDir() / "trommel.json"); j && j->is_object()) {
    if (j->contains("sift")) for (auto& [k, rows] : (*j)["sift"].items()) for (auto& r : rows) if (r.is_array() && r.size() >= 4)
      t.sift[k].push_back({r[0].get<float>(), r[1].get<std::string>(), r[2].get<int>(), r[3].get<int>()});
    if (j->contains("alias")) for (auto& [k, v] : (*j)["alias"].items()) if (v.is_string()) t.alias[k] = v.get<std::string>();
  }
  return t;
}
const std::vector<Row>* rowsOf(const std::string& key) {
  const Table& t = table();
  auto a = t.alias.find(key);
  auto it = t.sift.find(a != t.alias.end() ? a->second : key);
  return it == t.sift.end() ? nullptr : &it->second;
}
std::mt19937& rng() { static std::mt19937 r{std::random_device{}()}; return r; }
float uni() { return std::uniform_real_distribution<float>(0, 1)(rng()); }
// the front faces +z, +x, -z, -x for meta & 3 = 0..3 (the mesher's FACING_FACE); BTA throws out of the block's left
ivec3 outDir(u8 meta) {
  static const ivec3 FRONT[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};
  ivec3 f = FRONT[meta & 3];
  return ivec3(f.z, 0, -f.x);
}
}

bool trommelSifts(const std::string& key) { return rowsOf(key) != nullptr; }

std::vector<std::pair<float, std::string>> trommelYields(const std::string& key) {
  std::vector<std::pair<float, std::string>> out;
  const auto* rows = rowsOf(key);
  if (!rows) return out;
  float tot = 0; for (auto& r : *rows) tot += r.w;
  for (auto& r : *rows) out.push_back({tot > 0 ? r.w / tot : 0, r.item});
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.first > b.first; });
  return out;
}

static void sift(Level& lv, World& w, int realm, std::vector<ItemStack>& slots, int s, ivec3 pos, u8 meta) {
  const auto* rows = rowsOf(slots[s].id);
  if (!rows || rows->empty()) return;
  slots[s].n--; if (slots[s].n <= 0) slots[s].clear();
  // BTA's WeightedRandomBag: one of the rows by weight, a yield between its min and max
  float tot = 0; for (auto& r : *rows) tot += r.w;
  float pick = uni() * tot; const Row* row = &rows->back();
  for (auto& r : *rows) { if (pick < r.w) { row = &r; break; } pick -= r.w; }
  ItemStack res(row->item, row->lo + (int)(uni() * (row->hi - row->lo + 1)));
  if (res.n > row->hi) res.n = row->hi;
  const ivec3 d = outDir(meta), at = pos + d;
  const vec3 centre = vec3(pos) + vec3(0.5f);
  // a chest (or barrel) there takes it: onto stacks of the same first, then into an empty slot
  BlockId nb = w.getBlock(at.x, at.y, at.z);
  if (reg().block(nb).has("container")) if (json* ce = w.blockEntity(at.x, at.y, at.z)) if (ce->contains("items") && (*ce)["items"].is_array()) {
    json& arr = (*ce)["items"];
    const int mx = maxStackOf(res.id);
    for (auto& v : arr) { if (res.n <= 0) break; ItemStack c = ItemStack::fromJson(v); if (!c.empty() && c.id == res.id && c.tag == res.tag && c.n < mx) { int k = std::min(mx - c.n, res.n); c.n += k; res.n -= k; v = c.toJson(); } }
    for (auto& v : arr) { if (res.n <= 0) break; if (ItemStack::fromJson(v).empty()) { v = res.toJson(); res.n = 0; } }
    w.blockEntityChanged(at.x, at.y, at.z);
  }
  if (res.n > 0) {
    if (!reg().block(nb).solid) rs::dropStack(lv, realm, res, centre + vec3(d) * 0.6f, vec3(d) * 4.f + vec3(0, 1.5f, 0), 0.5f);
    else rs::dropStack(lv, realm, res, centre + vec3(0, 0.6f, 0), vec3(0, 2.f, 0), 0.5f);
  }
  // BTA: one sift in 4000 shakes a small slime out
  if (std::uniform_int_distribution<int>(0, 3999)(rng()) == 0) {
    vec3 sp = !reg().block(nb).solid ? centre + vec3(d) : centre + vec3(0, 1, 0);
    if (Entity* e = spawnCreature(lv, "slime", realm, sp)) { if (auto* c = dynamic_cast<Creature*>(e)) c->setSize(1); e->vel = vec3(d) * 3.f + vec3(0, 3, 0); }
  }
}

bool tickTrommel(Level& lv, World& w, int realm, json& e, double dt, ivec3 pos, u8 meta) {
  if (!e.contains("items") || !e["items"].is_array()) e["items"] = json::array();
  json& items = e["items"];
  while (items.size() < 5) items.push_back(nullptr);
  std::vector<ItemStack> sl(5);
  for (int i = 0; i < 5; i++) sl[i] = ItemStack::fromJson(items[i]);
  int next = e.value("next", 0) % 4;
  auto can = [&](int s) { return !sl[s].empty() && trommelSifts(sl[s].id); };
  for (int k = 0; k < 4 && !can(next); k++) next = (next + 1) % 4;   // on to an input that has something
  bool changed = false;
  double burn = e.value("burn", 0.0), cook = e.value("cook", 0.0);
  ItemStack& fuel = sl[4];
  if (burn <= 0 && can(next) && !fuel.empty() && fuelValue(fuel.id) > 0) {
    burn = fuelValue(fuel.id) * SMELT_TIME * 2; e["burnMax"] = burn;   // BTA: coal burns 1600 ticks, a sift takes 50
    if (fuel.id == "lava_bucket") fuel = ItemStack("bucket", 1);
    else { fuel.n--; if (fuel.n <= 0) fuel.clear(); }
    changed = true;
  }
  if (burn > 0 && can(next)) {
    cook += dt;
    while (cook >= TROMMEL_POP && can(next)) {
      cook -= TROMMEL_POP;
      sift(lv, w, realm, sl, next, pos, meta);
      next = (next + 1) % 4;
      for (int k = 0; k < 4 && !can(next); k++) next = (next + 1) % 4;
      changed = true;
    }
  } else cook = 0;
  if (burn > 0) { burn = std::max(0.0, burn - dt); if (burn <= 0) changed = true; }
  if (changed) for (int i = 0; i < 5; i++) items[i] = sl[i].empty() ? json() : sl[i].toJson();
  e["burn"] = burn; e["cook"] = cook; e["next"] = next;
  return changed;
}

} // namespace dq
