// tools/test_weapons.cpp — weapon families, quality, and the crafting-table recipes.
// Headless. Run from the source tree: ./build/test_weapons
#include "core/init.h"
#include "core/registry.h"
#include "sim/attack_moves.h"
#include "sim/weapon_quality.h"
#include "sim/crafting.h"
#include "core/tool_parts.h"
#include <cmath>
#include <cstdio>

using namespace dq;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static const char* movesetName(Moveset m) {
  switch (m) {
    case Moveset::Fist: return "fist";
    case Moveset::Sword: return "sword";
    case Moveset::Dagger: return "dagger";
    case Moveset::Axe: return "axe";
    case Moveset::Tool: return "tool";
    case Moveset::Greatsword: return "greatsword";
    case Moveset::Spear: return "spear";
    case Moveset::Mace: return "mace";
  }
  return "?";
}

static void expect(const char* key, Moveset want) {
  const ItemDef* it = reg().item(key);
  CHECK(it != nullptr, "%s is an item", key);
  if (!it) return;
  const Moveset got = movesetOf(it);
  CHECK(got == want, "%s fights as %s (got %s)", key, movesetName(want), movesetName(got));
}

int main() {
  CoreOptions co;
  co.server = true;
  co.userDirOverride = "/tmp/dq-test-weapons";
  fs::remove_all(co.userDirOverride);
  if (!initCore(co)) { printf("initCore failed\n"); return 2; }

  printf("old weapons keep their movesets\n");
  expect("iron_sword", Moveset::Sword);
  expect("frostbite_dagger", Moveset::Dagger);
  expect("stone_knife", Moveset::Dagger);
  expect("iron_axe", Moveset::Axe);
  expect("stone_hammer", Moveset::Tool);
  const ItemDef* bow = reg().item("bow");
  CHECK(bow && movesetOf(bow) != Moveset::Sword, "a bow is not a sword");

  printf("a moveset field names a family\n");
  ItemDef flagged;
  flagged.key = "test_greatsword";
  flagged.hasTool = true;
  flagged.tool.type = "sword";
  flagged.props = json::object();
  flagged.props["moveset"] = "greatsword";
  CHECK(movesetOf(&flagged) == Moveset::Greatsword, "moveset greatsword is a greatsword (got %s)",
        movesetName(movesetOf(&flagged)));

  ItemDef unknown = flagged;
  unknown.props["moveset"] = "halberd";
  CHECK(movesetOf(&unknown) == Moveset::Sword, "an unknown moveset keeps the old sword rule (got %s)",
        movesetName(movesetOf(&unknown)));

  printf("quality\n");
  ItemStack plain("iron_sword", 1);
  CHECK(weaponQuality(plain) == WQual::Normal && qualityDamageFactor(weaponQuality(plain)) == 1.f,
        "an untagged sword deals its normal damage");
  ItemStack poor("iron_sword", 1);
  poor.tag = json::object();
  poor.tag["qual"] = "poor";
  CHECK(std::fabs(qualityDamageFactor(weaponQuality(poor)) - 0.85f) < 0.001f, "a poor sword deals 0.85");
  ItemStack a("iron_sword", 1), b("iron_sword", 1);
  rollWeaponQuality(a, 42);
  rollWeaponQuality(b, 42);
  CHECK(a.tag.is_object() && a.tag["qual"] == b.tag["qual"], "the same seed rolls the same quality");
  int nPoor = 0, nNormal = 0, nFine = 0, nGreat = 0, nLegend = 0;
  for (uint32_t seed = 0; seed < 100; seed++) {
    ItemStack s("iron_sword", 1);
    rollWeaponQuality(s, seed);
    const std::string q = s.tag.value("qual", "");
    nPoor += q == "poor"; nNormal += q == "normal"; nFine += q == "fine"; nGreat += q == "great"; nLegend += q == "legendary";
  }
  CHECK(nPoor == 18 && nNormal == 42 && nFine == 25 && nGreat == 12 && nLegend == 3,
        "a hundred seeds are 18 poor, 42 normal, 25 fine, 12 great, 3 legendary (got %d %d %d %d %d)",
        nPoor, nNormal, nFine, nGreat, nLegend);
  CHECK(std::fabs(qualityDamageFactor(WQual::Great) - 1.28f) < 0.001f && std::fabs(qualityDamageFactor(WQual::Legendary) - 1.5f) < 0.001f,
        "great is 1.28 and legendary is 1.5");
  CHECK(std::fabs(weaponQualityFactor(poor) - 0.85f) < 0.001f, "a poor sword's blow is scaled");

  printf("procedural weapons\n");
  CHECK(reg().item("iron_greatsword") == nullptr && reg().item("mixed_greatsword") != nullptr, "a greatsword is not a fixed item");
  ItemStack plainSword, plainGreat;
  CHECK(makePartTool("sword", {"iron", "iron"}, "stick", plainSword) && makePartTool("greatsword", {"iron", "iron", "iron"}, "stick", plainGreat),
        "iron mixes into a sword and a greatsword");
  CHECK(plainGreat.id == "mixed_greatsword" && movesetOf(plainGreat.def()) == Moveset::Greatsword && plainGreat.def()->num("hands") == 2,
        "the greatsword uses both hands");
  CHECK(toolOf(plainGreat).damage > toolOf(plainSword).damage, "the same iron hits harder as a greatsword");
  auto gridOf = [](std::vector<std::pair<int, ItemStack>> cells) {
    std::vector<ItemStack> g(9);
    for (auto& c : cells) g[c.first] = c.second;
    return matchGrid(g, 3);
  };
  const ItemStack ingot("iron_ingot", 1), stick("stick", 1);
  CraftMatch gsGrid = gridOf({{0, ingot}, {1, ingot}, {2, ingot}, {4, stick}});
  CraftMatch spGrid = gridOf({{0, ingot}, {1, ingot}, {2, stick}});
  CraftMatch mcGrid = gridOf({{0, ingot}, {1, ingot}, {4, stick}});
  auto qualOk = [](const ItemStack& s) {
    const std::string q = s.tag.value("qual", "");
    return q == "poor" || q == "normal" || q == "fine" || q == "great" || q == "legendary";
  };
  CHECK(gsGrid && gsGrid.result().id == "mixed_greatsword" && qualOk(gsGrid.result()), "three irons over a stick make a mixed greatsword with a quality");
  CHECK(spGrid && spGrid.result().id == "mixed_spear" && spGrid.result().def()->num("hands") == 2, "two irons and a stick in a line make a spear");
  CHECK(mcGrid && mcGrid.result().id == "mixed_mace" && mcGrid.result().def()->num("hands") == 1, "two irons over a stick make a mace");
  ItemStack again = gsGrid.result();
  const std::string kept = again.tag["qual"];
  stampGearQuality(again);
  CHECK(again.tag["qual"] == kept, "a quality already on the piece stays");
  CHECK(comboLength(Moveset::Greatsword) == 2 && comboLength(Moveset::Spear) == 2 && comboLength(Moveset::Mace) == 2,
        "the new families chain two cuts");
  const AttackMove& g1 = attackMove(attackMoveFor(Moveset::Greatsword, 0, false, false));
  const AttackMove& sp = attackMove(attackMoveFor(Moveset::Spear, 0, false, false));
  const AttackMove& mc = attackMove(attackMoveFor(Moveset::Mace, 0, false, false));
  const AttackMove& sw = attackMove(MV_SWORD1);
  CHECK(std::fabs(g1.reach - 1.15f) < 0.001f && std::fabs(g1.impact - 1.4f) < 0.001f, "a greatsword reaches 1.15 and hits harder");
  CHECK(std::fabs(sp.reach - 1.35f) < 0.001f && std::fabs(sp.impact - 1.1f) < 0.001f, "a spear reaches 1.35");
  CHECK(std::fabs(mc.reach - 0.9f) < 0.001f && std::fabs(mc.impact - 1.6f) < 0.001f, "a mace is short and heavy");
  CHECK(std::fabs(sw.reach - 1.f) < 0.001f && std::fabs(sw.impact - 1.f) < 0.001f, "a sword's first cut is unchanged");
  CHECK(attackMoveFor(Moveset::Greatsword, 0, true, false) == MV_AXE2 && attackMoveFor(Moveset::Spear, 0, false, true) == MV_SWORD_AIR,
        "until their own poses, a dash reuses the axe and the air cut reuses the sword");

  printf(fails ? "%d failed\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
