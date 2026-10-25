#include "sim/inventory.h"
#include "sim/player.h"

namespace dq {

bool canMergeStacks(const ItemStack& a, const ItemStack& b) {
  if (a.empty() || b.empty() || a.id != b.id || a.d || b.d) return false;
  if ((a.tag.is_object() && a.tag.contains("c")) || (b.tag.is_object() && b.tag.contains("c"))) return false;
  if (a.tag != b.tag) return false;
  return maxStackOf(a.id) > 1;
}
int maxStackOf(const std::string& id) { auto* d = reg().item(id); return d ? d->maxStack : 64; }
std::string itemName(const ItemStack& s) { auto* d = reg().item(s.id); return d ? d->name : s.id; }

int addToSlots(Slots& slots, ItemStack& stack, const std::vector<int>* order) {
  if (stack.empty()) return 0;
  std::vector<int> idx;
  if (order) idx = *order; else { idx.resize(slots.size()); for (size_t i = 0; i < slots.size(); i++) idx[i] = (int)i; }
  int max = maxStackOf(stack.id);
  bool plain = !(stack.tag.is_object() && stack.tag.contains("c")) && !stack.d;
  if (max > 1 && plain) {
    for (int i : idx) {
      ItemStack& s = slots[i];
      if (!s.empty() && canMergeStacks(s, stack) && s.n < max) {
        int k = std::min(max - s.n, stack.n); s.n += k; stack.n -= k;
        if (!stack.n) return 0;
      }
    }
  }
  for (int i : idx) {
    if (slots[i].empty()) {
      int k = std::min(max, stack.n);
      slots[i] = stack; slots[i].n = k;
      stack.n -= k;
      if (!stack.n) return 0;
    }
  }
  return stack.n;
}
int countIn(const Slots& slots, const std::string& id) { int n = 0; for (auto& s : slots) if (!s.empty() && s.id == id) n += s.n; return n; }
int removeFrom(Slots& slots, const std::string& id, int n) {
  for (int i = (int)slots.size() - 1; i >= 0 && n > 0; i--) {
    ItemStack& s = slots[i];
    if (!s.empty() && s.id == id && !(s.tag.is_object() && s.tag.contains("c"))) { int k = std::min(n, s.n); s.n -= k; n -= k; if (!s.n) s.clear(); }
  }
  return n;
}
ItemStack normalizeStack(const json& j) {
  ItemStack s = ItemStack::fromJson(j);
  if (s.empty()) return s;
  json keep = json::object();
  if (s.tag.is_object()) {
    for (auto& [k, v] : s.tag.items()) {
      if (k == "c" && v.is_array()) { json c = json::array(); for (auto& x : v) { ItemStack cs = normalizeStack(x); c.push_back(cs.toJson()); } keep["c"] = c; }
      else keep[k] = v;
    }
  }
  s.tag = keep.empty() ? json() : keep;
  return s;
}
json slotsToJson(const Slots& s) { json a = json::array(); for (auto& x : s) a.push_back(x.toJson()); return a; }
void slotsFromJson(const json& a, Slots& s) {
  for (auto& x : s) x.clear();
  if (!a.is_array()) return;
  for (size_t i = 0; i < s.size() && i < a.size(); i++) s[i] = a[i].is_null() ? ItemStack() : normalizeStack(a[i]);
}

// ---- the player's bags ----
Slots* backpackSlots(Player& p) {
  ItemStack& b = p.back[0];
  if (b.empty()) return nullptr;
  auto* d = reg().item(b.id);
  if (!d || !d->has("backpack")) return nullptr;
  int size = (int)d->num("backpack", 0);
  if (size <= 0) return nullptr;
  // the contents live on the stack (c), so a backpack carries them wherever it goes
  static thread_local Slots tmp;
  if (!b.tag.is_object()) b.tag = json::object();
  json& c = b.tag["c"];
  if (!c.is_array() || (int)c.size() != size) { json old = c.is_array() ? c : json::array(); c = json::array(); for (int i = 0; i < size; i++) c.push_back(i < (int)old.size() ? old[i] : json()); }
  tmp.assign(size, ItemStack());
  slotsFromJson(c, tmp);
  return &tmp;
}
static void storeBackpack(Player& p, const Slots& s) { if (!p.back[0].empty() && p.back[0].tag.is_object()) p.back[0].tag["c"] = slotsToJson(s); }

int Player::give(ItemStack s) {
  if (s.empty()) return 0;
  int left = addToSlots(inv, s);
  if (left > 0) if (Slots* bp = backpackSlots(*this)) { left = addToSlots(*bp, s); storeBackpack(*this, *bp); }
  return left;
}
int playerCount(Player& p, const std::string& id) {
  int n = countIn(p.inv, id);
  if (Slots* bp = backpackSlots(p)) n += countIn(*bp, id);
  return n;
}
int playerCountTag(Player& p, const std::string& key) {
  if (key.empty() || key[0] != '#') return playerCount(p, key);
  auto it = reg().tags.find(key); if (it == reg().tags.end()) return 0;
  int n = 0; for (auto& k : it->second) n += playerCount(p, k); return n;
}
int playerRemove(Player& p, const std::string& id, int n) {
  if (Slots* bp = backpackSlots(p)) { n = removeFrom(*bp, id, n); storeBackpack(p, *bp); }
  return removeFrom(p.inv, id, n);
}
int playerRemoveTag(Player& p, const std::string& key, int n) {
  if (key.empty() || key[0] != '#') return playerRemove(p, key, n);
  auto it = reg().tags.find(key); if (it == reg().tags.end()) return n;
  for (auto& k : it->second) { n = playerRemove(p, k, n); if (!n) break; }
  return n;
}
int armorPoints(const Player& p) {
  int pts = 0;
  for (auto& s : p.armor) if (!s.empty()) if (auto* d = reg().item(s.id)) if (d->props.contains("armor")) pts += d->props["armor"].value("points", 0);
  return pts;
}
float charmBonus(const Player& p, const std::string& kind) {
  const ItemStack& s = p.charm[0];
  if (s.empty()) return 0;
  auto* d = reg().item(s.id);
  if (!d || !d->props.contains("charm") || !d->props["charm"].is_object()) return 0;
  return d->props["charm"].value(kind, 0.0f);
}
float fireResist(const Player& p) {
  float r = charmBonus(p, "fire");
  for (auto& s : p.armor) if (!s.empty()) if (auto* d = reg().item(s.id)) if (d->props.contains("armor")) r += d->props["armor"].value("fireResist", 0.0f);
  return std::min(0.9f, r);
}
int itemLight(const std::string& id) {
  auto* d = reg().item(id);
  if (!d) return 0;
  if (d->props.contains("light") && d->props["light"].is_number()) return d->props["light"];
  return d->block.empty() ? 0 : reg().block(reg().blockId(d->block)).light;
}
int carriedLight(const Player& p) {
  int l = 0;
  for (const ItemStack* s : {&p.inv[p.selected], &p.offhand[0], &p.hook[0]}) if (!s->empty()) l = std::max(l, itemLight(s->id));
  return l;
}

} // namespace dq
