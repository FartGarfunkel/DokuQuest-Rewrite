// sim/piles.cpp — see piles.h
#include "sim/piles.h"
#include "core/registry.h"
#include "sim/inventory.h"

namespace dq {

static bool endsWith(const std::string& s, const char* e) { const size_t n = std::strlen(e); return s.size() >= n && s.compare(s.size() - n, n, e) == 0; }

PileKind pileKindOfItem(const std::string& id) {
  if (id.empty()) return PileKind::None;
  if (endsWith(id, "_ingot")) return PileKind::Ingots;
  if (reg().itemMatches(id, "#log")) return PileKind::Logs;
  return PileKind::None;
}
PileKind pileKindOfBlock(BlockId id) {
  if (!id) return PileKind::None;
  const BlockDef& b = reg().block(id);
  return b.has("ingotPile") ? PileKind::Ingots : b.has("logPile") ? PileKind::Logs : PileKind::None;
}
int pileMax(PileKind k) { return k == PileKind::Ingots ? INGOT_PILE_MAX : k == PileKind::Logs ? LOG_PILE_MAX : 0; }
u8 pileMeta(PileKind k, int n) {
  if (n <= 0) return 0;
  return (u8)(k == PileKind::Ingots ? std::min(8, (n + 7) / 8) : std::min(4, (n + 3) / 4));
}

namespace {
BlockId blockOf(PileKind k) { return reg().blockId(k == PileKind::Ingots ? "ingot_pile" : "log_pile"); }
ItemStack pileStack(World& w, int x, int y, int z) {
  json* e = w.blockEntity(x, y, z);
  return e && e->is_object() && e->contains("item") ? ItemStack::fromJson((*e)["item"]) : ItemStack();
}
void setPile(World& w, int x, int y, int z, PileKind k, const ItemStack& s) {
  if (s.empty()) { w.setBlockEntity(x, y, z, nullptr); w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent); return; }
  if (w.getBlock(x, y, z) != blockOf(k)) w.setBlock(x, y, z, blockOf(k), pileMeta(k, s.n), SetFlags::Urgent);
  else w.setMeta(x, y, z, pileMeta(k, s.n), SetFlags::Urgent);
  w.setBlockEntity(x, y, z, json{{"type", k == PileKind::Ingots ? "ingots" : "logs"}, {"item", s.toJson()}});
}
bool sameThing(const ItemStack& a, const ItemStack& b) { return a.id == b.id && a.tag == b.tag; }
const char* mismatch(PileKind k) { return k == PileKind::Ingots ? "One metal to a pile" : "One wood to a pile"; }
// one from held onto the pile at (x,y,z) holding `in`
void moveOne(World& w, int x, int y, int z, PileKind k, ItemStack in, ItemStack& held) {
  if (in.empty()) { in = held; in.n = 0; }
  in.n++;
  if (--held.n <= 0) held = ItemStack();
  setPile(w, x, y, z, k, in);
}
}

bool pilePlace(World& w, int x, int y, int z, ItemStack& held, std::string* why) {
  const PileKind k = pileKindOfItem(held.id);
  if (k == PileKind::None || held.empty()) return false;
  const Registry& R = reg();
  const BlockId below = w.getBlock(x, y - 1, z);
  if (pileKindOfBlock(below) == k) {   // standing on a pile of its kind: it's that column's to add to
    return pileAdd(w, x, y - 1, z, held, why);
  }
  const BlockId cur = w.getBlock(x, y, z);
  if (cur != AIR && !(R.tReplaceable[cur] && !R.tLiquid[cur])) { if (why) *why = "No room there"; return false; }
  if (!R.tSolid[below] || pileKindOfBlock(below) != PileKind::None) { if (why) *why = "It needs firm ground"; return false; }
  moveOne(w, x, y, z, k, ItemStack(), held);
  return true;
}

bool pileAdd(World& w, int x, int y, int z, ItemStack& held, std::string* why) {
  const PileKind k = pileKindOfBlock(w.getBlock(x, y, z));
  if (k == PileKind::None || held.empty() || pileKindOfItem(held.id) != k) return false;
  int y0 = y;   // the bottom of this column of piles
  while (pileKindOfBlock(w.getBlock(x, y0 - 1, z)) == k) y0--;
  for (int yy = y0;; yy++) {
    if (pileKindOfBlock(w.getBlock(x, yy, z)) != k) {   // all full: a new pile on top
      const BlockId cur = w.getBlock(x, yy, z);
      if (cur != AIR && !(reg().tReplaceable[cur] && !reg().tLiquid[cur])) { if (why) *why = "No room on top"; return false; }
      moveOne(w, x, yy, z, k, ItemStack(), held);
      return true;
    }
    const ItemStack in = pileStack(w, x, yy, z);
    if (!in.empty() && !sameThing(in, held)) { if (why) *why = mismatch(k); return false; }
    if (in.n < pileMax(k)) { moveOne(w, x, yy, z, k, in, held); return true; }
  }
}

ItemStack pileTake(World& w, int x, int y, int z) {
  const PileKind k = pileKindOfBlock(w.getBlock(x, y, z));
  if (k == PileKind::None) return ItemStack();
  ItemStack in = pileStack(w, x, y, z);
  if (in.empty()) { setPile(w, x, y, z, k, ItemStack()); return ItemStack(); }
  ItemStack out = in; out.n = 1;
  in.n--;
  setPile(w, x, y, z, k, in);
  return out;
}

PileSlot ingotSlot(int i) {
  const int layer = i / 8, j = i % 8, col = j % 4, row = j / 4;
  const float a = 0.125f + col * 0.25f, b = 0.25f + row * 0.5f, y = layer * 0.125f;
  return layer % 2 ? PileSlot{vec3(b, y, a), PI / 2} : PileSlot{vec3(a, y, b), 0.f};
}
PileSlot logSlot(int i) {
  const int course = i / 4, j = i % 4;
  const float a = 0.125f + j * 0.25f, y = 0.125f + course * 0.25f;
  return course % 2 ? PileSlot{vec3(0.5f, y, a), PI / 2} : PileSlot{vec3(a, y, 0.5f), 0.f};
}

} // namespace dq
