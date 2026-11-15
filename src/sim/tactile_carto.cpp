// sim/tactile_carto.cpp — the cartography desk worked as Tactile Blocks' cartographe (sim/tactile.h)
#include "sim/tactile.h"
#include "sim/maps.h"
#include "sim/content.h"
#include "sim/player.h"
#include "core/registry.h"
#include "world/world.h"

namespace dq {

int tactileCartoFacing(int f) { static const int CW[4] = {3, 0, 1, 2}; return CW[f & 3]; }   // (S->W, E->S, N->E, W->N)

bool tactileCartographyUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held) {
  const ivec3 at = hit.block;
  if (reg().block(w.getBlock(at.x, at.y, at.z)).str("station") != "cartography") return false;
  json* e = w.blockEntity(at.x, at.y, at.z, true);
  if (!e) return false;
  if (!e->is_object() || e->value("type", "") != "mapwork") *e = json{{"type", "mapwork"}, {"items", json::array({nullptr, nullptr, nullptr})}};
  if (!e->contains("items") || !(*e)["items"].is_array()) (*e)["items"] = json::array();
  while ((*e)["items"].size() < 3) (*e)["items"].push_back(nullptr);
  json& items = (*e)["items"];
  const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  auto changed = [&] { w.blockEntityChanged(at.x, at.y, at.z); };
  auto slot = [&](int k) { return ItemStack::fromJson(items[k]); };
  // the zone (CartographeBlockEntity.zoneAt): in the desk's own north frame, sixteenths
  const vec3 n = tactileNorthLocal(tactileCartoFacing(tactileFacing(w, at)), hit.point - vec3(at));
  const float lx = n.x * 16, lz = n.z * 16;
  enum { IN, BLANK, QUILL, INK, COMPASS, PILE } zone = lx < 7.5f ? (lz < 6.5f ? IN : lz < 9.5f ? QUILL : BLANK) : lz < 7.5f ? (lx < 12.f ? INK : COMPASS) : PILE;
  int ink = e->value("ink", 0), feather = e->value("feather", 0);
  if (!held.empty()) {
    if (held.id == "ink_sac") {
      if (ink >= 3) return true;
      (*e)["ink"] = 3; if (--held.n <= 0) held = ItemStack(); changed(); sfxAt(lv, "brew", c); return true;
    }
    if (held.id == "feather") {
      if (feather > 0) return true;
      (*e)["feather"] = 3; if (--held.n <= 0) held = ItemStack(); changed(); sfxAt(lv, "pop", c); return true;
    }
    auto lay = [&](int k, int max) {
      ItemStack s = slot(k);
      if (!s.empty() && !(s.id == held.id && s.canMerge(held) && s.n < max)) return true;
      if (s.empty()) { s = held; s.n = 1; } else s.n++;
      items[k] = s.toJson(); if (--held.n <= 0) held = ItemStack();
      changed(); sfxAt(lv, "map_paper", c); return true;
    };
    if (held.id == "filled_map") return slot(0).empty() ? lay(0, 1) : true;
    if (held.id == "empty_map" || held.id == "paper") {
      if (zone == PILE) return lay(2, 16);
      if (held.id == "paper") { toast(lv, p, "Paper goes on the pile on the right (to enlarge a map)"); return true; }
      if (zone == IN) return lay(0, 1);
      if (zone == BLANK) return lay(1, 1);
      return true;
    }
    return false;
  }
  auto takeBack = [&](int k) {
    ItemStack s = slot(k); if (s.empty()) return false;
    items[k] = nullptr;
    tactileGive(lv, p, s);
    changed(); sfxAt(lv, "map_paper", c); return true;
  };
  if (zone == QUILL) {   // (tryCraft: a blank on the board drawn into a map of here; a map copied onto the blank)
    const ItemStack in = slot(0), blank = slot(1);
    const bool draw = in.id == "empty_map" && blank.empty(), copy = in.id == "filled_map" && blank.id == "empty_map";
    if (!draw && !copy) { toast(lv, p, "Lay a blank map on the board to draw, or a map and a blank behind it to copy"); return true; }
    if (ink <= 0 || feather <= 0) { toast(lv, p, ink <= 0 ? "The ink pot's dry (an ink sac fills it)" : "No quill (a feather)"); return true; }
    if (draw) items[0] = mapStartHere(lv, p).toJson();
    else { ItemStack cp = in; cp.n = 1; items[1] = cp.toJson(); }
    (*e)["ink"] = ink - 1; (*e)["feather"] = feather - 1;
    changed(); sfxAt(lv, "map_paper", c);
    return true;
  }
  if (zone == COMPASS) {   // (tryEnlarge: the map zoomed out with eight from the pile)
    ItemStack in = slot(0), pile = slot(2);
    if (in.id != "filled_map" || pile.n < 8) { toast(lv, p, "A map on the board and eight blanks or paper on the pile to enlarge it"); return true; }
    PaperMap* src = stackMapId(in).empty() ? nullptr : mapStore(lv).get(stackMapId(in));
    if (!src || src->scale >= MAP_MAX_SCALE) { toast(lv, p, "That map can't be enlarged further"); return true; }
    in.tag = {{"map", mapStore(lv).zoomed(*src)->id}}; items[0] = in.toJson();
    pile.n -= 8; items[2] = pile.n <= 0 ? json() : pile.toJson();
    changed(); sfxAt(lv, "map_paper", c);
    return true;
  }
  if (zone == IN) return takeBack(0);
  if (zone == BLANK) return takeBack(1);
  if (zone == PILE) return takeBack(2);
  return true;
}

} // namespace dq
