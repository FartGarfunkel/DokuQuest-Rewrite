// tools/test_weapons.cpp — weapon families, quality, and the crafting-table recipes.
// Headless. Run from the source tree: ./build/test_weapons
#include "core/init.h"
#include "core/registry.h"
#include "sim/attack_moves.h"
#include "sim/weapon_quality.h"
#include "sim/crafting.h"
#include "sim/inventory.h"
#include "sim/player.h"
#include "sim/combat_rpg.h"
#include "sim/skills.h"
#include "core/tool_parts.h"
#include "sim/balance.h"
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

  printf("tools and armour use that same quality\n");
  ItemStack pick;
  CHECK(makePartTool("pickaxe", {"iron", "iron", "iron"}, "stick", pick), "an iron pick mixes");
  const int pickBase = toolMaxDurability(pick);
  pick.tag["qual"] = "poor";
  CHECK(std::fabs(gearQualityFactor(pick) - 0.85f) < 0.001f, "a poor pick uses the poor factor");
  CHECK(toolMaxDurability(pick) == (int)std::lround(pickBase * 0.85f), "a poor pick wears out sooner (%d vs %d)",
        toolMaxDurability(pick), (int)std::lround(pickBase * 0.85f));
  ItemStack helm;
  CHECK(makePartArmor("helmet", std::vector<std::string>(5, "iron"), helm), "an iron helmet mixes");
  const float orePts = armorPointsOf(helm);
  helm.tag["qual"] = "legendary";
  helm.tag["pa"]["fr"] = 0.2f;
  pick.tag["qual"] = "legendary";
  CHECK(std::fabs(gearQualityFactor(helm) - gearQualityFactor(pick)) < 0.001f && std::fabs(gearQualityFactor(helm) - 1.5f) < 0.001f,
        "a legendary helmet and a legendary pick share the weapon factor");
  Player wearer;
  wearer.armor.assign(4, ItemStack());
  wearer.armor[0] = helm;
  CHECK(armorPoints(wearer) == (int)std::lround(orePts * 1.5f), "the helmet's protection is the ore mix times legendary");
  CHECK(std::fabs(fireResist(wearer) - 0.3f) < 0.02f, "its fire resistance scales the same way");

  printf("procedural weapons\n");
  CHECK(reg().item("iron_greatsword") == nullptr && reg().item("mixed_greatsword") != nullptr, "a greatsword is not a fixed item");
  CHECK(reg().item("iron_spear") == nullptr && reg().item("iron_mace") == nullptr, "a spear and a mace are not fixed items");
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
  CraftMatch gsGrid = gridOf({{0, ingot}, {1, ingot}, {3, ingot}, {6, stick}});   // a long blade, wide at the tip
  CraftMatch spGrid = gridOf({{0, ingot}, {1, ingot}, {3, stick}, {6, stick}});     // a point on a long shaft
  CraftMatch mcGrid = gridOf({{0, ingot}, {1, ingot}, {2, ingot}, {4, stick}});     // a heavy head over a short haft
  auto qualOk = [](const ItemStack& s) {
    const std::string q = s.tag.value("qual", "");
    return q == "poor" || q == "normal" || q == "fine" || q == "great" || q == "legendary";
  };
  CHECK(gsGrid && gsGrid.result().id == "mixed_greatsword" && qualOk(gsGrid.result()), "three irons over a stick make a mixed greatsword with a quality");
  CraftMatch ironCraft = gridOf({{0, ingot}, {3, ingot}, {6, stick}});
  ItemStack ironMade = ironCraft ? ironCraft.result() : ItemStack();
  CHECK(ironCraft && ironMade.id == "mixed_sword" && ironMade.id == ironCraft.out && ironMade.n == 1 && qualOk(ironMade),
        "the old iron sword craft keeps its id and count and gains a quality (%s x%d %s)",
        ironMade.id.c_str(), ironMade.n, ironMade.tag.value("qual", "").c_str());
  const ItemStack plank("planks", 1);
  CraftMatch woodCraft = gridOf({{0, plank}, {3, plank}, {6, stick}});
  ItemStack woodMade = woodCraft ? woodCraft.result() : ItemStack();
  CHECK(woodCraft && woodMade.id == "wood_sword" && woodMade.n == 1 && qualOk(woodMade),
        "an old fixed sword recipe keeps its id and count and gains a quality (%s x%d %s)",
        woodMade.id.c_str(), woodMade.n, woodMade.tag.value("qual", "").c_str());
  CHECK(spGrid && spGrid.result().id == "mixed_spear" && spGrid.result().def()->num("hands") == 2, "two irons and a stick in a line make a spear");
  CHECK(mcGrid && mcGrid.result().id == "mixed_mace" && mcGrid.result().def()->num("hands") == 1, "two irons over a stick make a mace");
  ItemStack again = gsGrid.result();
  const std::string kept = again.tag["qual"];
  stampGearQuality(again);
  CHECK(again.tag["qual"] == kept, "a quality already on the piece stays");
  again.tag["qual"] = "fine";
  stampGearQuality(again);
  CHECK(again.id == "mixed_greatsword" && again.n == 1 && again.tag.value("qual", "") == "fine", "a pre-set qual fine stays fine");
  ItemStack fineSword("iron_sword", 1);
  fineSword.tag = json::object();
  fineSword.tag["qual"] = "fine";
  fineSword.d = 40;
  ItemStack fineSwordB = fineSword;
  fineSwordB.d = 15;
  std::vector<ItemStack> fineGrid(9);
  fineGrid[0] = fineSword;
  fineGrid[1] = fineSwordB;
  CraftMatch fineCraft = matchGrid(fineGrid, 3);
  ItemStack fineOut = fineCraft ? fineCraft.result() : ItemStack();
  CHECK(fineCraft && fineOut.id == "iron_sword" && fineOut.n == 1 && fineOut.tag.value("qual", "") == "fine",
        "a second craft of an iron sword that already has qual fine leaves it fine");
  MobLoot found; found.gearTier = 2;
  ItemStack dropA = mobGearRoll(found, 0.25, 0.4, 0.5), dropB = mobGearRoll(found, 0.25, 0.4, 0.5);
  CHECK(!dropA.empty() && qualOk(dropA) && dropA.tag.value("qual", "") == dropB.tag.value("qual", ""),
        "found gear rolls a quality from the loot rolls (%s)", dropA.tag.value("qual", "").c_str());
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
  CHECK(spGrid && mcGrid && movesetOf(spGrid.result().def()) == Moveset::Spear && movesetOf(mcGrid.result().def()) == Moveset::Mace,
        "the spear and the mace fight as those families");

  printf("the iron sword recipe stays ingots and a stick\n");
  int shapedSwords = 0, badOut = 0, recipeRows = 0;
  bool swordOk = false;
  auto familyName = [](const std::string& s) {
    return s.find("greatsword") != std::string::npos || s.find("spear") != std::string::npos || s.find("mace") != std::string::npos;
  };
  auto castName = [](const std::string& s) {
    return s.find("mold") != std::string::npos || s.find("_head") != std::string::npos;
  };
  for (const Recipe& r : reg().recipes) {
    if (r.out == "iron_greatsword" || r.out == "iron_spear" || r.out == "iron_mace" ||
        r.out == "mixed_greatsword" || r.out == "mixed_spear" || r.out == "mixed_mace") recipeRows++;
    bool family = familyName(r.out), cast = castName(r.out);
    auto note = [&](const std::string& s) { family = family || familyName(s); cast = cast || castName(s); };
    for (const auto& kv : r.key) note(kv.second);
    for (const std::string& it : r.items) note(it);
    if (family && cast) badOut++;
    if (r.out != "iron_sword" || !r.shaped()) continue;
    shapedSwords++;
    const auto mit = r.key.find('M'), sit = r.key.find('S');
    const bool shape = r.shape.size() == 3 && r.shape[0] == "M" && r.shape[1] == "M" && r.shape[2] == "S";
    const bool key = mit != r.key.end() && sit != r.key.end() && mit->second == "iron_ingot" && sit->second == "stick" && r.key.size() == 2;
    swordOk = shape && key && r.props.value("classicOnly", false);
    printf("  iron_sword shape");
    for (const std::string& row : r.shape) printf(" \"%s\"", row.c_str());
    printf("\n");
  }
  CHECK(shapedSwords == 1 && swordOk, "the old iron sword is still [\"M\",\"M\",\"S\"] of an ingot and a stick");
  CHECK(recipeRows == 0, "a greatsword, spear, or mace is not a recipe row");
  CHECK(badOut == 0, "no greatsword, spear, or mace recipe is a mold or a head");

  printf("who may hold one, and a new roll\n");
  ItemStack ironGs, copperGs;
  CHECK(makePartTool("greatsword", {"iron", "iron", "iron"}, "stick", ironGs), "iron greatsword");
  CHECK(makePartTool("greatsword", {"copper", "copper", "copper"}, "stick", copperGs), "copper greatsword");
  Player hand;
  hand.inv.assign(9, ItemStack());
  hand.selected = 0;
  hand.stats["skills"] = json::object();
  CHECK(weaponSkillMet(hand, ItemStack("iron_sword", 1)), "an iron sword needs no melee practice");
  CHECK(weaponSkillMet(hand, copperGs), "a copper greatsword needs no melee practice (tier %d)", toolOf(copperGs).tier);
  CHECK(!weaponSkillMet(hand, ironGs), "an iron greatsword needs melee 15");
  hand.stats["skills"]["melee"] = skillXpFor(15);
  CHECK(skillLv(hand, "melee") >= 15 && weaponSkillMet(hand, ironGs), "melee 15 can hold the iron greatsword");
  ironGs.tag["qual"] = "poor";
  std::vector<ItemStack> grid(9);
  grid[0] = ironGs;
  grid[1] = ItemStack("iron_ingot", 1);
  CraftMatch rolled = matchGrid(grid, 3);
  CHECK(rolled && rolled.result().id == "mixed_greatsword" && rolled.result().tag.value("qual", "") != "poor",
        "an ingot of its metal rolls a new quality and keeps the weapon");

  printf("a weapon guard is weaker than a shield\n");
  Player guard;
  guard.inv.assign(9, ItemStack());
  guard.shield.assign(1, ItemStack());
  guard.selected = 0;
  guard.shield[0] = ItemStack("shield", 1);
  const float shieldKeep = guardKeep(guard);
  CHECK(std::fabs(shieldKeep - 0.35f) < 0.001f && std::fabs(guardWindow(guard) - 0.18f) < 0.001f, "a shield still keeps 0.35 inside 0.18s");
  guard.shield[0].clear();
  guard.inv[0] = ItemStack("iron_sword", 1);
  const float oneKeep = guardKeep(guard);
  CHECK(std::fabs(oneKeep - (0.35f + 0.5f * 0.65f)) < 0.001f && std::fabs(guardWindow(guard) - 0.18f * 0.6f) < 0.001f,
        "a one-handed weapon keeps more of the blow");
  guard.inv[0] = ironGs;
  CHECK(std::fabs(guardKeep(guard) - (0.35f + 0.25f * 0.65f)) < 0.001f && std::fabs(guardWindow(guard) - 0.18f * 0.8f) < 0.001f,
        "a two-handed weapon sits between the sword and the shield");
  guard.shield[0] = ItemStack("shield", 1);
  CHECK(std::fabs(guardKeep(guard) - (0.35f + 0.25f * 0.65f)) < 0.001f, "a shield on the back does not replace a two-handed guard");

  printf(fails ? "%d failed\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
