// sim/quiver.cpp — the quiver (BTA's): a leather case of arrows (up to 192, kept on the stack's tag "arrows"). Use it to
// gather every loose arrow you carry into it; crouch and use it to tip them back out. A bow draws from the off hand
// first, then from a quiver anywhere in your bag, then from loose arrows (sim/controller.cpp).
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/player.h"
#include "sim/inventory.h"
#include "core/registry.h"

namespace dq {

static constexpr int QUIVER_CAP = 192;
static int held(const ItemStack& s) { return s.tag.is_object() ? s.tag.value("arrows", 0) : 0; }
static void setHeld(ItemStack& s, int n) { if (!s.tag.is_object()) s.tag = json::object(); if (n > 0) s.tag["arrows"] = n; else { s.tag.erase("arrows"); if (s.tag.empty()) s.tag = nullptr; } }

int quiverArrows(Player& p) {
  int n = 0;
  for (auto* v : {&p.inv, &p.offhand}) for (auto& s : *v) if (!s.empty() && s.id == "quiver") n += held(s);
  return n;
}
// the golden quiver (BTA's): never runs dry, but its arrows hit a little softer and break where they land
bool goldenQuiver(Player& p) {
  for (auto* v : {&p.inv, &p.offhand}) for (auto& s : *v) if (!s.empty() && s.id == "golden_quiver") return true;
  return false;
}
bool quiverTake(Player& p) {
  for (auto* v : {&p.offhand, &p.inv}) for (auto& s : *v) if (!s.empty() && s.id == "quiver" && held(s) > 0) { setHeld(s, held(s) - 1); return true; }
  return false;
}

void registerQuiver() {
  static bool done = false;
  if (done) return;
  done = true;
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& q, const ItemDef& def, bool repeat) {
    if (repeat || def.key != "quiver") return false;
    Player& p = pc.p;
    int have = held(q);
    pc.swingArm();
    if (p.sneaking) {   // tip them out
      if (!have) { pc.toastMsg("The quiver is empty"); return true; }
      int left = giveItem(pc.lv, p, ItemStack("arrow", have));
      setHeld(q, left);
      pc.toastMsg(left ? "No room for all the arrows" : "You take out " + std::to_string(have - left) + " arrows", left ? "warn" : "");
      pc.lv.fx->soundUi(p.id, "pop");
      return true;
    }
    int loose = playerCount(p, "arrow"), take = std::min(loose, QUIVER_CAP - have);
    if (take <= 0) { pc.toastMsg(have >= QUIVER_CAP ? "The quiver is full (" + std::to_string(have) + " arrows)" : "The quiver holds " + std::to_string(have) + " arrows"); return true; }
    playerRemove(p, "arrow", take);
    setHeld(q, have + take);
    pc.toastMsg("The quiver holds " + std::to_string(have + take) + " arrows", "good");
    pc.lv.fx->soundUi(p.id, "pop");
    return true;
  });
}

} // namespace dq
