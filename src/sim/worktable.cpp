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

int worktableCell(const RayHit& hit) {
  if (!hit.hit || hit.normal.y != 1) return -1;
  const float fx = hit.point.x - std::floor(hit.point.x), fz = hit.point.z - std::floor(hit.point.z);
  const int cx = std::clamp((int)(fx * 3), 0, 2), cz = std::clamp((int)(fz * 3), 0, 2);
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

bool worktableUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held) {
  const int cell = worktableCell(hit);
  if (cell < 0) return false;
  const int x = hit.block.x, y = hit.block.y, z = hit.block.z;
  json* ep = w.blockEntity(x, y, z);
  const bool any = ep && ep->is_object() && ep->value("type", "") == "worktable";
  std::vector<ItemStack> g = any ? worktableGrid(*ep) : std::vector<ItemStack>(9);
  bool empty = true; for (auto& s : g) if (!s.empty()) empty = false;
  auto save = [&] {
    json items = json::array(); bool none = true;
    for (auto& s : g) { items.push_back(s.empty() ? json() : s.toJson()); if (!s.empty()) none = false; }
    if (none) w.setBlockEntity(x, y, z, nullptr);
    else w.setBlockEntity(x, y, z, {{"type", "worktable"}, {"items", items}});
  };
  const vec3 at(x + 0.5f, y + 1.f, z + 0.5f);
  if (!held.empty()) {   // one piece onto the square
    if (!g[cell].empty()) { toast(lv, p, "Something's on that square already"); return true; }
    g[cell] = held; g[cell].n = 1;
    if (--held.n <= 0) held = ItemStack();
    save(); sfxMaterial(lv, "wood", "place", at);
    return true;
  }
  if (!g[cell].empty()) {   // an empty hand takes the piece back
    ItemStack s = g[cell]; g[cell] = ItemStack();
    save(); giveOrDropHere(lv, p, s); sfxMaterial(lv, "wood", "hit", at);
    return true;
  }
  if (empty) return false;   // nothing on it: the crafting screen
  recipeGate().immersive = w.immersive;
  const std::array<int, 9> ord = worktableOrder(p.yaw);
  std::vector<ItemStack> rg(9); for (int i = 0; i < 9; i++) rg[i] = g[ord[i]];   // read from where you stand
  CraftMatch r = matchGrid(rg, 3);
  if (!r) { toast(lv, p, "Those make nothing yet"); return true; }
  ItemStack out = r.result();
  // as the crafting screen (sim/inv_ops.cpp takeOutput): an upgraded backpack keeps what was in it; a mod may cancel it
  const ItemDef* od = out.def();
  for (auto& s : rg) if (!s.empty() && s.tag.is_object() && s.tag.contains("c") && s.def() && s.def()->has("backpack") && od && od->has("backpack")) { if (!out.tag.is_object()) out.tag = json::object(); out.tag["c"] = s.tag["c"]; break; }
  if (lv.events && !lv.events->emit("craft", json::array({p.name, out.id, out.n}))) return true;
  consumeCraft(rg, r, [&](const ItemStack& b) { giveOrDropHere(lv, p, b); });
  for (int i = 0; i < 9; i++) g[ord[i]] = rg[i];
  save();
  giveOrDropHere(lv, p, out);
  sfxUi(lv, p, "craft");
  return true;
}

}  // namespace dq
