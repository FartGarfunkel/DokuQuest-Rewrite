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
#include "sim/moving_piece.h"
#include "sim/moving.h"
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
    // chisel corners: bit (x<<2)|(y<<1)|z
    check(rotateCarveY(1 << ((1 << 2) | 0 | 0), 1) == 1 << ((1 << 2) | 0 | 1) && rotateCarveY(0x5A, 4) == 0x5A, "a cut corner turns about the upright: the east-north corner goes to east-south");
    check(rotateCarveAbout(1 << ((0 << 2) | (1 << 1) | 0), 1, 1) == 1 << ((0 << 2) | (1 << 1) | 1) && rotateCarveAbout(1 << ((0 << 2) | (1 << 1) | 0), 2, 1) == 1 << ((1 << 2) | (1 << 1) | 0), "about east-west a top-north corner goes top-south (up to south); about north-south a top-west one goes top-east");
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

  printf("[piece and path]\n");
  {
    for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) for (int y = Y + 1; y < Y + 8; y++) w.setBlock(x, y, z, AIR);
    const BlockId hingeB = reg().blockId("hinge"), slider = reg().blockId("slider"), grass = reg().blockId("tall_grass"), door = reg().blockId("oak_door");
    const ivec3 H(6, Y + 1, 6);
    w.setBlock(H.x, H.y, H.z, hingeB, 1);   // (facing up)
    // a gate: three planks above the hinge running east, glued to it and to each other
    for (int i = 0; i < 3; i++) w.setBlock(H.x + i, H.y + 1, H.z, planks);
    setGlue(w, H, 2, true); setGlue(w, H + ivec3(0, 1, 0), 0, true); setGlue(w, H + ivec3(1, 1, 0), 0, true);
    Motion m; m.kind = 0; m.machine = H; m.face = 2; m.quarters = 1;
    Piece pc; std::string why;
    const bool gOk = gatherPiece(w, m, pc, &why);
    check(gOk && pc.cells.size() == 3, "a three-plank gate glued to the hinge is one piece (" + std::to_string(pc.cells.size()) + ")");
    check(movedCell(m, H + ivec3(1, 1, 0)) == H + ivec3(0, 1, 1), "a quarter turn clockwise takes the plank east of the hinge round to its south");
    setGlue(w, H + ivec3(0, 1, 0), 0, false);
    Piece small; gatherPiece(w, m, small, &why);
    check(small.cells.size() == 1, "unglued in the middle, only the plank on the hinge moves");
    setGlue(w, H + ivec3(0, 1, 0), 0, true);
    std::vector<ivec3> brk; ivec3 at(0);
    w.setBlock(H.x + 1, H.y + 1, H.z + 2, stone);   // (in the far plank's sweep)
    check(!pathClear(w, m, pc, &brk, &at), "a stone in the gate's swing blocks it");
    w.setBlock(H.x + 1, H.y + 1, H.z + 2, grass);
    brk.clear();
    check(pathClear(w, m, pc, &brk, &at) && !brk.empty(), "tall grass in the swing doesn't block it: it's broken as the gate goes through");
    w.setBlock(H.x + 1, H.y + 1, H.z + 2, AIR);
    Motion s; s.kind = 1; s.machine = H; s.face = 0; s.distance = 3;
    check(movedCell(s, H + ivec3(1, 0, 0)) == H + ivec3(4, 0, 0), "a slider facing east, distance 3: a block moves three east");
    // a door glued by its lower half only
    const ivec3 D(12, Y + 1, 12), DH = D + ivec3(0, -1, 0);
    w.setBlock(DH.x, DH.y + 0, DH.z, stone);
    w.setBlock(D.x, D.y, D.z, door, 0); w.setBlock(D.x, D.y + 1, D.z, door, 16);
    w.setBlock(D.x - 1, D.y, D.z, hingeB, 5);   // (facing east, toward the door)
    setGlue(w, D + ivec3(-1, 0, 0), 0, true);
    Motion dm; dm.kind = 0; dm.machine = D + ivec3(-1, 0, 0); dm.face = 0;
    Piece dp; why.clear();
    const bool dOk = gatherPiece(w, dm, dp, &why);
    check(!dOk && why.find("both halves") != std::string::npos, "a door glued by one half only is refused: glue both halves (" + why + ")");
    setGlue(w, D, 2, true);
    check(gatherPiece(w, dm, dp, &why) && dp.cells.size() == 2, "both halves glued: the door moves whole");
    // too big
    for (int x = 0; x < 10; x++) for (int z = 0; z < 10; z++) for (int y = 0; y < 6; y++) w.setBlock(x - 2, Y + 1 + y, z - 2 + 14, planks);
    const ivec3 S(-3, Y + 1, 12); w.setBlock(S.x, S.y, S.z, slider, 5);
    setGlue(w, S, 0, true);
    for (int x = 0; x < 10; x++) for (int z = 0; z < 10; z++) for (int y = 0; y < 6; y++) { const ivec3 q(x - 2, Y + 1 + y, z - 2 + 14); if (x < 9) setGlue(w, q, 0, true); if (z < 9) setGlue(w, q, 4, true); if (y < 5) setGlue(w, q, 2, true); }
    Motion bm; bm.kind = 1; bm.machine = S; bm.face = 0; bm.distance = 1;
    Piece bp; why.clear();
    const bool bOk = gatherPiece(w, bm, bp, &why);
    check(!bOk && why.find("512") != std::string::npos, "600 glued blocks are too many (" + why + ")");
  }

  printf("[moving]\n");
  {
    for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) for (int y = Y + 1; y < Y + 8; y++) w.setBlock(x, y, z, AIR);
    const BlockId hingeB = reg().blockId("hinge"), stairs = reg().blockId("oak_stairs"), cobble = reg().blockId("cobblestone");
    auto tick = [&](double s) { for (double t = 0; t < s; t += 0.05) L->tick(0.05); };
    auto planksCount = [&] { int n = 0; for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) for (int y = Y + 1; y < Y + 8; y++) n += w.getBlock(x, y, z) == planks; return n; };
    // a gate two wide and three tall on a hinge facing up; its top far block is stairs, a chest by the hinge column holds 5 cobblestone
    const ivec3 H(6, Y + 1, 6);
    w.setBlock(H.x, H.y, H.z, hingeB, 1);
    for (int k = 1; k <= 3; k++) for (int i = 0; i < 2; i++) w.setBlock(H.x + i, H.y + k, H.z, planks);
    w.setBlock(H.x + 1, H.y + 3, H.z, stairs, 1);
    const u8 stairCarve = w.getCarve(H.x + 1, H.y + 3, H.z);
    w.setBlock(H.x + 1, H.y + 1, H.z, chest);
    w.setBlockEntity(H.x + 1, H.y + 1, H.z, json{{"type", "chest"}, {"items", json::array({ItemStack("cobblestone", 5).toJson()})}});
    setGlue(w, H, 2, true);
    for (int k = 1; k <= 3; k++) { setGlue(w, H + ivec3(0, k, 0), 0, true); if (k < 3) { setGlue(w, H + ivec3(0, k, 0), 2, true); setGlue(w, H + ivec3(1, k, 0), 2, true); } }
    const int before = planksCount();
    check(startMove(*L, REALM_MAIN, H, nullptr), "the hinge starts the gate turning");
    check(w.getBlock(H.x + 1, H.y + 2, H.z) == AIR && !movingBodiesList(*L).empty(), "while it turns the gate's blocks are out of the world, in a moving body");
    tick(1.2);
    bool turned = true; for (int k = 1; k <= 3; k++) turned = turned && w.getBlock(H.x + 1, H.y + k, H.z) == AIR && w.getBlock(H.x, H.y + k, H.z + 1) != AIR;
    check(turned && machineOpen(w, H) && movingBodiesList(*L).empty(), "after a second the gate stands a quarter turn round, open");
    check(stairCarve != 0 && w.getCarve(H.x, H.y + 3, H.z + 1) == rotateCarveY(stairCarve, 1), "the stairs (chiseled planks) turned with it: their cut corners turned a quarter");
    const json* ce = w.blockEntity(H.x, H.y + 1, H.z + 1);
    check(w.getBlock(H.x, H.y + 1, H.z + 1) == chest && ce && (*ce)["items"].size() == 1 && ItemStack::fromJson((*ce)["items"][0]).n == 5, "the chest went with it, its 5 cobblestone inside");
    check(glued(w, H + ivec3(0, 1, 1), 2) && glued(w, H + ivec3(0, 1, 1), 5), "the glue turned with the blocks (up and back toward the hinge column)");
    check(startMove(*L, REALM_MAIN, H, nullptr), "using it again starts it back");
    tick(1.2);
    bool back = true; for (int k = 1; k <= 3; k++) back = back && w.getBlock(H.x + 1, H.y + k, H.z) != AIR && w.getBlock(H.x, H.y + k, H.z + 1) == AIR;
    check(back && !machineOpen(w, H) && w.getCarve(H.x + 1, H.y + 3, H.z) == stairCarve && planksCount() == before, "shut again: every block back as it was, nothing lost");
    // blocked
    w.setBlock(H.x, H.y + 2, H.z + 1, stone);
    std::string why;
    check(!startMove(*L, REALM_MAIN, H, &why) && w.getBlock(H.x + 1, H.y + 2, H.z) != AIR, "a stone in the swing: it doesn't start and nothing moves (" + why + ")");
    w.setBlock(H.x, H.y + 2, H.z + 1, AIR);
    // finished early: by finishAll, and by a save
    startMove(*L, REALM_MAIN, H, nullptr); tick(0.3);
    finishAll(*L);
    check(movingBodiesList(*L).empty() && w.getBlock(H.x, H.y + 2, H.z + 1) != AIR && planksCount() == before, "finishAll mid-move sets every block down at the end");
    startMove(*L, REALM_MAIN, H, nullptr); tick(0.3);
    L->save(true);
    check(movingBodiesList(*L).empty() && w.getBlock(H.x + 1, H.y + 2, H.z) != AIR && planksCount() == before, "a save mid-move finishes it first: nothing is saved in mid-air or lost");
    // a slider: out three and back
    const BlockId sliderB = reg().blockId("slider");
    const ivec3 S(2, Y + 1, 14); w.setBlock(S.x, S.y, S.z, sliderB, 5);   // (facing east)
    w.setBlock(S.x + 1, S.y, S.z, planks); setGlue(w, S, 0, true);
    w.setBlockEntity(S.x, S.y, S.z, json{{"type", "machine_state"}, {"distance", 3}, {"glue", (int)glueMask(w, S)}});
    check(startMove(*L, REALM_MAIN, S, nullptr), "the slider starts its plank sliding");
    tick(2.0);
    check(w.getBlock(S.x + 1, S.y, S.z) == AIR && w.getBlock(S.x + 4, S.y, S.z) == planks && machineOpen(w, S), "it slides three blocks east");
    check(startMove(*L, REALM_MAIN, S, nullptr), "and starts back (the plank no longer touches it)");
    tick(2.0);
    check(w.getBlock(S.x + 1, S.y, S.z) == planks && w.getBlock(S.x + 4, S.y, S.z) == AIR && !machineOpen(w, S) && glued(w, S, 0), "back against the slider, glued to it again");
  }

  printf("[review fixes]\n");
  {
    for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) for (int y = Y + 1; y < Y + 8; y++) w.setBlock(x, y, z, AIR);
    auto tick = [&](double s) { for (double t = 0; t < s; t += 0.05) L->tick(0.05); };
    auto items = [&] { int n = 0; for (auto& e : L->entities()) if (!e->removed && e->type == "item") n++; return n; };
    for (auto& e : L->entities()) if (e->type == "item") e->removed = true;
    L->tick(0.05);
    const BlockId hingeB = reg().blockId("hinge"), door = reg().blockId("oak_door"), torch = reg().blockId("torch");
    // a door and a torch carried on a plank: lifting the plank must not knock them off (they ride along)
    const ivec3 H(4, Y + 1, 4);
    w.setBlock(H.x, H.y, H.z, hingeB, 1);
    w.setBlock(H.x, H.y + 1, H.z, planks); w.setBlock(H.x + 1, H.y + 1, H.z, planks);
    w.setBlock(H.x + 1, H.y + 2, H.z, door, 0); w.setBlock(H.x + 1, H.y + 3, H.z, door, 16);
    w.setBlock(H.x, H.y + 2, H.z, torch, 0);
    setGlue(w, H, 2, true); setGlue(w, H + ivec3(0, 1, 0), 0, true); setGlue(w, H + ivec3(1, 1, 0), 2, true); setGlue(w, H + ivec3(1, 2, 0), 2, true); setGlue(w, H + ivec3(0, 1, 0), 2, true);
    const int before = items();
    check(startMove(*L, REALM_MAIN, H, nullptr), "a gate with a door and a torch on it starts");
    tick(1.3);
    check(items() == before, "nothing dropped as it lifted (" + std::to_string(items() - before) + " items)");
    check(w.getBlock(H.x, H.y + 2, H.z + 1) == door && w.getBlock(H.x, H.y + 3, H.z + 1) == door && w.getBlock(H.x, H.y + 2, H.z) == torch, "the door and the torch rode round with it");
    // its setting changed while it stands open: it still shuts the way it came
    machineState(w, H)["quarters"] = -1;
    check(startMove(*L, REALM_MAIN, H, nullptr), "set the other way while open, it starts back");
    tick(1.3);
    check(!machineOpen(w, H) && w.getBlock(H.x + 1, H.y + 1, H.z) == planks && w.getBlock(H.x + 1, H.y + 2, H.z) == door, "and shuts where it began");
    const BlockId sliderB = reg().blockId("slider");
    const ivec3 S(4, Y + 1, 10); w.setBlock(S.x, S.y, S.z, sliderB, 5);
    w.setBlock(S.x + 1, S.y, S.z, planks); setGlue(w, S, 0, true);
    startMove(*L, REALM_MAIN, S, nullptr); tick(1.0);
    machineState(w, S)["distance"] = 4;
    check(startMove(*L, REALM_MAIN, S, nullptr), "a slider set further while out still comes back");
    tick(3.0);
    check(!machineOpen(w, S) && w.getBlock(S.x + 1, S.y, S.z) == planks && w.getBlock(S.x + 2, S.y, S.z) == AIR, "back against the slider");
    // water in its way: it doesn't go, and the water stays
    w.setBlock(S.x + 3, S.y, S.z, water);
    machineState(w, S)["distance"] = 3;
    check(!startMove(*L, REALM_MAIN, S, nullptr) && w.getBlock(S.x + 3, S.y, S.z) == water && w.getBlock(S.x + 1, S.y, S.z) == planks, "water in the way stops it, and stays");
    w.setBlock(S.x + 3, S.y, S.z, AIR);
  }

  printf("[by hand]\n");
  {
    for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) for (int y = Y + 1; y < Y + 8; y++) w.setBlock(x, y, z, AIR);
    auto tick = [&](double s) { for (double t = 0; t < s; t += 0.05) L->tick(0.05); };
    const BlockId hingeB = reg().blockId("hinge"), sliderB = reg().blockId("slider");
    const ivec3 H(4, Y + 1, 4);
    w.setBlock(H.x, H.y, H.z, hingeB, 1);
    for (int k = 1; k <= 2; k++) for (int i = 0; i < 3; i++) w.setBlock(H.x + i, H.y + k, H.z, planks);
    setGlue(w, H, 2, true);
    for (int k = 1; k <= 2; k++) for (int i = 0; i < 2; i++) setGlue(w, H + ivec3(i, k, 0), 0, true);
    for (int i = 0; i < 3; i++) setGlue(w, H + ivec3(i, 1, 0), 2, true);
    P->sneaking = true;
    std::vector<int> seen;
    for (int i = 0; i < 4; i++) { use("", H, 2); const json* e = w.blockEntity(H.x, H.y, H.z); seen.push_back(e ? e->value("quarters", 1) : 1); }
    check(seen == std::vector<int>({-1, 2, 0, 1}), "sneak-use on the hinge: anticlockwise, half turn, turning, clockwise (stored)");
    check(glued(w, H, 2), "changing the setting keeps the hinge's glue");
    const ivec3 Sl(12, Y + 1, 4); w.setBlock(Sl.x, Sl.y, Sl.z, sliderB, 5);
    use("", Sl, 2); use("", Sl, 2);
    const json* se = w.blockEntity(Sl.x, Sl.y, Sl.z);
    check(se && se->value("distance", 1) == 3, "sneak-use on a slider twice: 3 blocks");
    for (int i = 0; i < 14; i++) use("", Sl, 2);
    se = w.blockEntity(Sl.x, Sl.y, Sl.z);
    check(se && se->value("distance", 1) == 1, "sixteen goes round to 1");
    P->sneaking = false;
    check(!use("", H + ivec3(1, 2, 0), 4) && !movingBodiesList(*L).empty(), "using the gate's middle block starts it");
    tick(1.2);
    check(machineOpen(w, H) && w.getBlock(H.x, H.y + 2, H.z + 1) == planks, "the gate swung open");
    check(!use("", H + ivec3(0, 2, 2), 4) && !movingBodiesList(*L).empty(), "using it open swings it shut");
    tick(1.2);
    check(!machineOpen(w, H) && w.getBlock(H.x + 2, H.y + 2, H.z) == planks, "shut again");
    const u8 m0 = glueMask(w, H + ivec3(1, 2, 0));
    use("glue", H + ivec3(1, 2, 0), 0);
    check(movingBodiesList(*L).empty() && !machineOpen(w, H) && glueMask(w, H + ivec3(1, 2, 0)) != m0, "holding glue: it glues, the gate doesn't move");
    use("glue", H + ivec3(1, 2, 0), 0);
    w.setBlock(H.x + 1, H.y + 1, H.z, chest);
    w.setBlockEntity(H.x + 1, H.y + 1, H.z, json{{"type", "chest"}});
    setGlue(w, H + ivec3(0, 1, 0), 0, true); setGlue(w, H + ivec3(1, 1, 0), 0, true); setGlue(w, H + ivec3(1, 1, 0), 2, true);
    use("", H + ivec3(1, 1, 0), 4);
    check(movingBodiesList(*L).empty(), "using a chest in the gate opens the chest, not the gate");
    // a slider slid out: its plank, using it, slides it back
    w.setBlock(Sl.x + 1, Sl.y, Sl.z, planks); setGlue(w, Sl, 0, true);
    use("", Sl, 2); tick(1.0);
    check(w.getBlock(Sl.x + 2, Sl.y, Sl.z) == planks && machineOpen(w, Sl), "using the slider slides its plank out");
    use("", Sl + ivec3(1, 0, 0) * 2, 2); tick(1.0);
    check(w.getBlock(Sl.x + 1, Sl.y, Sl.z) == planks && !machineOpen(w, Sl), "using the slid-out plank brings it back");
  }

  printf("%d passed, %d failed\n", passes, fails);
  return fails;
}
