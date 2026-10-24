// core/itemstack.h — an item stack, like the original's plain { id, n, d?, c?, g?, ... } object.
#pragma once
#include "core/registry.h"

namespace dq {

struct ItemStack {
  std::string id;      // item key ("" = empty)
  int n = 0;           // count
  int d = 0;           // durability used
  json tag;            // everything else the original kept on a stack (c: contents, g, ench, name, ...); null if none

  ItemStack() = default;
  ItemStack(std::string key, int count, int dmg = 0) : id(std::move(key)), n(count), d(dmg) {}
  bool empty() const { return id.empty() || n <= 0; }
  explicit operator bool() const { return !empty(); }
  const ItemDef* def() const { return reg().item(id); }
  int maxStack() const { auto* d = def(); return d ? d->maxStack : 64; }
  bool canMerge(const ItemStack& o) const {
    return !empty() && !o.empty() && id == o.id && d == 0 && o.d == 0 && tag.is_null() && o.tag.is_null() && maxStack() > 1;
  }
  void clear() { id.clear(); n = 0; d = 0; tag = nullptr; }

  json toJson() const {
    if (empty()) return nullptr;
    json j = tag.is_object() ? tag : json::object();
    j["id"] = id; j["n"] = n; if (d) j["d"] = d;
    return j;
  }
  static ItemStack fromJson(const json& j) {
    ItemStack s;
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string()) return s;
    s.id = j["id"]; s.n = std::max(1, j.value("n", 1)); s.d = j.value("d", 0);
    for (auto& [k, v] : j.items()) if (k != "id" && k != "n" && k != "d") { if (s.tag.is_null()) s.tag = json::object(); s.tag[k] = v; }
    return s;
  }
};

} // namespace dq
