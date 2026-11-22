// tools/test_weapons.cpp — weapon families, quality, and the crafting-table recipes.
// Headless. Run from the source tree: ./build/test_weapons
#include "core/init.h"
#include "core/registry.h"
#include "sim/attack_moves.h"
#include "sim/weapon_quality.h"
#include "sim/crafting.h"
#include "sim/inventory.h"
#include "sim/player.h"
#include "sim/combat.h"
#include "sim/combat_rpg.h"
#include "sim/skills.h"
#include "sim/shield_shape.h"
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
    case Moveset::Throw: return "throw";
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
  ItemDef thrown = flagged;
  thrown.props["moveset"] = "throw";
  CHECK(movesetOf(&thrown) == Moveset::Throw, "moveset throw is a throw (got %s)", movesetName(movesetOf(&thrown)));
  CHECK(attackMoveFor(Moveset::Throw, 0, false, false) == MV_NONE, "a throw has no melee cut");

  printf("quality\n");
  ItemStack plain("iron_sword", 1);
  CHECK(weaponQuality(plain) == WQual::Normal && qualityDamageFactor(weaponQuality(plain)) == 1.f,
        "an untagged sword deals its normal damage");
  ItemStack poor("iron_sword", 1);
  poor.tag = json::object();
  poor.tag["qual"] = "poor";
  CHECK(std::fabs(qualityDamageFactor(weaponQuality(poor)) - 0.92f) < 0.001f, "a poor sword deals 0.92");
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
  CHECK(std::fabs(qualityDamageFactor(WQual::Great) - 1.15f) < 0.001f && std::fabs(qualityDamageFactor(WQual::Legendary) - 1.22f) < 0.001f,
        "great is 1.15 and legendary is 1.22");
  CHECK(std::fabs(weaponQualityFactor(poor) - 0.92f) < 0.001f, "a poor sword's blow is scaled");

  printf("tools and armour use that same quality\n");
  ItemStack pick;
  CHECK(makePartTool("pickaxe", {"iron", "iron", "iron"}, "stick", pick), "an iron pick mixes");
  const int pickBase = toolMaxDurability(pick);
  pick.tag["qual"] = "poor";
  CHECK(std::fabs(gearQualityFactor(pick) - 0.92f) < 0.001f, "a poor pick uses the poor factor");
  CHECK(toolMaxDurability(pick) == (int)std::lround(pickBase * 0.92f), "a poor pick wears out sooner (%d vs %d)",
        toolMaxDurability(pick), (int)std::lround(pickBase * 0.92f));
  ItemStack helm;
  CHECK(makePartArmor("helmet", std::vector<std::string>(5, "iron"), helm), "an iron helmet mixes");
  const float orePts = armorPointsOf(helm);
  helm.tag["qual"] = "legendary";
  helm.tag["pa"]["fr"] = 0.2f;
  pick.tag["qual"] = "legendary";
  CHECK(std::fabs(gearQualityFactor(helm) - gearQualityFactor(pick)) < 0.001f && std::fabs(gearQualityFactor(helm) - 1.22f) < 0.001f,
        "a legendary helmet and a legendary pick share the weapon factor");
  Player wearer;
  wearer.armor.assign(4, ItemStack());
  wearer.armor[0] = helm;
  CHECK(armorPoints(wearer) == (int)std::lround(orePts * 1.22f), "the helmet's protection is the ore mix times legendary");
  CHECK(std::fabs(fireResist(wearer) - 0.244f) < 0.02f, "its fire resistance scales the same way");

  printf("ore steps beat a lucky quality roll\n");
  {
    const char* ores[] = {"copper", "bronze", "iron", "steel", "mithril", "adamantite"};
    float prev = 0.f;
    bool stepped = true;
    for (const char* o : ores) {
      ItemStack s;
      if (!makePartTool("sword", {o, o}, "stick", s)) { stepped = false; break; }
      const float d = toolOf(s).damage;
      CHECK(d > prev, "%s sword deals %.1f, more than the ore before", o, d);
      if (d <= prev) stepped = false;
      prev = d;
    }
    CHECK(stepped, "copper < bronze < iron < steel < mithril < adamantite");
    ItemStack sil, irn;
    CHECK(makePartTool("sword", {"silver", "silver"}, "stick", sil) && makePartTool("sword", {"iron", "iron"}, "stick", irn),
          "silver and iron mix into swords");
    CHECK(std::fabs(toolOf(sil).damage - toolOf(irn).damage) < 0.01f, "silver matches iron on the blow (%.1f)", toolOf(sil).damage);
    CHECK(gearIsHoly(sil) && !gearIsHoly(irn), "silver is the holy sidegrade to iron");
    ItemStack poorI, fineC;
    CHECK(makePartTool("sword", {"iron", "iron"}, "stick", poorI) && makePartTool("sword", {"copper", "copper"}, "stick", fineC),
          "iron and copper swords for the quality check");
    poorI.tag["qual"] = "poor";
    fineC.tag["qual"] = "fine";
    Player w;
    w.selected = 0;
    w.inv[0] = poorI;
    const float dPoorIron = heldTool(w) ? heldTool(w)->damage : 0.f;
    w.inv[0] = fineC;
    const float dFineCu = heldTool(w) ? heldTool(w)->damage : 0.f;
    CHECK(dPoorIron > dFineCu, "a poor iron sword still beats a fine copper one (%.2f > %.2f)", dPoorIron, dFineCu);
  }

  printf("procedural weapons\n");
  CHECK(reg().item("iron_greatsword") == nullptr && reg().item("mixed_greatsword") != nullptr, "a greatsword is not a fixed item");
  CHECK(reg().item("iron_spear") == nullptr && reg().item("iron_mace") == nullptr, "a spear and a mace are not fixed items");
  ItemStack plainSword, plainGreat;
  CHECK(makePartTool("sword", {"iron", "iron"}, "stick", plainSword) && makePartTool("greatsword", {"iron", "iron", "iron", "iron", "iron", "iron"}, "stick", plainGreat),
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
  CraftMatch gsGrid = gridOf({{0, ingot}, {1, ingot}, {2, ingot}, {3, ingot}, {4, ingot}, {5, ingot}, {7, stick}});   // six head cells, stick at (1,2)
  CraftMatch spGrid = gridOf({{0, ingot}, {1, ingot}, {3, stick}, {6, stick}});     // a point on a long shaft
  CraftMatch mcGrid = gridOf({{0, ingot}, {1, ingot}, {2, ingot}, {4, stick}});     // a heavy head over a short haft
  auto qualOk = [](const ItemStack& s) {
    const std::string q = s.tag.value("qual", "");
    return q == "poor" || q == "normal" || q == "fine" || q == "great" || q == "legendary";
  };
  CHECK(gsGrid && gsGrid.result().id == "mixed_greatsword" && qualOk(gsGrid.result()), "six irons and a stick at 0,1,2,3,4,5 and 7 make a mixed greatsword with a quality");
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
  CHECK(reg().item("mixed_longsword") && reg().item("mixed_battleaxe") && reg().item("mixed_warhammer")
        && reg().item("mixed_throwing_knife") && reg().item("mixed_throwing_axe"),
        "longsword, battleaxe, warhammer and throwing weapons are items");
  CraftMatch lsGrid = gridOf({{1, ingot}, {4, ingot}, {6, ingot}, {8, ingot}, {7, stick}});   // blade and a crossguard
  CraftMatch baGrid = gridOf({{0, ingot}, {2, ingot}, {3, ingot}, {4, ingot}, {5, ingot}, {7, stick}});   // two bits
  CraftMatch whGrid = gridOf({{0, ingot}, {1, ingot}, {2, ingot}, {4, ingot}, {7, stick}});   // a heavy head
  CraftMatch tkGrid = gridOf({{0, ingot}, {3, stick}});   // a short blade over a grip
  CraftMatch taGrid = gridOf({{0, ingot}, {1, ingot}, {4, stick}});   // a small head, stick at (1,1)
  CHECK(lsGrid && lsGrid.result().id == "mixed_longsword" && lsGrid.result().def()->num("hands") == 1
        && movesetOf(lsGrid.result().def()) == Moveset::Sword && qualOk(lsGrid.result()),
        "four irons and a stick make a mixed longsword");
  CHECK(baGrid && baGrid.result().id == "mixed_battleaxe" && baGrid.result().def()->num("hands") == 2
        && movesetOf(baGrid.result().def()) == Moveset::Axe && qualOk(baGrid.result()),
        "five irons and a stick make a mixed battleaxe");
  CHECK(whGrid && whGrid.result().id == "mixed_warhammer" && whGrid.result().def()->num("hands") == 2
        && movesetOf(whGrid.result().def()) == Moveset::Mace && qualOk(whGrid.result()),
        "four irons and a stick make a mixed warhammer");
  CHECK(tkGrid && tkGrid.result().id == "mixed_throwing_knife" && movesetOf(tkGrid.result().def()) == Moveset::Throw
        && attackMoveFor(Moveset::Throw, 0, false, false) == MV_NONE && qualOk(tkGrid.result()),
        "an iron and a stick make a throwing knife");
  CHECK(taGrid && taGrid.result().id == "mixed_throwing_axe" && movesetOf(taGrid.result().def()) == Moveset::Throw
        && qualOk(taGrid.result()),
        "two irons and a stick make a throwing axe");
  {
    ItemStack ls, ba, wh, tk, ta;
    CHECK(makePartTool("longsword", {"iron", "iron", "iron", "iron"}, "stick", ls)
          && makePartTool("battleaxe", {"iron", "iron", "iron", "iron", "iron"}, "stick", ba)
          && makePartTool("warhammer", {"iron", "iron", "iron", "iron"}, "stick", wh)
          && makePartTool("throwing_knife", {"iron"}, "stick", tk)
          && makePartTool("throwing_axe", {"iron", "iron"}, "stick", ta),
          "the new mixed kinds craft from parts");
    CHECK(toolOf(ls).damage > toolOf(plainSword).damage && toolOf(plainGreat).damage > toolOf(ls).damage,
          "a longsword sits between a sword and a greatsword (%.1f / %.1f / %.1f)",
          toolOf(plainSword).damage, toolOf(ls).damage, toolOf(plainGreat).damage);
    ItemStack ax, mc;
    CHECK(makePartTool("axe", {"iron", "iron", "iron"}, "stick", ax) && makePartTool("mace", {"iron", "iron", "iron"}, "stick", mc),
          "an iron axe and mace for the mix check");
    CHECK(toolOf(ba).damage > toolOf(ax).damage, "a battleaxe hits harder than an axe (%.1f > %.1f)", toolOf(ba).damage, toolOf(ax).damage);
    CHECK(toolOf(wh).damage > toolOf(mc).damage && toolOf(wh).speed < toolOf(mc).speed,
          "a warhammer is slower and hits harder than a mace (%.1f @ %.2f vs %.1f @ %.2f)",
          toolOf(wh).damage, toolOf(wh).speed, toolOf(mc).damage, toolOf(mc).speed);
    CHECK(attackMove(attackMoveFor(movesetOf(wh.def()), 0, false, false)).pierce >= 0.2f, "a warhammer's blows pierce");
  }
  ItemStack again = gsGrid ? gsGrid.result() : ItemStack();
  const std::string kept = again.tag.is_object() ? again.tag.value("qual", "") : "";
  stampGearQuality(again);
  CHECK(gsGrid && again.tag.is_object() && again.tag.value("qual", "") == kept, "a quality already on the piece stays");
  if (!again.tag.is_object()) again.tag = json::object();
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
    return s.find("greatsword") != std::string::npos || s.find("spear") != std::string::npos || s.find("mace") != std::string::npos
        || s.find("longsword") != std::string::npos || s.find("battleaxe") != std::string::npos || s.find("warhammer") != std::string::npos
        || s.find("throwing_knife") != std::string::npos || s.find("throwing_axe") != std::string::npos;
  };
  auto castName = [](const std::string& s) {
    return s.find("mold") != std::string::npos || s.find("_head") != std::string::npos;
  };
  for (const Recipe& r : reg().recipes) {
    if (r.out == "iron_greatsword" || r.out == "iron_spear" || r.out == "iron_mace" ||
        r.out == "mixed_greatsword" || r.out == "mixed_spear" || r.out == "mixed_mace" ||
        r.out == "mixed_longsword" || r.out == "mixed_battleaxe" || r.out == "mixed_warhammer" ||
        r.out == "mixed_throwing_knife" || r.out == "mixed_throwing_axe") recipeRows++;
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
  CHECK(makePartTool("greatsword", {"iron", "iron", "iron", "iron", "iron", "iron"}, "stick", ironGs), "iron greatsword");
  CHECK(makePartTool("greatsword", {"copper", "copper", "copper", "copper", "copper", "copper"}, "stick", copperGs), "copper greatsword");
  Player hand;
  hand.inv.assign(9, ItemStack());
  hand.selected = 0;
  hand.stats["skills"] = json::object();
  CHECK(weaponSkillMet(hand, ItemStack("iron_sword", 1)), "an iron sword needs no melee practice");
  CHECK(weaponSkillMet(hand, copperGs), "a copper greatsword needs no melee practice (tier %d)", toolOf(copperGs).tier);
  CHECK(!weaponSkillMet(hand, ironGs), "an iron greatsword needs melee 15");
  hand.mode = "creative";
  CHECK(weaponSkillMet(hand, ironGs), "creative can try an iron greatsword");
  hand.mode = "survival";
  hand.stats["skills"]["melee"] = skillXpFor(15);
  CHECK(skillLv(hand, "melee") >= 15 && weaponSkillMet(hand, ironGs), "melee 15 can hold the iron greatsword");
  {
    ItemStack ironLs, copperLs, ironBa, copperBa, ironWh, copperWh, ironTk;
    CHECK(makePartTool("longsword", {"iron", "iron", "iron", "iron"}, "stick", ironLs)
          && makePartTool("longsword", {"copper", "copper", "copper", "copper"}, "stick", copperLs)
          && makePartTool("battleaxe", {"iron", "iron", "iron", "iron", "iron"}, "stick", ironBa)
          && makePartTool("battleaxe", {"copper", "copper", "copper", "copper", "copper"}, "stick", copperBa)
          && makePartTool("warhammer", {"iron", "iron", "iron", "iron"}, "stick", ironWh)
          && makePartTool("warhammer", {"copper", "copper", "copper", "copper"}, "stick", copperWh)
          && makePartTool("throwing_knife", {"iron"}, "stick", ironTk),
          "iron and copper mix the new heavy and throwing kinds");
    const int meleeXp = hand.stats["skills"].value("melee", 0);
    hand.stats["skills"]["melee"] = 0;
    CHECK(weaponSkillMet(hand, copperLs) && weaponSkillMet(hand, copperBa) && weaponSkillMet(hand, copperWh),
          "copper longsword, battleaxe and warhammer need no melee practice");
    CHECK(!weaponSkillMet(hand, ironLs) && !weaponSkillMet(hand, ironBa) && !weaponSkillMet(hand, ironWh),
          "iron longsword, battleaxe and warhammer need melee 15");
    CHECK(weaponSkillMet(hand, ironTk), "an iron throwing knife needs no melee practice");
    hand.stats["skills"]["melee"] = meleeXp;
    CHECK(weaponSkillMet(hand, ironLs) && weaponSkillMet(hand, ironBa) && weaponSkillMet(hand, ironWh),
          "melee 15 can hold the iron longsword, battleaxe and warhammer");
  }
  ironGs.tag["qual"] = "poor";
  ItemStack wornGs = ironGs;
  wornGs.d = std::max(2, toolMaxDurability(wornGs) / 2);
  std::vector<ItemStack> mendGrid(9);
  mendGrid[0] = wornGs;
  mendGrid[1] = ItemStack("iron_ingot", 1);
  CraftMatch mended = matchGrid(mendGrid, 3);
  CHECK(mended && mended.repair >= 0 && mended.result().id == wornGs.id && mended.result().d < wornGs.d
        && mended.result().tag.value("qual", "") == "poor",
        "a worn greatsword and one ingot mends and keeps its quality (repair %d)", mended.repair);
  std::vector<ItemStack> grid(9);
  grid[0] = ironGs;
  grid[1] = ItemStack("iron_ingot", 1);
  CraftMatch rolled = matchGrid(grid, 3);
  CHECK(rolled && rolled.result().id == "mixed_greatsword" && rolled.result().tag.value("qual", "") != "poor" && rolled.repair < 0,
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

  printf("every mixed tool and weapon swings, and the mix changes the blow\n");
  struct MixKind { const char* kind; int n; Moveset want; };
  const MixKind kinds[] = {
    {"sword", 2, Moveset::Sword}, {"axe", 3, Moveset::Axe}, {"pickaxe", 3, Moveset::Tool},
    {"shovel", 1, Moveset::Tool}, {"hoe", 2, Moveset::Tool}, {"greatsword", 6, Moveset::Greatsword},
    {"spear", 2, Moveset::Spear}, {"mace", 3, Moveset::Mace}, {"knife", 1, Moveset::Dagger},
    {"longsword", 4, Moveset::Sword}, {"battleaxe", 5, Moveset::Axe}, {"warhammer", 4, Moveset::Mace},
    {"hammer", 1, Moveset::Tool}, {"javelin", 1, Moveset::Sword}, {"saw", 1, Moveset::Axe},
    {"scythe", 1, Moveset::Tool}, {"propick", 1, Moveset::Tool}, {"chisel", 1, Moveset::Tool},
  };
  Player wielder;
  wielder.selected = 0;
  for (const MixKind& k : kinds) {
    ItemStack iron, copper;
    std::vector<std::string> nI(k.n, "iron"), nC(k.n, "copper");
    const bool made = makePartTool(k.kind, nI, "stick", iron) && makePartTool(k.kind, nC, "stick", copper);
    CHECK(made, "mixed %s crafts", k.kind);
    if (!made) continue;
    CHECK(movesetOf(iron.def()) == k.want, "mixed %s fights as %s (got %s)", k.kind, movesetName(k.want),
          movesetName(movesetOf(iron.def())));
    CHECK(attackMoveFor(movesetOf(iron.def()), 0, false, false) != MV_NONE, "mixed %s has a first cut", k.kind);
    wielder.inv[0] = iron;
    const float dI = heldTool(wielder) ? heldTool(wielder)->damage : 0.f;
    wielder.inv[0] = copper;
    const float dC = heldTool(wielder) ? heldTool(wielder)->damage : 0.f;
    CHECK(dI > 0.f && dC > 0.f, "mixed %s deals damage in the hand (%.1f)", k.kind, dI);
    if (toolOf(iron).damage > toolOf(copper).damage)
      CHECK(dI > dC, "iron mixed %s hits harder than copper (%.1f > %.1f)", k.kind, dI, dC);
    iron.tag["qual"] = "legendary";
    copper.tag["qual"] = "poor";
    wielder.inv[0] = iron;
    const float leg = heldTool(wielder) ? heldTool(wielder)->damage : 0.f;
    wielder.inv[0] = copper;
    const float poorD = heldTool(wielder) ? heldTool(wielder)->damage : 0.f;
    CHECK(leg > poorD * 1.2f, "a legendary mixed %s outhits a poor copper one (%.1f vs %.1f)", k.kind, leg, poorD);
  }

  printf("mixed armour follows the ore, and a two-hand guard sits in front\n");
  ItemStack helmI, helmC, chestI, legsI, bootsI;
  CHECK(makePartArmor("helmet", std::vector<std::string>(5, "iron"), helmI) &&
        makePartArmor("helmet", std::vector<std::string>(5, "copper"), helmC) &&
        makePartArmor("chestplate", std::vector<std::string>(8, "iron"), chestI) &&
        makePartArmor("leggings", std::vector<std::string>(7, "iron"), legsI) &&
        makePartArmor("boots", std::vector<std::string>(4, "iron"), bootsI),
        "mixed helmet, chest, legs and boots craft");
  CHECK(armorPointsOf(chestI) > armorPointsOf(helmI), "the chestplate is the heaviest piece");
  ItemStack chestC;
  CHECK(makePartArmor("chestplate", std::vector<std::string>(8, "copper"), chestC), "a copper chestplate mixes");
  CHECK(armorPointsOf(chestI) > armorPointsOf(chestC), "an iron chestplate turns more than a copper one (%.1f > %.1f)",
        armorPointsOf(chestI), armorPointsOf(chestC));
  ItemStack swordMix, swordIron, swordDia;
  CHECK(makePartTool("sword", {"iron", "iron"}, "stick", swordIron) &&
        makePartTool("sword", {"diamond", "diamond"}, "stick", swordDia) &&
        makePartTool("sword", {"iron", "diamond"}, "stick", swordMix),
        "iron, diamond, and a mix of both make swords");
  CHECK(toolOf(swordDia).damage > toolOf(swordIron).damage &&
        toolOf(swordMix).damage > toolOf(swordIron).damage &&
        toolOf(swordMix).damage < toolOf(swordDia).damage,
        "a mixed iron-diamond blade sits between the two (%.1f / %.1f / %.1f)",
        toolOf(swordIron).damage, toolOf(swordMix).damage, toolOf(swordDia).damage);
  Player strong;
  strong.statStr = 40;
  CHECK(strDamageFactor(strong) > 1.2f, "Strength raises melee damage (x%.2f)", strDamageFactor(strong));
  helmI.tag["qual"] = "legendary";
  helmC.tag["qual"] = "poor";
  const float legMix = armorPointsOf(helmI) * gearQualityFactor(helmI);
  const float poorMix = armorPointsOf(helmC) * gearQualityFactor(helmC);
  CHECK(legMix > poorMix, "legendary iron helmet's mix protects more than a poor copper one (%.2f > %.2f)",
        legMix, poorMix);

  Player blocker;
  blocker.pos = vec3(0);
  blocker.yaw = 0;
  blocker.blockT = 0.2f;
  blocker.shield[0] = ItemStack("shield", 1);
  ShieldBox raised;
  CHECK(playerGuardBox(blocker, raised), "a raised shield has a box");
  CHECK(raised.c.z < -0.35f, "that shield sits in front (z=%.2f)", raised.c.z);
  CHECK(bladeSweepHitsShield(MV_SWORD1, 0, 1, blocker.pos, 0, 0, raised, 1.f),
        "a sword swing meets a raised shield in front");
  ShieldBox behind = raised;
  behind.c.z = 1.2f;
  CHECK(!bladeSweepHitsShield(MV_SWORD1, 0, 1, blocker.pos, 0, 0, behind, 1.f),
        "the same swing misses a shield behind you");

  ItemStack gs;
  CHECK(makePartTool("greatsword", {"iron", "iron", "iron", "iron", "iron", "iron"}, "stick", gs), "iron greatsword for the guard");
  blocker.inv[0] = gs;
  blocker.selected = 0;
  blocker.shield[0] = ItemStack("shield", 1);
  ShieldBox twoHand;
  CHECK(playerGuardBox(blocker, twoHand), "a two-hand guard with a slung shield still has a box");
  CHECK(std::fabs(twoHand.c.x) < 0.35f && twoHand.c.z < -0.35f,
        "the two-hand guard sits in front, not on the hip (%.2f, %.2f)", twoHand.c.x, twoHand.c.z);
  CHECK(bladeSweepHitsShield(MV_GS1, 0, 1, blocker.pos, 0, 0, twoHand, 1.15f),
        "a greatsword cut meets that two-hand guard");

  Player swordGuard;
  swordGuard.pos = vec3(0);
  swordGuard.yaw = 0;
  swordGuard.blockT = 0.2f;
  swordGuard.inv[0] = ItemStack("iron_sword", 1);
  ShieldBox oneHand;
  CHECK(playerGuardBox(swordGuard, oneHand), "a one-hand sword guard has a box");
  CHECK(bladeSweepHitsShield(MV_SWORD1, 0, 1, swordGuard.pos, 0, 0, oneHand, 1.f),
        "a sword swing can be caught on a sword guard");

  PlayerLimbBox limbs[6];
  const int nL = playerLimbBoxes(blocker, limbs);
  CHECK(nL == 6 && limbs[0].armorSlot == 0 && limbs[1].armorSlot == 1 && limbs[4].armorSlot == 2,
        "limb boxes map head, chest and legs onto armour slots");
  CHECK(limbs[0].lo.y > limbs[4].hi.y, "the head box sits above the legs");

  printf("an off-hand cut is the main hand's, mirrored\n");
  {
    const vec3 feet(0);
    vec3 h, tp; bladeAt(MV_SWORD1, attackMove(MV_SWORD1).hitFrom, h, tp);
    const vec3 fistR = attackFrameToWorld(h, feet, 0, 0);
    vec3 hL = h; hL.x = -hL.x;
    const vec3 fistL = attackFrameToWorld(hL, feet, 0, 0);
    const vec3 half(0.10f, 0.10f, 0.10f);
    CHECK(std::fabs(fistR.x - fistL.x) > 0.3f, "the off-hand fist is on the other side (R %.2f, L %.2f)", fistR.x, fistL.x);
    CHECK(bladeSweepHits(MV_SWORD1, 0, 1, feet, 0, 0, fistR - half, fistR + half, 1.f),
          "the main-hand cut meets a box on its own fist");
    CHECK(bladeSweepHits(MV_SWORD1, 0, 1, feet, 0, 0, fistL - half, fistL + half, 1.f, nullptr, true),
          "the off-hand cut meets a box on the other fist");
  }

  printf(fails ? "%d failed\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
