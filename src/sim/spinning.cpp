// sim/spinning.cpp — see spinning.h
#include "sim/spinning.h"
#include "sim/player.h"
#include "sim/gameplay.h"
#include "core/registry.h"

namespace dq {

bool spindleUse(Level& lv, Player& p, bool repeat, float step) {
  if (p.held().id != "spindle") return false;
  ItemStack& wool = p.offhand[0];
  if (wool.empty() || !reg().itemMatches(wool.id, "#wool")) { if (!repeat) toast(lv, p, "Hold wool in your other hand and keep using the spindle"); return true; }
  json& d = p.data; if (!d.is_object()) d = json::object();
  if (!repeat) { d["spinT"] = 0.0; return true; }
  const float t = d.value("spinT", 0.f) + step;
  d["spinT"] = t;
  if (t < SPIN_HOLD) return true;
  d["spinT"] = 0.0;
  if (--wool.n <= 0) wool = ItemStack();
  if (int left = giveItem(lv, p, ItemStack("yarn", YARN_PER_WOOL)); left > 0) dropItem(lv, p.realm, ItemStack("yarn", left), p.pos + vec3(0, 1, 0), true);
  return true;
}

} // namespace dq
