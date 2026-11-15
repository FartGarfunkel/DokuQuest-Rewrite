// tools/test_deep.cpp — the deep world (docs/WORLD_DEPTH.md, layout 2): the column's layout, the land running on
// under the Overworld into the Underworld, its floor over the Nether, light, water and players across the seams,
// saving, the y shown; and a world from before it keeping its four bands. Run: build/test_deep (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/events.h"
#include "sim/content.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include "world/light.h"
#include <thread>
#include <cstdio>

using namespace dq;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }

static void streamUntil(World& w, vec3 at, const std::vector<std::pair<int, int>>& need, double secs = 90) {
  const double t0 = nowSeconds();
  while (nowSeconds() - t0 < secs) {
    w.stream({{at, 2, true}}, 20);
    bool all = true;
    for (auto [d, k] : need) for (int cx = -1; cx <= 1; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(d, (ifloor(at.x) >> 4) + cx, (ifloor(at.z) >> 4) + cz)) all = false;
    if (all) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-deep").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }

  printf("[the layouts]\n");
  {
    WorldLayout a = makeLayout(1), b = makeLayout(2, 4), c = makeLayout(2, 1);
    check(a.bands == 4 && a.posOf[DIM_END] == 0 && a.posOf[DIM_NETHER] == 1 && a.posOf[DIM_OVERWORLD] == 2 && a.posOf[DIM_SKYLANDS] == 3 && a.posOf[DIM_UNDER1] < 0, "layout 1: End, Nether, Overworld, Skylands (as ever)");
    check(b.bands == 10 && b.dimAt[0] == DIM_END && b.dimAt[1] == DIM_NETHER && b.dimAt[2] == DIM_UNDER4 && b.dimAt[5] == DIM_UNDER1 && b.dimAt[6] == DIM_OVERWORLD &&
          b.dimAt[7] == DIM_SKY1 && b.dimAt[8] == DIM_SKY2 && b.dimAt[9] == DIM_SKYLANDS, "layout 2: End, Nether, the Underworld deepest first, Overworld, open sky, Skylands");
    check(c.bands == 7 && c.posOf[DIM_UNDER1] == 2 && c.posOf[DIM_UNDER2] < 0, "a shallower Underworld: fewer bands");
    setWorldLayout(2, 4);
    bool round = true; for (int y = 0; y < WORLD_H; y += 7) { const int d = dimOfY(y); if (y < dimBase(d) || y >= dimBase(d) + DIM_H) round = false; }
    check(round && WORLD_H == 2560, "every y is in the band of its dimension; the column 2560 tall");
    check(dimBelow(DIM_OVERWORLD) == DIM_UNDER1 && dimAbove(DIM_OVERWORLD) == DIM_SKY1 && dimBelow(DIM_SKYLANDS) == DIM_SKY2 && dimAbove(DIM_NETHER) == DIM_UNDER4, "the bands above and below");
    check(displayY(dimBase(DIM_OVERWORLD) + SEA_LEVEL) == 63 && displayY(dimBase(DIM_OVERWORLD) - 64) == -65 && fromDisplayY(-58, DIM_OVERWORLD) == dimBase(DIM_OVERWORLD) - 57, "the y shown the familiar way (sea level 63, the Underworld below 0)");
    check(landFloorAt(dimBase(DIM_UNDER4)) && !landFloorAt(dimBase(DIM_OVERWORLD)) && !landFloorAt(dimBase(DIM_UNDER1)) && landRoofAt(dimBase(DIM_SKY2) + 255), "the seams between worlds: under the Underworld and over the open sky, not inside the Overworld's land");
    setWorldLayout(1);
    check(WORLD_H == 1024 && dimOfY(600) == DIM_OVERWORLD && dimBase(DIM_SKYLANDS) == 768 && displayY(512 + 64) == 64, "back to layout 1: exactly as before");
  }

  ThreadPool pool{2}; EventBus bus;
  printf("[a deep world]\n");
  std::string folder;
  {
    LevelInfo li; li.name = "Deep"; li.seedText = "deep";
    auto lv = Level::create(userSub("saves"), li, &pool, &bus);
    folder = lv->info.folder;
    check(lv->info.layout == 2 && worldLayout().id == 2 && lv->info.genVersion >= 19, "a new world is a deep one (layout 2, terrain version 19)");
    World& w = lv->world();
    const vec3 at(8.5f, (float)dimBase(DIM_OVERWORLD) + 2, 8.5f);
    streamUntil(w, at, {{DIM_OVERWORLD, 0}, {DIM_UNDER1, 0}});
    const BlockId bedrock = reg().blockId("bedrock"), crust = reg().blockId("ember_crust"), slate = reg().blockId("slate"), lava = reg().blockId("lava");
    int bed = 0, agree = 0, cols = 0, solidSeam = 0;
    const int ob = dimBase(DIM_OVERWORLD);
    for (int x = -16; x < 32; x++) for (int z = -16; z < 32; z++) {
      if (!w.isLoaded(x, ob, z) || !w.isLoaded(x, ob - 1, z)) continue;
      cols++;
      const BlockId a = w.getBlock(x, ob, z), b = w.getBlock(x, ob - 1, z);
      if (a == bedrock || a == crust || b == bedrock || b == crust) bed++;
      const bool sa = reg().tSolid[a], sb = reg().tSolid[b];
      if (sa == sb) agree++;
      if (sb && b == slate) solidSeam++;
    }
    check(cols > 1000 && bed == 0, strFormat("no bedrock or crust at the Overworld's old floor (%d of %d columns)", bed, cols));
    check(cols > 0 && agree > cols * 85 / 100 && solidSeam > cols / 2, strFormat("the land runs on through the seam: slate under it, solid over solid (%d of %d agree)", agree, cols));
    // the deep caves below y -55 hold lava
    int lavaLow = 0, lavaHigh = 0;
    for (int x = -16; x < 32; x++) for (int z = -16; z < 32; z++) for (int y = ob - 64; y < ob - 1; y++) if (w.getBlock(x, y, z) == lava) (displayY(y) <= -55 ? lavaLow : lavaHigh)++;
    check(lavaHigh == 0, strFormat("the lava of the deep caves lies at y -55 and below (%d below, %d above)", lavaLow, lavaHigh));
    // no sky down there
    int litDeep = 0, airDeep = 0;
    for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) for (int y = ob - 200; y < ob - 120; y++) if (w.getBlock(x, y, z) == AIR) { airDeep++; if ((w.getLight(x, y, z) >> 4) > 0) litDeep++; }
    check(airDeep > 0 && litDeep == 0, strFormat("no sunlight in the Underworld's caverns (%d of %d open cells lit)", litDeep, airDeep));
    // water poured over a hole in the floor runs on down into the Underworld
    {
      const BlockId water = reg().blockId("water"), stone = reg().blockId("stone");
      for (int y = ob - 6; y <= ob + 3; y++) for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) w.setBlock(4 + dx, y, 4 + dz, (dx || dz) ? stone : AIR);
      w.setBlock(4, ob - 6, 4, stone);
      w.setBlock(4, ob + 2, 4, water);
      for (int i = 0; i < 400; i++) { lv->tick(TICK_DT); w.stream({{at, 2, true}}, 5); }
      check(reg().tLiquid[w.getBlock(4, ob - 3, 4)] == 1, "water runs on down through the seam into the Underworld");
    }
    // a player falling through the Overworld's old floor: no shaft is dug to the Nether
    {
      auto p = std::make_unique<Player>(); p->name = "Faller"; p->pos = vec3(4.5f, ob + 1.2f, 4.5f);
      Player* P = static_cast<Player*>(lv->spawn(std::move(p)));
      const BlockId below = w.getBlock(4, ob - 7, 4);
      for (int i = 0; i < 120; i++) { lv->tick(TICK_DT); P->move(w, PlayerInput{}, (float)TICK_DT, MoveOpts{}); }
      check(w.getBlock(4, ob - 7, 4) == below && P->pos.y > ob - 8, strFormat("a body falls through the old floor and lands on the Underworld's rock (y %d), nothing dug", displayY(ifloor(P->pos.y))));
    }
    // the floor of the world
    {
      const vec3 deepAt(8.5f, (float)dimBase(DIM_UNDER4) + 3, 8.5f);
      streamUntil(w, deepAt, {{DIM_UNDER4, 0}});
      int bedN = 0, crustN = 0, lavaSea = 0, open = 0;
      const int fb = dimBase(DIM_UNDER4);
      for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) {
        crustN += w.getBlock(x, fb, z) == crust; bedN += w.getBlock(x, fb + 1, z) == bedrock || w.getBlock(x, fb + 1, z) != AIR;
        for (int y = fb + 5; y < fb + 22; y++) { const BlockId b = w.getBlock(x, y, z); if (b == lava) lavaSea++; else if (b == AIR) open++; }
      }
      check(crustN == 256, "the Ember Crust over the Nether at the bottom of the Underworld");
      check(lavaSea > 0 && open == 0, strFormat("a sea of lava fills the deepest caverns (%d lava, %d open)", lavaSea, open));
    }
    // the caverns keep their plants: nothing washes them off and drops them (it piled up items until the game crawled)
    {
      const vec3 cav(40.5f, (float)dimBase(DIM_UNDER1) + 150, 40.5f);
      streamUntil(w, cav, {{DIM_UNDER1, 0}, {DIM_UNDER2, 0}});
      for (int i = 0; i < 1200; i++) { lv->tick(TICK_DT); if (i % 20 == 0) w.stream({{cav, 3, true}}, 5); }
      int items = 0; for (auto& e : lv->entities()) if (!e->removed && e->type == "item") items++;
      check(items < 20, strFormat("a minute in the Underworld drops next to nothing on its own (%d items)", items));
    }
    // something built down there is kept
    const int ty = dimBase(DIM_UNDER2) + 100;
    streamUntil(w, vec3(8.5f, (float)ty, 8.5f), {{DIM_UNDER2, 0}});
    w.setBlock(8, ty, 8, reg().blockId("gold_block"));
    lv->save(); lv->close();
  }
  {
    auto lv = Level::open(userSub("saves") / folder, &pool, &bus);
    check(lv && lv->info.layout == 2 && worldLayout().id == 2, "opened again: still a deep world");
    if (lv) {
      World& w = lv->world();
      const int ty = dimBase(DIM_UNDER2) + 100;
      streamUntil(w, vec3(8.5f, (float)ty, 8.5f), {{DIM_UNDER2, 0}});
      check(w.getBlock(8, ty, 8) == reg().blockId("gold_block"), "and what was built deep in the Underworld is still there");
      lv->close();
    }
  }
  printf("[a world from before the deep world]\n");
  {
    LevelInfo li; li.name = "Old"; li.seedText = "old"; li.genVersion = 18; li.layout = 1;
    auto lv = Level::create(userSub("saves"), li, &pool, &bus);
    const std::string f2 = lv->info.folder; lv->close();
    json j = *readJson(userSub("saves") / f2 / "level.json"); j.erase("layout"); j.erase("underDepth"); writeJson(userSub("saves") / f2 / "level.json", j);
    auto lv2 = Level::open(userSub("saves") / f2, &pool, &bus);
    check(lv2 && lv2->info.layout == 1 && WORLD_H == 1024 && dimBase(DIM_OVERWORLD) == 512, "a save without a layout opens as it always was (four bands, the Overworld at 512)");
    if (lv2) {
      World& w = lv2->world();
      const vec3 at(8.5f, 520, 8.5f);
      streamUntil(w, at, {{DIM_OVERWORLD, 0}});
      check(w.getBlock(8, 512, 8) == reg().blockId("ember_crust"), "its Overworld still has the Ember Crust at its floor");
      lv2->close();
    }
  }
  printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
