// sim/farming.cpp — the watering can, rice's water, wild seeds (game/farming.js, game/obtain.js).
#include "sim/farming.h"
#include "sim/physics.h"
#include <regex>

namespace dq {

std::string wildSeed() {
  static const char* pool[] = {"tomato_seeds", "cabbage_seeds", "corn_seeds", "pepper_seeds", "onion", "strawberry", "rice"};
  std::string k = pool[std::min(6, (int)(frand() * 7))];
  return reg().item(k) ? k : "";
}

std::vector<ItemStack> obtainExtraDrops(const std::string& k, const std::string& tool) {
  std::vector<ItemStack> out;
  bool shears = tool == "shears";
  if (!shears && (k == "tall_grass" || k == "fern" || k == "tall_grass_double" || k == "large_fern")) {
    if (frand() < 0.025f) out.emplace_back("carrot", 1);
    else if (frand() < 0.025f) out.emplace_back("potato", 1);
  }
  static const std::regex roots("^(crimson_roots|warped_roots|nether_sprouts|soul_sand|soul_soil)$");
  if (std::regex_match(k, roots) && frand() < 0.08f && reg().item("nether_wart_item")) out.emplace_back("nether_wart_item", 1);
  return out;
}

bool riceSpotWet(World& w, int x, int y, int z) {
  static const int D[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
  for (auto& d : D) if (reg().tLiquid[w.getBlock(x + d[0], y - 1, z + d[1])] == 1) return true;
  return false;
}
int canWater(const ItemStack& s) { return s.tag.is_object() ? s.tag.value("water", 0) : 0; }

bool farmingUse(UseCtx& u) {
  Level& lv = u.lv; Player& p = u.p;
  ItemStack& held = p.held();
  const ItemDef* d = held.def();
  if (!d || held.id != u.item) return false;
  World& w = u.world();
  // rice wants water beside its farmland
  if (d->str("plants") == "rice_crop" && u.hasHit()) {
    ivec3 t = reg().block(w.getBlock(u.hit.x, u.hit.y, u.hit.z)).replaceable ? u.hit : u.placeAt();
    if (!riceSpotWet(w, t.x, t.y, t.z) && w.getBlock(t.x, t.y - 1, t.z) == reg().blockId("farmland")) {
      toast(lv, p, "Rice needs water right beside its farmland");
      return true;
    }
    return false;
  }
  if (!d->has("wateringCan")) return false;
  // the watering can: fill it at water, pour it on the crops around where you look
  RayHit hit = raycastBlocks(w, glm::dvec3(p.pos) + glm::dvec3(0, EYE, 0), lookVec(p), REACH, true);
  if (!hit.hit) return true;
  if (reg().tLiquid[hit.id] == 1) {
    if (!held.tag.is_object()) held.tag = json::object();
    held.tag["water"] = WATERING_CAN_MAX;
    sfxUi(lv, p, "bucket"); toast(lv, p, "The can is full");
    return true;
  }
  int water = canWater(held);
  if (water <= 0) { toast(lv, p, "The can is empty: fill it at water"); return true; }
  // one pour every half second
  static std::unordered_map<u32, double> last;
  double now = nowSeconds();
  auto it = last.find(p.id);
  if (it != last.end() && now - it->second < 0.5) return true;
  last[p.id] = now;
  held.tag["water"] = water - 1;
  int grew = 0;
  for (int ox = -1; ox <= 1; ox++) for (int oz = -1; oz <= 1; oz++) for (int oy = -1; oy <= 1; oy++) {
    int x = hit.block.x + ox, y = hit.block.y + oy, z = hit.block.z + oz;
    BlockId id = w.getBlock(x, y, z);
    const BlockDef& b = reg().block(id);
    if (!b.props.contains("crop") || !b.props["crop"].is_object()) continue;
    int stages = b.props["crop"].value("stages", 1), m = w.getMeta(x, y, z) & 15;
    if (m < stages - 1 && frand() < 0.4f) { w.setBlock(x, y, z, id, (u8)((w.getMeta(x, y, z) & ~15) | (m + 1))); grew++; }
  }
  vec3 c = vec3(hit.block) + vec3(0.5f, 1.4f, 0.5f);
  for (int i = 0; i < 16; i++) lv.fx->particles("water", c + vec3(frand2() * 2.4f, 0, frand2() * 2.4f), 1, {{"spread", 0.6}});
  sfxAt(lv, "splash_small", vec3(hit.block) + vec3(0.5f, 1, 0.5f), 12);
  if (grew && lv.events) lv.events->emit("skillGain", json::array({p.name, "farming", 3 * grew}));
  return true;
}

} // namespace dq
