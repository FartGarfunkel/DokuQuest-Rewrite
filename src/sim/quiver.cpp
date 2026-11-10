// sim/quiver.cpp — the quiver (BTA's, worn now): a leather case of arrows in its own slot beside the backpack's, four
// slots of arrows of any kind (its stack's tag "c", as a backpack keeps its contents; an older quiver's "arrows" count
// comes out as stacks of plain arrows). A bow draws from the off hand first, then from the worn quiver, then from a
// quiver carried in the bag, then loose arrows (sim/controller.cpp); arrows you pick up go into the worn quiver first
// (Player::give). In the hand: use it to gather your loose arrows into it; crouch and use it to tip them back out. The
// golden quiver never runs dry (its arrows hit a little softer and break where they land).
#include "sim/quiver.h"
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/inventory.h"
#include "core/registry.h"

namespace dq {

bool isArrowItem(const ItemStack& s) {
  if (s.empty()) return false;
  if (s.id == "arrow") return true;
  const ItemDef* d = reg().item(s.id); return d && d->has("arrow");
}
bool isQuiverItem(const ItemStack& s) { return !s.empty() && (s.id == "quiver" || s.id == "golden_quiver"); }
int quiverSlotsOf(const ItemStack& q) { return !q.empty() && q.id == "quiver" ? QUIVER_SLOTS : 0; }

std::vector<ItemStack> quiverLoad(const ItemStack& q) {
  std::vector<ItemStack> c(quiverSlotsOf(q));
  if (c.empty() || !q.tag.is_object()) return c;
  if (auto it = q.tag.find("c"); it != q.tag.end() && it->is_array())
    for (size_t i = 0; i < c.size() && i < it->size(); i++) c[i] = ItemStack::fromJson((*it)[i]);
  int old = q.tag.value("arrows", 0);   // (an older quiver: just a count of plain arrows)
  for (auto& s : c) if (old > 0 && s.empty()) { s = ItemStack("arrow", std::min(64, old)); old -= s.n; }
  return c;
}
void quiverSave(ItemStack& q, const std::vector<ItemStack>& c) {
  if (q.empty()) return;
  if (!q.tag.is_object()) q.tag = json::object();
  q.tag.erase("arrows");
  bool any = false; json a = json::array();
  for (auto& s : c) { a.push_back(s.toJson()); any |= !s.empty(); }
  if (any) q.tag["c"] = a; else q.tag.erase("c");
  if (q.tag.empty()) q.tag = nullptr;
}

static int countIn(const ItemStack& q) { int n = 0; for (auto& s : quiverLoad(q)) if (isArrowItem(s)) n += s.n; return n; }
int quiverFill(const ItemStack& q) {
  if (q.empty()) return 0;
  if (q.id == "golden_quiver") return 4;
  const int n = countIn(q);
  return n <= 0 ? 0 : 1 + std::min(3, (n - 1) * 4 / (QUIVER_SLOTS * 64) );
}
static ItemStack* worn(Player& p) { return !p.quiver.empty() && quiverSlotsOf(p.quiver[0]) ? &p.quiver[0] : nullptr; }

int quiverArrows(Player& p) {
  int n = 0;
  if (ItemStack* w = worn(p)) n += countIn(*w);
  for (auto* v : {&p.inv, &p.offhand}) for (auto& s : *v) if (quiverSlotsOf(s)) n += countIn(s);
  return n;
}
bool goldenQuiver(Player& p) {
  if (!p.quiver.empty() && !p.quiver[0].empty() && p.quiver[0].id == "golden_quiver") return true;
  for (auto* v : {&p.inv, &p.offhand}) for (auto& s : *v) if (!s.empty() && s.id == "golden_quiver") return true;
  return false;
}
static std::string takeFrom(ItemStack& q) {
  auto c = quiverLoad(q);
  for (auto& s : c) if (isArrowItem(s)) { std::string kind = s.id; if (--s.n <= 0) s.clear(); quiverSave(q, c); return kind; }
  return "";
}
std::string quiverTake(Player& p) {
  if (ItemStack* w = worn(p)) if (std::string k = takeFrom(*w); !k.empty()) return k;
  for (auto* v : {&p.offhand, &p.inv}) for (auto& s : *v) if (quiverSlotsOf(s)) if (std::string k = takeFrom(s); !k.empty()) return k;
  return "";
}
int quiverStow(Player& p, ItemStack& s) {
  ItemStack* w = worn(p);
  if (!w || !isArrowItem(s)) return s.n;
  auto c = quiverLoad(*w);
  const int left = addToSlots(c, s);
  quiverSave(*w, c);
  return left;
}

void registerQuiver() {
  static bool done = false;
  if (done) return;
  done = true;
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& q, const ItemDef& def, bool repeat) {
    if (repeat || def.key != "quiver") return false;
    Player& p = pc.p;
    auto c = quiverLoad(q);
    pc.swingArm();
    if (p.sneaking) {   // tip them out
      int out = 0, kept = 0;
      for (auto& s : c) if (!s.empty()) { const int n = s.n; const int left = giveItem(pc.lv, p, s); out += n - left; kept += left; if (left > 0) s.n = left; else s.clear(); }
      quiverSave(q, c);
      pc.toastMsg(!out && !kept ? "The quiver is empty" : kept ? "No room for all the arrows" : "You take out " + std::to_string(out) + " arrows", kept ? "warn" : "");
      if (out) pc.lv.fx->soundUi(p.id, "pop");
      return true;
    }
    int moved = 0;   // gather the loose arrows of every kind from the bag into it
    for (auto& s : p.inv) if (isArrowItem(s)) { const int n = s.n; const int left = addToSlots(c, s); moved += n - left; if (left <= 0) s.clear(); else s.n = left; }
    quiverSave(q, c);
    int have = 0; for (auto& s : c) if (!s.empty()) have += s.n;
    pc.toastMsg(moved ? "The quiver holds " + std::to_string(have) + " arrows" : have >= QUIVER_SLOTS * 64 ? "The quiver is full" : "The quiver holds " + std::to_string(have) + " arrows", moved ? "good" : "");
    if (moved) pc.lv.fx->soundUi(p.id, "pop");
    return true;
  });
}

} // namespace dq
