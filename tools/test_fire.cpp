// tools/test_fire.cpp — headless checks for fire and clay (sim/heat.h, sim/firepit.h, sim/firestarter.h, sim/pit_kiln.h,
// sim/charcoal_pit.h, sim/straw.h; after TerraFirmaCraft): item heat, the firepit's fuel and temperature, rain, lighting
// with the firestarter, straw, clay pottery, the pit kiln and the charcoal pit. Run: build/test_fire (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/rules.h"
#include "sim/events.h"
#include "sim/inventory.h"
#include "sim/heat.h"
#include "sim/firepit.h"
#include "sim/firestarter.h"
#include "sim/knapping.h"
#include "sim/pit_kiln.h"
#include "sim/charcoal_pit.h"
#include "sim/piles.h"
#include "sim/weather.h"
#include "sim/gameplay.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include <thread>
#include <cstdio>
#include <cmath>

using namespace dq;
constexpr u8 CAMPFIRE_OUT = 8;   // (sim/camping.h; that header and sim/gameplay.h clash over toast())
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
  std::string ud = (fs::temp_directory_path() / "dq-test-fire").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }

  printf("[heat]\n");
  {
    ItemStack ing("copper_ingot", 1);
    check(itemHeat(ing) == 20, "a cold ingot is at 20 degrees");
    setItemHeat(ing, 900);
    check(std::fabs(itemHeat(ing) - 900) < 0.5f, "heat it to 900");
    float h = 1000; for (int i = 0; i < 60; i++) h = heatTowards(h, 20, 1.f);
    check(h < 600 && h > 20, "a minute in the air: below 600 (" + std::to_string(h) + ")");
    for (int i = 0; i < 3000; i++) h = heatTowards(h, 20, 1.f);
    check(h >= 20, "never colder than the air");
    float up = 20; for (int i = 0; i < 10; i++) up = heatTowards(up, 1200, 1.f);
    check(up > 500 && up <= 1200, "on a hot fire it warms quickly (" + std::to_string(up) + ")");
    ItemStack cold("copper_ingot", 1);
    check(!canMergeStacks(ing, cold), "a hot ingot doesn't stack with a cold one");
    setItemHeat(ing, 22);
    check(ing.tag.is_null() || !ing.tag.contains("heat"), "cooled down, the heat leaves its tag");
    check(heatName(100) == "Warming" && heatName(600) == "Faint Red" && heatName(1150) == "Orange" && heatName(50).empty(), "TFC's names for the heat");
    check(heatGlow(300) == 0 && heatGlow(900) != 0, "it glows from red heat");
    check(heatable("copper_ingot") && heatable("iron_pickaxe_head") && !heatable("apple"), "what can be heated");
  }

  printf("[pottery]\n");
  {
    const auto& clay = knapPatterns("clay");
    bool real = true, fires = true;
    for (auto& p : clay) {
      const ItemDef* d = reg().item(p.out);
      if (!d || p.out.rfind("unfired_", 0) != 0) { real = false; printf("    %s\n", p.out.c_str()); continue; }
      if (!reg().item(d->str("fires"))) { fires = false; printf("    %s fires into %s?\n", p.out.c_str(), d->str("fires").c_str()); }
    }
    check(clay.size() >= 14 && real, "clay makes unfired pottery and molds (" + std::to_string(clay.size()) + " shapes)");
    check(fires, "every unfired piece fires into something");
    bool distinct = true; for (size_t i = 0; i < clay.size(); i++) for (size_t j = 0; j < i; j++) if (clay[i].rows == clay[j].rows) distinct = false;
    check(distinct, "no two clay shapes alike");
    auto shapeOf = [&](const std::string& out) { for (auto& p : clay) if (p.out == out) { KnapGrid g; g.kept.fill(p.defOn); for (int y = 0; y < (int)p.rows.size(); y++) for (int x = 0; x < (int)p.rows[y].size(); x++) g.kept[y * 5 + x] = p.rows[y][x] == '#'; return g; } return KnapGrid(); };
    const KnapPattern* v = knapMatch("clay", shapeOf("unfired_vessel"));
    check(v && v->out == "unfired_vessel", "shaping a vessel");
    const KnapPattern* pm = knapMatch("clay", shapeOf("unfired_pickaxe_mold"));
    check(pm && pm->out == "unfired_pickaxe_mold", "shaping a pickaxe mold");
    check(knapMaterialOf("fire_clay") == "fireclay" && !knapPatterns("fireclay").empty() && reg().item("fire_clay"), "fire clay makes the crucible");
  }

  Rig rg("Fire", true, true);
  World& w = rg.lv->world();
  printf("[cooling in your bag]\n");
  {
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->inv[3] = ItemStack("iron_ingot", 1); setItemHeat(rg.P->inv[3], 1000);
    rg.tick(20);
    const float h = itemHeat(rg.P->inv[3]);
    check(h < 900 && h > 300, "a hot ingot in your bag cools (" + std::to_string(h) + " after 20 s)");
  }

  printf("[firepit]\n");
  const BlockId FIRE = reg().blockId("campfire");
  auto ent = [&](int x, int y, int z) -> json& { return *w.blockEntity(x, y, z, true); };
  {
    check(fuelOf("log").peak >= 600 && fuelOf("log").secs >= 60 && fuelOf("charcoal").peak > 1300 && fuelOf("apple").secs == 0, "TFC's fuels");
    const int X = 3, Z = 3, Y1 = rg.Y + 1;
    w.setBlock(X, Y1, Z, FIRE, 0);
    json& e = ent(X, Y1, Z); e = json{{"type", "campfire"}, {"items", {nullptr, nullptr, nullptr, nullptr}}, {"t", {0, 0, 0, 0}}, {"temp", 20}, {"burn", 0}, {"fuel", json::array()}};
    ItemStack log("log", 2);
    check(firepitAddFuel(w, X, Y1, Z, log) && log.n == 1, "a log goes on the fire");
    float peak = 0; double outAt = -1;
    for (double t = 0; t < 400; t += 0.5) { rg.tick(0.5); peak = std::max(peak, (float)ent(X, Y1, Z).value("temp", 0.0)); if (outAt < 0 && (w.getMeta(X, Y1, Z) & CAMPFIRE_OUT)) outAt = t; }
    check(peak > 600, "it burns hot (" + std::to_string((int)peak) + ")");
    check(outAt > 60 && outAt < 300, "and goes out when the log's burnt away (" + std::to_string((int)outAt) + " s)");
    // an old campfire (no fuel or temperature yet) keeps burning
    w.setBlock(X + 3, Y1, Z, FIRE, 0); ent(X + 3, Y1, Z) = json{{"type", "campfire"}, {"items", {nullptr, nullptr, nullptr, nullptr}}};
    rg.tick(30);
    check(!(w.getMeta(X + 3, Y1, Z) & CAMPFIRE_OUT), "an old campfire keeps burning");
    // rain on an open fire; a roof keeps it lit
    WeatherSystem* W = weatherOf(*rg.lv);
    if (W) {
      W->set("rain", 600);
      w.setBlock(X + 6, Y1, Z, FIRE, 0); ent(X + 6, Y1, Z) = json{{"type", "campfire"}, {"items", {nullptr, nullptr, nullptr, nullptr}}, {"temp", 600}, {"burn", 200}, {"fuel", json::array()}};
      w.setBlock(X + 9, Y1, Z, FIRE, 0); ent(X + 9, Y1, Z) = json{{"type", "campfire"}, {"items", {nullptr, nullptr, nullptr, nullptr}}, {"temp", 600}, {"burn", 200}, {"fuel", json::array()}};
      w.setBlock(X + 9, Y1 + 3, Z, reg().blockId("stone"));
      rg.tick(10);
      const bool rainHere = W->precipAt(w, X + 6.5f, Y1 + 1.f, Z + 0.5f) != 0;
      check(!rainHere || (w.getMeta(X + 6, Y1, Z) & CAMPFIRE_OUT), "rain puts out a fire in the open");
      check(!(w.getMeta(X + 9, Y1, Z) & CAMPFIRE_OUT), "a roof keeps it burning");
      W->set("clear", 600);
    }
  }

  printf("[firestarter]\n");
  {
    const ItemDef* fs = reg().item("firestarter");
    check(fs && fs->hasTool && fs->tool.durability == 8, "a firestarter, good for 8 tries");
    const int X = 16, Z = 9, Y1 = rg.Y + 1;
    for (int i = 0; i < 3; i++) dropItem(*rg.lv, 0, ItemStack("stick", 1), vec3(X + 0.5f, Y1 + 0.2f, Z + 0.5f), false);
    rg.tick(1);
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; rg.P->inv[0] = ItemStack("firestarter", 1);
    pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(X, rg.Y, Z); pc.target.normal = ivec3(0, 1, 0); pc.target.id = w.getBlock(X, rg.Y, Z);
    setFirestarterRoll(0.0f);   // a sure spark
    pc.use(false, PlayerInput{});
    check(w.getBlock(X, Y1, Z) != FIRE, "one press isn't enough: you have to keep at it");
    for (int i = 0; i < 6; i++) pc.use(true, PlayerInput{});
    check(w.getBlock(X, Y1, Z) == FIRE && !(w.getMeta(X, Y1, Z) & CAMPFIRE_OUT), "three sticks and a firestarter make a lit firepit");
    int sticks = 0; for (auto& e2 : rg.lv->entities()) if (!e2->removed && e2->type == "item" && e2->data.value("stack", json::object()).value("id", "") == "stick") sticks++;
    check(sticks == 0, "  ...the sticks used up");
    check(rg.P->inv[0].d >= 1, "  ...and the firestarter worn");
    setFirestarterRoll(0.99f);   // no luck
    w.setBlock(X, Y1, Z, FIRE, CAMPFIRE_OUT);
    for (int i = 0; i < 6; i++) pc.use(i > 0, PlayerInput{});
    check(w.getMeta(X, Y1, Z) & CAMPFIRE_OUT, "without a spark it stays out");
    setFirestarterRoll(-1);
  }

  printf("[straw]\n");
  {
    const BlockId TG = reg().blockId("tall_grass"), GRASSB = reg().blockId("grass");
    PlayerController pc(*rg.lv, *rg.P);
    auto straws = [&]() { int n = 0; for (auto& e2 : rg.lv->entities()) if (!e2->removed && e2->type == "item" && e2->data.value("stack", json::object()).value("id", "") == "straw") { n += e2->data["stack"].value("n", 1); e2->removed = true; } return n; };
    for (int x = 1; x < 8; x++) for (int z = 12; z < 18; z++) { w.setBlock(x, rg.Y, z, GRASSB); w.setBlock(x, rg.Y + 1, z, TG); }
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0;
    straws();
    auto cut = [&](int n) { int k = 0; for (int x = 1; x < 8 && k < n; x++) for (int z = 12; z < 18 && k < n; z++) { w.setBlock(x, rg.Y + 1, z, TG); pc.breakBlock(x, rg.Y + 1, z); k++; } };
    cut(20); const int byHand = straws();
    rg.P->inv[0] = ItemStack("flint_knife", 1);
    cut(20); const int byKnife = straws();
    check(byHand < 10, "by hand, grass gives straw only now and then (" + std::to_string(byHand) + " of 20)");
    check(byKnife >= 20, "cut with a knife, every tuft gives straw (" + std::to_string(byKnife) + " of 20)");
    for (int x = 1; x < 8; x++) for (int z = 12; z < 18; z++) w.setBlock(x, rg.Y + 1, z, TG);
    rg.P->inv[0] = ItemStack("flint_scythe", 1);
    pc.breakBlock(4, rg.Y + 1, 15);
    int left = 0; for (int x = 3; x <= 5; x++) for (int z = 14; z <= 16; z++) if (w.getBlock(x, rg.Y + 1, z) == TG) left++;
    check(left == 0, "a scythe cuts the grass all round");
    const int sc = straws();
    check(sc >= 9, "  ...and gathers its straw (" + std::to_string(sc) + ")");
    check(reg().blockId("thatch") != 0, "thatch");
  }

  printf("[pit kiln]\n");
  {
    const BlockId STONE = reg().blockId("stone"), KILN = reg().blockId("pit_kiln");
    check(KILN != 0, "the pit kiln block");
    auto build = [&](int X, int Z, bool openSide) {
      for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) if (dx || dz) w.setBlock(X + dx, rg.Y + 1, Z + dz, STONE);
      if (openSide) w.setBlock(X + 1, rg.Y + 1, Z, AIR);
      ItemStack pot("unfired_vessel", 1), mold("unfired_pickaxe_mold", 2);
      check(kilnPlace(w, X, rg.Y + 1, Z, 0, pot) && pot.empty(), "unfired pottery set down in a pit");
      kilnPlace(w, X, rg.Y + 1, Z, 3, mold);
      ItemStack logs("log", 8), straw("straw", 8);
      std::string why;
      check(!kilnAdd(w, X, rg.Y + 1, Z, logs, &why), "logs only go on once the straw is in");
      for (int i = 0; i < 8; i++) kilnAdd(w, X, rg.Y + 1, Z, straw);
      check(!kilnLight(*rg.lv, w, X, rg.Y + 1, Z), "half-built, it won't light");
      for (int i = 0; i < 8; i++) kilnAdd(w, X, rg.Y + 1, Z, logs);
      check(straw.empty() && logs.empty(), "eight straw and eight logs");
    };
    build(4, -2, false);
    build(10, -2, true);
    check(kilnLight(*rg.lv, w, 4, rg.Y + 1, -2), "lit with a firestarter's spark");
    check(!kilnLight(*rg.lv, w, 10, rg.Y + 1, -2), "an open side: it won't light");
    w.setBlock(11, rg.Y + 1, -2, STONE);
    check(kilnLight(*rg.lv, w, 10, rg.Y + 1, -2), "walled in, it lights");
    w.setBlock(11, rg.Y + 1, -2, AIR);   // ...and then a wall comes down
    rg.tick(gameHourSecs(*rg.lv) * 3 + 5);
    json* e = w.blockEntity(4, rg.Y + 1, -2);
    const ItemStack a = e ? ItemStack::fromJson((*e)["items"][0]) : ItemStack(), b = e ? ItemStack::fromJson((*e)["items"][3]) : ItemStack();
    check(a.id == "ceramic_vessel" && b.id == "ceramic_pickaxe_mold" && b.n == 2, "three hours later: fired pottery");
    check(e && e->value("straw", 1) == 0 && e->value("logs", 1) == 0 && !e->value("lit", true), "  ...the fuel burnt away");
    json* e2 = w.blockEntity(10, rg.Y + 1, -2);
    check(e2 && ItemStack::fromJson((*e2)["items"][0]).id == "unfired_vessel" && !e2->value("lit", true) && e2->value("logs", 0) == 0, "a wall came down: the fire went out, the pots unfired, the fuel lost");
  }

  printf("[pit kiln controls]\n");
  {
    const BlockId KILN = reg().blockId("pit_kiln");
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0;
    auto aim = [&](int x, int y, int z) { pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(x, y, z); pc.target.normal = ivec3(0, 1, 0); pc.target.id = w.getBlock(x, y, z); pc.targetPoint = vec3(x + 0.25f, y + 1, z + 0.25f); };
    PlayerInput sneak; sneak.sneak = true;
    rg.P->inv[0] = ItemStack("unfired_jug", 1); aim(16, rg.Y, -2); pc.use(false, sneak);
    check(w.getBlock(16, rg.Y + 1, -2) == KILN, "sneak + use with unfired pottery on the ground starts a pit kiln");
    rg.P->inv[0] = ItemStack("straw", 3); aim(16, rg.Y + 1, -2); pc.use(false, PlayerInput{});
    json* e = w.blockEntity(16, rg.Y + 1, -2);
    check(e && e->value("straw", 0) == 1 && rg.P->inv[0].n == 2, "use with straw adds straw");
    rg.P->inv[0] = ItemStack(); pc.use(false, PlayerInput{});
    int jugs = 0; for (auto& st : rg.P->inv) if (st.id == "unfired_jug") jugs += st.n;
    check(jugs == 1, "an empty hand takes the pottery back");
  }

  printf("[charcoal pit]\n");
  {
    const BlockId DIRT = reg().blockId("dirt"), LP = reg().blockId("log_pile"), PILE = reg().blockId("item_pile");
    const int Y1 = rg.Y + 1;
    for (int x = -3; x <= 0; x++) for (int z = -3; z <= 0; z++) { w.setBlock(x, Y1, z, DIRT); w.setBlock(x, Y1 + 1, z, DIRT); }
    for (int x = -2; x <= -1; x++) for (int z = -2; z <= -1; z++) { w.setBlock(x, Y1, z, AIR); ItemStack logs("log", 16); pilePlace(w, x, Y1, z, logs); while (!logs.empty()) pileAdd(w, x, Y1, z, logs); }
    // one on its own, its top open to the sky
    ItemStack lone("log", 16); pilePlace(w, 18, Y1, 18, lone); while (!lone.empty()) pileAdd(w, 18, Y1, 18, lone);
    check(w.getBlock(-2, Y1, -2) == LP && w.getBlock(18, Y1, 18) == LP, "log piles ready");
    check(charcoalLight(*rg.lv, w, -2, Y1, -2), "a spark on one log pile lights the pit");
    int lit = 0; for (int x = -2; x <= -1; x++) for (int z = -2; z <= -1; z++) if (json* e = w.blockEntity(x, Y1, z); e && e->value("lit", false)) lit++;
    check(lit == 4, "  ...it spreads to every pile touching it");
    check(charcoalLight(*rg.lv, w, 18, Y1, 18), "and the lone one");
    rg.tick(gameHourSecs(*rg.lv) * CHARCOAL_HOURS + 5);
    int piles = 0, charcoal = 0; bool each = true;
    for (int x = -2; x <= -1; x++) for (int z = -2; z <= -1; z++) {
      if (w.getBlock(x, Y1, z) != PILE) { each = false; continue; }
      piles++;
      int n = 0; if (json* e = w.blockEntity(x, Y1, z)) for (auto& it : (*e)["items"]) if (!it.is_null() && it.value("id", "") == "charcoal") n += it.value("n", 0);
      if (n < 4 || n > 8) each = false;
      charcoal += n;
    }
    check(piles == 4 && each, "six hours on, covered: four heaps of charcoal, 4 to 8 each (" + std::to_string(charcoal) + ")");
    check(w.getBlock(18, Y1, 18) != LP && w.getBlock(18, Y1, 18) != PILE, "the one left open burnt away");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  rg.lv->close();
  return fails;
}
