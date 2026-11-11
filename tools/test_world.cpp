// tools/test_world.cpp — headless checks for the world (after TerraFirmaCraft): rock layers and where the ores lie,
// cave-ins and support beams, the new crops and plants. Run: build/test_world (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/rules.h"
#include "sim/events.h"
#include "sim/cavein.h"
#include "sim/husbandry.h"
#include "sim/machines.h"
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include <thread>
#include <map>
#include <set>
#include <cstdio>
#include <cmath>

using namespace dq;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }

struct Rig {
  ThreadPool pool{2}; EventBus bus; std::unique_ptr<Level> lv; Player* P = nullptr; int Y = 0;
  Rig(const std::string& name, bool immersiveOn, bool setRule) {
    LevelInfo li; li.name = name; li.seedText = "immersive";
    if (setRule) li.rules = json{{"immersive", immersiveOn}, {"_v", 1}};
    lv = Level::create(userSub("saves"), li, &pool, &bus);
    World& w = lv->world();
    Y = dimBase(DIM_OVERWORLD) + 200;
    auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 8.5f); P = static_cast<Player*>(lv->spawn(std::move(p)));
    double t0 = nowSeconds();
    while (nowSeconds() - t0 < 60) {
      w.stream({{P->pos, 2, false}}, 20);
      bool all = true;
      for (int cx = -1; cx <= 1; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
      if (all) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const BlockId stone = reg().blockId("stone");
    for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 12; y++) w.setBlock(x, y, z, AIR); }
    P->onGround = true;
  }
  void tick(double secs) {   // (keeps the test's chunks streamed in, or a long wait could see them unloaded and made afresh)
    double since = 1;
    for (double t = 0; t < secs; t += TICK_DT) { lv->tick(TICK_DT); if ((since += TICK_DT) >= 1) { since = 0; lv->world().stream({{P->pos, 2, false}}, 20); } }
  }
  void fall(double secs) { lv->tick(TICK_DT); for (double t = 0; t < secs; t += 1 / 60.0) P->move(lv->world(), PlayerInput{}, 1 / 60.f, MoveOpts{}); }
};

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-world").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  Rig rg("World", true, true);
  World& w = rg.lv->world();

  printf("[cave-ins]\n");
  {
    const BlockId STONE = reg().blockId("stone"), BEAM = reg().blockId("support_beam");
    check(isRock(STONE) && !isRock(reg().blockId("stone_bricks")) && !isRock(reg().blockId("log")), "raw rock is rock; bricks and logs aren't");
    // a cave 9 wide, its roof of stone
    const int Y0 = rg.Y + 2;
    for (int x = -2; x <= 12; x++) for (int z = -2; z <= 12; z++) { w.setBlock(x, Y0 + 3, z, STONE); w.setBlock(x, Y0 + 4, z, STONE); for (int y = Y0; y < Y0 + 3; y++) w.setBlock(x, y, z, AIR); }
    check(caveInRisk(w, 5, Y0 + 2, 5), "a wide open cave with no supports is at risk");
    w.setBlock(3, Y0, 5, BEAM); w.setBlock(3, Y0 + 1, 5, BEAM); w.setBlock(3, Y0 + 2, 5, BEAM);
    check(!caveInRisk(w, 5, Y0 + 2, 5), "a support beam within four blocks makes it safe");
    for (int y = Y0; y < Y0 + 3; y++) w.setBlock(3, y, 5, AIR);
    const int fell = caveIn(*rg.lv, w, 5, Y0 + 2, 5, 0.0f);
    check(fell >= 1 && fell <= 3, "an unlucky blow: a little rubble falls (" + std::to_string(fell) + ")");
    check(caveIn(*rg.lv, w, 5, Y0 + 2, 5, 0.5f) == 0, "mostly nothing happens");
    // a narrow tunnel: no risk
    for (int x = 18; x <= 32; x++) for (int z = 15; z <= 29; z++) for (int y = Y0 - 1; y <= Y0 + 4; y++) w.setBlock(x, y, z, (z == 22 && y >= Y0 && y < Y0 + 3) ? AIR : STONE);   // (solid rock all round)
    check(!caveInRisk(w, 25, Y0 + 2, 22), "a one-wide tunnel holds itself up");
  }

  printf("[rock layers]\n");
  {
    check(rg.lv->info.genVersion >= 12, "a new world is made with rock layers (generation " + std::to_string(rg.lv->info.genVersion) + ")");
    std::map<std::string, int> rocks; int zinc = 0, zincHosted = 0;
    const std::set<std::string> META = {"marble", "slate", "gneiss", "schist", "quartzite"};
    for (int ox : {3000, 6000}) {
      const vec3 at(ox + 8, dimBase(DIM_OVERWORLD) + 100, ox + 8);
      for (int k = 0; k < 600; k++) { w.stream({{at, 2, false}, {rg.P->pos, 2, false}}, 40); bool all = true; for (int cx = -1; cx <= 0; cx++) for (int cz = -1; cz <= 0; cz++) if (!w.chunk(DIM_OVERWORLD, (ox >> 4) + cx, (ox >> 4) + cz)) all = false; if (all) break; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
      for (int x = ox - 16; x < ox + 16; x++) for (int z = ox - 16; z < ox + 16; z++) for (int y = dimBase(DIM_OVERWORLD) + 1; y < dimBase(DIM_OVERWORLD) + 200; y++) {
        const BlockId b = w.getBlock(x, y, z);
        if (!b) continue;
        const std::string& k = reg().block(b).key;
        if (isRock(b)) rocks[k]++;
        if (k == "zinc_ore") {
          zinc++;
          for (auto& n : {ivec3(1, 0, 0), ivec3(-1, 0, 0), ivec3(0, 1, 0), ivec3(0, -1, 0), ivec3(0, 0, 1), ivec3(0, 0, -1)}) if (META.count(reg().block(w.getBlock(x + n.x, y + n.y, z + n.z)).key)) { zincHosted++; break; }
        }
      }
    }
    std::string list; int kinds = 0; for (auto& [k, n] : rocks) if (n > 200) { kinds++; list += k + " "; }
    check(kinds >= 4, "several kinds of rock in layers: " + list);
    check(rocks["stone"] < 2000, "hardly any plain stone left (" + std::to_string(rocks["stone"]) + ")");
    check(zinc == 0 || zincHosted * 10 >= zinc * 8, "zinc lies in metamorphic rock (" + std::to_string(zincHosted) + " of " + std::to_string(zinc) + ")");
  }

  printf("[crops and plants]\n");
  {
    bool crops = true;
    for (const char* c : {"barley", "oat", "rye", "jute", "garlic", "soybean", "green_beans", "squash"}) {
      const BlockId b = reg().blockId(std::string(c) + "_crop");
      const json& cp = b ? reg().block(b).props["crop"] : json();
      const std::string seed = cp.is_object() ? cp.value("seed", "") : "";
      const ItemDef* sd = reg().item(seed);
      if (!b || !sd || (sd->block != std::string(c) + "_crop" && sd->str("plants") != std::string(c) + "_crop") || !reg().item(cp.value("drop", ""))) { crops = false; printf("    %s\n", c); }
    }
    check(crops, "TFC's crops: barley, oat, rye, jute, garlic, soybean, green beans, squash, each planted from its seed");
    bool bushes = true;
    for (const char* b : {"blueberry", "raspberry", "blackberry", "snowberry", "cloudberry", "cranberry", "elderberry", "gooseberry", "bunchberry", "wintergreen"}) {
      const BlockId full = reg().blockId(std::string(b) + "_bush"), empty = reg().blockId(std::string(b) + "_bush_empty");
      if (!full || !empty || reg().block(empty).str("refill") != std::string(b) + "_bush" || !reg().item(reg().block(full).str("harvest")) || !reg().tTick[empty]) { bushes = false; printf("    %s\n", b); }
    }
    check(bushes, "ten berry bushes, picked and bearing again");
    check(reg().blockId("plum_leaves") && reg().block(reg().blockId("plum_leaves")).str("fruit") == "plum" && reg().blockId("plum_sapling"), "fruit trees: plum leaves bear plums, from a plum sapling");
    int newBush = 0, fruit = 0;
    for (int ox : {3000, 6000}) for (int x = ox - 16; x < ox + 16; x++) for (int z = ox - 16; z < ox + 16; z++) for (int y = dimBase(DIM_OVERWORLD) + 40; y < dimBase(DIM_OVERWORLD) + 200; y++) {
      const std::string& k = reg().block(w.getBlock(x, y, z)).key;
      if (k.size() > 5 && k.compare(k.size() - 5, 5, "_bush") == 0 && k != "berry_bush" && k != "dead_bush") newBush++;
      if (k == "banana_leaves" || k == "lemon_leaves" || k == "olive_leaves" || k == "orange_leaves" || k == "peach_leaves" || k == "plum_leaves") fruit++;
    }
    printf("    (in the generated land: %d of the new bushes, %d fruit leaves)\n", newBush, fruit);
  }

  printf("[husbandry]\n");
  {
    Creature* a = spawnCreature(*rg.lv, 0, "cow", vec3(4.5f, rg.Y + 1, 4.5f));
    Creature* b = spawnCreature(*rg.lv, 0, "cow", vec3(5.5f, rg.Y + 1, 4.5f));
    check(a && b, "two cows");
    if (a && b) {
      setMale(*a, true); setMale(*b, false);
      check(isMale(*a) && !isMale(*b), "a bull and a cow");
      check(familiarity(*a) == 0 && !canBreed(*rg.lv, *a, *b), "strangers to you: no calves");
      check(husbandryFeed(*rg.lv, *a) && !husbandryFeed(*rg.lv, *a), "fed once a day (not twice)");
      for (int d = 1; d <= 3; d++) { rg.lv->info.day += 1; husbandryFeed(*rg.lv, *a); husbandryFeed(*rg.lv, *b); }
      check(familiarity(*a) > 0.5f && canBreed(*rg.lv, *a, *b), "fed day after day they come to know you, and breed");
      setMale(*b, true);
      check(!canBreed(*rg.lv, *a, *b), "two bulls don't");
      setMale(*b, false);
      rg.lv->info.day += 6; husbandryDaily(*rg.lv, *a);
      check(familiarity(*a) < 0.6f, "left alone a week, it forgets a little (" + std::to_string(familiarity(*a)) + ")");
      check(husbandryLabel(*a).find("Bull") != std::string::npos || husbandryLabel(*a).find("Male") != std::string::npos, "on look: " + husbandryLabel(*a));
    }
  }

  printf("[machines]\n");
  {
    const BlockId WM = reg().blockId("windmill"), AX = reg().blockId("axle"), GB = reg().blockId("gearbox"), Q = reg().blockId("quern"), RB = reg().blockId("redstone_block");
    check(GB && Q && reg().block(Q).str("mech") == "quern", "the gearbox; the quern is a machine");
    const int X = 60, Y = rg.Y + 12, Z = 60;
    for (int k = 0; k < 600; k++) { w.stream({{vec3(X, Y, Z), 2, false}, {rg.P->pos, 2, false}}, 40); if (w.isLoaded(X + 4, Y, Z + 4) && w.isLoaded(X - 4, Y, Z - 4)) break; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    for (int x = X - 6; x <= X + 6; x++) for (int y = Y - 6; y <= Y + 6; y++) for (int z = Z - 6; z <= Z + 6; z++) w.setBlock(x, y, z, AIR);
    w.setBlock(X, Y, Z, WM, 1);            // sails facing east-west, high in the open air
    w.setBlock(X + 1, Y, Z, AX, 1);        // an axle east
    w.setBlock(X + 2, Y, Z, GB);           // a gearbox turning it...
    w.setBlock(X + 2, Y, Z + 1, AX, 2);    // ...south through a right angle
    w.setBlock(X + 2, Y, Z + 2, Q);        // into a quern
    json* e = w.blockEntity(X + 2, Y, Z + 2, true); *e = json{{"type", "machine"}, {"items", {ItemStack("wheat", 8).toJson(), nullptr}}, {"progress", 0}};
    rg.tick(60);
    e = w.blockEntity(X + 2, Y, Z + 2);
    const ItemStack out = e ? ItemStack::fromJson((*e)["items"][1]) : ItemStack();
    Mechanics* M = mechanicsOf(*rg.lv, 0);
    check(M && M->speedAt(X + 2, Y, Z + 2) > 0.05f, "the windmill turns the quern through the gearbox (speed " + std::to_string(M ? M->speedAt(X + 2, Y, Z + 2) : 0.f) + ")");
    check(out.id == "flour" && out.n >= 1, "  ...and it grinds the wheat on its own (" + std::to_string(out.n) + " flour)");
    w.setBlock(X + 2, Y + 1, Z, RB);       // a redstone signal on the gearbox
    rg.tick(25);
    check(M && M->speedAt(X + 2, Y, Z + 2) < 0.05f, "a powered gearbox lets go: the quern stops");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  rg.lv->close();
  return fails;
}
