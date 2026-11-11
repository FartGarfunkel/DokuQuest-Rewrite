// sim/quern.cpp — see quern.h
#include "sim/quern.h"
#include "sim/player.h"
#include "sim/gameplay.h"
#include "common/fs.h"
#include "core/registry.h"
#include <mutex>
#include <unordered_map>

namespace dq {

std::pair<std::string, int> quernResult(const std::string& id) {
  static std::unordered_map<std::string, std::pair<std::string, int>> t; static std::once_flag once;
  std::call_once(once, [] {
    auto j = readJson(dataDir() / "quern.json");
    if (j && (*j)["grind"].is_object()) for (auto& [k, v] : (*j)["grind"].items()) if (v.is_array() && v.size() >= 2) t[k] = {v[0].get<std::string>(), v[1].get<int>()};
  });
  auto it = t.find(id);
  return it == t.end() || !reg().item(it->second.first) ? std::pair<std::string, int>{"", 0} : it->second;
}

bool quernUse(Level& lv, Player& p, const RayHit& hit, bool repeat, float step) {
  if (reg().block(hit.id).key != "quern") return false;
  ItemStack& held = p.held();
  const auto [out, n] = quernResult(held.id);
  if (held.empty() || out.empty()) { if (!repeat) toast(lv, p, "Hold grain, ore, limestone, bone or flowers and keep using the quern"); return true; }
  json& d = p.data; if (!d.is_object()) d = json::object();
  const json key = {hit.block.x, hit.block.y, hit.block.z};
  if (!repeat || d.value("quernAt", json()) != key) { d["quernAt"] = key; d["quernT"] = 0.0; return true; }
  const float t = d.value("quernT", 0.f) + step;
  d["quernT"] = t;
  if (t < QUERN_HOLD) return true;
  d["quernT"] = 0.0;
  if (--held.n <= 0) held = ItemStack();
  dropItem(lv, p.realm, ItemStack(out, n), vec3(hit.block) + vec3(0.5f, 1.1f, 0.5f), false);
  lv.fx->material("stone", "dig", vec3(hit.block) + vec3(0.5f, 1, 0.5f));
  return true;
}

} // namespace dq
