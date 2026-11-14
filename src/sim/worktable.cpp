// sim/worktable.cpp — see worktable.h
#include "sim/worktable.h"
#include "sim/crafting.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/events.h"
#include "core/registry.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace dq {

std::vector<ItemStack> worktableGrid(const json& be) {
  std::vector<ItemStack> g(9);
  if (be.is_object() && be.contains("items") && be["items"].is_array())
    for (int i = 0; i < 9 && i < (int)be["items"].size(); i++) if (!be["items"][i].is_null()) g[i] = ItemStack::fromJson(be["items"][i]);
  return g;
}

namespace { vec4 g_grid(0, 0, 1, 1); }   // the grid drawn on the table's top (x0, z0, x1, z1 of the block): thirds by default
void setWorktableGrid(vec4 rect) { if (rect.z - rect.x > 0.2f && rect.w - rect.y > 0.2f) g_grid = rect; }
vec4 worktableGridRect() { return g_grid; }
vec3 worktableCellCenter(ivec3 at, int cell) {
  const float w = (g_grid.z - g_grid.x) / 3, d = (g_grid.w - g_grid.y) / 3;
  return vec3(at) + vec3(g_grid.x + (cell % 3 + 0.5f) * w, 1.f, g_grid.y + (cell / 3 + 0.5f) * d);
}
int worktableCell(const RayHit& hit) {
  if (!hit.hit || hit.normal.y != 1) return -1;
  const float fx = hit.point.x - std::floor(hit.point.x), fz = hit.point.z - std::floor(hit.point.z);
  // the drawn squares (a click off the grid's edge takes the nearest square)
  const int cx = std::clamp((int)std::floor((fx - g_grid.x) / (g_grid.z - g_grid.x) * 3), 0, 2), cz = std::clamp((int)std::floor((fz - g_grid.y) / (g_grid.w - g_grid.y) * 3), 0, 2);
  return cz * 3 + cx;
}

std::array<int, 9> worktableOrder(float yaw) {
  // the recipe's row 0 is the side away from you, its column 0 your left (looking down the table from where you stand)
  vec3 d = lookVector(yaw, 0);
  ivec2 f = std::fabs(d.x) > std::fabs(d.z) ? ivec2(d.x > 0 ? 1 : -1, 0) : ivec2(0, d.z > 0 ? 1 : -1);
  const ivec2 left(f.y, -f.x);
  std::array<int, 9> o{};
  for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
    const int cx = f.x * (1 - r) + left.x * (1 - c), cz = f.y * (1 - r) + left.y * (1 - c);
    o[r * 3 + c] = (cz + 1) * 3 + (cx + 1);
  }
  return o;
}

static void giveOrDropHere(Level& lv, Player& p, ItemStack s) {   // in the bag, or at your feet if it's full
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true); }
}

namespace {
std::vector<ItemStack> gridAt(World& w, ivec3 at) {
  json* ep = w.blockEntity(at.x, at.y, at.z);
  const bool any = ep && ep->is_object() && ep->value("type", "") == "worktable";
  return any ? worktableGrid(*ep) : std::vector<ItemStack>(9);
}
void saveGrid(World& w, ivec3 at, const std::vector<ItemStack>& g) {
  json items = json::array(); bool none = true;
  for (auto& s : g) { items.push_back(s.empty() ? json() : s.toJson()); if (!s.empty()) none = false; }
  if (none) w.setBlockEntity(at.x, at.y, at.z, nullptr);
  else w.setBlockEntity(at.x, at.y, at.z, {{"type", "worktable"}, {"items", items}});
}
}  // namespace

bool worktablePut(Level& lv, Player& p, World& w, ivec3 at, int cell, ItemStack& held) {
  if (cell < 0 || cell > 8 || held.empty()) return false;
  std::vector<ItemStack> g = gridAt(w, at);
  if (!g[cell].empty()) { toast(lv, p, "Something's on that square already"); return false; }
  g[cell] = held; g[cell].n = 1;
  if (--held.n <= 0) held = ItemStack();
  saveGrid(w, at, g); sfxMaterial(lv, "wood", "place", vec3(at) + vec3(0.5f, 1.f, 0.5f));
  return true;
}

std::string worktableLayout(Level& lv, Player& p, World& w, ivec3 at, int recipe) {
  if (recipe < 0 || recipe >= (int)reg().recipes.size()) return "No such recipe";
  const Recipe& r = reg().recipes[recipe];
  const std::vector<std::string> lay = recipeLayout(r, 3);
  auto matches = [](const std::string& want, const ItemStack& s) {
    if (s.empty() || want.empty()) return false;
    if (want == s.id) return true;
    if (want[0] == '#') { auto it = reg().tags.find(want); return it != reg().tags.end() && std::find(it->second.begin(), it->second.end(), s.id) != it->second.end(); }
    return false;
  };
  // enough of everything in the bag? (counted with what's on the table now, which comes back first)
  std::vector<ItemStack> bag = p.inv;
  for (auto& s : gridAt(w, at)) if (!s.empty()) { bool put = false; for (auto& b : bag) if (b.empty() || (b.canMerge(s) && b.n < b.maxStack())) { if (b.empty()) b = s; else b.n += s.n; put = true; break; } (void)put; }
  for (const std::string& want : lay) {
    if (want.empty()) continue;
    bool got = false;
    for (auto& b : bag) if (matches(want, b)) { if (--b.n <= 0) b = ItemStack(); got = true; break; }
    if (!got) { const std::string why = "You haven't got all it takes: " + ingredientLabel(want); toast(lv, p, why); return why; }
  }
  for (int c = 0; c < 9; c++) worktableTake(lv, p, w, at, c);
  const std::array<int, 9> order = worktableOrder(p.yaw);
  for (int k = 0; k < 9; k++) {
    if (lay[k].empty()) continue;
    for (auto& b : p.inv) if (matches(lay[k], b)) { worktablePut(lv, p, w, at, order[k], b); break; }
  }
  return "";
}

bool worktableTake(Level& lv, Player& p, World& w, ivec3 at, int cell) {
  if (cell < 0 || cell > 8) return false;
  std::vector<ItemStack> g = gridAt(w, at);
  if (g[cell].empty()) return false;
  ItemStack s = g[cell]; g[cell] = ItemStack();
  saveGrid(w, at, g); giveOrDropHere(lv, p, s); sfxMaterial(lv, "wood", "hit", vec3(at) + vec3(0.5f, 1.f, 0.5f));
  return true;
}

// the grid read from the side you stand at first, then from the others round the table: what you set out makes the
// same thing from wherever you look at it (it showed only from the one side)
static CraftMatch matchAround(const std::vector<ItemStack>& g, float yaw, std::array<int, 9>& ordOut) {
  for (int k = 0; k < 4; k++) {
    const std::array<int, 9> ord = worktableOrder(yaw + k * PI / 2);
    std::vector<ItemStack> rg(9); for (int i = 0; i < 9; i++) rg[i] = g[ord[i]];
    if (CraftMatch r = matchGrid(rg, 3)) { ordOut = ord; return r; }
  }
  ordOut = worktableOrder(yaw);
  return CraftMatch{};
}
CraftMatch worktableMatch(World& w, ivec3 at, float yaw, bool immersiveWorld) {
  std::vector<ItemStack> g = gridAt(w, at);
  recipeGate().immersive = immersiveWorld;
  std::array<int, 9> ord;
  return matchAround(g, yaw, ord);
}

bool worktableCraft(Level& lv, Player& p, World& w, ivec3 at) {
  std::vector<ItemStack> g = gridAt(w, at);
  bool empty = true; for (auto& s : g) if (!s.empty()) empty = false;
  if (empty) return false;
  recipeGate().immersive = w.immersive;
  std::array<int, 9> ord;
  CraftMatch r = matchAround(g, p.yaw, ord);
  if (!r) { toast(lv, p, "Those make nothing yet"); return false; }
  std::vector<ItemStack> rg(9); for (int i = 0; i < 9; i++) rg[i] = g[ord[i]];
  ItemStack out = r.result();
  // as the crafting screen (sim/inv_ops.cpp takeOutput): an upgraded backpack keeps what was in it; a mod may cancel it
  const ItemDef* od = out.def();
  for (auto& s : rg) if (!s.empty() && s.tag.is_object() && s.tag.contains("c") && s.def() && s.def()->has("backpack") && od && od->has("backpack")) { if (!out.tag.is_object()) out.tag = json::object(); out.tag["c"] = s.tag["c"]; break; }
  if (lv.events && !lv.events->emit("craft", json::array({p.name, out.id, out.n}))) return false;
  consumeCraft(rg, r, [&](const ItemStack& b) { giveOrDropHere(lv, p, b); });
  for (int i = 0; i < 9; i++) g[ord[i]] = rg[i];
  saveGrid(w, at, g);
  giveOrDropHere(lv, p, out);
  sfxUi(lv, p, "craft");
  return true;
}

bool worktableUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held) {
  const int cell = worktableCell(hit);
  if (cell < 0) return false;
  const ivec3 at = hit.block;
  std::vector<ItemStack> g = gridAt(w, at);
  bool empty = true; for (auto& s : g) if (!s.empty()) empty = false;
  if (!held.empty()) { if (!g[cell].empty()) toast(lv, p, "Something's on that square already"); else worktablePut(lv, p, w, at, cell, held); return true; }   // one piece onto the square
  if (!g[cell].empty()) { worktableTake(lv, p, w, at, cell); return true; }   // an empty hand takes the piece back
  if (empty) return false;   // nothing on it: the crafting screen
  worktableCraft(lv, p, w, at);
  return true;
}

}  // namespace dq
