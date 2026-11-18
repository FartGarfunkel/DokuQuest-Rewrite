// tools/test_catchup.cpp — headless checks that time keeps passing where nobody is (sim/catchup.h): crops, stems, cane,
// saplings, drips, furnaces, hoppers and the young make up the time their chunk was away (unloaded, or loaded but out
// of the simulation distance, or a night slept through), statistically and spread over the ticks.
// Run: build/test_catchup [--userdir dir] (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/rules.h"
#include "sim/events.h"
#include "sim/catchup.h"
#include "sim/worldsys.h"
#include "sim/creature.h"
#include "sim/cave_drips.h"
#include "sim/creature.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include "world/dirs.h"
#include <thread>
#include <cstdio>
#include <cmath>

using namespace dq;
static int fails = 0, passes = 0;
static bool OFF = false;   // --off: the catch-up turned off (the world as it was: what's away stands still)
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); fflush(stdout); (ok ? passes : fails)++; }

struct Rig {
  ThreadPool pool{2}; EventBus bus; std::unique_ptr<Level> lv; Player* P = nullptr; int Y = 0;
  vec3 home;
  explicit Rig(const std::string& name) {
    LevelInfo li; li.name = name; li.seedText = "catchup";
    li.rules = json{{"immersive", false}, {"_v", 1}, {"mobSpawning", false}, {"seasons", false}, {"weatherCycle", false}, {"catchUp", !OFF}};
    lv = Level::create(userSub("saves"), li, &pool, &bus);
    World& w = lv->world();
    Y = dimBase(DIM_OVERWORLD) + 200;
    home = vec3(8.5f, Y + 1, 8.5f);
    auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = home; P = static_cast<Player*>(lv->spawn(std::move(p)));
    streamUntil([&] { for (int cx = -1; cx <= 2; cx++) for (int cz = -1; cz <= 2; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) return false; return true; }, 3);
    const BlockId stone = reg().blockId("stone");
    for (int x = -8; x < 40; x++) for (int z = -8; z < 40; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 14; y++) w.setBlock(x, y, z, AIR); }
    P->onGround = true;
  }
  template <class F> void streamUntil(F done, int radius) {
    double t0 = nowSeconds();
    while (nowSeconds() - t0 < 90) {
      lv->world().stream({{P->pos, radius, false}}, 20);
      if (done()) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    printf("(streaming timed out:");
    for (auto& [k, c] : lv->world().chunks()) if (k.dim == DIM_OVERWORLD && std::abs(k.cx) < 6 && std::abs(k.cz) < 6) printf(" %d,%d", k.cx, k.cz);
    printf(")\n");
  }
  void tick(double secs, int radius = 3) {
    double since = 1;
    for (double t = 0; t < secs; t += TICK_DT) { lv->tick(TICK_DT); if ((since += TICK_DT) >= 1) { since = 0; lv->world().stream({{P->pos, radius, false}}, 20); } }
  }
  // the player goes far off: the farm's chunks unload (saved); `ticked` seconds pass ticking, `skipped` more go by at once
  void away(double ticked, double skipped) {
    tick(2);   // (the stamps are written)
    World& w = lv->world();
    P->pos = home + vec3(3000, 0, 0);
    streamUntil([&] { return !w.chunk(DIM_OVERWORLD, 0, 0) && !w.chunk(DIM_OVERWORLD, 1, 1); }, 2);
    check(!w.chunk(DIM_OVERWORLD, 0, 0), "(the farm's chunk is unloaded while away)");
    tick(ticked, 2);
    advanceSimClock(*lv, skipped);
    P->pos = home;
    streamUntil([&] { for (int cx = -1; cx <= 2; cx++) for (int cz = -1; cz <= 2; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) return false; return true; }, 3);
    tick(3);
    catchUpFlush(*lv);
  }
};

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-catchup").string();
  for (int i = 1; i < argc; i++) { if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i]; else if (std::string(argv[i]) == "--off") OFF = true; }
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  const Registry& R = reg();
  auto id = [&](const char* k) { BlockId b = R.blockId(k); if (!b) printf("(no block %s)\n", k); return b; };
  const BlockId FARM = id("farmland"), WATER = id("water"), WHEAT = id("wheat_crop"), MSTEM = id("melon_stem"), MELON = id("melon"),
    DIRT = id("dirt"), GRASS = id("grass"), SAND = id("sand"), CANE = id("sugar_cane"), SAP = id("sapling"), FURN = id("furnace"),
    CHEST = id("chest"), HOP = id("hopper"), DS = id("pointed_dripstone"), CAUL = id("cauldron"), ST = id("stone");
  double DAY = 0;

  printf("[the clock]\n");
  {
    Rig rg("Clock");
    DAY = playSecondsBetween(*rg.lv, 0, DAY_LEN);
    check(std::fabs(DAY - (60 + 10) * 60) < 1, "a whole day is the day's and the night's minutes (" + std::to_string(DAY) + " s)");
    const double night = playSecondsBetween(*rg.lv, DAY_LEN * 0.75, DAY_LEN * 0.02);
    check(std::fabs(night - (300 + 24 * 6)) < 1, "sleeping from the middle of the night to morning passes the rest of the night (" + std::to_string(night) + " s)");
    const double c0 = simClock(*rg.lv);
    rg.tick(1);
    check(simClock(*rg.lv) - c0 > 0.9 && simClock(*rg.lv) - c0 < 1.1, "the simulation clock runs with the ticks");
  }

  Rig rg("Farm");
  World& w = rg.lv->world();
  const int Y = rg.Y, G = Y + 1;   // the ground's top layer, things stand at G + 1

  // a wheat patch by water, a ripe melon stem with room for its fruit, cane by water, a sapling, a drip over a cauldron
  w.setBlock(2, G, 2, WATER, 0);
  for (int x = 3; x <= 5; x++) { w.setBlock(x, G, 2, FARM, 7); w.setBlock(x, G + 1, 2, WHEAT, 0); }
  w.setBlock(2, G, 5, WATER, 0); w.setBlock(3, G, 5, FARM, 7); w.setBlock(3, G + 1, 5, MSTEM, 7); w.setBlock(4, G, 5, DIRT, 0);
  w.setBlock(6, G, 6, WATER, 0); w.setBlock(7, G, 6, SAND, 0); w.setBlock(7, G + 1, 6, CANE, 0);
  w.setBlock(10, G, 10, GRASS, 0); w.setBlock(10, G + 1, 10, SAP, 0);
  // the drip: water over stone, a stalactite, a cauldron 3 below
  w.setBlock(12, G + 8, 3, WATER); w.setBlock(12, G + 7, 3, ST); w.setBlock(12, G + 6, 3, DS, 1); w.setBlock(12, G + 1, 3, CAUL, 0);
  // a furnace with 8 ore and fuel
  std::string ore = !smeltResult("iron_ore", "furnace").empty() ? "iron_ore" : "cobblestone";
  const std::string ingot = smeltResult(ore, "furnace");
  w.setBlock(14, G + 1, 8, FURN, 0);
  w.setBlockEntity(14, G + 1, 8, json{{"type", "furnace"}, {"items", json::array({ItemStack(ore, 8).toJson(), ItemStack("coal", 4).toJson(), nullptr})}, {"burn", 0}, {"burnMax", 0}, {"cook", 0}});
  // a hopper chain: chest A over hopper 1, which feeds hopper 2 beside it, which drops into chest B
  w.setBlock(20, G + 3, 8, CHEST, 0); w.setBlockEntity(20, G + 3, 8, json{{"type", "chest"}, {"items", json::array({ItemStack("cobblestone", 64).toJson(), ItemStack("dirt", 16).toJson()})}});
  w.setBlock(20, G + 2, 8, HOP, 5);    // spout east
  w.setBlock(21, G + 2, 8, HOP, 0);    // spout down
  w.setBlock(21, G + 1, 8, CHEST, 0); w.setBlockEntity(21, G + 1, 8, json{{"type", "chest"}, {"items", json::array()}});
  // a calf
  Creature* calf = spawnCreature(*rg.lv, 0, "cow", vec3(16.5f, G + 1, 16.5f));
  if (calf) { calf->makeBaby(); calf->growT = 600; calf->persistent = true; }
  const u32 calfId = calf ? calf->id : 0;
  rg.tick(1);
  check(w.getMeta(3, G + 1, 2) < 7, "(the wheat starts young)");

  printf("[unloaded for three days, two minutes of them ticking]\n");
  const auto stats0 = catchUpStats(*rg.lv);
  rg.away(120, 3 * DAY);
  int ripe = 0; for (int x = 3; x <= 5; x++) ripe += (w.getBlock(x, G + 1, 2) == WHEAT && (w.getMeta(x, G + 1, 2) & 15) == 7);
  check(ripe == 3, "the wheat grew ripe while nobody was there (" + std::to_string(ripe) + " of 3)");
  int melons = 0; for (int d = 0; d < 4; d++) melons += w.getBlock(3 + DIRX[d], G + 1, 5 + DIRZ[d]) == MELON;
  check(melons == 1, "the ripe stem set one melon on the free spot beside it");
  int cane = 0; while (cane < 5 && w.getBlock(7, G + 1 + cane, 6) == CANE) cane++;
  check(cane == 3, "the sugar cane grew to its full height (" + std::to_string(cane) + ")");
  check(w.getBlock(10, G + 1, 10) != SAP && w.getBlock(10, G + 2, 10) != AIR, "the sapling grew into a tree");
  check(cauldronWater(w.getMeta(12, G + 1, 3)) == 3, "the cauldron under the stalactite filled (" + std::to_string(cauldronWater(w.getMeta(12, G + 1, 3))) + ")");
  {
    json* f = w.blockEntity(14, G + 1, 8);
    ItemStack out = f ? ItemStack::fromJson((*f)["items"][2]) : ItemStack(), in = f ? ItemStack::fromJson((*f)["items"][0]) : ItemStack();
    check(out.id == ingot && out.n == 8 && in.empty(), "the furnace smelted all 8 " + ore + " into " + std::to_string(out.n) + " " + out.id);
  }
  {
    json* b = w.blockEntity(21, G + 1, 8); json* a = w.blockEntity(20, G + 3, 8);
    int nb = 0, na = 0;
    if (b) for (auto& s : (*b)["items"]) nb += ItemStack::fromJson(s).n;
    if (a) for (auto& s : (*a)["items"]) na += ItemStack::fromJson(s).n;
    check(nb == 80 && na == 0, "the hopper chain emptied chest A into chest B (" + std::to_string(na) + " left, " + std::to_string(nb) + " moved)");
  }
  {
    Creature* c = dynamic_cast<Creature*>(rg.lv->entity(calfId));
    check(c && !c->baby, "the calf grew up while away");
  }
  const auto& stats = catchUpStats(*rg.lv);
  printf("(catch-up: %ld chunks, %ld growth events, %ld hopper moves, %ld machine steps, %.2f ms in all, longest tick %.2f ms)\n",
         stats.chunks - stats0.chunks, stats.events - stats0.events, stats.hopperMoves - stats0.hopperMoves, stats.machineSteps - stats0.machineSteps, stats.totalMs, stats.maxTickMs);

  printf("[loaded, but beyond the simulation distance]\n");
  {
    rg.lv->info.rules["simulationDistance"] = 2;
    for (int x = 3; x <= 5; x++) w.setBlock(x, G + 1, 2, WHEAT, 0);
    rg.tick(2);
    rg.P->pos = rg.home + vec3(16 * 5, 0, 0);   // five chunks off: still loaded (radius 7), not ticking (2)
    rg.tick(60, 7);
    check(w.chunk(DIM_OVERWORLD, 0, 0) != nullptr, "(the farm stays loaded)");
    advanceSimClock(*rg.lv, 2 * DAY);
    rg.tick(3, 7);
    int grown = 0; for (int x = 3; x <= 5; x++) grown += (w.getMeta(x, G + 1, 2) & 15);
    check(grown == 0, "out of range nothing grows there live (" + std::to_string(grown) + ")");
    rg.P->pos = rg.home;
    rg.tick(3, 7);
    catchUpFlush(*rg.lv);
    ripe = 0; for (int x = 3; x <= 5; x++) ripe += (w.getMeta(x, G + 1, 2) & 15) == 7;
    check(ripe == 3, "back in range, the time it was out of range is made up (" + std::to_string(ripe) + " of 3 ripe)");
    rg.lv->info.rules.erase("simulationDistance");
  }

  printf("[a night slept away]\n");
  {
    for (int x = 3; x <= 5; x++) w.setBlock(x, G + 1, 2, WHEAT, 0);
    rg.tick(2);
    advanceSimClock(*rg.lv, 2 * DAY);   // (what sleeping does: sim/blockfx.cpp)
    rg.tick(2);
    catchUpFlush(*rg.lv);
    int grown = 0; for (int x = 3; x <= 5; x++) grown += (w.getMeta(x, G + 1, 2) & 15) == 7;
    check(grown == 3, "the crops by the bed grew through the night that was skipped (" + std::to_string(grown) + " of 3)");
  }

  printf("[an old save: no stamps, nothing owed]\n");
  {
    for (int x = 3; x <= 5; x++) w.setBlock(x, G + 1, 2, WHEAT, 0);
    rg.lv->info.rules["catchUp"] = false;
    rg.tick(2);
    rg.lv->info.rules["catchUp"] = !OFF;
    Chunk* c = w.chunk(DIM_OVERWORLD, 0, 0);
    c->extra.erase("simT"); c->extra.erase("simM");
    rg.lv->info.data["simClock"] = simClock(*rg.lv) + 5 * DAY;   // (time that chunk never knew about)
    for (auto& [k, ch] : w.chunks()) if (k.cx != 0 || k.cz != 0) { ch->extra["simT"] = simClock(*rg.lv); ch->extra["simM"] = simClock(*rg.lv); }
    rg.tick(2);
    catchUpFlush(*rg.lv);
    int grown = 0; for (int x = 3; x <= 5; x++) grown += (w.getMeta(x, G + 1, 2) & 15);
    check(grown <= 1, "a chunk saved before the stamps makes up nothing (" + std::to_string(grown) + ")");
  }

  printf("[a dark room far over your head]\n");
  {   // monsters come out in the dark wherever it is in the column, not only within a dozen blocks of your height
    const BlockId ST = reg().blockId("stone");
    const int F = rg.Y + 40;   // a dark room 40 blocks up: floor F, two air, roof F + 3
    for (int x = -30; x <= 46; x++) for (int z = -30; z <= 46; z++) { w.setBlock(x, F, z, ST); w.setBlock(x, F + 3, z, ST); }
    const std::string dc = rg.lv->info.rules.dump();
    rg.lv->info.rules["daylightCycle"] = false; rg.lv->info.rules["mobSpawning"] = true; rg.lv->info.time = DAY_LEN * 0.75;   // midnight
    rg.P->mode = "survival";
    int up = 0;
    for (int s = 0; s < 240 && up < 2; s++) {
      rg.tick(1, 3);
      up = 0;
      for (auto& e : rg.lv->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && c->T->hostile) {
        if (c->pos.y > F + 0.5f && c->pos.y < F + 3) up++;
        else c->removed = true;   // (the ones out on the roof in the night: out of the way, so the cap is the room's)
      }
    }
    check(up >= 2, "monsters come out in a dark room 40 blocks over the player's head (" + std::to_string(up) + ")");
    for (auto& e : rg.lv->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (c->T->hostile) c->removed = true;
    for (int x = -30; x <= 46; x++) for (int z = -30; z <= 46; z++) { w.setBlock(x, F, z, AIR); w.setBlock(x, F + 3, z, AIR); }
    rg.lv->info.rules = json::parse(dc); rg.lv->info.time = 300;
  }

  printf("[cost: a big farm comes back]\n");
  {
    // four chunks of wheat (32 x 32), 24 hoppers under chests: away a week, back; how long any one tick spends catching up
    for (int x = 0; x < 32; x++) for (int z = 16; z < 48; z++) {
      w.setBlock(x, G, z, (x % 9 == 4 && z % 9 == 4) ? WATER : FARM, 7);
      if (!(x % 9 == 4 && z % 9 == 4)) w.setBlock(x, G + 1, z, WHEAT, 0);
    }
    for (int k = 0; k < 24; k++) {
      const int x = 34 + (k % 6) * 1, z = 18 + (k / 6) * 2;
      w.setBlock(x, G + 3, z, CHEST, 0); w.setBlockEntity(x, G + 3, z, json{{"type", "chest"}, {"items", json::array({ItemStack("cobblestone", 64).toJson()})}});
      w.setBlock(x, G + 2, z, HOP, 0);
      w.setBlock(x, G + 1, z, CHEST, 0); w.setBlockEntity(x, G + 1, z, json{{"type", "chest"}, {"items", json::array()}});
    }
    rg.tick(2);
    catchUpStats(*rg.lv).maxTickMs = 0;
    const auto s0 = catchUpStats(*rg.lv);
    rg.tick(2);
    World& W = w;
    rg.P->pos = rg.home + vec3(3000, 0, 0);
    rg.streamUntil([&] { return !W.chunk(DIM_OVERWORLD, 0, 1) && !W.chunk(DIM_OVERWORLD, 1, 2); }, 2);
    advanceSimClock(*rg.lv, 7 * DAY);
    rg.P->pos = rg.home;
    rg.streamUntil([&] { for (int cx = -1; cx <= 2; cx++) for (int cz = -1; cz <= 3; cz++) if (!W.chunk(DIM_OVERWORLD, cx, cz)) return false; return true; }, 3);
    // tick by tick, timing the ticks: the catch-up's own share stays within its budget, so no tick spikes
    double worst = 0, sum = 0; int n = 0, ticksToDone = 0;
    for (int t = 0; t < 30 * 20; t++) {
      const double t0 = nowSeconds();
      rg.lv->tick(TICK_DT);
      const double ms = (nowSeconds() - t0) * 1000;
      worst = std::max(worst, ms); sum += ms; n++;
      if (t % 30 == 0) W.stream({{rg.P->pos, 3, false}}, 20);
      if (!ticksToDone && t > 40 && catchUpPending(*rg.lv) == 0) ticksToDone = t;
    }
    const auto& s1 = catchUpStats(*rg.lv);
    int ripeN = 0, total = 0;
    for (int x = 0; x < 32; x++) for (int z = 16; z < 48; z++) if (W.getBlock(x, G + 1, z) == WHEAT) { total++; ripeN += (W.getMeta(x, G + 1, z) & 15) == 7; }
    int moved = 0; for (int k = 0; k < 24; k++) { const int x = 34 + (k % 6), z = 18 + (k / 6) * 2; if (json* b = W.blockEntity(x, G + 1, z)) for (auto& s : (*b)["items"]) moved += ItemStack::fromJson(s).n; }
    printf("(a week away: %ld chunks caught up, %ld growth events, %ld hopper moves, done %d ticks after the land came back; "
           "catch-up's longest tick %.2f ms, longest stamp scan %.2f ms (%d chunks loaded); whole ticks: worst %.2f ms, mean %.2f ms)\n",
           s1.chunks - s0.chunks, s1.events - s0.events, s1.hopperMoves - s0.hopperMoves, ticksToDone, s1.maxTickMs, s1.maxScanMs, (int)W.chunks().size(), worst, sum / n);
    check(ripeN == total && total > 900, "the whole field is ripe (" + std::to_string(ripeN) + " of " + std::to_string(total) + ")");
    check(moved == 24 * 64, "every chest emptied through its hopper (" + std::to_string(moved) + ")");
    check(s1.maxTickMs < 4, "no tick spends more than a few milliseconds catching up (" + std::to_string(s1.maxTickMs) + " ms)");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  rg.lv->save(true);
  return fails;
}
