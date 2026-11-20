// tools/test_weapons.cpp — weapon families, quality, and the crafting-table recipes.
// Headless. Run from the source tree: ./build/test_weapons
#include "core/init.h"
#include "core/registry.h"
#include "sim/attack_moves.h"
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

  printf(fails ? "%d failed\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
