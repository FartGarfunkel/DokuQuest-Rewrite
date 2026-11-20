// tools/test_weapons.cpp — weapon families, quality, and the crafting-table recipes.
// Headless. Run from the source tree: ./build/test_weapons
#include "core/init.h"
#include "core/registry.h"
#include "sim/attack_moves.h"
#include "sim/weapon_quality.h"
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
  int nPoor = 0, nNormal = 0, nFine = 0;
  for (uint32_t seed = 0; seed < 100; seed++) {
    ItemStack s("iron_sword", 1);
    rollWeaponQuality(s, seed);
    const std::string q = s.tag.value("qual", "");
    nPoor += q == "poor"; nNormal += q == "normal"; nFine += q == "fine";
  }
  CHECK(nPoor == 25 && nNormal == 55 && nFine == 20, "a hundred seeds are 25 poor, 55 normal, 20 fine (got %d %d %d)",
        nPoor, nNormal, nFine);
  ItemStack pick("stone_pickaxe", 1);
  pick.tag = json::object();
  pick.tag["qual"] = "poor";
  CHECK(std::fabs(weaponQualityFactor(pick) - 1.f) < 0.001f, "a pick ignores weapon quality");
  CHECK(std::fabs(weaponQualityFactor(poor) - 0.85f) < 0.001f, "a poor sword's blow is scaled");

  printf("new weapons on the table\n");
  const Recipe* oldSword = nullptr;
  int oldShaped = 0;
  for (const Recipe& r : reg().recipes) {
    if (r.out == "iron_sword" && r.shaped()) { oldShaped++; if (r.props.value("classicOnly", false)) oldSword = &r; }
  }
  CHECK(oldShaped >= 1 && oldSword && oldSword->shape.size() == 3 && oldSword->shape[0] == "M" && oldSword->shape[1] == "M" && oldSword->shape[2] == "S"
        && oldSword->key.at('M') == "iron_ingot" && oldSword->key.at('S') == "stick",
        "the iron sword is still two ingots over a stick");
  auto family = [&](const char* key, Moveset want, double hands, int ingots, const char* mat) {
    const ItemDef* it = reg().item(key);
    CHECK(it && movesetOf(it) == want && it->num("hands") == hands, "%s is that family in %g hands", key, hands);
    const Recipe* rec = nullptr;
    for (const Recipe& r : reg().recipes) if (r.out == key && r.shaped()) rec = &r;
    int metals = 0, sticks = 0;
    if (rec) for (const std::string& row : rec->shape) for (char ch : row) {
      if (ch == 'M') metals++;
      if (ch == 'S') sticks++;
    }
    CHECK(rec && !rec->props.value("classicOnly", false) && metals == ingots && sticks == 1
          && rec->key.at('M') == mat && rec->key.at('S') == "stick"
          && rec->out.find("mold") == std::string::npos && rec->out.find("_head") == std::string::npos,
          "%s is a shaped table recipe of %s and a stick", key, mat);
  };
  family("iron_greatsword", Moveset::Greatsword, 2, 3, "iron_ingot");
  family("iron_spear", Moveset::Spear, 2, 2, "iron_ingot");
  family("iron_mace", Moveset::Mace, 1, 2, "iron_ingot");
  family("wood_greatsword", Moveset::Greatsword, 2, 3, "#planks");
  family("wood_spear", Moveset::Spear, 2, 2, "#planks");
  family("wood_mace", Moveset::Mace, 1, 2, "#planks");
  family("flint_greatsword", Moveset::Greatsword, 2, 3, "flint");
  family("flint_spear", Moveset::Spear, 2, 2, "flint");
  family("flint_mace", Moveset::Mace, 1, 2, "flint");
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
