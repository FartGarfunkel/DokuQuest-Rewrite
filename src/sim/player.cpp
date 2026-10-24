#include "sim/player.h"

namespace dq {

int Player::give(ItemStack s) {
  if (s.empty()) return 0;
  int max = s.maxStack();
  for (int pass = 0; pass < 2 && s.n > 0; pass++) {
    for (int i = 0; i < INV_MAIN && s.n > 0; i++) {
      ItemStack& slot = inv[i];
      if (pass == 0 && slot.canMerge(s) && slot.n < max) { int k = std::min(max - slot.n, s.n); slot.n += k; s.n -= k; }
      else if (pass == 1 && slot.empty()) { int k = std::min(max, s.n); slot = s; slot.n = k; s.n -= k; }
    }
  }
  return s.n;
}

static json stacks(const std::vector<ItemStack>& v) { json a = json::array(); for (auto& s : v) a.push_back(s.toJson()); return a; }
static void unstacks(const json& a, std::vector<ItemStack>& v) { if (!a.is_array()) return; for (size_t i = 0; i < v.size() && i < a.size(); i++) v[i] = ItemStack::fromJson(a[i]); }

json Player::save() const {
  json j = Entity::save();
  j["name"] = name; j["skin"] = skin; j["mode"] = mode; j["selected"] = selected; j["flying"] = flying; j["air"] = air;
  j["xp"] = xp; j["level"] = level; j["inv"] = stacks(inv); j["armor"] = stacks(armor); j["offhand"] = stacks(offhand);
  j["spawnPoint"] = {spawnPoint.x, spawnPoint.y, spawnPoint.z}; j["spawnRealm"] = spawnRealm; j["stats"] = stats;
  return j;
}
void Player::load(const json& j) {
  Entity::load(j);
  type = "player";
  name = j.value("name", name); skin = j.value("skin", skin); mode = j.value("mode", mode); selected = j.value("selected", 0);
  flying = j.value("flying", false); air = j.value("air", 10.0f); xp = j.value("xp", 0); level = j.value("level", 0);
  if (j.contains("inv")) unstacks(j["inv"], inv);
  if (j.contains("armor")) unstacks(j["armor"], armor);
  if (j.contains("offhand")) unstacks(j["offhand"], offhand);
  if (j.contains("spawnPoint")) spawnPoint = {j["spawnPoint"][0], j["spawnPoint"][1], j["spawnPoint"][2]};
  spawnRealm = j.value("spawnRealm", 0);
  if (j.contains("stats")) stats = j["stats"];
}

} // namespace dq
