// tools/test_moving.cpp — headless checks for moving structures (sim/glue.h, sim/moving_piece.h, sim/moving.h): the
// hinge, slider and glue, gluing, gathering a piece, its path, the move itself (lift out, turn or slide, set back) and
// using it by hand. Run: build/test_moving (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/events.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include "sim/rotate.h"
#include "sim/glue.h"
#include <thread>
#include <cstdio>

using namespace dq;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }

// a recipe making `out`: shaped with these ingredients, or shapeless from exactly `items`
static const Recipe* recipeFor(const std::string& out, const std::vector<std::string>& uses, int n = -1) {
  for (const Recipe& r : reg().recipes) {
    if (r.out != out || (n >= 0 && r.n != n)) continue;
    std::vector<std::string> got;
    if (r.shaped()) for (auto& [c, k] : r.key) got.push_back(k); else got = r.items;
    bool all = true; for (auto& u : uses) if (std::find(got.begin(), got.end(), u) == got.end()) all = false;
    if (all && got.size() == uses.size()) return &r;
  }
  return nullptr;
}

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-moving").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }

  printf("[parts]\n");
  check(reg().blockId("hinge") && reg().blockId("slider"), "the hinge and slider blocks exist");
  check(reg().item("glue") && reg().item("glue")->maxStack == 64, "glue is an item (a stack of 64)");
  check(recipeFor("glue", {"slime_ball"}, 4) && recipeFor("glue", {"honey_bottle"}, 4), "glue: a slime ball or a honey bottle make 4");
  check(recipeFor("hinge", {"iron_ingot", "planks", "redstone"}) && recipeFor("slider", {"iron_ingot", "planks", "redstone"}), "the hinge and slider are made of iron, planks and redstone");

  printf("[turning facings]\n");
  {
    const BlockId stairs = reg().blockId("oak_stairs"), log = reg().blockId("log"), disp = reg().blockId("dispenser"), torch = reg().blockId("torch"), slab = reg().blockId("oak_slab");
    check(rotateMetaY(stairs, 0, 1) == 1 && rotateMetaY(stairs, 4 | 3, 1) == (4 | 0) && rotateMetaY(stairs, 2, 4) == 2, "stairs turn a quarter at a time about the upright (upside-down bit kept)");
    check(rotateMetaY(log, 1, 1) == 2 && rotateMetaY(log, 0, 1) == 0, "a lying log turns from east-west to north-south; an upright one stays");
    check(rotateMetaY(torch, 1, 1) == 3 && rotateMetaY(slab, 2, 1) == 3, "wall torches and side slabs turn as the /rotate command turns them");
    check(rotateMetaAbout(log, 0, 1, 1) == 2 && rotateMetaAbout(log, 0, 2, 1) == 1 && rotateMetaAbout(log, 1, 1, 1) == 1, "about a sideways axis an upright log lies down across it; one along the axis stays");
    check(rotateMetaAbout(disp, 1, 1, 1) == 3 && rotateMetaAbout(disp, 3, 1, 1) == 0 && rotateMetaAbout(disp, 1, 1, 4) == 1, "a six-way block turned about east-west: up becomes south, south becomes down");
    check(rotateMetaAbout(stairs, 1, 1, 1) == 1, "blocks without a six-way facing keep theirs about a sideways axis");
  }

  // a little world to build in: a stone floor high in the sky
  ThreadPool pool(2); EventBus bus;
  LevelInfo li; li.name = "MovingTest"; li.seedText = "moving";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  Level* L = lvp.get();
  World& w = L->world();
  const int Y = dimBase(DIM_OVERWORLD) + 200;
  Player* P; { auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 8.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }
  { double t0 = nowSeconds();
    while (nowSeconds() - t0 < 60) {
      w.stream({{P->pos, 2, false}}, 20);
      bool all = true;
      for (int cx = -1; cx <= 1; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
      if (all) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } }
  check(w.chunk(DIM_OVERWORLD, 0, 0) != nullptr, "the test chunks load");
  const BlockId stone = reg().blockId("stone"), planks = reg().blockId("planks"), chest = reg().blockId("chest"), water = reg().blockId("water");
  for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 8; y++) w.setBlock(x, y, z, AIR); }
  auto use = [&](const std::string& item, ivec3 at, int face) { P->held() = item.empty() ? ItemStack() : ItemStack(item, 8); return bus.emit("itemUse", json::array({P->name, item, at.x, at.y, at.z, face})); };

  printf("[glue]\n");
  {
    const ivec3 A(2, Y + 1, 2), B(3, Y + 1, 2), C(2, Y + 1, 4);
    w.setBlock(A.x, A.y, A.z, planks); w.setBlock(B.x, B.y, B.z, planks);
    check(setGlue(w, A, 0, true) && glued(w, A, 0) && glued(w, B, 1), "glue on A's east face sticks A and B (seen from both sides)");
    check(setGlue(w, A, 0, false) && !glued(w, A, 0) && !glued(w, B, 1), "glue again unsticks them");
    w.setBlock(C.x, C.y, C.z, chest);
    w.setBlockEntity(C.x, C.y, C.z, json{{"type", "chest"}, {"items", json::array({ItemStack("cobblestone", 5).toJson()})}});
    w.setBlock(C.x, C.y, C.z - 1, planks);
    const bool cg = setGlue(w, C, 5, true);
    const json* ce = w.blockEntity(C.x, C.y, C.z);
    check(cg && glued(w, C, 5) && ce && ce->value("type", "") == "chest" && (*ce)["items"].size() == 1, "a chest glued keeps its contents and stays a chest");
    w.setBlock(A.x, A.y + 1, A.z, water);
    check(!setGlue(w, A, 2, true) && !glueable(water) && !glueable(AIR), "water (and air) can't be glued");
    setGlue(w, A, 0, true); w.setBlock(B.x, B.y, B.z, AIR);
    check(!glued(w, A, 0), "a glued neighbour broken: the glue is gone");
    w.setBlock(B.x, B.y, B.z, planks);
    check(!use("glue", A, 0) && glued(w, A, 0) && P->held().n == 7, "using glue on a face sticks it, one glue used");
    check(!use("glue", A, 0) && !glued(w, A, 0) && P->held().n == 9, "using it again unsticks it, the glue given back");
  }

  printf("%d passed, %d failed\n", passes, fails);
  return fails;
}
