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
#include "sim/worldsys.h"
#include "sim/hands.h"
#include "sim/brewing.h"
#include "sim/chest_work.h"
#include "sim/stations.h"
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
    check(old.P->pos.y < old.Y + 1.95f, "classic too: you sink through leaves (every world)");
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

  printf("[a furnace's front shows the cooking: its stages reach the players]\n");
  {
    json e = {{"type", "furnace"}, {"items", json::array({ItemStack("beef", 2).toJson(), ItemStack("coal", 4).toJson(), nullptr})}};
    check(tickFurnace(e, 0.1, "furnace"), "lighting it is sent");
    int sent = 0; double t = 0.1;
    while (t < SMELT_TIME * 0.9) { const bool ch = tickFurnace(e, 0.25, "furnace"); t += 0.25; if (ch) sent++; }
    check(sent == 2, "browning: two updates before it's done (ore glows, food turns cooked), got " + std::to_string(sent));
    check(e.value("cook", 0.0) > SMELT_TIME * 0.55, "the cooking time is in the record");
    check(smeltResult("beef", "smoker") == "cooked_beef", "beef smokes into cooked beef (what the smoker shows it turning into)");
  }

  printf("[a hands-on furnace cooks on four spots at once]\n");
  {
    json e = {{"type", "furnace"}, {"items", json::array({ItemStack("beef", 2).toJson(), ItemStack("coal", 4).toJson(), nullptr,
                                                          ItemStack("porkchop", 1).toJson(), nullptr, nullptr, nullptr, nullptr, nullptr})}};
    double t = 0; while (t < SMELT_TIME * 1.05) { tickFurnace(e, 0.25, "furnace"); t += 0.25; }
    std::vector<ItemStack> out; for (int k : {2, 6, 7, 8}) out.push_back(ItemStack::fromJson(e["items"][k]));
    bool beef = false, pork = false; for (auto& o : out) { beef |= o.id == "cooked_beef"; pork |= o.id == "cooked_porkchop"; }
    check(beef && pork, "beef and pork cooked side by side in one smelting time");
    check(ItemStack::fromJson(e["items"][0]).n == 1 && ItemStack::fromJson(e["items"][3]).empty(), "one of each taken from its spot");
    json c = {{"type", "furnace"}, {"items", json::array({ItemStack("beef", 1).toJson(), ItemStack("coal", 4).toJson(), nullptr})}};
    t = 0; while (t < SMELT_TIME * 1.05) { tickFurnace(c, 0.25, "furnace"); t += 0.25; }
    check(c["items"].size() == 3 && ItemStack::fromJson(c["items"][2]).id == "cooked_beef", "a classic furnace keeps its three slots and cooks as ever");
  }

  printf("[the hands-on grindstone: a worn piece held to the wheel]\n");
  {
    World& w = im.lv->world();
    const ivec3 at(6, im.Y + 1, 10);
    im.P->pos = vec3(8.5f, im.Y + 1, 8.5f);   // (back on the rig's floor, its chunks loaded again)
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 60 && !w.chunk(dimOfY(at.y), at.x >> 4, at.z >> 4);) { w.stream({{im.P->pos, 2, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    w.setBlock(at.x, at.y, at.z, reg().blockId("grindstone"));
    im.P->level = 5; im.P->xp = 0; im.P->mode = "survival";
    ItemStack sword("iron_sword", 1); sword.d = 200;
    GrindSession gs; double t = 100;
    for (int i = 0; i < 15; i++) { handsGrind(*im.lv, *im.P, w, at, sword, t, gs); t += 0.22; }
    check(sword.d < 200 && sword.d > 0, "ground back a little in three seconds (" + std::to_string(sword.d) + " left of 200)");
    check(im.P->level < 5, "paid for in experience");
    check(sword.tag.is_object() && sword.tag.value("gw", 0.f) > 0, "a little wear ground in for good");
    const json* e = w.blockEntity(at.x, at.y, at.z);
    check(e && e->value("grind", 0) >= 15, "the wheel's turning shows to everyone");
    ItemStack book("enchanted_book", 1); book.tag = {{"en", {{"sharp", 2}}}};
    gs = GrindSession(); t += 5;
    for (int i = 0; i < 50; i++) { handsGrind(*im.lv, *im.P, w, at, book, t, gs); t += 0.22; }
    check(book.id == "book", "an enchanted book held there ten seconds comes away plain");
  }

  printf("[the hands-on chest: its lid's buttons, standing up out of the block]\n");
  {
    World& w = im.lv->world();
    const ivec3 at(4, im.Y + 1, 12);
    w.setBlock(at.x, at.y, at.z, reg().blockId("chest"), 3);   // (facing west)
    json* e = containerEntity(w, at.x, at.y, at.z);
    for (int i = 0; i < 20; i++) (*e)["items"][i] = ItemStack(i < 15 ? "apple" : "bread", 1).toJson();
    setChestLidOpen(w, at, true);
    const int f = handsFacing(w, at);
    auto aimAt = [&](vec3 north) {   // (from in front of it, at eye height, toward a point given in its own north frame)
      const vec3 eye = vec3(at) + handsFromNorth(f, vec3(0.5f, 1.62f, -0.5f)), tgt = vec3(at) + handsFromNorth(f, north);
      return std::make_pair(eye, glm::normalize(tgt - eye));
    };
    auto [eye, dir] = aimAt(vec3(12.5f / 16, 17.5f / 16, 12.f / 16));
    check(handsChestAim(w, at, eye, dir).what == HandsChestAim::Swap, "aimed at the upper button: it's the page button");
    check(handsChestLidRay(w, at, eye, dir) > 0, "it stands out of the block and can be aimed at");
    ItemStack none;
    handsChestUse(*im.lv, *im.P, w, at, eye, dir, none, false, false);
    check(w.blockEntity(at.x, at.y, at.z)->value("page", 0) == 1, "pressed, the other page is out");
    auto [e2, d2] = aimAt(vec3(12.5f / 16, 13.3f / 16, 12.f / 16));
    check(handsChestAim(w, at, e2, d2).what == HandsChestAim::Sort, "the lower button sorts");
    auto [e3, d3] = aimAt(vec3(4.f / 16, 14.f / 16, 13.f / 16));
    check(handsChestAim(w, at, e3, d3).what == HandsChestAim::Lid, "the rest of the lid shuts it");
    std::vector<std::pair<vec3, vec3>> ol;
    check(handsChestOutline(w, at, ol) && ol.size() == 2 && ol[1].second.y > at.y + 1.3f, "open, its outline takes in the lid standing up over it");
  }

  printf("[a large chest: each half worked as the hands-on chest, its own 27]\n");
  {
    World& w = im.lv->world();
    ivec3 A(4, im.Y + 1, 18), B(5, im.Y + 1, 18), hs[2];
    int n = 0;
    for (u8 m = 0; m < 4 && n != 2; m++) { w.setBlock(A.x, A.y, A.z, reg().blockId("chest"), m); w.setBlock(B.x, B.y, B.z, reg().blockId("chest"), m); n = chestHalves(w, A, hs); }
    check(n == 2, "two chests side by side make a large one");
    check(handsChest(w, A) && handsChest(w, B), "both halves are worked as the hands-on chest");
    const int f = handsFacing(w, A);
    const ChestLayout LA = handsChestLayout(w, A), LB = handsChestLayout(w, B);
    const ivec3 left = LA.sheet ? A : B, right = LA.sheet ? B : A;
    check(LA.slots == 54 && LA.key == LB.key && LA.sheet != LB.sheet && LA.buttons != LB.buttons && LA.off != LB.off, "one chest of 54: one sheet (left lid), one pair of buttons (right lid), a page across both trays");
    auto aimAt = [&](ivec3 at, vec3 north) {
      const vec3 eye = vec3(at) + handsFromNorth(f, vec3(0.5f, 1.62f, -0.5f)), tgt = vec3(at) + handsFromNorth(f, north);
      return std::make_pair(eye, glm::normalize(tgt - eye));
    };
    ItemStack none;
    auto [e0, d0] = aimAt(left, vec3(0.5f, 0.5f, 0.3f));
    handsChestUse(*im.lv, *im.P, w, left, e0, d0, none, false, false);
    check(chestLidOpen(w, A) && chestLidOpen(w, B), "opening one half opens both lids");
    check(w.blockEntity(A.x, A.y, A.z) && w.blockEntity(B.x, B.y, B.z), "both halves have their record (both lids are drawn)");
    const vec3 btn(12.5f / 16, 17.5f / 16, 12.f / 16);
    { auto [eb, db] = aimAt(left, btn); check(handsChestAim(w, left, eb, db).what != HandsChestAim::Swap, "no buttons on the left lid"); }
    auto [eb, db] = aimAt(right, btn);
    check(handsChestAim(w, right, eb, db).what == HandsChestAim::Swap, "the right lid has the page button");
    // sixteen different things put in: the first fifteen fill the left tray, the sixteenth lies first on the right one
    static const char* K[16] = {"apple", "bread", "stick", "dirt", "cobblestone", "torch", "coal", "iron_ingot", "gold_ingot", "diamond", "feather", "string", "bone", "arrow", "flint", "wheat"};
    auto [e1, d1] = aimAt(right, vec3(2.4f / 16, 9.5f / 16, 9.5f / 16));
    check(handsChestAim(w, right, e1, d1).what == HandsChestAim::Tray, "aimed into the right half's tray");
    for (const char* k : K) { ItemStack h(k, 1); handsChestUse(*im.lv, *im.P, w, right, e1, d1, h, false, false); }
    const std::vector<ItemStack> all = chestItems(*im.lv, *im.P, w, A);
    int n16 = 0; for (auto& q : all) if (!q.empty()) n16++;
    check(all.size() == 54 && n16 == 16, "sixteen things in the large chest (" + std::to_string(n16) + ")");
    auto wheat = [&] { int n = 0; for (auto& q : im.P->inv) if (q.id == "wheat") n += q.n; return n; };
    const int w0 = wheat();
    check(handsChestTake(*im.lv, *im.P, w, right, e1, d1, true) && wheat() == w0 + 1, "the right tray's first spot holds the sixteenth");
    const int page0 = w.blockEntity(left.x, left.y, left.z)->value("page", 0);
    handsChestUse(*im.lv, *im.P, w, right, eb, db, none, false, false);
    check(w.blockEntity(left.x, left.y, left.z)->value("page", 0) == 1 - page0, "the right lid's button turns the whole chest's page (kept by the left half)");
  }

  printf("[the hands-on still: its recipe book's pages]\n");
  {
    World& w = im.lv->world();
    const ivec3 at(4, im.Y + 1, 14);
    w.setBlock(at.x, at.y, at.z, reg().blockId("brewing_stand"));
    auto click = [&](float nx, float nz) { RayHit h; h.hit = true; h.block = at; h.id = w.getBlock(at.x, at.y, at.z); h.normal = ivec3(0, 1, 0);
      h.point = vec3(at) + handsFromNorth(handsFacing(w, at), vec3(nx / 16, 0.1f, nz / 16)); ItemStack none; handsBrewUse(*im.lv, *im.P, w, h, none, false); };
    const json* e = nullptr;
    click(3.6f, 0.6f);   // (the next arrow, on the right-hand page)
    e = w.blockEntity(at.x, at.y, at.z);
    check(brewRecipes().size() > 10, "the book has our brews (" + std::to_string(brewRecipes().size()) + ")");
    check(e && e->value("bookPage", 0) == 1, "the next arrow turns the page");
    click(12.4f, 0.6f);
    check(w.blockEntity(at.x, at.y, at.z)->value("bookPage", 0) == 0, "the back arrow turns it back");
  }

  printf("[a cupboard worked by hand: its shelves aimed at from in front]\n");
  {
    World& w = im.lv->world();
    for (int m = 0; m < 4; m++) {
      const ivec3 at(8 + m * 2, im.Y + 1, 4);
      w.setBlock(at.x, at.y, at.z, reg().blockId("oak_cabinet"), (u8)m);
      setChestLidOpen(w, at, true);
      // stand where its doors face (its model's front: north, east, south, west for meta 0..3)
      static const vec3 OUT[4] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
      for (float hy : {0.75f, 0.45f, 0.18f}) {
        const vec3 eye = vec3(at) + vec3(0.5f, hy, 0.5f) + OUT[m] * 1.6f;
        const int s = chestSlotAtRay(w, at, eye, -OUT[m]);
        check(s >= 0, "meta " + std::to_string(m) + ", looked at from its doors' side at " + std::to_string(hy) + ": a shelf spot (" + std::to_string(s) + ")");
      }
    }
  }

  printf("[a barrel worked as the chest is]\n");
  {
    World& w = im.lv->world();
    const ivec3 at(12, im.Y + 1, 12);
    w.setBlock(at.x, at.y, at.z, reg().blockId("barrel"));
    const vec3 eye = vec3(at) + vec3(-1.2f, 1.62f, 0.5f);   // (stood west of it)
    ItemStack none;
    handsBarrelUse(*im.lv, *im.P, w, at, eye, glm::normalize(vec3(at) + vec3(0.5f, 0.9f, 0.5f) - eye), none, false, false);
    check(chestLidOpen(w, at), "use opens it");
    const int f = handsBarrelFacing(w, at);
    const vec3 front = handsFromNorth(f, vec3(0.5f, 0, 0)) - vec3(0.5f, 0, 0.5f);
    check(front.x < -0.4f, "its front is toward you (west)");
    const vec3 btn = vec3(at) + handsFromNorth(f, handsBarrelButton(w, at, 1));
    check(btn.x > at.x + 0.5f && btn.y > at.y + 1.f, "its buttons are up on the top, on the far side (" + std::to_string(btn.x - at.x) + ", " + std::to_string(btn.y - at.y) + ")");
    check(handsBarrelAim(w, at, eye, glm::normalize(btn - eye)).what == HandsChestAim::Swap, "aimed at the upper button: the next layer");
    const vec3 topMid = vec3(at) + handsFromNorth(f, handsBarrelLidPoint(w, at, vec3(8, 10.8f, 2)));
    check(handsBarrelAim(w, at, eye, glm::normalize(topMid - eye)).what == HandsChestAim::Lid, "aimed at the raised top: it shuts");
    check(handsBarrelAim(w, at, eye, glm::normalize(vec3(at) + vec3(0.5f, 0.3f, 0.5f) - eye)).what < HandsChestAim::Tray, "aimed at its side: not the tray (it can be broken)");
    check(handsBarrelAim(w, at, eye, glm::normalize(vec3(at) + vec3(0.5f, 0.75f, 0.5f) - eye)).what == HandsChestAim::Tray, "aimed into it: the tray");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  im.lv->close();
  return fails;
}
