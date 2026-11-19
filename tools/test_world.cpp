// tools/test_world.cpp — headless checks for the world: rock layers and where the ores lie,
// cave-ins and support beams, the new crops and plants. Run: build/test_world (exit code = failures)
#include "world/carve_forms.h"
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
#include "gen/generator.h"
#include "gen/overworld.h"
#include "gen/nether_painted.h"
#include "sim/dyntree.h"
#include "sim/blockinfo.h"
#include "sim/physics.h"
#include "sim/worldsys.h"
#include "world/chunk.h"
#include <thread>
#include <map>
#include "sim/cave_drips.h"
#include "sim/controller.h"
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

  printf("[cave drips]\n");
  {   // a stalactite drips what lies over the stone it hangs from, and fills a cauldron under it (sim/cave_drips.h)
    const BlockId DS = reg().blockId("pointed_dripstone"), ST = reg().blockId("stone"), WA = reg().blockId("water"), LA = reg().blockId("lava"), CA = reg().blockId("cauldron");
    const int X = 14, Y0 = rg.Y + 1, Z = 14;
    for (int y = Y0; y <= Y0 + 8; y++) w.setBlock(X, y, Z, AIR);
    w.setBlock(X, Y0 + 6, Z, ST);                                          // the roof
    w.setBlock(X, Y0 + 5, Z, DS, 1); w.setBlock(X, Y0 + 4, Z, DS, 1);      // a stalactite two long
    w.setBlock(X, Y0, Z, CA, 0);                                           // a cauldron under it
    check(caveDripAt(w, X, Y0 + 4, Z).liquid == 0, "a dry roof: no drips");
    check(caveDripAt(w, X, Y0 + 5, Z).liquid == 0 && caveDripAt(w, X, Y0 + 5, Z).length == 0, "only the tip drips, not the stalactite's root");
    w.setBlock(X, Y0 + 7, Z, WA);
    const CaveDrip d = caveDripAt(w, X, Y0 + 4, Z);
    check(d.liquid == 1 && d.length == 2, "water over the stone: the tip drips water (" + std::to_string(d.liquid) + ", length " + std::to_string(d.length) + ")");
    for (int i = 0; i < 3; i++) caveDripTick(w, X, Y0 + 4, Z, 0.0);
    check(cauldronWater(w.getMeta(X, Y0, Z)) == 3, "the cauldron under it fills with water, a level at a time");
    caveDripTick(w, X, Y0 + 4, Z, 0.0);
    check(cauldronLevel(w.getMeta(X, Y0, Z)) == 3, "...and stops when it's full");
    w.setBlock(X, Y0, Z, CA, 0); w.setBlock(X, Y0 + 7, Z, LA);
    check(caveDripAt(w, X, Y0 + 4, Z).liquid == 2, "lava over the stone: the tip drips lava");
    for (int i = 0; i < 3; i++) caveDripTick(w, X, Y0 + 4, Z, 0.0);
    const u8 lm = w.getMeta(X, Y0, Z);
    check(cauldronLava(lm) && cauldronLevel(lm) == 3 && cauldronWater(lm) == 0, "a cauldron of lava fills under it (no water in it)");
    check(reg().metaLight(CA, lm) > 10 && reg().metaLight(CA, 3) == 0, "a cauldron of lava glows; one of water doesn't");
    w.setBlock(X, Y0, Z, CA, 1); w.setBlock(X, Y0 + 7, Z, WA);   // a little water in it already, then lava over the roof
    w.setBlock(X, Y0 + 7, Z, LA); caveDripTick(w, X, Y0 + 4, Z, 0.0);
    check(w.getMeta(X, Y0, Z) == 1, "lava doesn't drip into a cauldron of water");
    caveDripTick(w, X, Y0 + 4, Z, 0.99);
    check(w.getMeta(X, Y0, Z) == 1, "(and a drip is now and then, not every tick)");
    w.setBlock(X, Y0 + 2, Z, ST); w.setBlock(X, Y0, Z, CA, 0); w.setBlock(X, Y0 + 7, Z, WA); caveDripTick(w, X, Y0 + 4, Z, 0.0);
    check(w.getMeta(X, Y0, Z) == 0, "something between the tip and the cauldron catches the drops");
    for (int y = Y0; y <= Y0 + 8; y++) w.setBlock(X, y, Z, AIR);
  }

  printf("[saplings on rooted dirt]\n");
  {   // a sapling takes to rooted dirt as to any soil, and grows there
    const BlockId RD = reg().blockId("rooted_dirt"), SAP = reg().blockId("sapling");
    const int X = 2, Y0 = rg.Y + 1, Z = 2;
    for (int y = Y0; y <= Y0 + 10; y++) for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) w.setBlock(X + dx, y, Z + dz, AIR);
    w.setBlock(X, Y0 - 1, Z, RD);
    rg.P->inv[0] = ItemStack("sapling", 1); rg.P->selected = 0;
    PlayerController pc(*rg.lv, *rg.P);
    pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(X, Y0 - 1, Z); pc.target.normal = ivec3(0, 1, 0); pc.target.id = RD; pc.target.point = vec3(X + 0.5f, Y0, Z + 0.5f);
    rg.P->pos = vec3(X + 0.5f, Y0, Z + 3.5f);
    pc.use(false, PlayerInput{});
    check(w.getBlock(X, Y0, Z) == SAP, "a sapling can be planted on rooted dirt");
    bool grew = false; for (int k = 0; k < 50 && !grew; k++) { growTree(*rg.lv, w, X, Y0, Z, (k % 10) / 10.0); grew = bhas(w.getBlock(X, Y0, Z), BF::IsLog); }
    check(grew, "and grows into a tree there");
    for (int y = Y0; y <= Y0 + 12; y++) for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) w.setBlock(X + dx, y, Z + dz, AIR);
  }

  printf("[movement]\n");
  {   // a jump from right against a slab's side is a whole jump: the step up mustn't swallow its rise (the "slow, sticky"
      // jump on stairs and roofs)
    const BlockId slab = reg().blockId("oak_slab");
    w.setBlock(8, rg.Y + 1, 7, slab, 0);
    rg.P->pos = vec3(8.5f, rg.Y + 1, 8.0f + rg.P->w + 0.001f); rg.P->vel = vec3(0); rg.fall(0.3);
    PlayerInput fw; fw.mz = -1; PlayerInput fj = fw; fj.jump = true;
    float peak = 0;
    for (int i = 0; i < 40; i++) { rg.P->move(w, i < 2 ? fj : fw, 1 / 60.f, MoveOpts{}); peak = std::max(peak, rg.P->pos.y - (rg.Y + 1)); }
    check(peak > 1.2f, "a jump against a slab's side goes the whole way up (peak +" + std::to_string(peak) + ")");
    w.setBlock(8, rg.Y + 1, 7, AIR);
    // a block's height you can jump onto: no catching its edge to hang and pull up (the "slow, sticky" jump on roofs)
    const BlockId stone = reg().blockId("stone");
    for (int x = 6; x <= 10; x++) w.setBlock(x, rg.Y + 1, 7, stone);
    rg.P->pos = vec3(8.5f, rg.Y + 1, 8.0f + rg.P->w + 0.001f); rg.P->vel = vec3(0); rg.fall(0.3);
    bool hung = false; float onTop = -1;
    for (int i = 0; i < 60; i++) {
      rg.P->move(w, i < 20 ? fj : fw, 1 / 60.f, MoveOpts{});
      hung |= (bool)rg.P->hang || (bool)rg.P->climb;
      if (onTop < 0 && rg.P->onGround && rg.P->pos.y > rg.Y + 1.99f) onTop = i / 60.f;
    }
    check(!hung && onTop >= 0 && onTop < 0.5f, "a one-block step is jumped onto, not hung from (" + std::string(hung ? "hung" : "no hang") + ", on top at " + std::to_string(onTop) + " s)");
    for (int x = 6; x <= 10; x++) w.setBlock(x, rg.Y + 1, 7, AIR);
  }
  {   // fences and walls: you stand on the top you see (not half a block over it), and jump up onto them; a creature can't
    const BlockId fence = reg().blockId("oak_fence"), wall = reg().blockId("cobblestone_wall");
    for (BlockId b : {fence, wall}) {
      w.setBlock(8, rg.Y + 1, 4, b);
      rg.P->pos = vec3(8.5f, rg.Y + 2.4f, 4.5f); rg.P->vel = vec3(0); rg.fall(1.0);
      check(rg.P->onGround && std::fabs(rg.P->pos.y - (rg.Y + 2)) < 0.01f, "standing on a " + reg().block(b).key + ": feet on its top (+" + std::to_string(rg.P->pos.y - (rg.Y + 1)) + ")");
      rg.P->pos = vec3(8.5f, rg.Y + 1, 5.0f + rg.P->w + 0.05f); rg.P->vel = vec3(0); rg.fall(0.3);
      PlayerInput fw; fw.mz = -1; PlayerInput fj = fw; fj.jump = true;
      float onTop = -1;
      for (int i = 0; i < 60 && onTop < 0; i++) { rg.P->move(w, i < 20 ? fj : fw, 1 / 60.f, MoveOpts{}); if (rg.P->onGround && rg.P->pos.y > rg.Y + 1.99f) onTop = i / 60.f; }
      check(onTop >= 0, "a jump gets you up onto a " + reg().block(b).key);
      w.setBlock(8, rg.Y + 1, 4, AIR);
    }
    {   // a creature meets a fence a block taller than it is drawn: moving into it over its top, it is stopped
      w.setBlock(8, rg.Y + 1, 4, fence);
      vec3 p(8.5f, rg.Y + 2.1f, 3.3f); MoveResult r = moveBox(w, p, 0.3f, 0.9f, vec3(0, 0, 1.0f), 0.f, true);
      vec3 q(8.5f, rg.Y + 2.1f, 3.3f); moveBox(w, q, 0.3f, 0.9f, vec3(0, 0, 1.0f), 0.f, false);
      check(r.hitZ && p.z < 4.375f - 0.3f + 0.01f && q.z > 4.2f, strFormat("over a fence's top a creature is stopped (z %.3f), a player is not (z %.3f)", p.z, q.z));
      w.setBlock(8, rg.Y + 1, 4, AIR);
    }
    // pens: a ring of fence with a shut gate, and a ring of wall. Their creatures, running and jumping at the sides again and
    // again, never get up onto a post (a player can) and never out
    const BlockId gate = reg().blockId("oak_fence_gate");
    for (BlockId ring : {fence, wall}) {
      for (int x = 1; x <= 9; x++) for (int z = 1; z <= 9; z++) if (x == 1 || x == 9 || z == 1 || z == 9) w.setBlock(x, rg.Y + 1, z, ring);
      if (gate && ring == fence) w.setBlock(5, rg.Y + 1, 1, gate, 0);
      rg.P->pos = vec3(14.5f, rg.Y + 1, 14.5f);
      std::vector<Creature*> pen;
      for (const char* k : {"cow", "sheep", "pig", "chicken", "goat", "horse", "rabbit", "llama", "wolf"})
        if (reg().mobs.contains(k)) if (Creature* c = spawnCreature(*rg.lv, 0, k, vec3(5.5f, rg.Y + 1, 5.5f))) pen.push_back(c);
      std::set<std::string> up, out;
      for (int t = 0; t < 60 * 30; t++) {
        for (size_t i = 0; i < pen.size(); i++) {   // each runs at a side of the pen, jumping
          Creature* c = pen[i];
          const float a = (float)i * 0.7f + t * 0.002f;
          c->vel.x = std::cos(a) * 4; c->vel.z = std::sin(a) * 4; c->wantJump = true;
        }
        rg.lv->tick(TICK_DT);
      }
      for (Creature* c : pen) if (c->pos.x < 1.6f || c->pos.x > 9.4f || c->pos.z < 1.6f || c->pos.z > 9.4f) out.insert(c->type + strFormat("@%.2f,%.2f,%.2f", c->pos.x, c->pos.y - rg.Y, c->pos.z));
      std::string bad; for (auto& k : up) bad += " " + k + " (on top)"; for (auto& k : out) bad += " " + k + " (out)";
      check(!pen.empty() && bad.empty(), std::to_string(pen.size()) + " creatures jumping at a " + reg().block(ring).key + " pen for 30 s stay in it" + bad);
      for (Creature* c : pen) c->removed = true;
      rg.lv->tick(TICK_DT);
      for (int x = 1; x <= 9; x++) for (int z = 1; z <= 9; z++) w.setBlock(x, rg.Y + 1, z, AIR);
    }
  }
  {   // the view keeps up with a jump (the steady climb is for steps on the ground: the "slow, sticky" jump)
    float y = 0, cam = 0, vy = JUMP_V, lag = 0;
    for (int i = 0; i < 20; i++) { const float dt = 1 / 60.f; vy -= GRAVITY * dt; y += vy * dt; cam = viewRise(y, cam, 0, vy, false, dt); lag = std::max(lag, y - cam); }
    check(lag < 0.05f, "the view rises with a jump (most behind: " + std::to_string(lag) + ")");
    float c2 = 0; for (int i = 0; i < 3; i++) c2 = viewRise(0.5f, c2, 4.3f, 0, true, 1 / 60.f);
    check(c2 > 0.2f && c2 < 0.35f, "a step's view still climbs steadily (" + std::to_string(c2) + " of 0.5 after 0.05 s)");
  }

  printf("[cave-ins]\n");
  {
    const BlockId STONE = reg().blockId("stone"), BEAM = reg().blockId("support_beam");
    check(isRock(STONE) && !isRock(reg().blockId("stone_bricks")) && !isRock(reg().blockId("log")), "raw rock is rock; bricks and logs aren't");
    {   // corner stairs keep their corner as chiseled blocks (a hipped roof's corners whole)
      const BlockId st = reg().blockId("oak_stairs");
      auto solid = [](u8 carve) { int n = 0; for (int i = 0; i < 8; i++) n += !((carve >> i) & 1); return n; };
      const CarveForm s0 = carveFormOf(st, 0), in = carveFormOf(st, STAIR_CORNER), out = carveFormOf(st, STAIR_CORNER | STAIR_OUTER), inR = carveFormOf(st, STAIR_CORNER | STAIR_RIGHT);
      check(st && s0.base && solid(s0.carve) == 6 && solid(in.carve) == 7 && solid(out.carve) == 5 && in.carve != inR.carve,
            "corner stairs keep their corner chiseled (straight 6, inner 7, outer 5 of 8; left and right differ)");
    }
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
    check(crops, "the immersive crops: barley, oat, rye, jute, garlic, soybean, green beans, squash, each planted from its seed");
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

  printf("[farmland]\n");
  {   // moisture: water near keeps it moist, away from water it dries and goes back to dirt; landing hard on it tramples it
    const BlockId FL = reg().blockId("farmland"), DI = reg().blockId("dirt"), WA = reg().blockId("water"), WH = reg().blockId("wheat_crop"), ST = reg().blockId("stone");
    const int y = rg.Y + 1;
    for (int x = 1; x <= 12; x++) for (int z = 1; z <= 3; z++) { w.setBlock(x, y, z, ST); w.setBlock(x, y + 1, z, AIR); w.setBlock(x, y + 2, z, AIR); }
    w.setBlock(1, y, 2, WA); w.setBlock(2, y, 2, FL, 0); w.setBlock(11, y, 2, FL, 3); w.setBlock(12, y, 2, FL, 0);
    farmlandTick(*rg.lv, w, 2, y, 2, w.getMeta(2, y, 2));
    check(w.getBlock(2, y, 2) == FL && (w.getMeta(2, y, 2) & 7) == 7, "farmland by water turns moist");
    check(farmlandMoist(w, 2, y, 2) && !farmlandMoist(w, 12, y, 2), "moist by the water, dry far from it");
    farmlandTick(*rg.lv, w, 11, y, 2, w.getMeta(11, y, 2));
    check((w.getMeta(11, y, 2) & 7) == 2, "away from water it dries a step at a time");
    farmlandTick(*rg.lv, w, 12, y, 2, w.getMeta(12, y, 2));
    check(w.getBlock(12, y, 2) == DI, "dry farmland with nothing planted goes back to dirt");
    w.setBlock(11, y, 2, FL, 0); w.setBlock(11, y + 1, 2, WH, 3);
    farmlandTick(*rg.lv, w, 11, y, 2, 0);
    check(w.getBlock(11, y, 2) == FL, "...but not while something grows on it");
    trampleFarmland(*rg.lv, w, 11, y, 2);
    check(w.getBlock(11, y, 2) == DI && w.getBlock(11, y + 1, 2) == AIR, "trampled: back to dirt, the crop knocked off");
    w.setBlock(2, y + 1, 2, WH, 0);
    Creature* cow = spawnCreature(*rg.lv, 0, "cow", vec3(2.5f, y + 3.5f, 2.5f));
    rg.tick(1.5);
    check(cow && w.getBlock(2, y, 2) == DI, "a cow dropping onto farmland from a few blocks up tramples it");
    if (cow) cow->removed = true;
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

  printf("[the terrain-13 Nether lands]\n");
  {
    const i32 seed = 12345;
    // a chunk all terrain-13 land and one all classic Nether, found on the map
    ivec2 bnC(INT32_MIN, 0), classicC(INT32_MIN, 0); std::set<int> lands;
    for (int cz = -40; cz < 40 && (bnC.x == INT32_MIN || classicC.x == INT32_MIN || lands.size() < 3); cz += 3) for (int cx = -40; cx < 40; cx += 3) {
      int any = 0, none = 0;
      for (int k = 0; k < 16; k++) { const u8 l = netherBnLand(seed, 13, cx * 16 + (k & 3) * 5, cz * 16 + (k >> 2) * 5); l ? any++ : none++; if (l) lands.insert(l); }
      if (any == 16 && bnC.x == INT32_MIN) bnC = ivec2(cx, cz);
      if (none == 16 && classicC.x == INT32_MIN) classicC = ivec2(cx, cz);
    }
    check(bnC.x != INT32_MIN && classicC.x != INT32_MIN, "the map has both the new Nether lands and the classic ones");
    check(lands.size() >= 2, "more than one of the new Nether lands nearby (" + std::to_string(lands.size()) + ")");
    check(netherBnLand(seed, 12, bnC.x * 16, bnC.y * 16) == PN_NONE, "terrain 12 has none of them");
    auto gen = [&](int ver, ivec2 c) {
      WorldGenOptions o; o.seed = seed; o.genVersion = ver; WorldGen g(o);
      auto ch = std::make_unique<Chunk>(DIM_NETHER, c.x, c.y); g.forDim(DIM_NETHER)->generate(*ch); return ch;
    };
    auto pnBlocks = [&](const Chunk& ch) { int n = 0; for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) if (reg().block(ch.get(x, y, z)).has("paintedNether")) n++; return n; };
    auto same = [&](const Chunk& a, const Chunk& b) { for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) if (a.get(x, y, z) != b.get(x, y, z)) return false; return true; };
    auto a12 = gen(12, bnC), a13 = gen(13, bnC), b13 = gen(13, bnC);
    check(pnBlocks(*a12) == 0, "terrain 12: the classic Nether as it was (no new-land blocks)");
    int diff = 0; for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) if (a12->get(x, y, z) != a13->get(x, y, z)) diff++;
    printf("     (land %s: %d blocks differ from terrain 12)\n", pnLandName(netherBnLand(seed, 13, bnC.x * 16 + 8, bnC.y * 16 + 8)), diff);
    check(diff > 60, "terrain 13 repaints the new Nether land (" + std::to_string(diff) + " blocks)");
    check(pnBlocks(*a13) > 20 || netherBnLand(seed, 13, bnC.x * 16 + 8, bnC.y * 16 + 8) != PN_JUNGLE, "terrain 13: the new lands' ground and plants there (" + std::to_string(pnBlocks(*a13)) + " blocks)");
    check(same(*a13, *b13), "the same chunk generates the same twice");
    // each land found within 1500 blocks: up to three chunks of it (one may be a fortress), the best repainted
    std::map<int, std::vector<ivec2>> some;
    for (int cz = -90; cz < 90; cz += 2) for (int cx = -90; cx < 90; cx += 2) {
      const u8 l = netherBnLand(seed, 13, cx * 16, cz * 16);
      if (!l || some[l].size() >= 3) continue;
      bool all = true; for (int k = 0; k < 16 && all; k++) all = netherBnLand(seed, 13, cx * 16 + (k & 3) * 5, cz * 16 + (k >> 2) * 5) == l;
      if (all) some[l].push_back(ivec2(cx, cz));
    }
    for (auto& [l, cs] : some) {
      if (cs.empty()) continue;
      int best = 0, bn = 0;
      for (ivec2 c : cs) {
        auto x12 = gen(12, c), x13 = gen(13, c);
        int diff = 0; for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) if (x12->get(x, y, z) != x13->get(x, y, z)) diff++;
        if (diff > best) { best = diff; bn = pnBlocks(*x13); }
      }
      check(best > 40, std::string("  ") + pnLandName(l) + ": repainted (" + std::to_string(best) + " blocks, " + std::to_string(bn) + " of the new lands')");
    }
    check(same(*gen(12, classicC), *gen(13, classicC)), "outside the new Nether lands terrain 13 is terrain 12, block for block");
  }

  printf("[growing trees (terrain 13)]\n");
  {
    const Registry& R = reg();
    const BlockId DIRT = R.blockId("dirt"), SAP = R.blockId("sapling"), BR = R.blockId("oak_branch"), ROOTY = R.blockId("rooty_dirt"), LEAVES = R.blockId("leaves");
    const int X = 8, Y = rg.Y, Z = 8;
    for (int x = -2; x <= 18; x++) for (int z = -2; z <= 18; z++) for (int y = Y + 1; y < Y + 40; y++) w.setBlock(x, y, z, AIR);
    w.setBlock(X, Y, Z, DIRT); w.setBlock(X, Y + 1, Z, SAP);
    check(dynTreesOn(*rg.lv), "a new world grows its trees (terrain " + std::to_string(rg.lv->info.genVersion) + ")");
    check(growTree(*rg.lv, w, X, Y + 1, Z, 0.5), "a sapling takes root");
    check(w.getBlock(X, Y, Z) == ROOTY && (w.getMeta(X, Y, Z) & 15) == 15, "  rooty soil under it, full of life");
    check(w.getBlock(X, Y + 1, Z) == BR && branchRadius(BR, w.getMeta(X, Y + 1, Z)) == 1, "  a twig of oak where it stood");
    check(w.getBlock(X, Y + 2, Z) == LEAVES, "  a leaf on top");
    int grew = 0;
    for (u32 k = 0; k < 400; k++) { w.setBlock(X, Y, Z, ROOTY, 15); if (growDynTree(*rg.lv, w, X, Y, Z, k * 2654435761u)) grew++; }
    int parts = 0, maxUp = 0; bool taper = true;
    for (int x = -2; x <= 18; x++) for (int z = -2; z <= 18; z++) for (int y = Y + 1; y < Y + 40; y++) if (w.getBlock(x, y, z) == BR) { parts++; maxUp = std::max(maxUp, y - Y); }
    const int base = branchRadius(BR, w.getMeta(X, Y + 1, Z));
    for (int y = Y + 1; y < Y + 8; y++) {
      const int r0 = branchRadius(w.getBlock(X, y, Z), w.getMeta(X, y, Z)), r1 = branchRadius(w.getBlock(X, y + 1, Z), w.getMeta(X, y + 1, Z));
      if (r1 > r0) taper = false;
    }
    check(grew > 30 && parts > 20 && maxUp >= 6, "400 growth signals grow it to a grown tree (" + std::to_string(grew) + " grew, " + std::to_string(parts) + " branch blocks, " + std::to_string(maxUp) + " tall)");
    check(base >= 4, "  its trunk thickens at the foot (radius " + std::to_string(base) + " of 8)");
    check(taper, "  and thins upward (no branch thicker than the one under it)");
    // the soil spends its life; dead soil grows nothing
    int life = 15; for (u32 k = 0; k < 200; k++) growDynTree(*rg.lv, w, X, Y, Z, 77 + k * 31u); life = w.getMeta(X, Y, Z) & 15;
    check(life < 15, "  growing spends the soil's life (" + std::to_string(life) + " of 15 left)");
    w.setBlock(X, Y, Z, ROOTY, 0);
    check(!growDynTree(*rg.lv, w, X, Y, Z, 5), "  dead soil grows no more");
    // felled: by volume
    std::vector<std::pair<BlockId, u8>> wood;
    for (int x = -2; x <= 18; x++) for (int z = -2; z <= 18; z++) for (int y = Y + 1; y < Y + 40; y++) if (w.getBlock(x, y, z) == BR) wood.push_back({BR, w.getMeta(x, y, z)});
    std::map<std::string, int> logs; int sticks = 0; dynTreeYield(wood, logs, sticks);
    check(logs["log"] >= 1 && logs["log"] < (int)wood.size(), "felled, it gives oak logs by its wood's volume (" + std::to_string(logs["log"]) + " logs, " + std::to_string(sticks) + " sticks from " + std::to_string(wood.size()) + " blocks)");
    std::map<std::string, int> one; int st1 = 0; dynTreeYield({{BR, branchMeta(8)}}, one, st1);
    check(one["log"] == 1 && st1 == 0, "  a whole-block branch is one log");
    check(branchRadius(BR, 0) == 4 && branchRadius(BR, 1) == 3 && branchRadius(R.blockId("log"), 0) == 8, "the old generated branches keep their size");
    // the trunk gone: the soil turns back to dirt
    w.setBlock(X, Y, Z, ROOTY, 10); w.setBlock(X, Y + 1, Z, AIR);
    growDynTree(*rg.lv, w, X, Y, Z, 9);
    check(w.getBlock(X, Y, Z) == DIRT, "rooty soil with no tree is dirt again");
    for (int x = -2; x <= 18; x++) for (int z = -2; z <= 18; z++) for (int y = Y + 1; y < Y + 40; y++) w.setBlock(x, y, z, AIR);
    // new land: grown trees (gen/dyntree_gen.cpp)
    {
      auto gen = [&](int ver, int cx, int cz) { WorldGenOptions o; o.seed = 4242; o.genVersion = ver; WorldGen g(o);
        auto ch = std::make_unique<Chunk>(DIM_OVERWORLD, cx, cz); g.forDim(DIM_OVERWORLD)->generate(*ch); return ch; };
      int rooty13 = 0, rooty12 = 0, thick = 0, thin = 0, floating = 0;
      for (int c = 0; c < 60 && rooty13 < 8; c++) {
        auto a = gen(13, c * 5, c * 3), o = gen(12, c * 5, c * 3);
        for (int y = 1; y < DIM_H - 1; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
          if (a->get(x, y, z) == ROOTY) { rooty13++; const BlockId t = a->get(x, y + 1, z); if (R.block(t).has("dynTree")) { const int r = branchRadius(t, a->getMeta(x, y + 1, z)); r >= 3 ? thick++ : thin++; } else floating++; }
          if (o->get(x, y, z) == ROOTY) rooty12++;
        }
      }
      check(rooty13 > 3 && rooty12 == 0, "new land's trees stand in rooty soil (" + std::to_string(rooty13) + "; terrain 12: " + std::to_string(rooty12) + ")");
      check(thick > thin && floating == 0, "  with trunks thick at the foot (" + std::to_string(thick) + " thick, " + std::to_string(thin) + " thin, " + std::to_string(floating) + " without a trunk)");
    }
    rg.lv->info.genVersion = 12;
    check(!dynTreesOn(*rg.lv), "terrain 12 worlds keep their trees as they were");
    rg.lv->info.genVersion = 13;
  }

  printf("[terrain 23: no land floating in the air, its overhangs kept]\n");
  {   // (a deep world, as new worlds are; a stretch of seed 421506624 where terrain 22 left a lump of rock hanging in the sky)
    setWorldLayout(2, 4);
    const Registry& R = reg();
    // the biggest piece of solid ground (no trees, no plants) with nothing joining it to the land under band y 48 or to the
    // area's edge
    auto biggestFloating = [&](int ver) {
      WorldGenOptions o; o.seed = 421506624; o.genVersion = ver; WorldGen g(o);
      const int n = 9, W = n * 16, cx0 = 33, cz0 = 17;
      std::vector<u8> m((size_t)W * W * DIM_H, 0);
      auto at = [&](int x, int y, int z) -> u8& { return m[((size_t)y * W + z) * W + x]; };
      for (int cz = 0; cz < n; cz++) for (int cx = 0; cx < n; cx++) {
        Chunk ch(DIM_OVERWORLD, cx0 + cx, cz0 + cz); g.forDim(DIM_OVERWORLD)->generate(ch);
        for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
          const BlockId id = ch.get(x, y, z); if (!id || !R.tSolid[id] || !R.tOpaque[id] || R.tLeaves[id]) continue;   // (the land: whole solid blocks)
          const std::string& k = R.block(id).key;
          if (k.find("log") != std::string::npos || k.find("wood") != std::string::npos || k.find("branch") != std::string::npos) continue;
          at(cx * 16 + x, y, cz * 16 + z) = 1;
        }
      }
      int biggest = 0; std::vector<ivec3> st;
      for (int y = 48; y < DIM_H; y++) for (int z = 0; z < W; z++) for (int x = 0; x < W; x++) {
        if (at(x, y, z) != 1) continue;
        bool grounded = false; int size = 0; st.assign(1, ivec3(x, y, z)); at(x, y, z) = 2;
        while (!st.empty()) {
          const ivec3 c = st.back(); st.pop_back(); size++;
          if (c.y < 48 || c.x == 0 || c.z == 0 || c.x == W - 1 || c.z == W - 1) grounded = true;
          for (ivec3 d : {ivec3(1, 0, 0), ivec3(-1, 0, 0), ivec3(0, 1, 0), ivec3(0, -1, 0), ivec3(0, 0, 1), ivec3(0, 0, -1)}) {
            const ivec3 q = c + d;
            if (q.x < 0 || q.z < 0 || q.x >= W || q.z >= W || q.y < 0 || q.y >= DIM_H || at(q.x, q.y, q.z) != 1) continue;
            at(q.x, q.y, q.z) = 2; st.push_back(q);
          }
        }
        if (!grounded) biggest = std::max(biggest, size);
      }
      return biggest;
    };
    const int b22 = biggestFloating(22), b23 = biggestFloating(23);
    check(b22 > 500, "terrain 22 here: a lump of rock hanging in the air (" + std::to_string(b22) + " blocks)");
    check(b23 < 20, "terrain 23: none (the biggest piece left " + std::to_string(b23) + " blocks)");
    setWorldLayout(1, 4);
  }
  printf("[the Dappled Forest and leaf litter (terrain 26)]\n");
  {
    const Registry& R = reg();
    const char* colours[5] = {"green", "red", "orange", "yellow", "brown"};
    bool thin = true;
    for (const char* c : colours) {
      const BlockId id = R.blockId(std::string("leaf_litter_") + c);
      const BlockDef& d = R.block(id);
      thin = thin && id && d.shapeKind == 10 && d.replaceable && d.hardness <= 0.2f && d.drop == d.key && d.sound == "leaves";
    }
    check(thin, "leaf litter in five colours: a thin carpet, replaceable, quick to break, drops itself");
    std::vector<ivec2> wood;   // chunks inside a Dappled Forest (found by its biome, in a terrain 26 world)
    {
      WorldGenOptions o; o.seed = 4242; o.genVersion = 26; WorldGen g(o); auto* og = dynamic_cast<OverworldGen*>(g.forDim(DIM_OVERWORLD));
      for (int z = -3000; z < 3000 && wood.size() < 10; z += 48) for (int x = -3000; x < 3000 && wood.size() < 10; x += 48)
        if (og && og->landBiomeName(x, z) == "dappled:dappled_forest") wood.push_back(ivec2(x >> 4, z >> 4));
    }
    check(wood.size() >= 10, "the Dappled Forest turns up across the land (" + std::to_string(wood.size()) + " places found)");
    auto count = [&](int ver, int& poplar, int& litter, int& logs, int& shrub) {
      WorldGenOptions o; o.seed = 4242; o.genVersion = ver; WorldGen g(o);
      const BlockId lg = R.blockId("poplar_log"), h1 = R.blockId("hollow_poplar_log"), h2 = R.blockId("hollow_poplar_log_fungus"), sh = R.blockId("red_shrub_leaves");
      BlockId lit[5]; for (int k = 0; k < 5; k++) lit[k] = R.blockId(std::string("leaf_litter_") + colours[k]);
      poplar = litter = logs = shrub = 0;
      for (ivec2 cc : wood) {
        const int cx = cc.x, cz = cc.y;
        auto ch = std::make_unique<Chunk>(DIM_OVERWORLD, cx, cz); g.forDim(DIM_OVERWORLD)->generate(*ch);
        for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
          const BlockId b = ch->get(x, y, z);
          if (b == lg) poplar++; else if (b == h1 || b == h2) logs++; else if (b == sh) shrub++;
          else for (BlockId l : lit) if (b == l) litter++;
        }
      }
    };
    int p26, l26, g26, s26, p25, l25, g25, s25;
    count(26, p26, l26, g26, s26); count(25, p25, l25, g25, s25);
    printf("     (terrain 26: %d poplar logs, %d litter, %d hollow logs, %d shrub leaves)\n", p26, l26, g26, s26);
    check(p26 > 20, "terrain 26: Dappled Forests grow poplars");
    check(l26 > 200, "terrain 26: leaf litter lies under the woods");
    check(g26 > 0, "terrain 26: fallen hollow poplar logs");
    check(s26 > 0, "terrain 26: red shrubs");
    check(p25 == 0 && l25 == 0 && g25 == 0 && s25 == 0, "terrain 25 (an old world) has none of it: its land is unchanged");
  }
  printf("\n%d passed, %d failed\n", passes, fails);
  rg.lv->close();
  return fails;
}
