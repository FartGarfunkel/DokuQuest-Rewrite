// tools/test_materials.cpp — headless checks for crafting and materials (after TerraFirmaCraft): the saw and lumber, the
// quern, the sealable barrel and leather, panning and the sluice. Run: build/test_materials (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "core/metals.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/rules.h"
#include "sim/events.h"
#include "sim/crafting.h"
#include "sim/worldsys.h"
#include "sim/quern.h"
#include "sim/barrel.h"
#include "sim/panning.h"
#include "sim/spinning.h"
#include "sim/firepit.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include <thread>
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
  std::string ud = (fs::temp_directory_path() / "dq-test-materials").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }

  printf("[saw and lumber]\n");
  {
    std::vector<ItemStack> g(4);
    g[0] = ItemStack("log", 3); g[1] = ItemStack("flint_saw", 1);
    recipeGate().immersive = true;
    CraftMatch m = matchGrid(g, 2);
    check(m && m.out == "lumber" && m.n == 8, "a log and a saw: eight lumber");
    if (m) consumeCraft(g, m, [](const ItemStack&) {});
    check(g[0].n == 2 && g[1].id == "flint_saw" && g[1].d == 1, "  ...the log used, the saw kept and worn");
    std::vector<ItemStack> l(4); l[0] = ItemStack("log", 1);
    CraftMatch p = matchGrid(l, 2);
    check(!p, "immersive: no planks from a log by hand (a saw makes lumber)");
    recipeGate().immersive = false;
    p = matchGrid(l, 2);
    check(p && p.out == "planks" && p.n == 4, "classic: four, as ever");
    recipeGate().immersive = true;   // (lumber is an immersive world's)
    std::vector<ItemStack> lu(4); for (int i = 0; i < 4; i++) lu[i] = ItemStack("lumber", 1);
    CraftMatch pl = matchGrid(lu, 2);
    check(pl && pl.out == "planks", "four lumber make planks");
    std::vector<ItemStack> sb(9); sb[1] = ItemStack("lumber", 1); sb[4] = ItemStack("lumber", 1); sb[7] = ItemStack("lumber", 1);
    CraftMatch sm = matchGrid(sb, 3);
    check(sm && sm.out == "support_beam", "lumber makes support beams");
  }

  printf("[quern]\n");
  {
    check(quernResult("wheat").first == "flour" && quernResult("copper_ore_piece").first == "copper_ore_powder" && quernResult("limestone").first == "flux", "wheat, ore and limestone grind");
    check(unitsOf("copper_ore_powder") == 30 && unitsOf("copper_ore_piece") == 25, "powder holds more metal than the piece it came from");
    check(quernResult("diamond").first.empty(), "a diamond doesn't");
  }
  Rig rg("Materials", true, true);
  World& w = rg.lv->world();
  {
    const BlockId Q = reg().blockId("quern");
    check(Q != 0, "the quern");
    w.setBlock(4, rg.Y + 1, 4, Q);
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; rg.P->inv[0] = ItemStack("wheat", 3);
    pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(4, rg.Y + 1, 4); pc.target.normal = ivec3(0, 1, 0); pc.target.id = Q;
    pc.use(false, PlayerInput{}); for (int i = 0; i < 6; i++) pc.use(true, PlayerInput{});
    int flour = 0; for (auto& e : rg.lv->entities()) if (!e->removed && e->type == "item" && e->data.value("stack", json::object()).value("id", "") == "flour") flour++;
    check(rg.P->inv[0].n == 2 && flour == 1, "hold use on the quern with wheat: flour");
  }

  printf("[barrel and leather]\n");
  {
    const BlockId BAR = reg().blockId("sealed_barrel");
    check(BAR != 0, "the sealable barrel");
    const int X = 8, Z = 4, Y1 = rg.Y + 1;
    w.setBlock(X, Y1, Z, BAR);
    for (int i = 0; i < 4; i++) { ItemStack wb("water_bucket", 1); check(barrelPour(w, X, Y1, Z, wb) && wb.id == "bucket", i == 0 ? "water poured in" : ""); }
    ItemStack lime("lime", 1);
    check(barrelPut(w, X, Y1, Z, lime) && barrelLiquid(w, X, Y1, Z) == "limewater", "lime in water: limewater");
    ItemStack hide("raw_hide", 2);
    check(barrelPut(w, X, Y1, Z, hide), "two hides in");
    rg.tick(gameHourSecs(*rg.lv) * 4);
    check(barrelItem(w, X, Y1, Z, 0).id == "raw_hide", "unsealed, nothing happens");
    check(barrelSeal(*rg.lv, w, X, Y1, Z, true), "sealed");
    rg.tick(gameHourSecs(*rg.lv) * 3 + 5);
    check(barrelItem(w, X, Y1, Z, 0).id == "soaked_hide" && barrelItem(w, X, Y1, Z, 0).n == 2, "three hours sealed: soaked hides");
    ItemStack soaked("soaked_hide", 1), knife("flint_knife", 1);
    check(scrapeHide(soaked, knife) && soaked.id == "scraped_hide" && knife.d == 1, "a knife scrapes a soaked hide");
    std::vector<ItemStack> g(4); g[0] = ItemStack("prepared_hide", 1);
    recipeGate().immersive = true;
    CraftMatch m = matchGrid(g, 2);
    check(m && m.out == "leather" && m.n == 2, "a prepared hide makes leather");
    check(hideFor("leather", true) == "raw_hide" && hideFor("leather", false) == "leather", "immersive: animals drop raw hide");
  }

  printf("[panning and the sluice]\n");
  {
    check(panFind(0.05f, 0.1f).id.find("_ore_sample") != std::string::npos, "a lucky swirl: an ore sample (" + panFind(0.05f, 0.1f).id + ")");
    check(!panFind(0.31f, 0.5f).empty() && panFind(0.9f, 0.5f).empty(), "now and then a gem; mostly nothing");
    const BlockId GRAVEL = reg().blockId("gravel"), WATER = reg().blockId("water"), SLUICE = reg().blockId("sluice");
    const BlockId STONE = reg().blockId("stone");
    for (int x = 10; x < 20; x++) for (int z = 8; z < 18; z++) w.setBlock(x, rg.Y - 1, z, STONE);   // (the test floor floats: gravel needs something under it)
    w.setBlock(12, rg.Y, 10, GRAVEL); w.setBlock(13, rg.Y, 10, WATER);
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; rg.P->inv[0] = ItemStack("ceramic_pan", 1);
    pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(12, rg.Y, 10); pc.target.normal = ivec3(0, 1, 0); pc.target.id = GRAVEL;
    setPanRoll(0.05f);
    pc.use(false, PlayerInput{}); for (int i = 0; i < 10; i++) pc.use(true, PlayerInput{});
    int samples = 0; for (auto& st : rg.P->inv) if (st.id.find("_ore_sample") != std::string::npos) samples += st.n;
    check(samples >= 1, "panning gravel by the water: an ore sample");
    w.setBlock(12, rg.Y, 13, GRAVEL);
    pc.target.block = ivec3(12, rg.Y, 13);
    const int before = samples;
    pc.use(false, PlayerInput{}); for (int i = 0; i < 10; i++) pc.use(true, PlayerInput{});
    samples = 0; for (auto& st : rg.P->inv) if (st.id.find("_ore_sample") != std::string::npos) samples += st.n;
    check(samples == before, "no water beside it: nothing");
    setPanRoll(-1);
    check(SLUICE != 0, "the sluice");
    w.setBlock(15, rg.Y + 1, 15, SLUICE); w.setBlock(16, rg.Y + 1, 15, WATER); w.setBlock(15, rg.Y, 15, GRAVEL);
    w.setBlock(18, rg.Y + 1, 15, SLUICE);
    setPanRoll(0.05f);
    rg.tick(65);
    setPanRoll(-1);
    check(!sluiceTake(w, 15, rg.Y + 1, 15).empty(), "a sluice in the water finds something in a minute");
    check(sluiceTake(w, 18, rg.Y + 1, 15).empty(), "a dry one finds nothing");
    {   // and by hand: an empty hand on the sluice takes its find (the use path, not sluiceTake)
      setPanRoll(0.05f); rg.tick(65); setPanRoll(-1);
      for (auto& st : rg.P->inv) st = ItemStack();
      rg.P->offhand[0] = ItemStack();
      PlayerController pc2(*rg.lv, *rg.P);
      pc2.target = RayHit{}; pc2.target.hit = true; pc2.target.block = ivec3(15, rg.Y + 1, 15); pc2.target.normal = ivec3(0, 1, 0); pc2.target.id = SLUICE;
      pc2.use(false, PlayerInput{});
      int got = 0; for (auto& st : rg.P->inv) got += st.n;
      check(got >= 1, "an empty hand on the sluice takes what it found");
    }
    {   // a broken (or worn-out) sluice spills its finds, not loses them
      json ent = {{"type", "sluice"}, {"out", json::array({ItemStack("gold_ore_sample", 1).toJson()})}};
      spillEntity(*rg.lv, w, ent, 15, rg.Y + 3, 15);
      int n = 0; for (auto& e : rg.lv->entities()) if (!e->removed && e->type == "item" && e->data.value("stack", json::object()).value("id", "") == "gold_ore_sample") n++;
      check(n == 1, "a broken sluice spills what it found");
    }
  }

  printf("[spindle and cloth]\n");
  {
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; rg.P->inv[0] = ItemStack("spindle", 1); rg.P->offhand[0] = ItemStack("wool", 2);
    pc.target = RayHit{};
    pc.use(false, PlayerInput{}); for (int i = 0; i < 12; i++) pc.use(true, PlayerInput{});
    int yarn = 0; for (auto& st : rg.P->inv) if (st.id == "yarn") yarn += st.n;
    check(yarn >= 4 && rg.P->offhand[0].n == 1, "spinning a wool block: yarn (" + std::to_string(yarn) + ")");
    std::vector<ItemStack> g(4); for (int i = 0; i < 4; i++) g[i] = ItemStack("yarn", 1);
    recipeGate().immersive = true;
    CraftMatch m = matchGrid(g, 2);
    check(m && m.out == "wool_cloth", "four yarn weave wool cloth");
  }

  printf("[classic crafting and smelting]\n");
  {
    recipeGate().immersive = false;
    std::vector<ItemStack> g(9); g[1] = ItemStack("stick", 1); g[4] = ItemStack("iron_ingot", 1);
    CraftMatch m = matchGrid(g, 3);
    printf("  (stick over iron ingot -> %s)\n", m ? m.out.c_str() : "nothing");
    check(m && m.out.find("chisel") != std::string::npos, "classic: a stick over an iron ingot is a chisel");
    check(smeltResult("bismuth_ore", "furnace") == "bismuth_ingot" && smeltResult("zinc_ore", "furnace") == "zinc_ingot", "classic: bismuth and zinc ore smelt in the furnace");
    std::vector<ItemStack> a(9); a[0] = ItemStack("copper_ingot", 1); a[1] = ItemStack("copper_ingot", 1); a[2] = ItemStack("copper_ingot", 1); a[3] = ItemStack("zinc_ingot", 1); a[4] = ItemStack("bismuth_ingot", 1);
    CraftMatch al = matchGrid(a, 3);
    check(al && al.out == "bismuth_bronze_ingot", "classic: bismuth bronze from its ingots");
    std::vector<ItemStack> l(4); l[0] = ItemStack("log", 1);
    CraftMatch pl = matchGrid(l, 2);
    check(pl && pl.out == "planks" && pl.n == 4, "classic: a log is four planks");
    std::vector<ItemStack> bw(9); for (int i : {0, 1, 2, 6, 7, 8}) bw[i] = ItemStack("planks", 1); for (int i : {3, 4, 5}) bw[i] = ItemStack("leather", 1);
    CraftMatch bm = matchGrid(bw, 3);
    check(!(bm && bm.out == "bellows"), "classic: TFC's bellows aren't crafted (immersive worlds only)");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  rg.lv->close();
  return fails;
}
