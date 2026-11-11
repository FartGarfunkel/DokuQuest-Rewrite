// tools/test_progression.cpp — two legitimate playthroughs from an empty inventory (headless): CLASSIC (punch a log ... diamond
// pickaxe, alloys, steel, redstone) and IMMERSIVE (TerraFirmaCraft's way: loose rocks, knapping, the pit kiln, charcoal, casting,
// the anvils, the bloomery, steel, a chisel). Every item comes from a game action: breaking blocks set in the arena (the world's
// own raw blocks) with the tool in hand, knapping, crafting through matchGrid/consumeCraft, the station functions and the
// controller's use, with time passed by ticking the level. A step that can't be done legitimately is a FAIL that says why; a
// missing item is granted only after its failure is recorded (the check says so). Run: build/test_progression (exit = failures)
#include "core/init.h"
#include "core/registry.h"
#include "core/metals.h"
#include "core/tool_parts.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/rules.h"
#include "sim/events.h"
#include "sim/inventory.h"
#include "sim/crafting.h"
#include "sim/worldsys.h"
#include "sim/heat.h"
#include "sim/knapping.h"
#include "sim/firestarter.h"
#include "sim/firepit.h"
#include "sim/pit_kiln.h"
#include "sim/charcoal_pit.h"
#include "sim/piles.h"
#include "sim/metalwork.h"
#include "sim/forge.h"
#include "sim/anvil.h"
#include "sim/bloomery.h"
#include "sim/immersive.h"
#include "sim/combat.h"
#include "sim/gameplay.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include <thread>
#include <cstdio>
#include <cmath>
#include <queue>

using namespace dq;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); fflush(stdout); (ok ? passes : fails)++; }
static std::string S(int n) { return std::to_string(n); }
#define CHECK(cond, msg) do { const bool c_ = (cond); check(c_, (msg)); } while (0)   // (the message after the action: it reads why)

struct Play {
  ThreadPool pool{2}; EventBus bus; std::unique_ptr<Level> lv; Player* P = nullptr; int Y = 0; bool imm = false, table = false;
  std::unique_ptr<PlayerController> pc;
  std::string opened; json openedOpts;
  World& w() { return lv->world(); }
  Play(const std::string& name, bool immersiveOn) : imm(immersiveOn) {
    LevelInfo li; li.name = name; li.seedText = "progression";
    li.rules = json{{"immersive", immersiveOn}, {"_v", 1}, {"mobSpawning", false}, {"hostileSpawns", false}, {"passiveSpawns", false}, {"weatherCycle", false}};
    lv = Level::create(userSub("saves"), li, &pool, &bus);
    Y = dimBase(DIM_OVERWORLD) + 200;
    auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 8.5f); P = static_cast<Player*>(lv->spawn(std::move(p)));
    double t0 = nowSeconds();
    while (nowSeconds() - t0 < 60) {
      w().stream({{P->pos, 3, false}}, 20);
      bool all = true;
      for (int cx = -2; cx <= 2; cx++) for (int cz = -2; cz <= 2; cz++) if (!w().chunk(DIM_OVERWORLD, cx, cz)) all = false;
      if (all) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const BlockId stone = reg().blockId("stone");
    for (int x = -14; x < 30; x++) for (int z = -14; z < 30; z++) { w().setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 12; y++) w().setBlock(x, y, z, AIR); }
    P->onGround = true;
    for (auto& s : P->inv) s = ItemStack();
    for (auto& s : P->offhand) s = ItemStack();
    P->selected = 0;
    pc = std::make_unique<PlayerController>(*lv, *P);
    bus.on("screenOpen", [this](const json& a) { if (a.is_array() && a.size() > 2) { opened = a[1].get<std::string>(); openedOpts = a[2]; } return true; });
    recipeGate().immersive = imm;
  }
  ~Play() { lv->close(); }
  void tick(double secs) {
    double since = 1;
    for (double t = 0; t < secs; t += TICK_DT) {
      lv->tick(TICK_DT);
      if ((since += TICK_DT) >= 1) { since = 0; w().stream({{P->pos, 3, false}}, 20); P->hp = P->maxHp; P->burn = 0; }
    }
  }
  // ---- the bag ----
  int count(const std::string& id) { int n = 0; for (auto& s : P->inv) if (!s.empty() && s.id == id) n += s.n; for (auto& s : P->offhand) if (!s.empty() && s.id == id) n += s.n; return n; }
  void give(ItemStack s) { if (s.empty()) return; const int left = P->give(s); if (left > 0) { static int told = 0; if (told++ < 3) { printf("    (bag full: %d %s lost; bag:", left, s.id.c_str()); for (auto& b : P->inv) printf(" %s%s", b.empty() ? "-" : b.id.c_str(), b.empty() ? "" : ("x" + S(b.n) + (b.tag.is_null() ? "" : "*")).c_str()); printf(")\n"); } } }
  int slotOf(const std::function<bool(const ItemStack&)>& f) { for (int i = 0; i < (int)P->inv.size(); i++) if (!P->inv[i].empty() && f(P->inv[i])) return i; return -1; }
  int slotOf(const std::string& id) { return slotOf([&](const ItemStack& s) { return s.id == id; }); }
  ItemStack take(const std::string& id, int n) {   // n of one stack kind out of the bag
    ItemStack out; for (auto& s : P->inv) if (!s.empty() && s.id == id && n > 0 && (out.empty() || canMergeStacks(out, s))) {
      const int k = std::min(n, s.n); if (out.empty()) { out = s; out.n = 0; } out.n += k; s.n -= k; n -= k; if (s.n <= 0) s = ItemStack(); }
    return out;
  }
  bool hold(const std::function<bool(const ItemStack&)>& f) { const int i = slotOf(f); if (i < 0) return false; std::swap(P->inv[i], P->inv[P->selected]); return true; }
  bool hold(const std::string& id) { return hold([&](const ItemStack& s) { return s.id == id; }); }
  void holdNothing() { if (P->held().empty()) return; for (int i = 0; i < (int)P->inv.size(); i++) if (P->inv[i].empty()) { std::swap(P->inv[i], P->inv[P->selected]); return; } }
  void tidy() {   // what a player does with a cluttered bag: throw the junk away, put like with like
    for (auto& b : P->inv) if (!b.empty() && (b.id == "sapling" || b.id == "apple" || b.id == "carrot" || b.id == "potato" || b.id.find("seeds") != std::string::npos || b.id == "leaves")) b = ItemStack();
    for (size_t i = 0; i < P->inv.size(); i++) for (size_t j = i + 1; j < P->inv.size(); j++) {
      ItemStack &a = P->inv[i], &b = P->inv[j];
      if (a.empty() || b.empty() || !canMergeStacks(a, b)) continue;
      const int k = std::min(b.n, maxStackOf(a.id) - a.n); a.n += k; b.n -= k; if (b.n <= 0) b = ItemStack();
    }
  }
  void cool(double secs = 300) { holdNothing(); tick(secs); tidy(); }
  std::map<std::string, int> collect() {   // pick up what lies about (the drops of what was just broken)
    std::map<std::string, int> got;
    for (auto& e : lv->entities()) if (!e->removed && e->type == "item") { ItemStack s = ItemStack::fromJson(e->data["stack"]); got[s.id] += s.n; give(s); e->removed = true; }
    return got;
  }
  void aim(int x, int y, int z, ivec3 n = ivec3(0, 1, 0), vec3 frac = vec3(0.5f, 1.f, 0.5f)) {
    pc->target = RayHit{}; pc->target.hit = true; pc->target.block = ivec3(x, y, z); pc->target.normal = n; pc->target.id = w().getBlock(x, y, z);
    pc->target.point = vec3(x, y, z) + frac; pc->targetPoint = pc->target.point; pc->targetEnt = 0;
  }
  void aimAir() { pc->target = RayHit{}; pc->targetEnt = 0; }
  void use(bool sneak = false, bool repeat = false) { PlayerInput in; in.sneak = sneak; P->sneaking = sneak; pc->use(repeat, in); P->sneaking = false; }
  bool place(const std::string& item, int x, int y, int z) {   // set down a block item from the bag
    const ItemDef* d = reg().item(item); if (!d || d->block.empty()) return false;
    ItemStack one = take(item, 1); if (one.empty()) return false;
    w().setBlock(x, y, z, reg().blockId(d->block)); return true;
  }
  // ---- breaking: the world's raw block set at (2, Y+1, 2) and broken with what's in hand (the real tier rules) ----
  std::string lastMine;
  std::map<std::string, int> mine(const std::string& block, int times = 1, u8 meta = 0) {
    std::map<std::string, int> got; lastMine.clear();
    const BlockId id = reg().blockId(block);
    if (!id) { lastMine = "no block " + block; return got; }
    for (int i = 0; i < times; i++) {
      w().setBlock(2, Y + 1, 2, id, meta); aim(2, Y + 1, 2);
      const float t = pc->breakTime(id, &pc->target);
      if (!std::isfinite(t)) { lastMine = block + " can't be broken with " + (P->held().empty() ? std::string("a bare hand") : P->held().id); w().setBlock(2, Y + 1, 2, AIR); break; }
      pc->breakBlock(2, Y + 1, 2);
      for (auto& [k, n] : collect()) got[k] += n;
    }
    return got;
  }
  static std::string list(const std::map<std::string, int>& m) { std::string s; for (auto& [k, n] : m) s += (s.empty() ? "" : ", ") + S(n) + " " + k; return s.empty() ? "nothing" : s; }
  // ---- crafting: the recipe book's way (an open recipe laid out from the bag, matchGrid, consumeCraft) ----
  std::string why;
  bool craft(const std::string& out, int times = 1) {
    recipeGate().immersive = imm;
    for (int t = 0; t < times; t++) {
      bool done = false; why = "no recipe for " + out + " is open in this mode";
      for (auto& r : reg().recipes) {
        if (r.out != out || !recipeOpen(r)) continue;
        int rw = 1, rh = 1; recipeDims(r, rw, rh);
        const int W = (rw > 2 || rh > 2) ? 3 : 2;
        if (W == 3 && !table) { why = "needs a crafting table"; continue; }
        const auto lay = recipeLayout(r, W);
        std::vector<std::pair<int, std::string>> cells;
        for (int i = 0; i < W * W && i < (int)lay.size(); i++) if (!lay[i].empty()) cells.push_back({i, lay[i]});
        if (craftAt(cells, W, out)) { done = true; break; }
        if (getenv("DQ_DEBUG")) printf("    [craft %s: %s]\n", out.c_str(), why.c_str());
      }
      if (!done) return false;
    }
    return true;
  }
  bool craftAt(const std::vector<std::pair<int, std::string>>& cells, int W, const std::string& expect) {   // cells: want = key or #tag
    recipeGate().immersive = imm;
    std::vector<ItemStack> grid(W * W);
    auto back = [&] { for (auto& g : grid) give(g); };
    for (auto& [i, want] : cells) {
      int s = slotOf([&](const ItemStack& st) { return tagMatch(want, st.id) && itemHeat(st) < 100; });
      if (s < 0) s = slotOf([&](const ItemStack& st) { return tagMatch(want, st.id); });
      if (s < 0) { why = "missing " + want; back(); return false; }
      grid[i] = P->inv[s]; grid[i].n = 1; if (--P->inv[s].n <= 0) P->inv[s] = ItemStack();
    }
    CraftMatch m = matchGrid(grid, W);
    ItemStack res = m ? m.result() : ItemStack();
    if (!m || (res.id != expect && m.out != expect && !(m.recipe && m.recipe->out == expect) && proceduralOf(expect).id != res.id)) { why = "the grid makes " + (m ? res.id : std::string("nothing")); back(); return false; }
    consumeCraft(grid, m, [&](const ItemStack& s) { give(s); });
    back(); why = "made " + S(res.n) + " " + res.id; give(res); return true;
  }
  // ---- knapping: use in the air opens the screen; then what the screen does (chip, match, take) ----
  bool knap(const std::string& item, const std::string& out) {
    const std::string mat = knapMaterialOf(item);
    if (!hold(item)) { why = "no " + item; return false; }
    aimAir(); opened.clear(); use();
    if (opened != "knapping") { why = "the knapping screen didn't open (" + S(count(item)) + " " + item + ")"; return false; }
    const KnapPattern* p = nullptr; for (auto& kp : knapPatterns(mat)) if (kp.out == out) { p = &kp; break; }
    if (!p) { why = "no " + mat + " pattern makes " + out; return false; }
    KnapGrid g; g.kept.fill(p->defOn);
    for (int y = 0; y < (int)p->rows.size(); y++) for (int x = 0; x < (int)p->rows[y].size(); x++) g.kept[y * 5 + x] = p->rows[y][x] == '#';
    const KnapPattern* m = knapMatch(mat, g);
    if (!m || m->out != out) { why = "the carved grid doesn't match"; return false; }
    if (count(item) < knapCost(mat)) { why = "not enough " + item; return false; }
    take(item, knapCost(mat));
    give(ItemStack(m->out, m->n)); return true;
  }
  // ---- the firestarter: held use for a second (luck fixed with setFirestarterRoll(0), the test hook) ----
  bool spark(int x, int y, int z) {
    for (int k = 0; !count("firestarter") && imm && count("stick") < 2 && k < 8; k++) { holdNothing(); mine("twigs"); }
    if (!count("firestarter") && !craft("firestarter")) { why = "no firestarter: " + why; return false; }
    hold("firestarter"); aim(x, y, z); setFirestarterRoll(0.f);
    use(); for (int i = 0; i < 6; i++) use(false, true);
    setFirestarterRoll(-1); return true;
  }
  // ---- the furnace (a placed furnace/blast furnace block; its slots filled from the bag) ----
  int smelt(int x, int y, int z, const std::string& kind, const std::string& in, int n, const std::string& fuel, int nf, double maxSecs = 400) {
    json* e = containerEntity(w(), x, y, z);
    if (!e) { why = "no furnace entity"; return 0; }
    ItemStack a = take(in, n), f = take(fuel, nf);
    if (a.empty()) { why = "no " + in; give(f); return 0; }
    (*e)["items"][0] = a.toJson(); (*e)["items"][1] = f.toJson(); w().blockEntityChanged(x, y, z);
    for (double t = 0; t < maxSecs; t += 5) { tick(5); e = containerEntity(w(), x, y, z); if ((*e)["items"][0].is_null()) break; }
    e = containerEntity(w(), x, y, z);
    ItemStack o = ItemStack::fromJson((*e)["items"][2]); const int got = o.empty() ? 0 : o.n;
    for (int i = 0; i < 3; i++) { give(ItemStack::fromJson((*e)["items"][i])); (*e)["items"][i] = nullptr; }
    why = "smelt result: " + (smeltResult(in, kind).empty() ? std::string("none in a ") + kind : smeltResult(in, kind));
    return got;
  }
};

// =====================================================================================================================
static void classic() {
  printf("\n[CLASSIC]\n");
  Play g("ProgClassic", false);
  CHECK(!immersive(*g.lv), "a classic world");
  auto got = g.mine("log", 4);
  CHECK(got["log"] == 4, "punch a log by hand: " + Play::list(got));
  CHECK(g.craft("planks", 4) && g.count("planks") == 16, "logs into planks (" + S(g.count("planks")) + ") " + g.why);
  CHECK(g.craft("crafting_table") && g.place("crafting_table", 6, g.Y + 1, 0), "a crafting table, set down");
  g.table = true;
  CHECK(g.craft("stick", 2) && g.count("stick") >= 8, "sticks (" + S(g.count("stick")) + ")");
  CHECK(g.craft("wood_pickaxe"), "a wooden pickaxe " + g.why);
  g.hold("wood_pickaxe");
  got = g.mine("stone", 20);
  CHECK(got["cobblestone"] == 20, "the wooden pickaxe mines stone: " + Play::list(got));
  got = g.mine("gneiss", 1);
  CHECK(got["cobbled_gneiss"] == 1, "a layer rock (gneiss) drops cobbled gneiss: " + Play::list(got) + " " + g.lastMine);
  got = g.mine("coal_ore", 6);
  CHECK(got["coal"] >= 6, "coal from coal ore: " + Play::list(got));
  got = g.mine("iron_ore", 1);
  CHECK(got.empty(), "the wooden pickaxe gets nothing from iron ore (tier): " + Play::list(got));
  CHECK(g.craft("stone_pickaxe") && g.craft("stone_pickaxe"), "stone pickaxes " + g.why);
  CHECK(g.craft("furnace") && g.place("furnace", 8, g.Y + 1, 0), "a furnace from cobblestone, set down " + g.why);
  const int FX = 8, FY = g.Y + 1, FZ = 0;
  std::map<std::string, int> ores;
  for (const char* o : {"iron_ore", "copper_ore", "tin_ore", "zinc_ore", "bismuth_ore"}) {
    if (!g.hold("stone_pickaxe")) g.craft("stone_pickaxe"), g.hold("stone_pickaxe");
    const int n = std::string(o) == "iron_ore" ? 6 : std::string(o) == "copper_ore" ? 10 : 2;
    got = g.mine(o, n);
    CHECK(got[o] == n, std::string("the stone pickaxe mines ") + o + ": " + Play::list(got) + " " + g.lastMine);
  }
  for (const char* o : {"gold_ore", "silver_ore", "diamond_ore", "redstone_ore"}) {
    g.hold("stone_pickaxe"); got = g.mine(o, 1);
    CHECK(got.empty(), std::string("the stone pickaxe can't get ") + o + " (tier 3): " + Play::list(got));
  }
  // smelt the ores, cobblestone to stone (fuel: coal)
  for (auto [o, out] : std::vector<std::pair<std::string, std::string>>{{"iron_ore", "iron_ingot"}, {"copper_ore", "copper_ingot"}, {"tin_ore", "tin_ingot"}, {"zinc_ore", "zinc_ingot"}, {"bismuth_ore", "bismuth_ingot"}}) {
    const int n = g.count(o); const int made = g.smelt(FX, FY, FZ, "furnace", o, n, "coal", 2);
    CHECK(made == n && g.count(out) >= n, "the furnace smelts " + o + " into " + out + " (" + S(made) + "/" + S(n) + ") " + (made == n ? "" : g.why));
  }
  CHECK(g.craft("bronze_ingot") && g.count("bronze_ingot") == 4, "classic bronze: 3 copper + 1 tin (" + S(g.count("bronze_ingot")) + ") " + g.why);
  CHECK(g.craft("brass_ingot"), "classic brass: 6 copper + 1 zinc (" + S(g.count("brass_ingot")) + ") " + g.why);
  if (g.count("copper_ingot") < 3) { got = g.mine("copper_ore", 0); }
  { g.hold("stone_pickaxe"); got = g.mine("copper_ore", 3); const int n = g.count("copper_ore"); g.smelt(FX, FY, FZ, "furnace", "copper_ore", n, "coal", 1); }
  CHECK(g.craft("bismuth_bronze_ingot"), "classic bismuth bronze: zinc + 3 copper + bismuth (" + S(g.count("bismuth_bronze_ingot")) + ") " + g.why);
  CHECK(g.craft("mixed_pickaxe") || g.craft("iron_pickaxe"), "an iron pickaxe from 3 iron ingots " + g.why);
  const int ip = g.slotOf([](const ItemStack& s) { return s.id == "mixed_pickaxe" || s.id == "iron_pickaxe"; });
  const ItemStack ipick = ip >= 0 ? g.P->inv[ip] : ItemStack();
  CHECK(ipick.id == "mixed_pickaxe" && stackName(ipick) == "Iron Pickaxe" && toolOf(ipick).tier >= 3, "  ...it's the procedural \"" + stackName(ipick) + "\", tier " + S(toolOf(ipick).tier));
  for (const char* o : {"gold_ore", "silver_ore", "diamond_ore", "redstone_ore"}) {
    g.hold([](const ItemStack& s) { return s.id == "mixed_pickaxe" || s.id == "iron_pickaxe"; });
    got = g.mine(o, std::string(o) == "diamond_ore" ? 3 : 1);
    CHECK(!got.empty(), std::string("the iron pickaxe mines ") + o + ": " + Play::list(got) + " " + g.lastMine);
  }
  for (const char* o : {"gold_ore", "silver_ore"}) { const int n = g.count(o); const int m = g.smelt(FX, FY, FZ, "furnace", o, n, "coal", 1); CHECK(n > 0 && m == n, std::string("smelt ") + o + " (" + S(m) + ") " + g.why); }
  g.holdNothing(); g.mine("log", 2); g.craft("planks", 2); g.craft("stick", 2);
  const int dia = g.count("diamond");
  CHECK(dia >= 3 && g.craft("diamond_pickaxe"), "a diamond pickaxe from " + S(dia) + " diamonds " + g.why);
  CHECK(g.craft("steel_blend") && g.smelt(FX, FY, FZ, "furnace", "steel_blend", 1, "coal", 1) == 1 && g.count("steel_ingot") >= 1, "steel: a steel blend smelted (" + S(g.count("steel_ingot")) + " steel) " + g.why);
  CHECK(g.craft("lever") && g.count("lever") == 1, "a lever from a stick and cobblestone " + g.why);
  CHECK(g.craft("piston") && g.count("piston") == 1, "a piston (planks, cobblestone, iron, redstone) " + g.why);
  // campfire cooking: charcoal from a log, a campfire, a potato from a ripe crop
  g.hold("stone_pickaxe"); g.mine("log", 5); g.holdNothing(); g.craft("planks"); g.craft("stick");
  const int ch = g.smelt(FX, FY, FZ, "furnace", "log", 1, "coal", 1);
  CHECK(ch == 1 && g.craft("campfire") && g.place("campfire", 12, g.Y + 1, 4), "charcoal (" + S(ch) + ") and a campfire, set down " + g.why);
  got = g.mine("potato_crop", 1, 3);
  CHECK(got["potato"] >= 1, "a ripe potato crop by hand: " + Play::list(got));
  g.hold("potato"); g.aim(12, g.Y + 1, 4); g.use();
  json* ce = g.w().blockEntity(12, g.Y + 1, 4);
  bool onIt = ce && (*ce)["items"].is_array() && !(*ce)["items"][0].is_null();
  g.tick(40);
  auto cooked = g.collect();
  if (!cooked.count("baked_potato") && ce) { ce = g.w().blockEntity(12, g.Y + 1, 4); if (ce) for (auto& it : (*ce)["items"]) if (!it.is_null() && it.value("id", "") == "baked_potato") cooked["baked_potato"]++; }
  CHECK(onIt && cooked["baked_potato"] >= 1, "the classic campfire cooks the potato: " + Play::list(cooked));
}

// =====================================================================================================================
struct Imm : Play {
  Imm() : Play("ProgImmersive", true) {}
  int granted = 0;
  void grant(const std::string& id, int n) { give(ItemStack(id, n)); granted++; printf("    GRANTED %d %s (after the failure above)\n", n, id.c_str()); }
  // ground gathering
  void gather(const std::string& item, const std::string& block, int n) { tidy(); for (int k = 0; count(item) < n && k < 4 * n; k++) { holdNothing(); const int b = count(item); mine(block); if (count(item) <= b) break; } }
  void pebbles(int n) { gather("pebble", "loose_stones", n); }
  void sticks(int n) { gather("stick", "twigs", n); }
  void flint(int n) { gather("flint", "loose_flint", n); }
  // tools, knapped again when one wears out
  bool stoneTool(const std::string& tool, const std::string& head) {
    if (count(tool)) return true;
    pebbles(2); sticks(1);
    if (!count(head) && !knap("pebble", head)) return false;
    return craftAt({{0, head}, {2, "stick"}}, 2, tool);
  }
  bool axe() { if (!count("stone_axe") && !stoneTool("stone_axe", "stone_axe_head")) return false; return hold("stone_axe"); }
  bool pick() {   // the best pickaxe in the bag
    for (int tier = 9; tier >= 1; tier--) if (hold([&](const ItemStack& s) { const ToolInfo t = toolOf(s); return t.type == "pickaxe" && t.tier == tier; })) return true;
    return stoneTool("crude_pickaxe", "crude_pickaxe_head") && hold("crude_pickaxe");
  }
  bool hammer() { if (!count("stone_hammer") && !stoneTool("stone_hammer", "stone_hammer_head")) return false; return hold("stone_hammer"); }
  int logs(int n) { int k = 0; while (count("log") < n && k++ < 2 * n) { if (!axe()) return count("log"); mine("log"); } return count("log"); }
  int ore(const std::string& block, const std::string& piece, int n) { int k = 0; while (count(piece) < n && k++ < 3 * n) { if (!pick()) break; auto g = mine(block); if (g.empty()) break; } return count(piece); }
  // the forge at (14, Y+1, 10): heat things to working heat
  int FX = 14, FZ = 10;
  bool forgeFuel() {
    json* e = w().blockEntity(FX, Y + 1, FZ);
    if (!e) return false;
    while (e->value("fuel", 0) < 6 && count("charcoal")) { hold("charcoal"); aim(FX, Y + 1, FZ); use(); e = w().blockEntity(FX, Y + 1, FZ); }
    if (!e->value("lit", false)) spark(FX, Y + 1, FZ);
    return w().blockEntity(FX, Y + 1, FZ)->value("lit", false);
  }
  int inForge() { int k = 0; for (auto& it : (*w().blockEntity(FX, Y + 1, FZ))["items"]) if (!it.is_null()) k++; return k; }
  bool takeAll() {   // an empty hand on the forge, till it's empty
    for (int i = 0; i < 10 && inForge(); i++) {
      holdNothing(); if (!P->held().empty()) { why = "the bag is full: can't take things out of the forge"; return false; }
      const int b = inForge(); aim(FX, Y + 1, FZ); use();
      if (inForge() == b) { why = "use with an empty hand didn't take from the forge: " + (*w().blockEntity(FX, Y + 1, FZ))["items"].dump().substr(0, 200); return false; }
    }
    return true;
  }
  bool heat(const std::function<bool(const ItemStack&)>& what, int n, float atLeast) {   // n of them in, heated together, taken out
    tidy(); why.clear();
    if (!forgeFuel()) { why = "the forge won't light"; return false; }
    int in = 0;
    for (int i = 0; i < n; i++) { if (!hold(what)) break; aim(FX, Y + 1, FZ); use(); in++; }
    { int inForge = 0, empty = 0; for (auto& it : (*w().blockEntity(FX, Y + 1, FZ))["items"]) if (!it.is_null()) inForge++; for (auto& b : P->inv) if (b.empty()) empty++;
      if (inForge != in) { why = S(in) + " put in, " + S(inForge) + " in the forge (" + S(empty) + " free bag slots): " + (*w().blockEntity(FX, Y + 1, FZ))["items"].dump().substr(0, 300); takeAll(); return false; } }
    if (in < n) { why = "only " + S(in) + " to heat"; }
    float h = 0;
    for (int t = 0; t < 800; t++) {   // watched closely: out as soon as it's hot enough (it mustn't go past melting)
      tick(0.25); if (t % 20 == 0) forgeFuel();
      json* e = w().blockEntity(FX, Y + 1, FZ); h = 1e9f;
      for (auto& it : (*e)["items"]) if (!it.is_null()) h = std::min(h, itemHeat(ItemStack::fromJson(it)));
      if (h >= atLeast) break;
    }
    if (!takeAll()) return false;
    if (getenv("DQ_DEBUG")) { const json& fi = (*w().blockEntity(FX, Y + 1, FZ))["items"]; bool any = false; for (auto& it : fi) if (!it.is_null()) any = true; if (any) printf("      [left in the forge after taking: %s; held %s; lit %d]\n", fi.dump().substr(0, 200).c_str(), P->held().id.c_str(), (int)w().blockEntity(FX, Y + 1, FZ)->value("lit", false)); }
    if (h < atLeast) { why = "the forge only got it to " + S((int)h) + " (needs " + S((int)atLeast) + ")"; return false; }
    return in == n;
  }
  // smithing: hammer in hand, the hot piece in the other; use on the anvil opens the work; then what the screen does
  static std::vector<AnvilMove> solve(const std::string& key, int target) {
    std::vector<int> prev(151, -2), pm(151, 0); prev[0] = -1; std::queue<int> q; q.push(0);
    while (!q.empty()) { int v = q.front(); q.pop(); for (int m = 0; m < (int)AnvilMove::Count; m++) { int nv = std::clamp(v + anvilMoveValue((AnvilMove)m), 0, 150); if (prev[nv] == -2) { prev[nv] = v; pm[nv] = m; q.push(nv); } } }
    std::vector<AnvilMove> best; float bq = 0;
    for (int a = 0; a < 512; a++) for (int v = 0; v <= 150; v++) {
      if (prev[v] == -2) continue;
      std::vector<AnvilMove> seq; for (int u = v; prev[u] != -1; u = prev[u]) seq.insert(seq.begin(), (AnvilMove)pm[u]);
      for (int k : {a / 64, (a / 8) % 8, a % 8}) seq.push_back((AnvilMove)k);
      AnvilWork wk; wk.recipe = key; wk.target = target; for (auto m : seq) anvilApply(wk, m);
      if (anvilDone(wk) && anvilQuality(wk) > bq) { bq = anvilQuality(wk); best = seq; if (bq > 1.1f) return best; }
    }
    return best;
  }
  bool smith(int ax, int ay, int az, const std::string& key, const std::function<bool(const ItemStack&)>& piece) {
    why.clear();
    const AnvilRecipe* r = anvilRecipe(key);
    if (!r) { why = "no anvil recipe " + key; return false; }
    const int s = slotOf([&](const ItemStack& st) { return piece(st) && st.n >= r->n; });
    if (s < 0) { why = "no stack of " + S(r->n) + " hot " + r->in + " in the bag";
      if (getenv("DQ_DEBUG")) for (auto& b : P->inv) if (!b.empty() && anvilPieceOf(b) == r->in) printf("      [%s x%d %s]\n", b.id.c_str(), b.n, b.tag.dump().c_str());
      return false; }
    std::swap(P->offhand[0], P->inv[s]);
    if (!hammer()) { why = "no hammer"; std::swap(P->offhand[0], P->inv[s]); return false; }
    aim(ax, ay, az); opened.clear(); use();
    auto putBack = [&] { give(P->offhand[0]); P->offhand[0] = ItemStack(); };
    if (opened != "anvil_smith") { why = "the anvil's work didn't open (" + reg().block(w().getBlock(ax, ay, az)).key + ")"; putBack(); return false; }
    const int tier = openedOpts.value("tier", -1);
    if (!anvilCanWork(P->offhand[0], *r, tier, &why)) { why += " (anvil tier " + S(tier) + ")"; putBack(); return false; }
    const auto moves = solve(key, anvilTarget((u64)(u32)lv->info.seed, key));
    AnvilWork wk; wk.recipe = key; wk.target = anvilTarget((u64)(u32)lv->info.seed, key);
    for (auto m : moves) { if (!hammer()) { why = "the hammer wore out"; putBack(); return false; } anvilApply(wk, m); wearHeld(*lv, *P, 1); }
    if (!anvilDone(wk)) { why = "couldn't finish the work"; putBack(); return false; }
    if (r->weld) { if (!count("flux")) { why = "welding needs flux"; putBack(); return false; } take("flux", 1); }
    ItemStack out = anvilOutput(*r, P->offhand[0], anvilQuality(wk));
    if (out.empty()) { why = "that can't be made of this"; putBack(); return false; }
    P->offhand[0].n -= r->n; if (P->offhand[0].n <= 0) P->offhand[0] = ItemStack();
    putBack(); give(out); return true;
  }
  // casting: ore into the vessel (use with the ore in the other hand), on the firepit, then poured into molds
  int PX = 14, PZ = 4;
  bool firepit() {
    const BlockId FIRE = reg().blockId("campfire");
    if (w().getBlock(PX, Y + 1, PZ) != FIRE) {
      sticks(3);
      for (int i = 0; i < 3; i++) { ItemStack s = take("stick", 1); dropItem(*lv, 0, s, vec3(PX + 0.5f, Y + 1.2f, PZ + 0.5f), false); }
      tick(0.5); spark(PX, Y, PZ);
    } else if (w().getMeta(PX, Y + 1, PZ) & 8) spark(PX, Y + 1, PZ);
    for (int i = 0; i < 4 && count("charcoal"); i++) { hold("charcoal"); aim(PX, Y + 1, PZ); use(); }
    return w().getBlock(PX, Y + 1, PZ) == FIRE && !(w().getMeta(PX, Y + 1, PZ) & 8);
  }
  bool fill(const std::vector<std::pair<std::string, int>>& load) {
    if (!hold("ceramic_vessel")) { why = "no fired vessel"; return false; }
    for (auto& [id, n] : load) {
      ItemStack o = take(id, n); if (o.n < n) { why = "only " + S(o.n) + " " + id; give(o); return false; }
      P->offhand[0] = o; aimAir(); use(); give(P->offhand[0]); P->offhand[0] = ItemStack();
    }
    return true;
  }
  bool melt() {   // the vessel on the fire till it's molten, then back in hand
    if (!firepit()) { why = "the firepit won't light"; return false; }
    hold("ceramic_vessel"); const bool wasMolten = vesselMolten(P->held()); const int u0 = vesselUnits(P->held());
    aim(PX, Y + 1, PZ); use();
    if (count("ceramic_vessel")) {
      const int u1 = vesselUnits(P->inv[slotOf("ceramic_vessel")]);
      std::string filled; for (auto& b : P->inv) if (!b.empty() && moldFilled(b)) filled += " " + b.id;
      why = std::string("use on the firepit with the ") + (wasMolten ? "still-molten" : "solid") + " vessel didn't set it on the fire" + (u1 < u0 ? " (it poured " + S(u0 - u1) + " units into a mold from the bag instead:" + filled + ")" : "");
      return false; }
    for (int t = 0; t < 60; t++) {
      tick(5); firepit();
      json* e = w().blockEntity(PX, Y + 1, PZ);
      bool m = false; for (auto& it : (*e)["items"]) if (!it.is_null() && vesselMolten(ItemStack::fromJson(it))) m = true;
      if (m) break;
    }
    holdNothing(); aim(PX, Y + 1, PZ); use();
    const int v = slotOf("ceramic_vessel");
    if (v < 0 || !vesselMolten(P->inv[v])) { why = "the vessel didn't melt (" + S(v < 0 ? 0 : (int)itemHeat(P->inv[v])) + " C)"; return false; }
    return true;
  }
  bool pour(const std::string& mold, int times) {   // the molten vessel in hand, the mold in the other
    for (int i = 0; i < times; i++) {
      hold("ceramic_vessel");
      const int ms = slotOf([&](const ItemStack& s) { return s.id == mold && !moldFilled(s); });
      if (ms < 0) { why = "no empty " + mold; return false; }
      ItemStack m = P->inv[ms]; m.n = 1; if (--P->inv[ms].n <= 0) P->inv[ms] = ItemStack();
      P->offhand[0] = m; aimAir(); use();
      give(P->offhand[0]); P->offhand[0] = ItemStack();
      if (count(mold) && slotOf([&](const ItemStack& s) { return s.id == mold && moldFilled(s); }) < 0) { why = "it didn't pour"; return false; }
    }
    return true;
  }
  int breakMolds() {   // cool, then use each filled mold
    int n = 0;
    for (int t = 0; t < 120 && slotOf([](const ItemStack& s) { return moldFilled(s); }) >= 0; t++) {
      holdNothing(); tick(5);
      while (hold([](const ItemStack& s) { return moldFilled(s); })) { const int before = (int)P->held().n; aimAir(); use(); if (P->held().n == before && moldFilled(P->held())) break; n++; }
    }
    return n;
  }
  bool castIngots(const std::string& metalName, const std::vector<std::pair<std::string, int>>& load, int ingots) {
    cool();
    if (!fill(load)) return false;
    int opened = 0;
    for (int done = 0; done < ingots; ) {   // in fours: a bag can't hold every hot mold and ingot at once (hot things don't stack)
      if (!melt()) return false;
      if (!done) { const int v = slotOf("ceramic_vessel"); printf("    (vessel: %d units of %s)\n", vesselUnits(P->inv[v]), compName(vesselComp(P->inv[v])).c_str()); }
      const int k = std::min(4, ingots - done);
      if (!pour("ceramic_ingot_mold", k)) return false;
      done += k; opened += breakMolds(); cool();
      if (getenv("DQ_DEBUG")) printf("    (batch: %d poured, %d ingots of %s now)\n", done, ingotsOf(namedAlloy(compOfStack(ItemStack(metalName + "_ingot", 1)))), metalName.c_str());
    }
    if (getenv("DQ_DEBUG")) { printf("    (opened %d; bag:", opened); for (auto& b : P->inv) if (!b.empty()) printf(" %s x%d %s", b.id.c_str(), b.n, b.tag.is_null() ? "" : b.tag.dump().c_str()); printf(")\n"); }
    (void)metalName; return true;
  }
  void junk() {   // done with these: thrown away (the bag has 36 slots, and hot things don't stack)
    for (auto& b : P->inv) if (!b.empty() && (b.id == "clay_ball" || b.id == "straw" || b.id == "flint" || b.id == "flint_saw" || b.id == "stone_knife" || b.id == "log" || b.id == "pebble" || b.id.find("_ore_piece") != std::string::npos || b.id.find("_mold") != std::string::npos || b.id.find("_head") != std::string::npos)) b = ItemStack();
    tidy();
  }
  int ingotsOf(const std::string& alloy) { int n = 0; for (auto& s : P->inv) if (!s.empty() && s.id.find("ingot") != std::string::npos && s.id.find("mold") == std::string::npos && namedAlloy(compOfStack(s)) == alloy) n += s.n; return n; }
};

static void immersivePlay() {
  printf("\n[IMMERSIVE]\n");
  Imm g;
  World& w = g.w();
  const int Y1 = g.Y + 1;
  CHECK(immersive(*g.lv), "an immersive world");
  // ---- the stone age ----
  g.holdNothing();
  CHECK(!std::isfinite(g.pc->breakTime(reg().blockId("log"), nullptr)), "logs can't be punched");
  auto got = g.mine("loose_stones", 4);
  CHECK(got["pebble"] == 4, "loose rocks by hand: " + Play::list(got));
  got = g.mine("twigs", 6);
  CHECK(got["stick"] == 6, "twigs by hand: " + Play::list(got));
  CHECK(g.knap("pebble", "stone_axe_head"), "knap a stone axe head " + g.why);
  CHECK(g.craftAt({{0, "stone_axe_head"}, {2, "stick"}}, 2, "stone_axe"), "stone axe head + stick: a stone axe " + g.why);
  g.hold("stone_axe"); got = g.mine("log", 4);
  CHECK(got["log"] == 4, "the stone axe fells logs: " + Play::list(got) + " " + g.lastMine);
  g.holdNothing(); got = g.mine("leaves", 40);
  CHECK(got["stick"] >= 3, "leaves give sticks now and then: " + Play::list(got));
  g.flint(2);
  CHECK(g.count("flint") >= 2 && g.knap("flint", "flint_saw_head") && g.craft("flint_saw"), "knap a flint saw head, a saw " + g.why);
  CHECK(g.craft("lumber") && g.count("lumber") == 8 && g.count("flint_saw") == 1, "log + saw: 8 lumber, the saw kept (" + S(g.count("lumber")) + ") " + g.why);
  CHECK(g.craft("planks", 2) && g.count("planks") >= 2, "lumber into planks (" + S(g.count("planks")) + ") " + g.why);
  g.craft("lumber"); g.craft("planks", 2);
  CHECK(g.craft("crafting_table") && g.place("crafting_table", 6, Y1, 0), "a crafting table " + g.why);
  g.table = true;
  CHECK(g.craft("firestarter"), "a firestarter from two sticks " + g.why);
  CHECK(g.stoneTool("stone_knife", "stone_knife_head"), "a stone knife " + g.why);
  { const BlockId GR = reg().blockId("grass"), TG = reg().blockId("tall_grass");
    g.hold("stone_knife"); int straw = 0;
    for (int x = 22; x < 28; x++) for (int z = 20; z < 26 && g.count("straw") < 10; z++) { w.setBlock(x, g.Y, z, GR); w.setBlock(x, Y1, z, TG); g.aim(x, Y1, z); g.pc->breakBlock(x, Y1, z); straw += g.collect()["straw"]; }
    CHECK(g.count("straw") >= 8, "straw cut with the knife (" + S(straw) + ")"); }
  // ---- clay: a vessel and molds, fired in a pit kiln ----
  { g.holdNothing(); int k = 0; while (g.count("clay_ball") < 60 && k++ < 80) g.mine("clay"); }
  CHECK(g.count("clay_ball") >= 60, "clay by hand (" + S(g.count("clay_ball")) + " clay) " + g.lastMine);
  bool shaped = g.knap("clay_ball", "unfired_vessel");
  for (int i = 0; i < 8; i++) shaped = g.knap("clay_ball", "unfired_ingot_mold") && shaped;
  shaped = g.knap("clay_ball", "unfired_pickaxe_mold") && shaped;
  shaped = g.knap("clay_ball", "unfired_chisel_mold") && shaped;
  CHECK(shaped, "shape a vessel, 8 ingot molds, a pickaxe mold and a chisel mold " + g.why);
  const int KX = 4, KZ = 10;
  for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) if (dx || dz) w.setBlock(KX + dx, Y1, KZ + dz, reg().blockId("stone"));
  int q = 0;
  for (const char* u : {"unfired_vessel", "unfired_ingot_mold", "unfired_pickaxe_mold", "unfired_chisel_mold"}) {
    g.hold(u); g.aim(KX, q == 0 ? g.Y : Y1, KZ, ivec3(0, 1, 0), vec3(q & 1 ? 0.75f : 0.25f, 1, q & 2 ? 0.75f : 0.25f));
    if (q > 0) g.aim(KX, Y1, KZ, ivec3(0, 1, 0), vec3(q & 1 ? 0.75f : 0.25f, 1, q & 2 ? 0.75f : 0.25f));
    while (g.count(u)) { const int b = g.count(u); g.use(true); if (g.count(u) == b) break; }
    q++;
  }
  CHECK(!g.count("unfired_ingot_mold") && w.getBlock(KX, Y1, KZ) == reg().blockId("pit_kiln"), "the pottery set down in a pit (sneak + use)");
  if (getenv("DQ_DEBUG")) printf("    kiln: %s\n", w.blockEntity(KX, Y1, KZ) ? w.blockEntity(KX, Y1, KZ)->dump().c_str() : "-");
  g.logs(8);
  for (int i = 0; i < 8; i++) { g.hold("straw"); g.aim(KX, Y1, KZ); g.use(); }
  for (int i = 0; i < 8; i++) { g.hold("log"); g.aim(KX, Y1, KZ); g.use(); }
  g.spark(KX, Y1, KZ);
  json* ke = w.blockEntity(KX, Y1, KZ);
  CHECK(ke && ke->value("lit", false), "eight straw, eight logs, lit with the firestarter");
  g.tick(gameHourSecs(*g.lv) * KILN_HOURS + 10);
  if (getenv("DQ_DEBUG")) printf("    kiln: %s\n", w.blockEntity(KX, Y1, KZ) ? w.blockEntity(KX, Y1, KZ)->dump().c_str() : "-");
  g.holdNothing();
  for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) { g.holdNothing(); g.aim(KX, Y1, KZ, ivec3(0, 1, 0), vec3(i & 1 ? 0.75f : 0.25f, 1, i & 2 ? 0.75f : 0.25f)); g.use(); }
  CHECK(g.count("ceramic_vessel") == 1 && g.count("ceramic_ingot_mold") >= 8 && g.count("ceramic_pickaxe_mold") == 1 && g.count("ceramic_chisel_mold") == 1,
        "fired: vessel " + S(g.count("ceramic_vessel")) + ", ingot molds " + S(g.count("ceramic_ingot_mold")) + ", pickaxe mold " + S(g.count("ceramic_pickaxe_mold")) + ", chisel mold " + S(g.count("ceramic_chisel_mold")));
  g.cool();
  // ---- charcoal: a pit of log piles, covered with earth ----
  const BlockId DIRT = reg().blockId("dirt");
  const int CX0 = -11, CZ0 = -11, CN = 6;
  for (int x = CX0 - 1; x <= CX0 + CN; x++) for (int z = CZ0 - 1; z <= CZ0 + CN; z++) { w.setBlock(x, Y1, z, DIRT); w.setBlock(x, Y1 + 1, z, DIRT); }
  int piles = 0;
  for (int x = CX0; x < CX0 + CN; x++) for (int z = CZ0; z < CZ0 + CN; z++) {
    w.setBlock(x, Y1, z, AIR); if (!(x == CX0 && z == CZ0)) {} else w.setBlock(x, Y1 + 1, z, AIR);
    if (g.logs(16) < 16) break;
    ItemStack l = g.take("log", 16);
    if (pilePlace(w, x, Y1, z, l)) { while (!l.empty() && pileAdd(w, x, Y1, z, l)) {} piles++; }
    g.give(l);
  }
  CHECK(piles == CN * CN, "36 log piles of 16 logs (" + S(piles) + "), chopped with stone axes");
  g.spark(CX0, Y1, CZ0);
  w.setBlock(CX0, Y1 + 1, CZ0, DIRT);
  json* pe = w.blockEntity(CX0 + 2, Y1, CZ0 + 2);
  CHECK(pe && pe->value("lit", false), "the firestarter on the one open pile lights the pit, then it's covered");
  g.tick(gameHourSecs(*g.lv) * CHARCOAL_HOURS + 10);
  for (int x = CX0; x < CX0 + CN; x++) for (int z = CZ0; z < CZ0 + CN; z++) { w.setBlock(x, Y1 + 1, z, AIR); for (int k = 0; k < 12; k++) { g.holdNothing(); g.aim(x, Y1, z); g.use(); } }
  g.collect();
  CHECK(g.count("charcoal") >= 100, "charcoal from the pit: " + S(g.count("charcoal")));
  // ---- copper: ore pieces with the crude pickaxe, melted in the vessel, cast ----
  CHECK(g.stoneTool("crude_pickaxe", "crude_pickaxe_head"), "a crude pickaxe (knapped head + stick) " + g.why);
  g.pick(); got = g.mine("copper_ore", 1);
  CHECK(got["copper_ore_piece"] >= 1, "the crude pickaxe breaks copper ore into pieces: " + Play::list(got) + " " + g.lastMine);
  for (const char* o : {"tin", "zinc", "bismuth"}) { g.pick(); got = g.mine(std::string(o) + "_ore", 1); CHECK(got[std::string(o) + "_ore_piece"] >= 1, std::string("  ...and ") + o + " ore: " + Play::list(got) + " " + g.lastMine); }
  g.ore("copper_ore", "copper_ore_piece", 4);
  CHECK(g.fill({{"copper_ore_piece", 4}}) && g.melt(), "4 copper pieces in the vessel, molten on a charcoal firepit " + g.why);
  CHECK(g.pour("ceramic_pickaxe_mold", 1) && g.breakMolds() == 1, "poured into the pickaxe mold, cooled and broken open " + g.why);
  const int ch = g.slotOf("metal_pickaxe_head");
  g.sticks(2);
  CHECK(ch >= 0 && namedAlloy(compOfStack(g.P->inv[ch])) == "copper" && g.craftAt({{0, "metal_pickaxe_head"}, {2, "stick"}}, 2, "mixed_pickaxe"), "a copper pickaxe head + stick: a copper pickaxe " + g.why);
  g.sticks(4);
  // copper ingots: 2 for a sheet (the bloomery), 8 for the copper anvil, 1 chisel
  g.ore("copper_ore", "copper_ore_piece", 48);
  bool cast = g.castIngots("copper", {{"copper_ore_piece", 16}, {"copper_ore_piece", 16}, {"copper_ore_piece", 12}}, 11);
  CHECK(cast && g.ingotsOf("copper") >= 10, "11 copper ingots cast (" + S(g.ingotsOf("copper")) + ") " + g.why);
  // the chisel: copper in the chisel mold
  g.ore("copper_ore", "copper_ore_piece", 4);
  g.sticks(2); bool chis = g.fill({{"copper_ore_piece", 4}}) && g.melt() && g.pour("ceramic_chisel_mold", 1) && g.breakMolds() >= 1 && g.craftAt({{0, "metal_chisel_head"}, {2, "stick"}}, 2, "mixed_chisel");
  CHECK(chis, "a copper chisel head from the mold + stick: a chisel " + g.why);
  if (chis) {
    w.setBlock(20, Y1, 16, reg().blockId("stone")); g.hold("mixed_chisel");
    g.aim(20, Y1, 16, ivec3(0, 1, 0), vec3(0.25f, 1.f, 0.25f)); g.use();
    CHECK(w.getCarve(20, Y1, 16) != 0, "the chisel carves a corner off a stone block");
  }
  // ---- stone: cobblestone, the furnace (for stone, smooth stone, bricks), the quern for flux ----
  int cobble = 0; for (int i = 0; i < 40 && g.count("cobblestone") < 32; i++) { g.pick(); cobble += g.mine("stone")["cobblestone"]; }
  CHECK(g.count("cobblestone") >= 32, "cobblestone from stone (" + S(g.count("cobblestone")) + ") " + g.lastMine);
  CHECK(g.craft("furnace") && g.place("furnace", 8, Y1, 0), "a furnace " + g.why);
  int st = g.smelt(8, Y1, 0, "furnace", "cobblestone", 24, "charcoal", 6, 600);
  CHECK(st == 24, "the furnace: cobblestone to stone (" + S(st) + ") " + g.why);
  int sm = g.smelt(8, Y1, 0, "furnace", "stone", 8, "charcoal", 3, 300);
  g.craft("stone_bricks", 4);   // (the rest of the stone, bricks for the bloomery now: one stack, not two)
  CHECK(sm == 8, "  ...stone to smooth stone (" + S(sm) + ") " + g.why);
  CHECK(g.craft("handstone") && g.craft("quern") && g.place("quern", 20, Y1, 10), "a handstone and a quern " + g.why);
  { g.pick(); auto lm = g.mine("limestone", 10); int fl = 0;
    for (int i = 0; i < 10 && g.count("limestone"); i++) { g.hold("limestone"); g.aim(20, Y1, 10); g.use(); for (int k = 0; k < 6; k++) g.use(false, true); fl += g.collect()["flux"]; }
    CHECK(g.count("flux") >= 16, "limestone (" + Play::list(lm) + ") ground in the quern: flux " + S(g.count("flux"))); }
  // ---- the forge and the stone anvil ----
  for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) if (dx || dz) w.setBlock(g.FX + dx, Y1, g.FZ + dz, reg().blockId("stone"));
  g.hold("charcoal"); g.aim(g.FX, g.Y, g.FZ); g.use(true);
  CHECK(w.getBlock(g.FX, Y1, g.FZ) == reg().blockId("forge"), "sneak + use charcoal into a stone-ringed pit: a forge");
  CHECK(g.forgeFuel(), "fuelled and lit with the firestarter");
  CHECK(g.hammer() && (g.aim(2, g.Y, 14), g.use(), w.getBlock(2, g.Y, 14) == reg().blockId("stone_anvil")), "a stone hammer on raw stone: a stone anvil " + g.why);
  auto isIngot = [](const std::string& alloy, float hot) { return [alloy, hot](const ItemStack& s) { return s.id.find("_ingot") != std::string::npos && s.id.find("double") == std::string::npos && namedAlloy(compOfStack(s)) == alloy && itemHeat(s) >= hot; }; };
  auto isPiece = [](const std::string& id, const std::string& alloy, float hot) { return [id, alloy, hot](const ItemStack& s) { return s.id == id && (alloy.empty() || namedAlloy(compOfStack(s)) == alloy) && itemHeat(s) >= hot; }; };
  const float cuMelt = metalDef("copper")->melt;
  g.cool(); int dbl = 0;
  for (int i = 0; i < 5; i++) if (g.heat(isIngot("copper", 0), 2, cuMelt * 0.8f) && g.smith(2, g.Y, 14, "double_ingot", isIngot("copper", cuMelt * 0.8f))) dbl++; else printf("    weld %d: %s\n", i, g.why.c_str());
  CHECK(dbl == 5, "copper ingots welded in pairs on the stone anvil (with flux): " + S(dbl) + " double ingots " + g.why);
  bool sheet = g.heat(isPiece("metal_double_ingot", "copper", 0), 1, cuMelt * 0.6f) && g.smith(2, g.Y, 14, "sheet", isPiece("metal_double_ingot", "copper", cuMelt * 0.6f));
  CHECK(sheet && g.count("metal_sheet") == 1, "a copper sheet " + g.why);
  g.cool(); bool canv = g.heat(isPiece("metal_double_ingot", "copper", 0), 4, cuMelt * 0.6f) && g.smith(2, g.Y, 14, "anvil", isPiece("metal_double_ingot", "copper", cuMelt * 0.6f));
  CHECK(canv && g.place("copper_anvil", 4, Y1, 14), "four copper double ingots: a copper anvil " + g.why);
  if (!w.getBlock(4, Y1, 14)) { w.setBlock(4, Y1, 14, reg().blockId("copper_anvil")); printf("    GRANTED the copper anvil block (after the failure above)\n"); g.granted++; }
  // ---- bronze: copper and tin, 9 to 1 ----
  g.ore("copper_ore", "copper_ore_piece", 36); g.ore("tin_ore", "tin_ore_piece", 4);
  cast = g.castIngots("bronze", {{"copper_ore_piece", 16}, {"copper_ore_piece", 16}, {"copper_ore_piece", 4}, {"tin_ore_piece", 4}}, 9);
  CHECK(cast && g.ingotsOf("bronze") == 9, "36 copper + 4 tin pieces melt to bronze: 9 bronze ingots (" + S(g.ingotsOf("bronze")) + ") " + g.why);
  if (g.count("ceramic_pickaxe_mold")) {
    g.ore("copper_ore", "copper_ore_piece", 9); g.ore("tin_ore", "tin_ore_piece", 1);
    g.sticks(2); bool bp = g.fill({{"copper_ore_piece", 9}, {"tin_ore_piece", 1}}) && g.melt() && g.pour("ceramic_pickaxe_mold", 1) && g.breakMolds() >= 1 && g.craftAt({{0, "metal_pickaxe_head"}, {2, "stick"}}, 2, "mixed_pickaxe");
    const int bs = g.slotOf([](const ItemStack& s) { return s.id == "mixed_pickaxe" && stackName(s) == "Bronze Pickaxe"; });
    CHECK(bp && bs >= 0, "a bronze pickaxe cast in the pickaxe mold " + g.why);
  } else CHECK(false, "the pickaxe mold broke in the copper casting: a second pickaxe mold is needed for bronze (not knapped)");
  for (const char* o : {"gold_ore", "silver_ore"}) { g.pick(); got = g.mine(o, 1); CHECK(!got.empty(), std::string("the best pickaxe (") + g.P->held().id + ") mines " + o + ": " + Play::list(got) + " " + g.lastMine); }
  const float brMelt = blendMetal({{"copper", 0.9f}, {"tin", 0.1f}}).melt;
  g.junk(); g.cool(); int bd = 0;
  for (int i = 0; i < 4; i++) if (g.heat(isIngot("bronze", 0), 2, brMelt * 0.8f) && g.smith(4, Y1, 14, "double_ingot", isIngot("bronze", brMelt * 0.8f))) bd++; else printf("    weld %d: %s\n", i, g.why.c_str());
  g.cool(); bool banv = bd == 4 && g.heat(isPiece("metal_double_ingot", "bronze", 0), 4, brMelt * 0.6f) && g.smith(4, Y1, 14, "anvil", isPiece("metal_double_ingot", "bronze", brMelt * 0.6f));
  CHECK(banv && g.place("bronze_anvil", 6, Y1, 14), "bronze double ingots on the copper anvil: a bronze anvil (" + S(bd) + " welds) " + g.why);
  if (!w.getBlock(6, Y1, 14)) { w.setBlock(6, Y1, 14, reg().blockId("bronze_anvil")); printf("    GRANTED the bronze anvil block (after the failure above)\n"); g.granted++; }
  // ---- iron: the bloomery ----
  g.junk();
  CHECK(g.count("stone_bricks") >= 10 && g.craft("bloomery") && g.place("bloomery", 20, Y1, 4), "stone bricks and a copper sheet: a bloomery " + g.why);
  if (w.getBlock(20, Y1, 4) != reg().blockId("bloomery")) { w.setBlock(20, Y1, 4, reg().blockId("bloomery")); printf("    GRANTED the bloomery block\n"); g.granted++; }
  for (int k = 1; k <= 3; k++) if (!g.place("stone_bricks", 20, Y1 + k, 4)) w.setBlock(20, Y1 + k, 4, reg().blockId("stone_bricks")), printf("    GRANTED a chimney brick\n");
  CHECK(bloomeryCapacity(w, 20, Y1, 4) == 24, "a chimney three high: room for 24");
  int blooms = 0;
  for (int run = 0; run < 3; run++) {
    g.ore("iron_ore", "iron_ore_piece", 24);
    if (g.count("charcoal") < 24) { printf("    (charcoal short: %d)\n", g.count("charcoal")); }
    for (int i = 0; i < 24; i++) { g.hold("iron_ore_piece"); g.aim(20, Y1, 4); g.use(); g.hold("charcoal"); g.use(); }
    g.spark(20, Y1, 4);
    { json* be = w.blockEntity(20, Y1, 4); printf("    (bloomery run %d: %s)\n", run + 1, be ? be->dump().substr(0, 200).c_str() : "-"); }
    g.tick(gameHourSecs(*g.lv) * BLOOMERY_HOURS + 10);
    for (int i = 0; i < 8; i++) { g.holdNothing(); g.aim(20, Y1, 4); g.use(); }
    blooms = g.count("raw_bloom");
  }
  CHECK(blooms >= 14, "three bloomery runs of 24 iron ore pieces: " + S(blooms) + " raw blooms");
  const float feMelt = metalDef("iron")->melt;
  g.cool(); int wrought = 0;
  for (int i = 0; i < blooms; i++) {
    if (!(g.heat(isPiece("raw_bloom", "", 0), 1, feMelt * 0.6f) && g.smith(6, Y1, 14, "bloom", isPiece("raw_bloom", "", feMelt * 0.6f)))) { printf("    refine: %s\n", g.why.c_str()); break; }
    if (!(g.heat(isPiece("refined_bloom", "", 0), 1, feMelt * 0.6f) && g.smith(6, Y1, 14, "wrought", isPiece("refined_bloom", "", feMelt * 0.6f)))) { printf("    wrought: %s\n", g.why.c_str()); break; }
    wrought++; if (wrought % 3 == 0) g.cool();
  }
  CHECK(wrought >= 14, "raw bloom -> refined bloom -> wrought iron ingot on the bronze anvil: " + S(wrought) + " " + g.why);
  g.cool(); int idb = 0;
  for (int i = 0; i < 4; i++) if (g.heat(isIngot("iron", 0), 2, feMelt * 0.8f) && g.smith(6, Y1, 14, "double_ingot", isIngot("iron", feMelt * 0.8f))) idb++; else printf("    weld %d: %s\n", i, g.why.c_str());
  g.cool(); bool ianv = idb == 4 && g.heat(isPiece("metal_double_ingot", "iron", 0), 4, feMelt * 0.6f) && g.smith(6, Y1, 14, "anvil", isPiece("metal_double_ingot", "iron", feMelt * 0.6f));
  CHECK(ianv && g.place("anvil", 8, Y1, 14), "wrought iron double ingots on the bronze anvil: an iron anvil (" + S(idb) + " welds) " + g.why);
  if (!w.getBlock(8, Y1, 14)) { w.setBlock(8, Y1, 14, reg().blockId("anvil")); printf("    GRANTED the iron anvil block (after the failure above)\n"); g.granted++; }
  // ---- steel: pig iron from the blast furnace, worked to high-carbon steel, then steel ----
  g.cool(); g.pick(); { g.aim(8, Y1, 0); if (std::isfinite(g.pc->breakTime(w.getBlock(8, Y1, 0), &g.pc->target))) g.pc->breakBlock(8, Y1, 0); auto fu = g.collect(); printf("    (the furnace picked up again: %s)\n", Play::list(fu).c_str()); }
  bool bf = g.craft("blast_furnace") && g.place("blast_furnace", 10, Y1, 0);
  CHECK(bf, "a blast furnace (5 wrought iron, a furnace, 3 smooth stone) " + g.why);
  CHECK(g.craft("steel_blend"), "a steel blend (iron + 2 charcoal) " + g.why);
  int pig = g.count("steel_blend") ? g.smelt(10, Y1, 0, "blast", "steel_blend", 1, "charcoal", 2) : 0;
  CHECK(pig == 1 && g.count("pig_iron") == 1, "the blast furnace makes pig iron (" + S(pig) + ") " + g.why);
  bool steel = g.count("pig_iron") && g.heat(isPiece("pig_iron", "", 0), 1, feMelt * 0.6f) && g.smith(8, Y1, 14, "steel", isPiece("pig_iron", "", feMelt * 0.6f));
  CHECK(steel && g.count("high_carbon_steel"), "pig iron worked into high-carbon steel " + g.why);
  const float stMelt = metalDef("steel") ? metalDef("steel")->melt : feMelt;
  bool steel2 = steel && g.heat(isPiece("high_carbon_steel", "", 0), 1, stMelt * 0.6f) && g.smith(8, Y1, 14, "hcsteel", isPiece("high_carbon_steel", "", stMelt * 0.6f));
  CHECK(steel2 && g.count("steel_ingot"), "high-carbon steel worked into a steel ingot on the iron anvil " + g.why);
  printf("  (granted: %d)\n", g.granted);
}

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-progression").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  bool doC = true, doI = true;
  for (int i = 1; i < argc; i++) { if (std::string(argv[i]) == "--classic") doI = false; if (std::string(argv[i]) == "--immersive") doC = false; }
  if (doC) classic();
  if (doI) immersivePlay();
  printf("\n%d passed, %d failed\n", passes, fails);
  return fails;
}
