// tools/test_piles.cpp — headless checks for ingot piles and log piles (sim/piles.h): placing,
// adding, taking, topping up the pile below, one metal (or wood) to a pile, the height by count, the slot layout, what a
// broken pile drops, and the controls. Run: build/test_piles (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/piles.h"
#include "sim/trough.h"
#include "sim/worktable.h"
#include "sim/anvil_work.h"
#include "sim/ench_work.h"
#include "sim/brewing.h"
#include "sim/furnace_work.h"
#include "sim/fallen.h"
#include "sim/controller.h"
#include "sim/creature.h"
#include "sim/crafting.h"
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
    // crouched, anything is set down on top of what you look at: a furnace, a crafting table, a chest
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
    // the crafting table scene's moves (ui/cinematic_table.cpp): put, take, make, even with all nine squares full
    {
      const ivec3 T(UX + 4, Y, UZ + 8);
      w.setBlock(T.x, T.y, T.z, reg().blockId("crafting_table")); w.setBlock(T.x, T.y + 1, T.z, AIR);
      L->info.rules["immersive"] = false; w.immersive = false;
      for (auto& st : P->inv) st = ItemStack();
      P->inv[0] = ItemStack("iron_ingot", 9); P->selected = 0;
      int put = 0; for (int c = 0; c < 9; c++) put += worktablePut(*L, *P, w, T, c, P->held()) ? 1 : 0;
      check(put == 9 && P->inv[0].empty(), "nine ingots set on the nine squares, one each");
      check(!worktablePut(*L, *P, w, T, 4, P->held()), "...nothing more goes on a filled square");
      check(worktableCraft(*L, *P, w, T) && !w.blockEntity(T.x, T.y, T.z), "with every square full it's still made (it needed an empty square to click)");
      int blocks = 0; for (auto& st : P->inv) if (st.id == "iron_block") blocks += st.n;
      check(blocks == 1, "...an iron block in your bag");
      P->inv[0] = ItemStack("stick", 2);
      worktablePut(*L, *P, w, T, 0, P->held());
      check(worktableTake(*L, *P, w, T, 0) && P->inv[0].n == 2, "a piece taken back off its square");
    }
    // the anvil worked in the world (sim/anvil_work.h): a worn sword and an ingot on its face, forged into the sword mended
    {
      const ivec3 A(UX + 8, Y, UZ + 8);
      w.setBlock(A.x, A.y, A.z, reg().blockId("anvil")); w.setBlock(A.x, A.y + 1, A.z, AIR);
      for (auto& st : P->inv) st = ItemStack();
      ItemStack sword("iron_sword", 1); const ItemDef* sd = sword.def(); sword.d = sd && sd->hasTool ? sd->tool.durability * 3 / 4 : 100;
      const int worn = sword.d;
      P->inv[0] = sword; P->inv[1] = ItemStack("stick", 3); P->inv[2] = ItemStack("iron_ingot", 2); P->selected = 0; P->level = 30;
      check(anvilWorkPut(*L, *P, w, A, 0, P->inv[0]) && P->inv[0].empty(), "the worn sword laid on the anvil");
      check(anvilWorkPut(*L, *P, w, A, 1, P->inv[1]), "...sticks beside it");
      check(!anvilWorkResult(w, A).ok && !anvilWorkForge(*L, *P, w, A), "sticks don't mend a sword: nothing forges");
      check(anvilWorkTake(*L, *P, w, A, 1) && anvilWorkPut(*L, *P, w, A, 1, P->inv[2]) && P->inv[2].empty(), "the sticks taken back, two ingots in their place");
      const StationResult r = anvilWorkResult(w, A);
      check(r.ok && r.out.id == "iron_sword" && r.out.d < worn, "the anvil would mend it");
      check(anvilWorkForge(*L, *P, w, A), "forged");
      int mended = -1; for (auto& st : P->inv) if (st.id == "iron_sword") mended = st.d;
      check(mended >= 0 && mended < worn, "...the sword back in the bag, mended (" + std::to_string(worn) + " -> " + std::to_string(mended) + ")");
      w.setBlock(A.x, A.y, A.z, AIR);
    }
    // the enchanting table worked in the world (sim/ench_work.h): a sword over it, lapis by it, an offer taken
    {
      const ivec3 E(UX + 10, Y, UZ + 8);
      w.setBlock(E.x, E.y, E.z, reg().blockId("enchanting_table")); w.setBlock(E.x, E.y + 1, E.z, AIR);
      for (auto& st : P->inv) st = ItemStack();
      P->inv[0] = ItemStack("iron_sword", 1); P->inv[1] = ItemStack("lapis_lazuli", 10); P->level = 30;
      check(enchWorkPut(*L, *P, w, E, P->inv[0]) && enchWorkPut(*L, *P, w, E, P->inv[1]) && P->inv[0].empty() && P->inv[1].empty(), "a sword over the enchanting table, lapis by it");
      ItemStack dirt("dirt", 4);
      check(!enchWorkPut(*L, *P, w, E, dirt) && dirt.n == 4, "...dirt can't be enchanted: it stays in hand");
      std::array<EnchOffer, 3> offers{};
      check(enchWorkOffers(w, E, *P, offers) && offers[0].cost > 0, "three offers hang over it");
      const int lv0 = P->level;
      check(enchWorkApply(*L, *P, w, E, 0).empty() && P->level < lv0, "the first taken: levels spent");
      const std::vector<ItemStack> on = enchWorkItems(w, E);
      check(on[0].tag.is_object() && on[0].tag.contains("en") && on[1].n < 10, "...the sword enchanted, lapis used (" + std::to_string(on[1].n) + " left)");
      check(enchWorkTake(*L, *P, w, E, 0) && enchWorkTake(*L, *P, w, E, 1) && !w.blockEntity(E.x, E.y, E.z), "both taken back");
      w.setBlock(E.x, E.y, E.z, AIR);
    }
    // the brewing stand worked in the world (sim/brewing.h brewPut / brewTake): bottles on its feet, the ingredient, the powder
    {
      const ivec3 B(UX + 12, Y, UZ + 8);
      w.setBlock(B.x, B.y, B.z, reg().blockId("brewing_stand")); w.setBlock(B.x, B.y + 1, B.z, AIR);
      for (auto& st : P->inv) st = ItemStack();
      P->inv[0] = ItemStack("potion_water", 3); P->inv[1] = ItemStack("nether_wart_item", 2); P->inv[2] = ItemStack("cinder_powder", 5);
      int bottles = 0; for (int k = 0; k < 3; k++) bottles += brewPut(*L, *P, w, B, k, P->inv[0]) ? 1 : 0;
      check(bottles == 3 && P->inv[0].empty(), "three water bottles stood on the stand's feet");
      ItemStack stick("stick", 1);
      check(!brewPut(*L, *P, w, B, 3, stick) && brewPut(*L, *P, w, B, 3, P->inv[1]) && P->inv[1].n == 1, "a stick isn't an ingredient; one wart in the bowl");
      check(brewPut(*L, *P, w, B, 4, P->inv[2]) && P->inv[2].empty(), "...all the powder at its foot");
      check(brewTake(*L, *P, w, B, 0), "a bottle taken back");
      w.setBlock(B.x, B.y, B.z, AIR);
    }
    {
      const ivec3 F(UX + 14, Y, UZ + 8);
      w.setBlock(F.x, F.y, F.z, reg().blockId("furnace")); w.setBlock(F.x, F.y + 1, F.z, AIR);
      for (auto& st : P->inv) st = ItemStack();
      P->inv[0] = ItemStack("raw_iron", 2); P->inv[1] = ItemStack("coal", 2); P->inv[2] = ItemStack("dirt", 1);
      if (!reg().item("raw_iron")) P->inv[0] = ItemStack("iron_ore", 2);
      check(!furnacePut(*L, *P, w, F, 0, P->inv[2]) && furnacePut(*L, *P, w, F, 0, P->inv[0]) && furnacePut(*L, *P, w, F, 1, P->inv[1]), "ore in the furnace's mouth, coal under it (dirt won't smelt)");
      for (int i = 0; i < 260 && furnaceItems(w, F)[2].empty(); i++) L->tick(0.05);
      const std::vector<ItemStack> fv = furnaceItems(w, F);
      check(!fv[2].empty(), "...it smelts: " + (fv[2].empty() ? std::string("nothing") : fv[2].id));
      check(furnaceTake(*L, *P, w, F, 2), "the smelted taken out by hand");
      w.setBlock(F.x, F.y, F.z, AIR);
    }
    // the fallen adventurers (sim/fallen.h): their things rolled the first time they're opened, the same at the same spot
    {
      int kinds = 0; for (const char* k : {"remains_traveller", "remains_desert", "remains_snowy", "remains_jungle", "remains_swamp", "remains_miner"}) kinds += reg().blockId(k) ? 1 : 0;
      check(kinds == 6, "six kinds of fallen adventurer");
      auto lootAt = [&](int x) {
        w.setBlock(x, Y, UZ + 12, reg().blockId("remains_traveller")); w.setBlockEntity(x, Y, UZ + 12, nullptr);
        json* e = containerEntity(w, x, Y, UZ + 12);
        std::string s; if (e) for (auto& it : (*e)["items"]) if (!it.is_null()) s += it.value("id", "") + ",";
        return s;
      };
      const std::string a = lootAt(UX), a2 = lootAt(UX);
      int found = 0, differ = 0;
      for (int x = UX; x < UX + 12; x++) { const std::string l = lootAt(x); if (!l.empty()) found++; if (l != a) differ++; }
      check(!a.empty() && a == a2 && found >= 10 && differ > 0, "their things: found on most, the same at the same spot, not all alike (" + a + ")");
      // a trap: two rise beside the bones, once
      const int TX = UX + 14, TZ = UZ + 12;
      for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) { w.setBlock(TX + dx, Y - 1, TZ + dz, reg().blockId("stone")); w.setBlock(TX + dx, Y, TZ + dz, AIR); w.setBlock(TX + dx, Y + 1, TZ + dz, AIR); }
      w.setBlock(TX, Y, TZ, reg().blockId("remains_desert"));
      w.setBlockEntity(TX, Y, TZ, {{"type", "chest"}, {"items", json::array()}, {"trap", "husk"}});
      auto count = [&] { int n = 0; for (auto& e : L->entities()) if (e && !e->removed && (e->type == "husk" || e->type == "zombie") && glm::distance(e->pos, vec3(TX, Y, TZ)) < 4) n++; return n; };
      const int before = count();
      fallenOpened(*L, *P, w, TX, Y, TZ);
      const int after = count();
      fallenOpened(*L, *P, w, TX, Y, TZ);
      check(after - before == 2 && count() == after, "a trapped one raises two beside the bones, and only the once");
      for (auto& e : L->entities()) if (e && (e->type == "husk" || e->type == "zombie")) e->removed = true;
    }
    // a feeding trough (sim/trough.h): food used on it goes in, even when you're hurt (it isn't eaten); an empty hand takes it back
    {
      const int X = UX + 6, Z = UZ + 8;
      w.setBlock(X, Y, Z, reg().blockId("feeding_trough")); w.setBlock(X, Y + 1, Z, AIR);
      for (auto& st : P->inv) st = ItemStack();
      P->hp = 6; P->foodCd = 0; P->inv[0] = ItemStack("carrot", 5);
      aim(X, Y, Z); pc.use(false, plain);
      check(troughStack(w, X, Y, Z).id == "carrot" && troughStack(w, X, Y, Z).n == 5 && P->hp == 6, "hurt, a carrot used on the trough goes in, not down your throat (" + std::to_string(troughStack(w, X, Y, Z).n) + ")");
      P->inv[0] = ItemStack("carrot", 3);
      aim(X, Y, Z); pc.use(false, sneak);
      check(troughStack(w, X, Y, Z).n == 8 && w.getBlock(X, Y + 1, Z) == AIR, "...crouched too (topped up, nothing set down on top of it)");
      aim(X, Y, Z); pc.use(false, plain);
      int back = 0; for (auto& st : P->inv) if (st.id == "carrot") back += st.n;
      check(back == 8 && troughStack(w, X, Y, Z).empty(), "an empty hand takes them back");
      P->hp = P->maxHp;
    }
    // the crafting table worked in the world, in normal and immersive worlds alike: planks on two squares, one above the
    // other as you look, make sticks
    for (bool im : {false, true}) {
      L->info.rules["immersive"] = im; w.immersive = im;
      const std::string mode = im ? " (immersive)" : " (normal)";
      const int X = UX + (im ? 2 : 0), Z = UZ + 8;
      w.setBlock(X, Y, Z, reg().blockId("crafting_table")); w.setBlock(X, Y + 1, Z, AIR);
      P->yaw = 0;   // looking north: the far row is the north one
      auto top = [&](float fx, float fz) { aim(X, Y, Z); pc.target.point = vec3(X + fx, Y + 1.f, Z + fz); pc.targetPoint = pc.target.point; };
      for (auto& st : P->inv) st = ItemStack();
      P->inv[0] = ItemStack("planks", 2);
      top(0.5f, 0.2f); pc.use(false, plain);
      top(0.5f, 0.5f); pc.use(false, plain);
      const json* te = w.blockEntity(X, Y, Z);
      int on = 0; if (te) for (auto& it : (*te)["items"]) if (!it.is_null()) on++;
      check(on == 2 && P->inv[0].empty(), "two planks set on the table's top, one square each" + mode);
      top(0.15f, 0.85f); pc.use(false, plain);   // an empty hand on an empty square: make it
      int sticks = 0; for (auto& st : P->inv) if (st.id == "stick") sticks += st.n;
      check(sticks == 4 && !w.blockEntity(X, Y, Z), "...an empty hand makes four sticks, and the table is clear (" + std::to_string(sticks) + ")" + mode);
      P->inv[0] = ItemStack("planks", 1); top(0.5f, 0.5f); pc.use(false, plain);
      P->inv[0] = ItemStack(); top(0.5f, 0.5f); pc.use(false, plain);
      int back = 0; for (auto& st : P->inv) if (st.id == "planks") back += st.n;
      check(back == 1 && !w.blockEntity(X, Y, Z), "a piece set down and taken back" + mode);
    }
  }

  printf("[potions set down, not drunk]\n");
  {
    PlayerController pc(*L, *P);
    auto aim = [&](int x, int y, int z, ivec3 n) {
      pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(x, y, z); pc.target.normal = n; pc.target.id = w.getBlock(x, y, z);
      pc.targetPoint = vec3(x + 0.5f, y + (n.y > 0 ? 1.f : 0.5f), z + 0.5f + n.z * 0.5f);
    };
    PlayerInput sneak; sneak.sneak = true; PlayerInput plain;
    const int SX = 6, SZ = 16;
    w.setBlock(SX, Y + 1, SZ, reg().blockId("oak_shelf"), 0);
    for (auto& s : P->inv) s = ItemStack();
    P->selected = 0; P->inv[0] = ItemStack("potion_regen", 2);
    P->sneaking = false; aim(SX, Y + 1, SZ, ivec3(0, 0, -1)); pc.use(false, plain);
    const json* e = w.blockEntity(SX, Y + 1, SZ);
    bool onShelf = false; if (e && e->contains("items")) for (auto& it : (*e)["items"]) if (it.is_object() && it.value("id", "") == "potion_regen") onShelf = true;
    check(onShelf && P->inv[0].empty(), "a potion used on a shelf stands on it (not drunk)");
    P->inv[0] = ItemStack("potion_regen", 1);
    const int GX = SX + 3, GZ = SZ + 4;
    w.setBlock(GX, Y, GZ, reg().blockId("stone"), 0); w.setBlock(GX, Y + 1, GZ, AIR, 0);
    P->sneaking = true; aim(GX, Y, GZ, ivec3(0, 1, 0)); pc.use(false, sneak); P->sneaking = false;
    check(P->inv[0].empty() && w.getBlock(GX, Y + 1, GZ) != AIR, "crouched on the ground it's set down there (not drunk)");
    P->inv[0] = ItemStack("egg", 1);
    w.setBlock(GX + 2, Y, GZ, reg().blockId("stone"), 0); w.setBlock(GX + 2, Y + 1, GZ, AIR, 0);
    P->sneaking = true; aim(GX + 2, Y, GZ, ivec3(0, 1, 0)); pc.use(false, sneak); P->sneaking = false;
    check(P->inv[0].empty() && w.getBlock(GX + 2, Y + 1, GZ) != AIR, "an egg crouched on the ground is set down there (not thrown)");
  }

  printf("[using a furnace with ore in hand]\n");
  {
    L->info.rules["immersive"] = false; w.immersive = false; L->tick(0.05);   // (the recipe gate follows the rule each tick)
    PlayerController pc(*L, *P);
    const int FX = 10, FZ = 20;
    for (const char* fk : {"furnace", "blast_furnace"}) {
      w.setBlock(FX, Y, FZ, reg().blockId("stone")); w.setBlock(FX, Y + 1, FZ, reg().blockId(fk), 0); w.setBlock(FX, Y + 1, FZ + 1, AIR);
      for (auto& st : P->inv) st = ItemStack();
      P->selected = 0; P->inv[0] = ItemStack("iron_ore", 4); P->sneaking = false;
      pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(FX, Y + 1, FZ); pc.target.normal = ivec3(0, 0, 1); pc.target.id = w.getBlock(FX, Y + 1, FZ);
      pc.targetPoint = vec3(FX + 0.5f, Y + 1.5f, FZ + 1.f);
      PlayerInput plain; pc.use(false, plain);
      check(w.getBlock(FX, Y + 1, FZ + 1) == AIR && P->inv[0].n == 4, std::string("using a ") + fk + " with ore in hand opens it (the ore isn't placed against it)");
      check(!smeltResult("iron_ore_piece", fk == std::string("furnace") ? "furnace" : "blast").empty(), std::string("  iron ore smelts in a ") + fk);
      w.setBlock(FX, Y + 1, FZ, AIR);
    }
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  L->close();
  return fails;
}
