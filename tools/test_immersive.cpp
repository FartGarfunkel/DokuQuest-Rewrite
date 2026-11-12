// tools/test_immersive.cpp — headless checks for the immersive progression's stone age (sim/immersive.h, sim/knapping.h,
// after TerraFirmaCraft): the world rule (old saves stay classic), trees need an axe, leaves you fall through, sticks from
// leaves, the ground decorations, knapping patterns, the recipes per mode, and the knapping screen being asked for.
// Run: build/test_immersive (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/rules.h"
#include "sim/events.h"
#include "sim/immersive.h"
#include "sim/knapping.h"
#include "sim/crafting.h"
#include "common/fs.h"
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
  std::string ud = (fs::temp_directory_path() / "dq-test-immersive").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  const BlockId LOG = reg().blockId("log"), LEAVES = reg().blockId("leaves");

  printf("[the rule]\n");
  {
    Rig old("OldSave", false, false);
    check(!immersive(*old.lv), "a save without the rule is classic");
    PlayerController pc(*old.lv, *old.P);
    for (auto& s : old.P->inv) s = ItemStack();
    check(std::isfinite(pc.breakTime(LOG, nullptr)), "classic: logs come by hand");
    old.lv->world().setBlock(4, old.Y + 1, 4, LEAVES);
    old.P->pos = vec3(4.5f, old.Y + 2.2f, 4.5f); old.P->vel = vec3(0); old.P->onGround = false;
    old.fall(1.5);
    check(old.P->pos.y >= old.Y + 1.95f, "classic: you stand on leaves");
    old.lv->close();
  }
  Rig im("Immersive", true, true);
  World& w = im.lv->world();
  {
    check(immersive(*im.lv), "an immersive world");
    PlayerController pc(*im.lv, *im.P);
    for (auto& s : im.P->inv) s = ItemStack();
    im.P->selected = 0;
    check(!std::isfinite(pc.breakTime(LOG, nullptr)), "immersive: no logs by hand");
    im.P->inv[0] = ItemStack("stone_axe", 1);
    check(std::isfinite(pc.breakTime(LOG, nullptr)), "  ...but with an axe");
    im.P->inv[0] = ItemStack("stone_pickaxe", 1);
    check(!std::isfinite(pc.breakTime(LOG, nullptr)), "  ...not with a pickaxe");
    w.setBlock(4, im.Y + 1, 4, LEAVES);
    im.P->pos = vec3(4.5f, im.Y + 2.2f, 4.5f); im.P->vel = vec3(0); im.P->onGround = false;
    im.fall(1.5);
    check(im.P->pos.y < im.Y + 1.6f, "immersive: you sink through leaves");
    im.P->pos = vec3(14.5f, im.Y + 1.05f, 14.5f);
    im.P->inv[0] = ItemStack();
    int sticks = 0;
    for (int i = 0; i < 400; i++) { w.setBlock(14, im.Y + 3, 14, LEAVES); pc.breakBlock(14, im.Y + 3, 14); }
    for (auto& e : im.lv->entities()) if (!e->removed && e->type == "item" && e->data.value("stack", json::object()).value("id", "") == "stick") { sticks += e->data["stack"].value("n", 1); e->removed = true; }
    check(sticks >= 70 && sticks <= 130, "leaves drop a stick about a quarter of the time (" + std::to_string(sticks) + " of 400)");
  }

  printf("[knapping]\n");
  {
    check(knapMaterialOf("pebble") == "stone" && knapMaterialOf("flint") == "flint" && knapMaterialOf("clay_ball") == "clay" && knapMaterialOf("stick").empty(), "what can be knapped");
    check(knapCost("stone") == 1 && knapCost("clay") == 5, "what knapping costs");
    KnapGrid full; full.kept.fill(true);
    check(knapMatch("stone", full) == nullptr, "an untouched stone is nothing yet");
    const KnapPattern* axe = nullptr;
    for (auto& p : knapPatterns("stone")) if (p.out == "stone_axe_head") axe = &p;
    check(axe != nullptr, "the stone axe head is a pattern");
    if (axe) {
      auto carve = [&](int ox, int oy, bool mirror) {
        KnapGrid g; g.kept.fill(false);
        const int h = (int)axe->rows.size(), wd = (int)axe->rows[0].size();
        for (int y = 0; y < h; y++) for (int x = 0; x < wd; x++) if (axe->rows[y][mirror ? wd - 1 - x : x] == '#') g.kept[(oy + y) * 5 + ox + x] = true;
        return g;
      };
      const int h = (int)axe->rows.size(), wd = (int)axe->rows[0].size();
      check(knapMatch("stone", carve(0, 0, false)) == axe, "carved at the corner it's an axe head");
      check(knapMatch("stone", carve(5 - wd, 5 - h, false)) == axe, "  ...and at the other corner");
      check(knapMatch("stone", carve(0, 0, true)) == axe, "  ...and mirrored");
      KnapGrid g = carve(0, 0, false);
      for (int i = 0; i < 25; i++) if (g.kept[i]) { g.kept[i] = false; break; }
      check(knapMatch("stone", g) == nullptr, "one chip too many: nothing");
    }
    bool shapes = true, distinct = true;
    for (const char* m : {"stone", "flint", "clay"}) {
      const auto& ps = knapPatterns(m);
      for (size_t i = 0; i < ps.size(); i++) {
        if (ps[i].rows.empty() || ps[i].rows.size() > 5) shapes = false;
        for (auto& r : ps[i].rows) if (r.size() > 5 || r.size() != ps[i].rows[0].size()) shapes = false;
        for (size_t j = 0; j < i; j++) if (ps[j].rows == ps[i].rows) distinct = false;
        if (!reg().item(ps[i].out)) { shapes = false; printf("    unknown item %s\n", ps[i].out.c_str()); }
      }
    }
    check(shapes, "every pattern fits the 5x5 grid and makes a real item");
    check(distinct, "no two patterns of one material are the same");
    check(knapPatterns("stone").size() >= 7 && knapPatterns("flint").size() >= 7, "TFC's stone shapes, and the crude pickaxe");
  }

  printf("[tools and recipes]\n");
  {
    auto grid = [](std::vector<std::pair<int, std::string>> cells, int W) { std::vector<ItemStack> g(W * W); for (auto& [i, k] : cells) g[i] = ItemStack(k, 1); return g; };
    recipeGate().immersive = true;
    CraftMatch a = matchGrid(grid({{0, "stone_axe_head"}, {1, "stick"}}, 2), 2);
    check(a && a.out == "stone_axe", "a stone axe head and a stick make a stone axe");
    CraftMatch k = matchGrid(grid({{0, "flint_knife_head"}, {2, "stick"}}, 2), 2);
    check(k && k.out == "flint_knife", "a flint knife head and a stick make a flint knife");
    CraftMatch c = matchGrid(grid({{0, "crude_pickaxe_head"}, {3, "stick"}}, 2), 2);
    check(c && c.out == "crude_pickaxe", "and the crude pickaxe");
    auto woodPick = grid({{0, "planks"}, {1, "planks"}, {2, "planks"}, {4, "stick"}, {7, "stick"}}, 3);
    check(!matchGrid(woodPick, 3), "immersive: no wooden pickaxe");
    auto stonePick = grid({{0, "cobblestone"}, {1, "cobblestone"}, {2, "cobblestone"}, {4, "stick"}, {7, "stick"}}, 3);
    check(!matchGrid(stonePick, 3), "immersive: no cobblestone pickaxe");
    auto mixPick = grid({{0, "planks"}, {1, "cobblestone"}, {2, "planks"}, {4, "stick"}, {7, "stick"}}, 3);
    check(!matchGrid(mixPick, 3), "immersive: no mixed wood-and-stone pickaxe from the grid either");
    auto ingotPick = grid({{0, "copper_ingot"}, {1, "iron_ingot"}, {2, "copper_ingot"}, {4, "stick"}, {7, "stick"}}, 3);
    check(!matchGrid(ingotPick, 3), "immersive: ingots don't make a tool at the table (cast or smith a head)");
    auto ingotHelm = grid({{0, "copper_ingot"}, {1, "iron_ingot"}, {2, "copper_ingot"}, {3, "copper_ingot"}, {5, "copper_ingot"}}, 3);
    check(!matchGrid(ingotHelm, 3), "immersive: nor armour (the anvil makes it)");
    recipeGate().immersive = false;
    CraftMatch wp = matchGrid(woodPick, 3);
    check(wp && wp.out == "wood_pickaxe", "classic: the wooden pickaxe as ever");
    check((bool)matchGrid(mixPick, 3), "classic: the mixed pickaxe as ever");
    auto tj = readJson(dataDir() / "tiles.json");
    bool named = true;
    for (const char* key : {"stone_axe_head", "stone_shovel_head", "stone_hoe_head", "stone_knife_head", "stone_hammer_head", "stone_javelin_head", "crude_pickaxe_head",
                            "flint_knife_head", "flint_hammer_head", "flint_javelin_head", "stone_knife", "stone_hammer", "stone_javelin", "flint_knife", "flint_hammer", "flint_javelin", "crude_pickaxe"}) {
      const ItemDef* d = reg().item(key);
      if (!d || d->name.empty() || d->str("icon").empty() || !tj || !(*tj)["names"].contains(d->str("icon"))) { named = false; printf("    %s\n", key); }
    }
    check(named, "every new item has a name and a picture");
  }

  printf("[opening knapping]\n");
  {
    std::string opened;
    im.bus.on("screenOpen", [&](const json& a) { if (a.is_array() && a.size() > 2 && a[1] == "knapping") opened = a[2].value("material", ""); return true; });
    PlayerController pc(*im.lv, *im.P);
    for (auto& s : im.P->inv) s = ItemStack();
    im.P->selected = 0; im.P->sneaking = false;
    pc.target = RayHit{};
    im.P->inv[0] = ItemStack("pebble", 1); pc.use(false, PlayerInput{});
    check(opened.empty(), "one pebble isn't enough to knap");
    im.P->inv[0] = ItemStack("pebble", 3); pc.use(false, PlayerInput{});
    check(opened == "stone", "use with pebbles in the air: knapping stone");
    opened.clear(); im.P->inv[0] = ItemStack("flint", 2); pc.use(false, PlayerInput{});
    check(opened == "flint", "  ...and flint");
    opened.clear(); im.P->inv[0] = ItemStack("clay_ball", 4); pc.use(false, PlayerInput{});
    check(opened.empty(), "four clay isn't enough");
  }

  printf("[on the ground]\n");
  {
    const BlockId LS = reg().blockId("loose_stones"), TW = reg().blockId("twigs"), FL = reg().blockId("loose_flint");
    check(TW && FL, "twigs and loose flint exist");
    int rocks = 0, twigs = 0, flint = 0, total = 0, chunks = 0, grounded = 0, land = 0;
    for (int ox : {600, 1400, 2600}) {
      const vec3 at(ox + 8, dimBase(DIM_OVERWORLD) + 100, ox + 8);
      double t0 = nowSeconds();
      while (nowSeconds() - t0 < 60) {
        w.stream({{at, 3, false}}, 40);
        bool all = true;
        for (int cx = -2; cx <= 1; cx++) for (int cz = -2; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, (ox >> 4) + cx, (ox >> 4) + cz)) all = false;
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      for (int x = ox - 32; x < ox + 32; x++) for (int z = ox - 32; z < ox + 32; z++) for (int y = dimBase(DIM_OVERWORLD) + 20; y < dimBase(DIM_OVERWORLD) + 250; y++) {
        const BlockId b = w.getBlock(x, y, z);
        if (b != LS && b != TW && b != FL) continue;
        total++; if (b == LS) rocks++; else if (b == TW) twigs++; else flint++;
        if (reg().tSolid[w.getBlock(x, y - 1, z)]) grounded++;
      }
      for (int x = ox - 32; x < ox + 32; x++) for (int z = ox - 32; z < ox + 32; z++) {   // land columns (the top isn't water)
        for (int y = dimBase(DIM_OVERWORLD) + 250; y > dimBase(DIM_OVERWORLD) + 20; y--) { const BlockId b = w.getBlock(x, y, z); if (!b) continue; if (!reg().tLiquid[b]) land++; break; }
      }
      chunks += 16;
    }
    const float perChunk = land ? rocks * 256.f / land : 0;
    check(perChunk >= 1.5f && perChunk <= 6, "loose rocks lie about, not too thick (" + std::to_string(rocks) + " on " + std::to_string(land) + " land columns: " + std::to_string(perChunk) + " a chunk of land)");
    check(twigs >= chunks / 8, "twigs in the woods and on beaches (" + std::to_string(twigs) + ")");
    check(grounded == total, "every one lies on solid ground");
    printf("  (flint: %d)\n", flint);
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  im.lv->close();
  return fails;
}
