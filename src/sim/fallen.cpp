// sim/fallen.cpp — see fallen.h
#include "sim/fallen.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/creature.h"
#include "core/registry.h"
#include <cmath>

namespace dq {

namespace {
struct Loot { const char* id; int lo, hi; float chance; };
// what each kind of adventurer carried (the ids not in this build are skipped)
const std::vector<Loot>& lootFor(const std::string& kind) {
  static const std::vector<Loot> traveller = {{"bread", 1, 3, 0.7f}, {"apple", 1, 3, 0.5f}, {"torch", 2, 6, 0.6f}, {"string", 1, 4, 0.4f}, {"arrow", 3, 9, 0.4f},
                                              {"gold_coin", 2, 7, 0.6f}, {"compass", 1, 1, 0.12f}, {"empty_map", 1, 1, 0.15f}, {"iron_ingot", 1, 3, 0.3f}, {"leather", 1, 3, 0.3f}};
  static const std::vector<Loot> desert = {{"gold_ingot", 1, 3, 0.5f}, {"gold_coin", 3, 10, 0.7f}, {"bone", 2, 5, 0.6f}, {"string", 1, 3, 0.4f},
                                           {"emerald", 1, 2, 0.25f}, {"melon_slice", 1, 3, 0.3f}, {"potion_heal", 1, 1, 0.12f}, {"gunpowder", 1, 3, 0.3f}};
  static const std::vector<Loot> snowy = {{"cooked_beef", 1, 3, 0.6f}, {"leather", 1, 4, 0.5f}, {"torch", 2, 6, 0.6f}, {"snowball", 2, 8, 0.4f},
                                          {"iron_ingot", 1, 3, 0.4f}, {"packed_ice", 1, 4, 0.3f}, {"compass", 1, 1, 0.15f}, {"gold_coin", 2, 6, 0.5f}};
  static const std::vector<Loot> jungle = {{"cocoa_beans", 1, 4, 0.6f}, {"glow_berries", 1, 4, 0.4f}, {"emerald", 1, 3, 0.35f}, {"gold_ingot", 1, 2, 0.3f},
                                           {"arrow", 3, 10, 0.5f}, {"bow", 1, 1, 0.15f}, {"diamond", 1, 1, 0.06f}, {"gold_coin", 2, 8, 0.5f}};
  static const std::vector<Loot> swamp = {{"bone", 2, 6, 0.7f}, {"rotten_flesh", 1, 4, 0.5f}, {"string", 1, 3, 0.4f}, {"potion_heal", 1, 1, 0.15f},
                                          {"gold_coin", 1, 5, 0.5f}, {"lapis_lazuli", 2, 6, 0.3f}, {"candle", 1, 2, 0.3f}};
  static const std::vector<Loot> miner = {{"coal", 3, 10, 0.7f}, {"torch", 4, 12, 0.7f}, {"iron_ingot", 1, 4, 0.5f}, {"gold_ingot", 1, 2, 0.3f},
                                          {"lapis_lazuli", 2, 8, 0.35f}, {"diamond", 1, 2, 0.1f}, {"amethyst_shard", 1, 4, 0.3f}, {"bread", 1, 2, 0.4f}};
  if (kind == "desert") return desert;
  if (kind == "snowy") return snowy;
  if (kind == "jungle") return jungle;
  if (kind == "swamp") return swamp;
  if (kind == "miner") return miner;
  return traveller;
}
// their worn-out gear: a tool or weapon now and then, well used
const char* gearFor(const std::string& kind) {
  return kind == "miner" ? "iron_pickaxe" : kind == "desert" ? "iron_sword" : kind == "snowy" ? "iron_axe" : kind == "jungle" ? "bow" : kind == "swamp" ? "stone_pickaxe" : "iron_sword";
}
u32 mix(u32 h) { h ^= h >> 16; h *= 0x7feb352d; h ^= h >> 15; h *= 0x846ca68b; h ^= h >> 16; return h; }
}  // namespace

void fallenFill(World& w, int x, int y, int z, json& be) {
  const std::string kind = reg().block(w.getBlock(x, y, z)).str("remains");
  u32 h = mix((u32)x * 73856093u ^ (u32)y * 19349663u ^ (u32)z * 83492791u ^ 0xFA11E7u);
  auto rnd = [&] { h = mix(h + 0x9e3779b9u); return (h & 0xFFFFFF) / 16777216.f; };
  json items = json::array();
  for (const Loot& l : lootFor(kind)) {
    if ((int)items.size() >= 9 || !reg().item(l.id) || rnd() > l.chance) continue;
    const int n = l.lo + (int)(rnd() * (l.hi - l.lo + 1));
    items.push_back(json{{"id", l.id}, {"n", std::min(n, l.hi)}});
  }
  if (rnd() < 0.3f && (int)items.size() < 9) if (const ItemDef* gd = reg().item(gearFor(kind))) {   // their gear, worn
    json g{{"id", gd->key}, {"n", 1}};
    if (gd->hasTool && gd->tool.durability > 0) g["d"] = (int)(gd->tool.durability * (0.55f + 0.35f * rnd()));
    items.push_back(g);
  }
  while ((int)items.size() < 9) items.push_back(nullptr);
  for (int i = 8; i > 0; i--) std::swap(items[i], items[(int)(rnd() * (i + 1))]);   // (scattered through the slots)
  be["items"] = items;
  if (rnd() < 0.125f) be["trap"] = kind == "desert" ? "husk" : kind == "snowy" ? "stray" : kind == "miner" ? "skeleton" : "zombie";
}

void fallenOpened(Level& lv, Player& p, World& w, int x, int y, int z) {
  if (lv.isRemote()) return;   // (the host's game raises them)
  json* e = w.blockEntity(x, y, z);
  if (!e || !e->is_object() || !e->contains("trap") || !(*e)["trap"].is_string()) return;
  std::string mob = (*e)["trap"];
  e->erase("trap"); w.blockEntityChanged(x, y, z);
  if (!reg().mobs.contains(mob)) mob = reg().mobs.contains("skeleton") ? "skeleton" : "zombie";
  for (int k = 0; k < 2; k++) {   // they rise beside the bones
    const float a = (k + 0.25f) * 3.14159f;
    vec3 at(x + 0.5f + std::cos(a) * 1.6f, (float)y, z + 0.5f + std::sin(a) * 1.6f);
    for (int dy = 2; dy >= -2; dy--) if (!reg().tSolid[w.getBlock((int)std::floor(at.x), y + dy, (int)std::floor(at.z))] && reg().tSolid[w.getBlock((int)std::floor(at.x), y + dy - 1, (int)std::floor(at.z))]) { at.y = (float)(y + dy); break; }
    spawnCreature(lv, p.realm, mob, at);
  }
  toast(lv, p, "It wasn't alone...", "warn");
  sfxAt(lv, "ghast_moan", vec3(x, y, z) + vec3(0.5f));
}

} // namespace dq
