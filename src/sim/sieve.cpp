// sim/sieve.cpp — the sieve (Better than Adventure's trommel, worked by hand): use a block of gravel, sand, dirt,
// mud, soul sand, ash or cinder sand on it and whatever was mixed in drops out below. Hold use to keep sifting.
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

float frand();   // sim/content.h

namespace {
struct Find { const char* item; float chance; int lo, hi; };
const std::unordered_map<std::string, std::vector<Find>>& finds() {
  static const std::unordered_map<std::string, std::vector<Find>> F = {
    {"gravel", {{"flint", 0.35f, 1, 1}, {"iron_nugget", 0.14f, 1, 2}, {"gold_nugget", 0.05f, 1, 1}, {"coal", 0.06f, 1, 1}, {"lapis_lazuli", 0.02f, 1, 2}}},
    {"sand", {{"clay_ball", 0.15f, 1, 1}, {"gold_nugget", 0.06f, 1, 2}, {"quartz", 0.03f, 1, 1}, {"amethyst_shard", 0.01f, 1, 1}}},
    {"red_sand", {{"gold_nugget", 0.12f, 1, 2}, {"redstone", 0.06f, 1, 2}, {"clay_ball", 0.1f, 1, 1}}},
    {"rich_scorched_dirt", {{"iron_nugget", 0.5f, 1, 3}, {"gold_nugget", 0.25f, 1, 2}, {"crushed_copper", 0.15f, 1, 1}, {"redstone", 0.08f, 1, 2}}},   // BTA
    {"scorched_dirt", {{"iron_nugget", 0.05f, 1, 1}, {"dead_bush", 0.04f, 1, 1}, {"wheat_seeds", 0.05f, 1, 1}}},
    {"dirt", {{"wheat_seeds", 0.2f, 1, 1}, {"pumpkin_seeds", 0.04f, 1, 1}, {"melon_seeds", 0.04f, 1, 1}, {"potato", 0.03f, 1, 1}, {"bone_meal", 0.08f, 1, 1}, {"string", 0.04f, 1, 1}}},
    {"mud", {{"clay_ball", 0.3f, 1, 2}, {"string", 0.05f, 1, 1}, {"bone", 0.05f, 1, 1}}},
    {"soul_sand", {{"quartz", 0.15f, 1, 2}, {"bone", 0.15f, 1, 1}, {"glowstone_dust", 0.08f, 1, 2}, {"gold_nugget", 0.08f, 1, 2}, {"nether_wart_item", 0.03f, 1, 1}}},
    {"soul_soil", {{"bone", 0.2f, 1, 1}, {"quartz", 0.1f, 1, 1}, {"gold_nugget", 0.06f, 1, 1}}},
    {"ash", {{"coal", 0.12f, 1, 1}, {"gunpowder", 0.05f, 1, 1}, {"bone_meal", 0.1f, 1, 1}}},
    {"cinder_sand", {{"redstone", 0.12f, 1, 2}, {"gold_nugget", 0.1f, 1, 2}, {"magma_cream", 0.04f, 1, 1}, {"glowstone_dust", 0.06f, 1, 1}}},
  };
  return F;
}
} // namespace

// what comes out of a block of it (nothing if it isn't siftable)
static bool siftOut(Level& lv, int realm, const std::string& block, vec3 at) {
  auto it = finds().find(block);
  if (it == finds().end()) return false;
  for (const Find& f : it->second) {
    if (frand() >= f.chance || !reg().item(f.item)) continue;
    int n = f.lo + (f.hi > f.lo ? (int)(frand() * (f.hi - f.lo + 1)) : 0);
    dropItem(lv, realm, ItemStack(f.item, n), at, true, nullptr, 0.f);
  }
  return true;
}

void registerSieve() {
  static bool done = false;
  if (done) return;
  done = true;
  // the hand sieve: used on a siftable block where it lies, it sifts it away
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack* held, const ItemDef*) {
    if (pc.lv.isRemote() || !held || held->id != "hand_sieve") return false;
    const BlockDef& b = reg().block(hit.id);
    vec3 c = vec3(hit.block) + vec3(0.5f);
    if (!siftOut(pc.lv, pc.p.realm, b.key, c)) { pc.toastMsg("Sieve gravel, sand, dirt, mud, soul sand, ash or cinder sand"); return true; }
    World& w = pc.lv.world(pc.p.realm);
    w.setBlock(hit.block.x, hit.block.y, hit.block.z, AIR);
    pc.lv.fx->particles("block", c, 10, {{"block", b.key}});
    pc.lv.fx->material(b.sound.empty() ? "gravel" : b.sound, "dig", c);
    if (!pc.p.creative() && ++held->d >= 96) { held->clear(); pc.lv.fx->sound("break_tool", c, 16); }   // it wears out
    pc.swingArm();
    return true;
  });
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack* held, const ItemDef* def) {
    if (pc.lv.isRemote() || !reg().block(hit.id).has("sieve") || !held || held->empty() || !def || def->block.empty()) return false;
    auto it = finds().find(def->block);
    if (it == finds().end()) { pc.toastMsg("Sieve gravel, sand, dirt, mud, soul sand, ash or cinder sand"); return true; }
    Level& lv = pc.lv;
    vec3 c = vec3(hit.block) + vec3(0.5f, 0.75f, 0.5f);
    const std::string mat = reg().block(reg().blockId(def->block)).sound;
    if (!pc.p.creative()) { if (--held->n <= 0) held->clear(); }
    siftOut(lv, pc.p.realm, def->block, vec3(hit.block) + vec3(0.5f, 0.2f, 0.5f));
    lv.fx->particles("block", c, 10, {{"block", def->block}});
    lv.fx->material(mat.empty() ? "gravel" : mat, "dig", c);
    pc.swingArm();
    return true;
  });
}

} // namespace dq
