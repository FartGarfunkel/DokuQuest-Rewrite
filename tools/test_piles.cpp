// tools/test_piles.cpp — headless checks for ingot piles and log piles (sim/piles.h, after TerraFirmaCraft): placing,
// adding, taking, topping up the pile below, one metal (or wood) to a pile, the height by count, the slot layout, what a
// broken pile drops, and the controls. Run: build/test_piles (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/piles.h"
#include "sim/events.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include "world/shapes.h"
#include "sim/controller.h"
#include <thread>
#include <cstdio>
#include <cmath>

using namespace dq;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-piles").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(2); EventBus bus;
  LevelInfo li; li.name = "PileTest"; li.seedText = "piles"; li.rules = json{{"immersive", true}, {"_v", 1}};   // (piles are immersive)
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  Level* L = lvp.get();
  World& w = L->world();
  const int Y = dimBase(DIM_OVERWORLD) + 200;
  Player* P; { auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 8.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }
  double t0 = nowSeconds();
  while (nowSeconds() - t0 < 60) {
    w.stream({{P->pos, 2, false}}, 20);
    bool all = true;
    for (int cx = -1; cx <= 1; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
    if (all) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(w.chunk(DIM_OVERWORLD, 0, 0) != nullptr, "the test chunks load");
  const BlockId stone = reg().blockId("stone");
  for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) w.setBlock(x, Y, z, stone);
  const BlockId IP = reg().blockId("ingot_pile"), LP = reg().blockId("log_pile");
  check(IP && LP, "the ingot_pile and log_pile blocks exist");

  printf("[kinds]\n");
  check(pileKindOfItem("iron_ingot") == PileKind::Ingots && pileKindOfItem("bronze_ingot") == PileKind::Ingots, "ingots pile as ingots");
  check(pileKindOfItem("log") == PileKind::Logs, "logs pile as logs");
  check(pileKindOfItem("stick") == PileKind::None && pileKindOfItem("") == PileKind::None, "other things don't");
  check(pileMeta(PileKind::Ingots, 1) == 1 && pileMeta(PileKind::Ingots, 8) == 1 && pileMeta(PileKind::Ingots, 9) == 2 && pileMeta(PileKind::Ingots, 64) == 8, "an ingot pile is a layer per 8");
  check(pileMeta(PileKind::Logs, 16) == 4 && pileMeta(PileKind::Logs, 5) == 2, "a log pile is a course per 4");

  printf("[ingot piles]\n");
  const int X = 4, Z = 4;
  ItemStack held("iron_ingot", 70);
  check(pilePlace(w, X, Y + 1, Z, held) && w.getBlock(X, Y + 1, Z) == IP, "an ingot set down makes an ingot pile");
  auto count = [&](int y) { json* e = w.blockEntity(X, y, Z); return e && e->contains("item") ? ItemStack::fromJson((*e)["item"]).n : 0; };
  check(count(Y + 1) == 1 && held.n == 69, "one ingot at a time");
  for (int i = 0; i < 63; i++) pileAdd(w, X, Y + 1, Z, held);
  check(count(Y + 1) == 64 && w.getMeta(X, Y + 1, Z) == 8 && held.n == 6, "64 to a pile, 8 layers high");
  check(pileAdd(w, X, Y + 1, Z, held) && w.getBlock(X, Y + 2, Z) == IP && count(Y + 2) == 1, "a full pile takes a new one on top");
  ItemStack one = pileTake(w, X, Y + 1, Z);
  check(one.id == "iron_ingot" && one.n == 1 && count(Y + 1) == 63, "taking one off");
  check(pileAdd(w, X, Y + 2, Z, held) && count(Y + 1) == 64 && count(Y + 2) == 1, "adding tops up the pile below first");
  ItemStack gold("gold_ingot", 5); std::string why;
  check(!pileAdd(w, X, Y + 2, Z, gold, &why) && gold.n == 5 && why == "One metal to a pile", "one metal to a pile");
  ItemStack tagged("iron_ingot", 3); tagged.tag = json{{"comp", {{"iron", 1}}}};
  check(!pileAdd(w, X, Y + 2, Z, tagged) && tagged.n == 3, "an ingot with a different makeup doesn't join");
  ItemStack h2("iron_ingot", 4);
  check(!pilePlace(w, X + 6, Y + 3, Z, h2) && h2.n == 4, "nothing to set it on: nothing happens");
  for (int i = 0; i < 70; i++) pileTake(w, X, Y + 2, Z);
  check(w.getBlock(X, Y + 2, Z) == AIR, "the last one taken: the pile is gone");
  w.setBlock(X + 2, Y + 1, Z, IP); w.setBlockEntity(X + 2, Y + 1, Z, json{{"type", "ingots"}});
  check(pileTake(w, X + 2, Y + 1, Z).empty() && w.getBlock(X + 2, Y + 1, Z) == AIR, "a pile with nothing in it (a garbled save) is cleared");

  printf("[layout]\n");
  {
    const PileSlot a = ingotSlot(0), b = ingotSlot(7), c = ingotSlot(8);
    check(glm::length(a.pos - vec3(0.125f, 0, 0.25f)) < 1e-4f && glm::length(b.pos - vec3(0.875f, 0, 0.75f)) < 1e-4f, "the first layer: two rows of four");
    check(std::fabs(c.yaw - PI / 2) < 1e-4f && std::fabs(c.pos.y - 0.125f) < 1e-4f, "the next layer turned across it");
    bool in = true, apart = true;
    for (int i = 0; i < 64; i++) {
      const vec3 p = ingotSlot(i).pos;
      if (p.x < 0 || p.x > 1 || p.z < 0 || p.z > 1 || p.y < 0 || p.y >= 1) in = false;
      for (int j = (i / 8) * 8; j < i; j++) if (glm::length(ingotSlot(j).pos - p) < 0.2f) apart = false;
    }
    check(in && apart, "all 64 in the block, none on top of another");
    bool lin = true, turns = true;
    for (int i = 0; i < 16; i++) { const PileSlot s = logSlot(i); if (s.pos.x < 0 || s.pos.x > 1 || s.pos.z < 0 || s.pos.z > 1 || s.pos.y < 0 || s.pos.y > 1) lin = false; if (i >= 4 && std::fabs(std::fabs(s.yaw - logSlot(i - 4).yaw) - PI / 2) > 1e-4f) turns = false; }
    check(lin && turns, "16 logs in the block, each course across the last (a crib)");
  }

  printf("[log piles]\n");
  {
    const int LX = 10;
    ItemStack logs("log", 20);
    check(pilePlace(w, LX, Y + 1, Z, logs) && w.getBlock(LX, Y + 1, Z) == LP, "a log set down makes a log pile");
    for (int i = 0; i < 15; i++) pileAdd(w, LX, Y + 1, Z, logs);
    json* e = w.blockEntity(LX, Y + 1, Z);
    check(e && ItemStack::fromJson((*e)["item"]).n == 16 && w.getMeta(LX, Y + 1, Z) == 4 && logs.n == 4, "16 logs to a pile");
    ItemStack birch("birch_log", 2);
    check(!pileAdd(w, LX, Y + 1, Z, birch) && birch.n == 2, "one wood to a pile");
  }

  printf("[collision and drops]\n");
  {
    auto height = [&](int x, int y, int z) {
      std::vector<ShapeBox> bx; WorldShapeNb nb(w, x, y, z);
      if (!shapeBoxes(w.getBlock(x, y, z), w.getMeta(x, y, z), nb, ShapePurpose::Collide, bx) || bx.empty()) return -1.f;
      float h = 0; for (auto& b : bx) h = std::max(h, b.b[4]); return h;
    };
    const int CX = 14;
    ItemStack in("copper_ingot", 64);
    pilePlace(w, CX, Y + 1, Z, in); for (int i = 0; i < 8; i++) pileAdd(w, CX, Y + 1, Z, in);
    check(height(CX, Y + 1, Z) == 4, "9 ingots stand 2 layers (4 px) high");
    while (in.n) pileAdd(w, CX, Y + 1, Z, in);
    check(height(CX, Y + 1, Z) == 16, "64 ingots: a full block");
    ItemStack lg("log", 5); pilePlace(w, CX, Y + 1, Z + 3, lg); while (lg.n) pileAdd(w, CX, Y + 1, Z + 3, lg);
    check(height(CX, Y + 1, Z + 3) == 8, "5 logs: two courses (8 px)");
    PlayerController pc(*L, *P);
    auto dropped = [&](const char* id) { int n = 0; for (auto& e : L->entities()) if (!e->removed && e->type == "item" && e->data.value("stack", json::object()).value("id", "") == id) n += e->data["stack"].value("n", 1); return n; };
    pc.breakBlock(CX, Y + 1, Z);
    check(w.getBlock(CX, Y + 1, Z) == AIR && dropped("copper_ingot") == 64, "a broken pile drops all it held");
  }

  printf("[controls]\n");
  {
    PlayerController pc(*L, *P);
    auto aim = [&](int x, int y, int z, ivec3 n = ivec3(0, 1, 0)) {
      pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(x, y, z); pc.target.normal = n; pc.target.id = w.getBlock(x, y, z);
      pc.targetPoint = vec3(x + 0.5f, y + (n.y > 0 ? 1.f : 0.5f), z + 0.5f);
    };
    PlayerInput sneak; sneak.sneak = true; PlayerInput plain;
    for (auto& s : P->inv) s = ItemStack();
    P->selected = 0;
    const int UX = 2, UZ = 12;
    P->inv[0] = ItemStack("iron_ingot", 5); aim(UX, Y, UZ); pc.use(false, sneak);
    check(w.getBlock(UX, Y + 1, UZ) == IP && P->inv[0].n == 4, "sneak + use with an ingot on the ground sets one down");
    aim(UX, Y + 1, UZ); pc.use(false, sneak);
    check(P->inv[0].n == 3, "sneak + use on the pile adds another");
    P->inv[0] = ItemStack("log", 3); aim(UX + 2, Y, UZ); pc.use(false, sneak);
    check(w.getBlock(UX + 2, Y + 1, UZ) == LP && P->inv[0].n == 2, "sneak + use with a log makes a log pile");
    aim(UX + 4, Y, UZ); pc.use(false, plain);
    check(w.getBlock(UX + 4, Y + 1, UZ) == reg().blockId("log"), "not sneaking: the log is placed as a block, as ever");
    P->inv[0] = ItemStack(); aim(UX, Y + 1, UZ); pc.use(false, plain);
    int back = 0; for (auto& st : P->inv) if (st.id == "iron_ingot") back += st.n;
    check(back == 1, "use on the pile takes one back");
    // crouched, anything is set down on top of what you look at: a furnace, a crafting table, a chest (Vintage Story's way)
    const BlockId PILE = reg().blockId("item_pile");
    struct Case { const char* block; const char* item; };
    int i = 0;
    for (Case c : {Case{"furnace", "iron_pickaxe"}, Case{"bookshelf", "bread"}, Case{"chest", "apple"}, Case{"stone", "ceramic_jug"}, Case{"stone", "stick"}}) {
      const int X = UX + i * 2, Z = UZ + 4; i++;
      w.setBlock(X, Y, Z, reg().blockId(c.block)); w.setBlock(X, Y + 1, Z, AIR);
      P->inv[0] = ItemStack(c.item, 2); aim(X, Y, Z); pc.use(false, sneak);
      check(w.getBlock(X, Y + 1, Z) == PILE && P->inv[0].empty(), std::string("crouched: ") + c.item + " set down on top of a " + c.block);
    }
    // a tool's crouched use is still the tool's: a hoe tills, it isn't set down
    {
      const int X = UX + 2, Z = UZ + 6;
      w.setBlock(X, Y, Z, reg().blockId("grass")); w.setBlock(X, Y + 1, Z, AIR);
      P->inv[0] = ItemStack("iron_hoe", 1); aim(X, Y, Z); pc.use(false, sneak);
      check(w.getBlock(X, Y + 1, Z) != PILE && w.getBlock(X, Y, Z) == reg().blockId("farmland"), "crouched with a hoe: the grass is tilled, the hoe isn't set down");
    }
    // the crafting table worked in the world (immersive): planks on two squares, one above the other as you look, make sticks
    {
      const int X = UX, Z = UZ + 8;
      w.setBlock(X, Y, Z, reg().blockId("crafting_table")); w.setBlock(X, Y + 1, Z, AIR);
      P->yaw = 0;   // looking north: the far row is the north one
      auto top = [&](float fx, float fz) { aim(X, Y, Z); pc.target.point = vec3(X + fx, Y + 1.f, Z + fz); pc.targetPoint = pc.target.point; };
      for (auto& st : P->inv) st = ItemStack();
      P->inv[0] = ItemStack("planks", 2);
      top(0.5f, 0.2f); pc.use(false, plain);
      top(0.5f, 0.5f); pc.use(false, plain);
      const json* te = w.blockEntity(X, Y, Z);
      int on = 0; if (te) for (auto& it : (*te)["items"]) if (!it.is_null()) on++;
      check(on == 2 && P->inv[0].empty(), "two planks set on the table's top, one square each");
      top(0.15f, 0.85f); pc.use(false, plain);   // an empty hand on an empty square: make it
      int sticks = 0; for (auto& st : P->inv) if (st.id == "stick") sticks += st.n;
      check(sticks == 4 && !w.blockEntity(X, Y, Z), "...an empty hand makes four sticks, and the table is clear (" + std::to_string(sticks) + ")");
      P->inv[0] = ItemStack("planks", 1); top(0.5f, 0.5f); pc.use(false, plain);
      P->inv[0] = ItemStack(); top(0.5f, 0.5f); pc.use(false, plain);
      int back = 0; for (auto& st : P->inv) if (st.id == "planks") back += st.n;
      check(back == 1 && !w.blockEntity(X, Y, Z), "a piece set down and taken back");
    }
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  L->close();
  return fails;
}
