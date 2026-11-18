// tools/test_ragdoll.cpp — headless checks of limp bodies (sim/ragdoll.h): creatures of every build (small, big, tall, flat,
// a blob, eight legs) killed on flat ground, on stairs, on a slab, at a one-block ledge, over a one-wide hole, against a
// wall, in water and beside lava. Over the whole fall no part of the drawn body may sink more than 1/16 of a block into a
// solid block (every part's box, sampled through its volume, against the blocks' real shapes), no joint may pull apart,
// and every body settles.
//   test_ragdoll [--userdir DIR] [--verbose]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/ragdoll.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "sim/combat.h"
#include <thread>
#include <cstdio>
using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }
static EventBus bus;

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-ragdoll").string();
  bool verbose = false;
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--userdir" && i + 1 < argc) ud = argv[++i]; if (a == "--verbose") verbose = true; }
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(2);
  LevelInfo li; li.name = "RagdollTest"; li.seedText = "ragdoll";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  Level& L = *lvp;
  World& w = L.world();
  const int Y = dimBase(DIM_OVERWORLD) + 200;   // high in the sky: nothing but what we build
  const vec3 eye(8.5f, Y + 1, 8.5f);
  double t0 = nowSeconds();
  while (nowSeconds() - t0 < 60) {
    w.stream({{eye, 2, false}}, 30);
    bool all = true;
    for (int cx = -1; cx <= 1; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
    if (all) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(w.chunk(DIM_OVERWORLD, 0, 0) != nullptr, "the test chunks load");
  auto B = [](const char* k) { return reg().blockId(k); };
  const BlockId stone = B("stone"), stairs = B("cobblestone_stairs"), slab = B("stone_slab"), water = B("water"), lava = B("lava");
  const int cx = 8, cz = 8;   // where each creature dies (the middle of a 13 x 13 stage)
  auto stage = [&](const std::string& kind) {
    for (int x = cx - 6; x <= cx + 6; x++) for (int z = cz - 6; z <= cz + 6; z++) {
      for (int y = Y - 3; y <= Y + 5; y++) w.setBlock(x, y, z, AIR);
      w.setBlock(x, Y, z, stone);
      w.setBlock(x, Y - 3, z, stone);
    }
    if (kind == "stairs") for (int x = cx + 1; x <= cx + 5; x++) for (int z = cz - 4; z <= cz + 4; z++)   // a staircase up to the east, a step from it
      for (int y = Y + 1; y <= Y + (x - cx); y++) w.setBlock(x, y, z, y == Y + (x - cx) ? stairs : stone, 1);
    if (kind == "slab") for (int x = cx - 1; x <= cx + 2; x++) for (int z = cz - 2; z <= cz + 2; z++) w.setBlock(x, Y + 1, z, slab);
    if (kind == "ledge") for (int x = cx + 1; x <= cx + 6; x++) for (int z = cz - 6; z <= cz + 6; z++) { w.setBlock(x, Y, z, AIR); w.setBlock(x, Y - 1, z, stone); }
    if (kind == "hole") for (int y = Y - 2; y <= Y; y++) w.setBlock(cx, y, cz, AIR);
    if (kind == "wall") for (int y = Y + 1; y <= Y + 3; y++) for (int z = cz - 6; z <= cz + 6; z++) w.setBlock(cx + 2, y, z, stone);   // (clear of the biggest one's feet)
    if (kind == "water") for (int x = cx - 3; x <= cx + 3; x++) for (int z = cz - 3; z <= cz + 3; z++) for (int y = Y + 1; y <= Y + 2; y++) w.setBlock(x, y, z, water);
    if (kind == "lava") for (int z = cz - 6; z <= cz + 6; z++) w.setBlock(cx + 1, Y, z, lava);
  };
  const std::vector<std::string> kinds = {"flat", "stairs", "slab", "ledge", "hole", "wall", "water", "lava"};
  const std::vector<std::string> mobs = {"pig", "cow", "spider", "skeleton", "horse", "camel", "enderman", "cod", "slime", "sheep", "chicken", "wolf", "ogre"};
  float worstAll = 0, gapAll = 0, endGapAll = 0; std::string worstWhat; double stepSecs = 0; long stepN = 0;
  int unsettled = 0, made = 0;
  for (const std::string& kind : kinds) {
    for (const std::string& mob : mobs) {
      if (!creatureType(mob)) continue;
      stage(kind);
      const float y0 = kind == "slab" ? Y + 1.5f : Y + 1.f;
      Creature* c = spawnCreature(L, REALM_MAIN, mob, vec3(cx + 0.5f, y0, cz + 0.5f));
      if (!c) continue;
      c->pos = vec3(cx + 0.5f, y0, cz + 0.5f); c->yaw = 0.7f; c->vel = vec3(0);
      // the blow comes from the west: every body is pushed east (over the ledge, into the wall, up the stairs)
      c->vel = vec3(4, 2, 0);
      c->data["rk"] = json::array({2.5, 1.2, 0});
      Ragdoll rd;
      if (!ragdollFromCreature(*c, rd)) { c->removed = true; continue; }
      c->removed = true;
      made++;
      float worst = 0, gap = 0; int part = -1; float at = 0; vec3 wpos(0);
      for (int i = 0; i < 60 * 10 && !rd.frozen; i++) {
        { const double t1 = nowSeconds(); rd.step(w, 1.f / 60); stepSecs += nowSeconds() - t1; stepN++; }
        int p = -1; const float d = rd.penetration(w, &p);
        if (d > worst) { worst = d; part = p; at = rd.age; if (p >= 0) wpos = rd.origin + vec3(rd.partMatrix(p)[3]) - vec3(cx, Y, cz); }
        gap = std::max(gap, rd.jointGap());
      }
      if (!rd.frozen) unsettled++;
      int gp = -1; const float endGap = rd.jointGap(&gp); endGapAll = std::max(endGapAll, endGap);
      const RigModel* M = rigModel(c->modelKey());
      const std::string pn = M && part >= 0 && part < (int)M->parts.size() ? M->parts[part].name : "-";
      if (verbose || worst > 1 / 16.f) printf("     %-8s %-9s deepest %.3f (%s at %.2f s), joint gap %.3f (%.3f at rest), %s in %.1f s  [part at %.2f %.2f %.2f from the stage] widest joint at rest: %s, moving %.4f/step (%s)\n", kind.c_str(), mob.c_str(), worst, pn.c_str(), at, gap, endGap, rd.frozen ? "settled" : "still moving", rd.age, wpos.x, wpos.y, wpos.z, M && gp >= 0 ? M->parts[gp].name.c_str() : "-", rd.motion, M && rd.motionPart >= 0 ? M->parts[rd.motionPart].name.c_str() : "-");
      if (worst > worstAll) { worstAll = worst; worstWhat = mob + " " + kind + " (" + pn + ")"; }
      gapAll = std::max(gapAll, gap);
      for (auto& e : L.entities()) if (e->type == "item") e->removed = true;   // (the drops)
      L.tick(TICK_DT);
    }
  }
  {   // a body lying still wakes when the ground goes from under it (or a block is set into it)
    stage("flat");
    Creature* c = spawnCreature(L, REALM_MAIN, "pig", vec3(cx + 0.5f, Y + 1.f, cz + 0.5f));
    Ragdoll rd;
    if (c && ragdollFromCreature(*c, rd)) {
      c->removed = true;
      for (int i = 0; i < 60 * 10 && !rd.frozen; i++) rd.step(w, 1.f / 60);
      const float y0 = rd.origin.y + rd.lowest();
      for (int x = cx - 3; x <= cx + 3; x++) for (int z = cz - 3; z <= cz + 3; z++) w.setBlock(x, Y, z, AIR);   // (Y - 3 is still there)
      float worst = 0;
      for (int i = 0; i < 60 * 6; i++) { rd.step(w, 1.f / 60); worst = std::max(worst, rd.penetration(w)); }
      const float y1 = rd.origin.y + rd.lowest();
      check(rd.frozen && y1 < y0 - 2.5f && worst <= 1 / 16.f, strFormat("a body lying still falls again when the ground is dug from under it (%.2f lower, deepest %.3f)", y0 - y1, worst));
      for (int x = cx - 3; x <= cx + 3; x++) for (int z = cz - 3; z <= cz + 3; z++) w.setBlock(x, Y - 2, z, stone);   // (a floor set into it)
      for (int i = 0; i < 60 * 4; i++) rd.step(w, 1.f / 60);
      check(rd.penetration(w) <= 1 / 16.f, strFormat("and comes out of a block set into it (deepest %.3f)", rd.penetration(w)));
    }
  }
  printf("     a falling body costs %.3f ms per frame on average (%ld frames)\n", stepN ? stepSecs / stepN * 1000 : 0., stepN);
  check(stepN && stepSecs / stepN < 0.002, strFormat("a falling body is cheap: %.3f ms a frame (under 2)", stepN ? stepSecs / stepN * 1000 : 0.));
  check(made >= 80, "limp bodies made (" + std::to_string(made) + ")");
  check(worstAll <= 1 / 16.f, strFormat("no part of a body sinks into the blocks more than 1/16 of a block (deepest %.3f: %s)", worstAll, worstWhat.c_str()));
  check(gapAll <= 0.3f && endGapAll <= 0.06f, strFormat("no joint pulls apart (widest %.3f on the way down, %.3f at rest)", gapAll, endGapAll));
  check(unsettled == 0, "every body settles (" + std::to_string(unsettled) + " still moving after 10 s)");
  printf("\n%d passed, %d failed\n", passes, fails);
  lvp.reset();
  fs::remove_all(ud, ec);
  return fails ? 1 : 0;
}
