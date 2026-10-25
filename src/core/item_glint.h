// core/item_glint.h — which stacks shimmer (game/enchanting.js: s.en, enchanted books, experience bottles).
#pragma once
#include "core/itemstack.h"

namespace dq {

inline bool itemGlints(const ItemStack& s) {
  if (s.empty()) return false;
  if (s.id == "enchanted_book" || s.id == "experience_bottle") return true;
  if (!s.tag.is_object()) return false;
  for (const char* k : {"en", "ench"}) { auto it = s.tag.find(k); if (it != s.tag.end() && !(it->is_object() && it->empty())) return true; }
  return false;
}

} // namespace dq
