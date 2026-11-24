// tools/test_content.cpp — headless checks of the content features (sim only): paper maps (records, exploring,
// markers, pins, crafting, saving), fishing, campfires / rope / the grappling hook / the sleeping bag, waystones
// and mounts, elytra, the stable, finding places, the watering can and rice.
//   test_content [--userdir DIR]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/combat_rpg.h"
#include "sim/maps.h"
#include "sim/fishing.h"
#include "sim/skills.h"
#include "sim/camping.h"
#include "sim/travel.h"
#include "sim/elytra.h"
#include "sim/stable.h"
#include "sim/places.h"
#include "sim/farming.h"
#include "sim/crafting.h"
#include "sim/stations.h"
#include "core/tool_parts.h"
#include "sim/creature.h"
#include "sim/treasure_maps.h"
#include "sim/inv_ops.h"
#include "sim/creature_entities.h"
#include "sim/creature_rig.h"
#include "sim/trough.h"
#include "sim/station_work.h"
#include "sim/rpg.h"
#include "sim/worktable.h"
#include "sim/combat.h"
#include "sim/balance.h"
#include "sim/attack_moves.h"
#include "sim/shield_shape.h"
#include "sim/spell_items.h"
#include "core/metals.h"
#include "sim/knapping.h"
#include "sim/item_uses.h"
#include "sim/magic_combat.h"
#include "sim/inventory.h"
#include "sim/villagers.h"
#include <thread>
#include <cstdio>
#include <regex>
#include <set>
using namespace dq;
static const char* const ARMOR_PIECE[4] = {"helmet", "chestplate", "leggings", "boots"};   // (no compound literals: MSVC)

// ---- every item has a use: what an item can be put to (placed, worn, eaten, an ingredient, a station's input, a
// creature's food, melted down, or named by the sim code that handles it). Selling or a transmute value is not a use.
// Returns the items with none, each with what was looked at. allowed: world-only things kept on purpose (with why).
static const std::map<std::string, std::string>& deadEndAllowed() {
  static const std::map<std::string, std::string> A = {
    {"summoning_wand", "creative only: calls up creatures for builders"},
    {"unfinished_*", "the anvil's armour pieces (sim/anvil.cpp names them by piece), padded at the crafting table (sim/crafting.cpp)"},
  };
  return A;
}
static std::vector<std::string> deadEndItems() {
  const Registry& R = reg();
  std::set<std::string> used;
  auto addKey = [&](const std::string& k) {
    if (k.empty()) return;
    if (k[0] == '#') { auto it = R.tags.find(k); if (it != R.tags.end()) for (auto& x : it->second) used.insert(x); }
    else used.insert(k);
  };
  for (auto& r : R.recipes) { for (auto& [c, k] : r.key) addKey(k); for (auto& k : r.items) addKey(k); }
  for (auto& [in, out] : R.smelting) addKey(in);
  auto keysOf = [&](const json* j) { if (j && j->is_object()) for (auto& [k, v] : j->items()) addKey(k); };
  auto data = [](const char* f) { return readJson(dataDir() / f).value_or(json()); };
  const json quern = data("quern.json"), mach = data("machines.json"), trom = data("trommel.json"), barrel = data("barrel.json"),
             magic = data("magic.json"), tm = data("tool_materials.json");
  if (quern.contains("grind")) keysOf(&quern["grind"]);
  for (const char* k : {"milling", "sawing", "crushing", "compost"}) if (mach.contains(k)) keysOf(&mach[k]);
  if (trom.contains("sift")) keysOf(&trom["sift"]);
  if (barrel.contains("recipes")) for (auto& r : barrel["recipes"]) if (r.contains("item") && r["item"].is_string()) addKey(r["item"]);
  if (magic.contains("brewIngs")) for (auto& k : magic["brewIngs"]) if (k.is_string()) addKey(k);
  if (magic.contains("enchItems")) keysOf(&magic["enchItems"]);
  if (tm.contains("materials")) for (auto& [k, m] : tm["materials"].items()) if (m.contains("items")) for (auto& i : m["items"]) if (i.is_string()) addKey(i);
  if (R.mobs.is_object()) for (auto& [k, m] : R.mobs.items()) {   // a creature's food, its taming food
    if (m.contains("food") && m["food"].is_array()) for (auto& f : m["food"]) if (f.is_string()) addKey(f);
    if (m.contains("tame") && m["tame"].is_object() && m["tame"].contains("foods")) for (auto& f : m["tame"]["foods"]) if (f.is_string()) addKey(f);
  }
  // the code: an item named by its key ("key") in the sim, the land's places or a station's screen is handled there (a
  // use, a key's gate, the anvil's flux)
  std::set<std::string> named;
#ifdef DQ_SRC_DIR
  {
    static const std::regex lit("\"([a-z0-9_]+)\"");
    for (const char* dir : {"sim", "gen", "ui"}) for (auto& e : fs::recursive_directory_iterator(fs::path(DQ_SRC_DIR) / dir)) {
      const std::string ext = e.path().extension().string();
      if (ext != ".cpp" && ext != ".h" && ext != ".inc") continue;
      auto txt = readFile(e.path()); if (!txt) continue;
      for (std::sregex_iterator it(txt->begin(), txt->end(), lit), end; it != end; ++it) named.insert((*it)[1].str());
    }
  }
#endif
  static const char* USE[] = {"tool", "armor", "food", "drink", "potion", "splash", "fuel", "spawnEgg", "forgeHead", "metalHead", "metalPiece", "tome",
    "cape", "dye", "firework", "plants", "hat", "mobHead", "charm", "cart", "boat", "staff", "hook", "bow", "disc", "bucket", "backpack", "compass",
    "igniter", "mapItem", "snowball", "bonemeal", "bottle", "weWand", "heartCrystal", "clock", "spyglass", "shears", "summon", "pocketKey", "fishing",
    "xpBottle", "gustPuff", "grapple", "campKit", "pearl", "light", "holy", "school", "lore", "forged", "pottery", "mold", "template", "wolfArmor",
    "horseArmor", "knife", "arrow", "javelin", "hammer", "scythe", "saw", "enderEye", "chorus", "wateringCan", "paintbrush", "summonWand", "cannonAmmo",
    "charges", "calendar", "label", "propick", "tongs", "shield", "mixedTool", "mixedArmor", "warHorn"};
  std::vector<std::string> out;
  for (auto& d : R.items) {
    if (d.has("hidden") || d.has("spawnEgg") || d.key.find("debug") != std::string::npos) continue;
    if (!d.block.empty() || R.blockIds.count(d.key)) continue;
    bool ok = used.count(d.key) || named.count(d.key) || !metalOfItem(d.key).empty() || !knapMaterialOf(d.key).empty() || deadEndAllowed().count(d.key);
    for (auto& [k, why] : deadEndAllowed()) if (!ok && k.back() == '*' && d.key.rfind(k.substr(0, k.size() - 1), 0) == 0) ok = true;
    for (const char* f : USE) if (!ok && d.has(f)) ok = true;
    if (!ok) out.push_back(d.key);
  }
  std::sort(out.begin(), out.end());
  return out;
}
namespace dq { bool deflectFireball(Level& lv, Entity& by, vec3 eye, vec3 look, float reach); }   // sim/creature_entities.cpp

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }
struct RecFx : LevelFx {
  std::vector<std::string> msgs; int sounds = 0, parts = 0;
  void message(u32, const std::string& t, const std::string&) override { msgs.push_back(t); }
  void sound(const std::string&, vec3, float, float, float) override { sounds++; }
  void soundUi(u32, const std::string&) override { sounds++; }
  void material(const std::string&, const std::string&, vec3) override { sounds++; }
  void particles(const std::string&, vec3, int, const json&) override { parts++; }
  bool heard(const std::string& s) const { for (auto& m : msgs) if (m.find(s) != std::string::npos) return true; return false; }
};

namespace dq { bool setCreatureLook(Entity& e, const std::string& look); }
static Level* L; static Player* P; static EventBus bus; static RecFx fx;
static BlockId B(const char* k) { return reg().blockId(k); }
static void tick(double secs) { for (double t = 0; t < secs; t += TICK_DT) L->tick(TICK_DT); }
static bool use(const std::string& item, ivec3 hit = ivec3(0), int face = -1) {   // false = the click was used up
  return bus.emit("itemUse", json::array({P->name, item, hit.x, hit.y, hit.z, face}));
}
static int countEntities(const std::string& type, const std::string& stackId = "") {
  int n = 0;
  for (auto& e : L->entities()) if (!e->removed && e->type == type && (stackId.empty() || e->data.value("stack", json::object()).value("id", "") == stackId)) n++;
  return n;
}

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-content").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(2);
  LevelInfo li; li.name = "ContentTest"; li.seedText = "content";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  L = lvp.get(); L->fx = &fx;
  World& w = L->world();
  const int base = dimBase(DIM_OVERWORLD), Y = base + 200;   // a test floor high in the sky
  { auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 8.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }
  double t0 = nowSeconds();
  while (nowSeconds() - t0 < 60) {
    w.stream({{P->pos, 2, false}}, 20);
    bool all = true;
    for (int cx = -1; cx <= 1; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
    if (all) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(w.chunk(DIM_OVERWORLD, 0, 0) != nullptr, "the test chunks load");
  BlockId stone = B("stone"), water = B("water");
  for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) w.setBlock(x, Y, z, stone);
  P->onGround = true;

  // ------------------------------------------------------------ maps: the record
  check(mapAlign(0, 0) == -64 && mapAlign(63, 0) == -64 && mapAlign(64, 0) == 64 && mapAlign(-65, 0) == -192 && mapAlign(300, 1) == 192, "maps line up on the 128 grid (mapAlign)");
  {
    PaperMap m; m.id = "abc"; m.dim = 0; m.x0 = -64; m.z0 = 64; m.scale = 2;
    for (int i = 0; i < MAP_PIX; i += 7) { m.ids[i] = (u16)(i % 5 == 0 ? stone : water); m.h[i] = (u8)(1 + i % 200); m.cl[i] = (u8)(i & 255); }
    json rec = encodeMap(m);
    auto back = decodeMap(rec);
    check(rec.value("k", "") == "map~abc" && rec["pal"].size() == 3, "a map record names its blocks by key (map~id, palette)");
    check(back && back->ids == m.ids && back->h == m.h && back->cl == m.cl && back->scale == 2 && back->z0 == 64, "a map record decodes to the same map");
    std::vector<u16> wide(5000); for (int i = 0; i < 5000; i++) wide[i] = (u16)(i / 9 * 3);
    std::vector<u16> out(5000); mapRleDecode(mapRleEncode(wide.data(), wide.size(), true), out.data(), out.size(), true);
    check(out == wide, "wide RLE round trip");
  }
  // using an empty map
  P->inv[0] = ItemStack("empty_map", 2); P->selected = 0;
  bool free1 = use("empty_map");
  int fm = -1; for (int i = 0; i < INV_MAIN; i++) if (isMapStack(P->inv[i])) fm = i;
  check(!free1 && P->inv[0].id == "empty_map" && P->inv[0].n == 1 && fm > 0, "an empty map becomes a filled map (the rest stay empty)");
  check(fx.heard("A new map"), "  ...with the original's words");
  std::string mid = fm > 0 ? stackMapId(P->inv[fm]) : "";
  PaperMap* m = mapStore(*L).get(mid);
  check(m && m->x0 == -64 && m->z0 == -64 && m->scale == 0 && m->dim == 0, "the new map covers the 128 x 128 around you");
  // exploring while it's held
  std::swap(P->inv[0], P->inv[fm]);
  tick(3);
  int i8 = (8 + 64) * MAP_N + (8 + 64);
  check(m && m->explored() > 0.05f, "holding it fills it in (" + std::to_string(m ? m->explored() : 0) + ")");
  check(m && m->ids[i8] == stone && m->h[i8] == Y - base + 1, "the pixel under you is the floor you stand on, with its height");
  auto mk = mapMarkers(*L, *m, P);
  bool you = false; for (auto& k : mk) if (k.cell == 0 && std::fabs(k.u - 72.5f) < 0.6f) you = true;
  check(you, "the map marks you");
  mapTogglePin(*L, *P, 0, 20, 20, 3);
  check(L->info.data["mapPins"].size() == 1, "a click sets a pin");
  mapTogglePin(*L, *P, 0, 21, 19, 3);
  check(L->info.data["mapPins"].empty(), "a click on it clears it");
  // crafting: copies and zooming out
  {
    std::vector<ItemStack> g(9);
    g[0] = P->inv[0]; g[1] = ItemStack("empty_map", 1); g[2] = ItemStack("empty_map", 1);
    ItemStack out; mapCraft(*L, g, 3, out, false);
    check(out.id == "filled_map" && out.n == 3 && stackMapId(out) == mid, "a map and empty maps make copies of the same map");
    for (int i = 0; i < 9; i++) g[i] = ItemStack("paper", 1);
    g[4] = P->inv[0];
    mapCraft(*L, g, 3, out, true);
    PaperMap* z = mapStore(*L).get(stackMapId(out));
    check(z && z->scale == 1 && z->explored() > 0 && z->x0 == -64, "a map ringed with paper zooms out, keeping what it knew");
  }
  L->save(false);
  check(fs::exists(L->folder / "maps" / (mid + ".json")), "maps are saved as records of their own");

  // ------------------------------------------------------------ fishing
  {
    int fish = 0, junk = 0, treasure = 0;
    for (int i = 0; i < 4000; i++) { auto c = fishingCatch(*L, 0, vec3(8, Y + 1, 8)); (c.kind == "fish" ? fish : c.kind == "junk" ? junk : treasure)++; }
    check(treasure > 80 && treasure < 260 && junk > 220 && junk < 440, "catches: ~4% treasure, ~8% junk (" + std::to_string(treasure) + ", " + std::to_string(junk) + ")");
    for (int x = 2; x < 7; x++) for (int z = 2; z < 7; z++) for (int y = Y - 2; y <= Y; y++) w.setBlock(x, y, z, water);
    P->inv[1] = ItemStack("fishing_rod", 1); P->selected = 1;
    use("fishing_rod");
    tick(0.5);
    Bobber* b = fishingOf(*L, *P);
    check(b != nullptr, "use the rod: you wind back and cast");
    if (b) {
      b->pos = vec3(4.5f, Y + 1.2f, 4.5f); b->vel = vec3(0, -2, 0);
      tick(0.5);
      check(b->state == Bobber::Float, "the float lands and bobs in the water");
      b->waitT = 0.05f; tick(0.15);
      check(b->biteT > 0, "a fish bites");
      int before = countEntities("item");
      fx.msgs.clear();
      use("fishing_rod");
      check(!fishingOf(*L, *P) && countEntities("item") == before + 1, "reel in quick: the catch flies out to you");
      check(fx.heard("You caught") || fx.heard("Treasure!") || fx.heard("Reeled in"), "  ...and you're told what it was");
      check(skillXp(*P, "fishing") > 0, "  ...and Fishing grows (" + std::to_string((int)skillXp(*P, "fishing")) + " xp)");
      check(P->held().d == 1, "the rod wears a little");
    }
  }
  // ------------------------------------------------------------ a sleeping bag and an oak coffin are beds (two cells, head bit 4)
  {
    check(reg().tShape[B("sleeping_bag")] == ShapeKind::Bed && reg().block(B("sleeping_bag")).shapeKind == ShapeKind::Bed
          && reg().tShape[B("oak_coffin")] == ShapeKind::Bed && reg().block(B("oak_coffin")).shapeKind == ShapeKind::Bed,
          "a sleeping bag and an oak coffin are beds (two cells, the head marked with bit 4)");
    int bags = 0; bool bagOk = false;
    for (const Recipe& r : reg().recipes) if (r.out == "sleeping_bag") {
      bags++;
      bagOk = r.n == 1 && r.shape.size() == 2 && r.shape[0] == "SSS" && r.shape[1] == "CCC"
              && r.key.count('S') && r.key.at('S') == "straw" && r.key.count('C') && r.key.at('C') == "rotten_cloth";
    }
    check(bags == 1 && bagOk, "a sleeping bag is 3 straw over 3 rotten cloth");
  }
  // ------------------------------------------------------------ campfires
  {
    BlockId cf = B("campfire");
    w.setBlock(12, Y + 1, 12, cf, 0);
    P->held() = ItemStack("beef", 3);
    use("beef", ivec3(12, Y + 1, 12), FACE_PY);
    json* e = w.blockEntity(12, Y + 1, 12);
    check(e && (*e)["items"][0].value("id", "") == "beef" && P->held().n == 2, "raw food goes on a lit fire");
    int cooked0 = countEntities("item", "cooked_beef");
    tick(24.5);
    check(countEntities("item", "cooked_beef") == cooked0 + 1 && (*w.blockEntity(12, Y + 1, 12))["items"][0].is_null(), "after 24 s it comes off cooked");
    P->held() = ItemStack("iron_shovel", 1);
    use("iron_shovel", ivec3(12, Y + 1, 12), FACE_PY);
    u8 meta = w.getMeta(12, Y + 1, 12);
    check((meta & CAMPFIRE_OUT) && reg().metaLight(cf, meta) == 0, "a shovel puts it out, and its light goes");
    P->held() = ItemStack("flint_and_steel", 1);
    use("flint_and_steel", ivec3(12, Y + 1, 12), FACE_PY);
    check(!(w.getMeta(12, Y + 1, 12) & CAMPFIRE_OUT) && reg().metaLight(cf, w.getMeta(12, Y + 1, 12)) > 0, "flint and steel lights it again");
    P->pos = vec3(12.5f, Y + 1.5f, 12.5f); P->onGround = true; float hp0 = P->hp;
    tick(1.7);
    check(P->hp < hp0, "stepping on a lit fire burns");
    P->hp = 20; P->pos = vec3(8.5f, Y + 1, 8.5f);
    // the kit
    P->held() = ItemStack("campfire_kit", 1);
    use("campfire_kit", ivec3(15, Y, 15), FACE_PY);
    check(w.getBlock(15, Y + 1, 15) == cf && (w.getMeta(15, Y + 1, 15) & CAMPFIRE_KIT) && P->held().empty(), "the campfire kit sets down a campfire");
    P->sneaking = true;
    use("", ivec3(15, Y + 1, 15), FACE_PY);
    P->sneaking = false;
    check(w.getBlock(15, Y + 1, 15) == AIR && countItem(*P, "campfire_kit") == 1, "sneak + empty hand packs it up again");
    // rope over the edge of the floor
    P->held() = ItemStack("rope", 10);
    fx.msgs.clear();
    use("rope", ivec3(27, Y, 10), FACE_PX);
    int n = 0; for (int y = Y; y > Y - 20; y--) if (w.getBlock(28, y, 10) == B("rope")) n++;
    check(n == 10 && fx.heard("The rope hangs 10 blocks down") && P->held().empty(), "rope hangs down from a ledge");
    int r0 = countEntities("item", "rope");
    bool def = bus.emit("blockBreak", json::array({P->name, 28, Y, 10, "rope"}));
    int left = 0; for (int y = Y - 1; y > Y - 20; y--) if (w.getBlock(28, y, 10) == B("rope")) left++;
    check(def && left == 0 && countEntities("item", "rope") == r0 + 1, "cut it and the rest below comes down with it");
    // the grappling hook on the side of the floor
    check(grappleLodge(*L, *P, ItemStack("grappling_hook", 1, 1).toJson(), -12, Y, 5, ivec3(-1, 0, 0)), "the hook catches on a wall");
    check(w.getBlock(-13, Y, 5) == B("grapple_anchor") && w.getBlock(-13, Y - 5, 5) == B("rope") && (w.getMeta(-13, Y - 5, 5) & HOOK_ROPE), "  ...and its rope drops to the ground");
    P->held().clear();
    use("", ivec3(-13, Y - 5, 5), FACE_PX);
    check(w.getBlock(-13, Y, 5) == AIR && w.getBlock(-13, Y - 5, 5) == AIR && countItem(*P, "grappling_hook") == 1, "use its rope to take it back");
    // the sleeping bag
    w.setBlock(20, Y + 1, 20, B("sleeping_bag"), 0);
    L->info.time = DAY_LEN * 0.7;
    P->spawnPoint = vec3(1, Y + 1, 1);
    check(sleepingBagSleep(*L, *P, 20, Y + 1, 20) && P->stats.contains("bagRestore"), "you can sleep in the bag at night");
    P->spawnPoint = vec3(20.5f, Y + 1.5f, 20.5f);
    L->info.day++; fx.msgs.clear();
    tick(0.1);
    check(P->spawnPoint.x == 1 && fx.heard("You roll up your sleeping bag"), "in the morning your old spawn is home again");
    L->info.time = 300;
  }
  // ------------------------------------------------------------ waystones and mounts
  {
    BlockId ws = B("waystone");
    w.setBlock(0, Y + 1, 0, ws); w.setBlock(24, Y + 1, 24, ws);
    int screens = 0;
    int h = bus.on("contentScreen", [&](const json& a) { if (a[1] == "waystones") screens++; return true; });
    P->held().clear(); fx.msgs.clear();
    use("", ivec3(0, Y + 1, 0), FACE_PY);
    check(P->stats["ways"].contains("p:0," + std::to_string(Y + 1) + ",0") && fx.heard("Attuned to the waystone") && screens == 1, "using a waystone attunes to it and opens the menu");
    // a pillar two blocks tall (tools/port/polish_data.py): its top is the same stone
    check(w.getBlock(0, Y + 2, 0) == B("waystone_upper") && w.getBlock(24, Y + 2, 24) == B("waystone_upper"), "a waystone stands two blocks tall");
    use("", ivec3(0, Y + 2, 0), FACE_PY);
    check(screens == 2 && P->stats["ways"].size() == 1 && wayKnown(*P, "p:0," + std::to_string(Y + 1) + ",0"), "using its top is using the stone (no second record)");
    w.setBlock(40, Y + 1, 40, ws); w.setBlock(40, Y + 2, 40, AIR);
    check(w.getBlock(40, Y + 1, 40) == AIR, "breaking its top takes the whole pillar");
    use("", ivec3(24, Y + 1, 24), FACE_PY);
    auto list = wayList(*L, *P, "p:24," + std::to_string(Y + 1) + ",24");
    check(list.size() == 1 && list[0].cost == 1, "the menu lists the others, a level for every 600 blocks");
    P->level = 0;
    std::string why = wayGo(*L, *P, list[0].id);
    check(why.find("You need 1 level") != std::string::npos, "too far for your experience: you're told");
    P->level = 3;
    check(wayGo(*L, *P, list[0].id).empty() && glm::distance(vec2(P->pos.x, P->pos.z), vec2(0.5f, 0.5f)) < 3 && P->level == 2, "travel there for a level");
    bus.off(h);
    VillageInfo v; v.id = "test"; v.name = "Testholm"; v.x = 8; v.y = Y; v.z = 8; v.r = 12;
    contentHooks().villageNear = [v](Level&, vec3, float) { return std::optional<VillageInfo>(v); };
    tick(3.2);
    check(L->info.data["waystones"].contains("v:test"), "a village gets its waystone in the square");
    contentHooks().villageNear = nullptr;
    Entity horse; horse.type = "horse"; horse.hp = horse.maxHp = 22;
    json hs = mountStats(horse);
    check(hs.is_array() && hs[0].get<float>() >= 0.8f && hs[0].get<float>() <= 1.2f && hsLine(hs).find("Toughness") != std::string::npos, "a mount has its measure (speed, jump, toughness)");
    json f = foalStats(hs, hs);
    check(f[1].get<float>() >= 0.7f && f[1].get<float>() <= 1.5f, "a foal takes after its parents");
  }
  // ------------------------------------------------------------ birds in flight (sim/creature_rig.cpp animBird)
  for (const char* bird : {"crow", "parrot"}) {
    Creature* c = spawnCreature(*L, P->realm, bird, vec3(60.5f, Y + 30, 60.5f));
    const RigModel* RM = c ? rigModel(c->modelKey()) : nullptr;
    if (!c || !RM) { check(false, std::string("a ") + bird + " to fly"); continue; }
    int wi = RM->find("wingR");   // (an imported body names it its own way: the creature import)
    for (const char* n : {"wing_right", "right_wing", "rightWing", "wingRight", "wing_r"}) if (wi < 0) wi = RM->find(n);
    auto wingDown = [&] { const vec3 d = glm::normalize(vec3(c->mats[wi] * vec4(0, -1, 0, 0))); return d.y; };   // how far the wing points down
    float worstAir = -1, worstGround = 1;
    float tAge = 1;
    for (int k = 0; k < 80; k++) {   // through the wingbeat (once the change from the ground has eased in: rigBlend)
      c->onGround = false; c->inWater = false; c->vel = vec3(2.5f, 0.3f, 0); c->age = (tAge += 0.011f);
      rigPose(*c, *RM, creatureBase(*c, false)); if (k > 45) worstAir = std::max(worstAir, wingDown());
    }
    for (int k = 0; k < 40; k++) { c->onGround = true; c->vel = vec3(0); c->age = (tAge += 0.02f); rigPose(*c, *RM, creatureBase(*c, false)); }
    worstGround = wingDown();
    if (RM->rig == "beast") {   // an imported body (its wing laid its own way): in the air it beats (the tip swings up and down a lot), perched it's still
      auto tipY = [&] { return (c->mats[wi] * vec4(0, 0, 0.3f, 1)).y + (c->mats[wi] * vec4(0.3f, 0, 0, 1)).y; };
      float lo = 1e9f, hi = -1e9f, glo = 1e9f, ghi = -1e9f;
      for (int k = 0; k < 120; k++) { c->onGround = false; c->vel = vec3(2.5f, 0.3f, 0); c->age = (tAge += 0.011f); rigPose(*c, *RM, creatureBase(*c, false)); if (k > 60) { lo = std::min(lo, tipY()); hi = std::max(hi, tipY()); } }
      for (int k = 0; k < 120; k++) { c->onGround = true; c->vel = vec3(0); c->age = (tAge += 0.02f); rigPose(*c, *RM, creatureBase(*c, false)); if (k > 80) { glo = std::min(glo, tipY()); ghi = std::max(ghi, tipY()); } }
      check(wi >= 0 && hi - lo > 0.08f && ghi - glo < (hi - lo) * 0.5f, std::string("a flying ") + bird + "'s wings beat (" + std::to_string(hi - lo) + " up and down), perched they rest (" + std::to_string(ghi - glo) + ")");
      c->removed = true; continue;
    }
    check(wi >= 0 && worstAir < 0.75f && worstGround < -0.85f, std::string("a flying ") + bird + "'s wings are spread out and beat (never hanging: " + std::to_string(worstAir) + "); perched, folded down (" + std::to_string(worstGround) + ")");
    c->removed = true;
  }
  // ------------------------------------------------------------ the feeding trough (sim/trough.h)
  {
    const BlockId tr = B("feeding_trough");
    check(tr != AIR, "the feeding trough is a block");
    const int TX = 40, TZ = 8;
    { const double tw = nowSeconds();   // (its chunk out at x 40: streamed in before the floor's laid, however long it takes)
      while (!w.chunk(DIM_OVERWORLD, TX >> 4, TZ >> 4) && nowSeconds() - tw < 30) { w.stream({{vec3(TX, Y, TZ), 1, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(20)); } }
    for (int x = TX - 6; x <= TX + 6; x++) for (int z = TZ - 6; z <= TZ + 6; z++) { w.setBlock(x, Y, z, B("stone")); for (int y = Y + 1; y <= Y + 3; y++) w.setBlock(x, y, z, AIR); }   // (a floor: the test world is bare out here)
    w.setBlock(TX, Y + 1, TZ, tr);
    RayHit h; h.hit = true; h.block = ivec3(TX, Y + 1, TZ); h.normal = ivec3(0, 1, 0); h.id = tr; h.point = vec3(TX + 0.5f, Y + 1.56f, TZ + 0.5f);
    Creature* cow = spawnCreature(*L, P->realm, "cow", vec3(TX + 5.5f, Y + 1, TZ + 0.5f));
    check(cow && !cow->T->food.empty(), "a cow to feed");
    if (cow && tr) {
      const std::string food = cow->T->food[0];
      ItemStack held(food, 20);
      check(troughUse(*L, *P, w, h, held) && held.empty() && troughStack(w, TX, Y + 1, TZ).n == 20 && troughLevel(20) == 2, "food tipped into the trough (" + food + " x20, level 2)");
      ItemStack other("stick", 3);
      check(!troughUse(*L, *P, w, h, other) && other.n == 3, "...it takes only what animals eat (a stick is left alone)");
      {   // its recipe is its own: planks in a U, wheat in the bottom (the rowboat keeps P P / PPP)
        auto grid = [](std::vector<const char*> c) { std::vector<ItemStack> g(9); for (int i = 0; i < 9; i++) if (c[i]) g[i] = ItemStack(c[i], 1); return g; };
        CraftMatch a = matchGrid(grid({"planks", nullptr, "planks", "planks", "wheat", "planks", nullptr, nullptr, nullptr}), 3);
        CraftMatch b = matchGrid(grid({"planks", nullptr, "planks", "planks", "planks", "planks", nullptr, nullptr, nullptr}), 3);
        check(a && a.result().id == "feeding_trough" && b && b.result().id != "feeding_trough", "the trough's recipe doesn't take another's (" + (b ? b.result().id : std::string("-")) + " keeps P P / PPP)");
      }
      Creature* calf = spawnCreature(*L, P->realm, "cow", vec3(TX - 4.5f, Y + 1, TZ + 0.5f));
      if (calf) calf->makeBaby();
      cow->loveT = 0; cow->breedCd = 0;
      tick(8);
      check(cow->loveT > 0 || cow->breedCd > 0, "a grown cow walks to the trough, eats and is in the mood");
      check(troughStack(w, TX, Y + 1, TZ).n == 19 && (!calf || calf->loveT <= 0), "...one bite of it (a calf leaves it be): " + std::to_string(troughStack(w, TX, Y + 1, TZ).n) + " left, calf love " + std::to_string(calf ? calf->loveT : 0.f));
      ItemStack empty;
      int got0 = 0; for (auto& s : P->inv) if (s.id == food) got0 += s.n;
      check(troughUse(*L, *P, w, h, empty) && troughStack(w, TX, Y + 1, TZ).empty(), "an empty hand takes what's left back");
      int got = 0; for (auto& s : P->inv) if (s.id == food) got += s.n;
      check(got - got0 == 19, "...into your bag");
      cow->removed = true; if (calf) calf->removed = true;
    }
  }
  // ------------------------------------------------------------ elytra
  {
    P->armor[1] = ItemStack("elytra", 1);
    P->pos = vec3(8.5f, Y + 45, 8.5f); P->vel = vec3(0, -1, 0); P->onGround = false; P->yaw = 0; P->pitch = -0.3f;
    check(elytraJump(*L, *P) && P->gliding, "jump while falling: the wings spread");
    for (int i = 0; i < 90; i++) elytraMove(*L, *P, TICK_DT);
    printf("     glide: z %.1f speed %.1f gliding %d\n", P->pos.z, glm::length(P->vel), (int)P->gliding);
    check(P->gliding && P->pos.z < 8.5f - 5 && glm::length(P->vel) > 6, "you glide where you look, gathering speed in a dive");
    P->held() = ItemStack("firework_rocket", 2);
    use("firework_rocket");
    check(P->glideBoost > 0.5f && P->held().n == 1, "a rocket in flight is a boost");
    for (int x = 0; x < 20; x++) for (int y = -6; y < 6; y++) w.setBlock(x, ifloor(P->pos.y) + y, ifloor(P->pos.z) - 3, stone);
    P->vel = vec3(0, 0, -38); P->pitch = 0; float hp0 = P->hp;
    for (int i = 0; i < 10; i++) elytraMove(*L, *P, TICK_DT);
    check(P->hp < hp0, "hit a wall fast and it hurts");
    P->hp = 20; P->gliding = false; P->h = PLAYER_H; P->armor[1].clear();
    P->pos = vec3(8.5f, Y + 1, 8.5f); P->vel = vec3(0); P->onGround = true;
  }
  // ------------------------------------------------------------ the stable
  {
    auto e = createEntity("horse"); e->type = "horse"; e->pos = vec3(10, Y + 1, 10); e->hp = e->maxHp = 22; e->data["owner"] = P->name; e->data["tamed"] = true;
    Entity* hz = L->spawn(std::move(e));
    fx.msgs.clear();
    tick(0.1);
    std::string uid = hz->data.value("uid", "");
    auto list = stableList(*L, P->name);
    check(!uid.empty() && list.size() == 1 && list[0].state == "out" && list[0].mount && fx.heard("joins your stable"), "a tamed creature joins your stable");
    stableDismiss(*L, *P, uid);
    tick(0.1);
    check(hz->removed || countEntities("horse") == 0, "dismiss sends it home");
    check(stableList(*L, P->name)[0].state == "stabled", "  ...and the stable has it");
    check(stableSummon(*L, *P, uid).empty() && countEntities("horse") == 1, "summon brings it to your side");
    Entity* h2 = nullptr; for (auto& q : L->entities()) if (!q->removed && q->type == "horse") h2 = q.get();
    check(h2 && h2->data.value("uid", "") == uid && glm::distance(h2->pos, P->pos) < 6, "  ...the same one, close by");
    check(h2 && stableCreatureDies(*L, *h2), "a companion that falls goes home");
    if (h2) h2->removed = true;
    tick(0.1);
    check(stableSummon(*L, *P, uid).find("is still resting") != std::string::npos, "  ...to rest for a few minutes");
    stableFav(*L, *P, uid);
    check(stableList(*L, P->name)[0].fav, "★ marks your favourite mount");
  }
  // ------------------------------------------------------------ places
  {
    contentHooks().placesNear = [](Level&, int, vec3 p, float) {
      PlaceInfo s; s.id = "sky_temple@1"; s.name = "the Sky Temple"; s.x0 = (int)p.x - 5; s.x1 = (int)p.x + 5; s.z0 = (int)p.z - 5; s.z1 = (int)p.z + 5; s.y = (int)p.y;
      return std::vector<PlaceInfo>{s};
    };
    int xp0 = P->xp; fx.msgs.clear();
    tick(1.2);
    check(P->stats["sseen"].contains("sky_temple@1") && fx.heard("You have found the Sky Temple!") && P->xp == xp0 + 25, "walk into a far place: you've found it (+25 experience)");
    check(foundText("a Cloud Shrine") == "You have found a Cloud Shrine!" && foundText("An Ember Bastion") == "You have found an Ember Bastion!", "the original's wording for finding places");
    bool listed = false; for (auto& f : foundPlaces(*L, *P)) if (f.id == "sky_temple@1") listed = true;
    check(listed, "the journal of places lists it");
    contentHooks().placesNear = nullptr;
  }
  // ------------------------------------------------------------ farming
  {
    BlockId farmland = B("farmland"), tomato = B("tomato_crop");
    for (int x = 2; x < 5; x++) for (int z = 14; z < 17; z++) { w.setBlock(x, Y, z, farmland); w.setBlock(x, Y + 1, z, tomato, 0); }
    P->pos = vec3(3.5f, Y + 1, 18.5f); P->yaw = 0; P->pitch = -0.6f;   // looking down at the crops
    P->held() = ItemStack("watering_can", 1);
    fx.msgs.clear();
    use("watering_can");
    check(fx.heard("The can is empty"), "an empty can has to be filled first");
    P->held().tag = {{"water", WATERING_CAN_MAX}};
    int grown = 0;
    for (int i = 0; i < 6; i++) { use("watering_can"); std::this_thread::sleep_for(std::chrono::milliseconds(520)); }
    for (int x = 2; x < 5; x++) for (int z = 14; z < 17; z++) grown += w.getMeta(x, Y + 1, z) & 15;
    check(grown > 0 && canWater(P->held()) < WATERING_CAN_MAX, "pouring the can brings the crops on (" + std::to_string(grown) + " stages)");
    w.setBlock(8, Y, 22, farmland);
    P->held() = ItemStack("rice", 4); fx.msgs.clear();
    check(!use("rice", ivec3(8, Y, 22), FACE_PY) && fx.heard("Rice needs water"), "rice wants water beside its farmland");
    w.setBlock(9, Y, 22, water);
    check(use("rice", ivec3(8, Y, 22), FACE_PY), "  ...and with water beside it, it can be planted");
    int seeds = 0; for (int i = 0; i < 200; i++) if (!wildSeed().empty()) seeds++;
    check(seeds == 200, "long grass can give up a garden seed");
  }
  // ------------------------------------------------------------ mixed tools (core/tool_parts.h)
  {
    auto grid = [](std::vector<std::string> ids) { std::vector<ItemStack> g(9); for (int i = 0; i < 9; i++) if (!ids[i].empty()) g[i] = ItemStack(ids[i], 1); return g; };
    CraftMatch m = matchGrid(grid({"iron_ingot", "diamond", "copper_ingot", "", "stick", "", "", "stick", ""}), 3);
    ItemStack t = m.ok ? m.result() : ItemStack();
    check(t.id == "mixed_pickaxe" && isPartTool(t) && t.tag["pt"]["m"] == json::array({"iron", "diamond", "copper"}), "iron, diamond and copper across a pickaxe's head make a mixed pickaxe");
    ToolInfo ti = toolOf(t), iron = reg().item("iron_pickaxe")->tool, dia = reg().item("diamond_pickaxe")->tool;
    check(ti.type == "pickaxe" && ti.durability > iron.durability && ti.durability < dia.durability && ti.tier == 3 && ti.speed > iron.speed * 0.8f,
          "its stats are the three's, mixed (dur " + std::to_string(ti.durability) + ", speed " + std::to_string(ti.speed) + ", tier " + std::to_string(ti.tier) + ")");
    check(stackName(t) == "Iron, Diamond & Copper Pickaxe", "and its name says so: " + stackName(t));
    check(partToolModelKey(t) == "pt:pickaxe:iron;diamond;copper:stick:", "its model key: " + partToolModelKey(t));
    CraftMatch plain = matchGrid(grid({"iron_ingot", "iron_ingot", "iron_ingot", "", "stick", "", "", "stick", ""}), 3);
    check(plain.ok && plain.out == "mixed_pickaxe" && stackName(plain.result()) == "Iron Pickaxe", "all iron: the procedural iron pickaxe (every metal tool is)");
    CraftMatch woody = matchGrid(grid({"planks", "planks", "planks", "", "stick", "", "", "stick", ""}), 3);
    check(woody.ok && woody.out == "wood_pickaxe", "all wood is still the ordinary wooden pickaxe");
    CraftMatch sw = matchGrid(grid({"", "gold_ingot", "", "", "steel_ingot", "", "", "bone", ""}), 3);
    ItemStack swt = sw.ok ? sw.result() : ItemStack();
    check(swt.id == "mixed_sword" && stackName(swt) == "Bone-Hafted Golden & Steel Sword", "a gold-tipped steel sword on a bone haft: " + stackName(swt));
    ItemStack worn = t; worn.d = 600;
    std::vector<ItemStack> g2(9); g2[0] = worn; g2[1] = ItemStack("diamond", 1);
    CraftMatch mend = matchGrid(g2, 3);
    ItemStack mended = mend.ok ? mend.result() : ItemStack();
    check(mended.id == "mixed_pickaxe" && mended.d < 600 && isPartTool(mended), "a worn mixed pickaxe and a diamond (one of its own) mend it (" + std::to_string(mended.d) + ")");
    std::vector<ItemStack> g3(9); g3[0] = worn; g3[1] = ItemStack("gold_ingot", 1);
    check(!matchGrid(g3, 3).ok, "  ...but not with a material it isn't made of");
    CraftMatch helm = matchGrid(grid({"gold_ingot", "diamond", "gold_ingot", "iron_ingot", "", "iron_ingot", "", "", ""}), 3);
    ItemStack h = helm.ok ? helm.result() : ItemStack();
    check(h.id == "mixed_helmet" && isPartArmor(h) && armorPointsOf(h) >= 2 && toolMaxDurability(h) > 77 && stackName(h) == "Golden, Diamond & Iron Helmet",
          "gold, diamond and iron in a helmet's pattern make a mixed helmet: " + stackName(h) + " (" + std::to_string(armorPointsOf(h)) + " armour, " + std::to_string(toolMaxDurability(h)) + ")");
    check(partArmorWornLook(h).rfind("pa:helmet:", 0) == 0, "worn, it shows its own mix of metals: " + partArmorWornLook(h));
    StationResult gem = stationResult("forge", ItemStack("iron_pickaxe", 1), ItemStack("diamond", 1));
    check(gem.ok && gem.out.tag.value("gem", "") == "diamond" && toolOf(gem.out).durability > iron.durability && stackName(gem.out) == "Iron Pickaxe (Diamond)",
          "the Tool Forge sets a diamond in an iron pickaxe: tougher (" + std::to_string(toolOf(gem.out).durability) + ")");
    StationResult again = stationResult("forge", gem.out, ItemStack("ruby", 1));
    check(!again.ok, "  ...one gem a tool");
  }
  // ------------------------------------------------------------ a saddled pig is ridden (no taming)
  {
    Creature* pig = spawnCreature(*L, P->realm, "pig", P->pos + vec3(1.5f, 0, 0));
    check(pig != nullptr, "a pig");
    if (pig) {
      pig->baby = false;
      P->held() = ItemStack("saddle", 1);
      check(useOnCreature(*L, *P, *pig, false) && pig->saddled && P->held().empty(), "a saddle goes on a wild pig");
      check(useOnCreature(*L, *P, *pig, false) && P->riding == pig->id && pig->riddenBy == P->id, "  ...and an empty hand gets on it");
      P->riding = 0; pig->riddenBy = 0; pig->removed = true;
    }
  }
  // ------------------------------------------------------------ the Wither: its skulls hit back hurt it, armoured or not
  {
    P->pos = vec3(8.5f, Y + 1, 8.5f); P->hp = 20;
    Creature* wi = spawnCreature(*L, P->realm, "wither", vec3(8.5f, Y + 4, -2.5f));
    check(wi != nullptr, "a Wither can be called up");
    if (wi) {
      wi->data["spawnT"] = 99.f;
      for (int armoured = 0; armoured < 2; armoured++) {
        if (armoured) wi->hp = wi->T->hp * 0.4f;
        const float hp0 = wi->hp;
        const vec3 eye = P->pos + vec3(0, 1.62f, 0), from = eye + vec3(0, 0, -2.5f);
        Entity* sk = spawnProjectile(*L, P->realm, from, vec3(0, 0, 13), "wither_skull", wi);
        const vec3 look = glm::normalize(wi->pos + vec3(0, wi->h * 0.5f, 0) - eye);
        const bool hitBack = sk && deflectFireball(*L, *P, eye, glm::normalize(from - eye), 4.5f);
        (void)look;
        if (sk) { const vec3 c = wi->pos + vec3(0, wi->h * 0.5f, 0); sk->pos = c + vec3(0, 0, 2.5f); sk->vel = vec3(0, 0, -20); }   // flying straight back at it
        tick(0.5);
        check(hitBack && wi->hp < hp0, std::string("a Wither skull punched back hurts the Wither") + (armoured ? " (armoured too)" : "") + " (" + std::to_string(hp0) + " -> " + std::to_string(wi->hp) + ")");
      }
      wi->removed = true;
    }
  }
  // ------------------------------------------------------------ fireballs punched back hurt the creatures of fire that threw them
  for (const char* who : {"cinder_wisp", "cinder_golem", "shulker"}) {
    for (auto& e : L->entities()) if (e && e->type == "projectile") e->removed = true;   // (nothing left flying from before: the punch must find this one)
    P->pos = vec3(8.5f, Y + 1, 8.5f); P->hp = 20;
    Creature* c = creatureType(who) ? spawnCreature(*L, P->realm, who, vec3(8.5f, Y + 1, -2.5f)) : nullptr;
    if (!c) continue;
    c->invulnT = 0; c->calm = true; c->data["spawnT"] = 99.f;
    mobLevelUpdate(*L, *c);   // (it takes its level from the players near when first met: its health is final after that)
    const float hp0 = c->hp;
    const vec3 eye = P->pos + vec3(0, 1.62f, 0), from = eye + vec3(0, 0, -2.5f);
    const bool shulker = std::string(who) == "shulker";
    Entity* f = spawnProjectile(*L, P->realm, from, vec3(0, 0, 13), shulker ? "shulker" : "fireball", c);
    if (f && shulker) f->data["target"] = P->id;
    const bool hitBack = f && deflectFireball(*L, *P, eye, glm::normalize(from - eye), 4.5f);
    if (f) { const vec3 at = c->pos + vec3(0, c->h * 0.5f, 0); f->pos = at + vec3(0, 0, 2.0f); f->vel = vec3(0, 0, -20); }
    tick(0.6);
    check(hitBack && c->hp < hp0, std::string("a shot punched back hurts the ") + who + " that sent it (" + std::to_string((int)hp0) + " -> " + std::to_string((int)c->hp) + ")");
    c->removed = true;
  }

  // ---- every creature's looks (tools/port/mob_variants.py, mobs.json variantDefs)
  {
    printf("\n[looks]\n");
    int types = 0, looks = 0, bad = 0;
    for (auto& [k, v] : reg().mobs.items()) if (const CreatureType* T = creatureType(k)) if (!T->vdefs.empty()) {
      types++;
      for (auto& d : T->vdefs) { looks++; const RigModel* M = rigModel(d.model); const RigModel* B = rigModel(T->model); if (!M || !B || M->parts.size() < B->parts.size()) { bad++; printf("  bad look %s\n", d.model.c_str()); } }
    }
    check(types > 100 && looks > 200 && bad == 0, "every look is a whole model of its own (" + std::to_string(looks) + " looks of " + std::to_string(types) + " creatures)");
    // underground (a stone room with a roof, in the dark), miners turn up; under the open sky, never
    World& w = L->world();
    const BlockId stone = reg().blockId("stone");
    const int cx = -8, cz = 24;
    for (int x = cx - 3; x <= cx + 3; x++) for (int z = cz - 3; z <= cz + 3; z++) for (int y = Y; y <= Y + 6; y++) {
      const bool wall = x == cx - 3 || x == cx + 3 || z == cz - 3 || z == cz + 3 || y == Y || y == Y + 6;
      w.setBlock(x, y, z, wall ? stone : AIR, 0);
    }
    tick(0.2);   // (the light engine catches up)
    int minerIn = 0, minerOut = 0;
    for (int i = 0; i < 400; i++) {
      if (Creature* c = spawnCreature(*L, P->realm, "skeleton", vec3(cx + 0.5f, Y + 1, cz + 0.5f))) { minerIn += c->variant == "skeleton_miner"; c->removed = true; }
      if (Creature* c = spawnCreature(*L, P->realm, "skeleton", vec3(cx + 28.5f, Y + 1, cz - 30.5f))) { minerOut += c->variant == "skeleton_miner"; c->removed = true; }
    }
    tick(0.05);
    check(minerIn > 10 && minerOut == 0, "miner skeletons turn up underground, never under the sky (" + std::to_string(minerIn) + " / " + std::to_string(minerOut) + ")");
    // a look by name, with what it carries; its story's drops when it dies
    Creature* sk = spawnCreature(*L, P->realm, "skeleton", vec3(cx + 28.5f, Y + 1, cz - 26.5f));
    check(sk && setCreatureLook(*sk, "miner") && sk->variant == "skeleton_miner" && sk->heldItem == "stone_pickaxe" && sk->label() == "a miner skeleton", "a look by name: the miner, his pickaxe and his name");
    int arrows = 0;
    for (int i = 0; i < 3; i++) {
      Creature* pin = spawnCreature(*L, P->realm, "skeleton", vec3(cx + 24.5f + i * 8, Y + 1, cz - 20.5f));
      if (!pin || !setCreatureLook(*pin, "pincushion")) continue;
      std::vector<Entity*> before; for (auto& e : L->entities()) before.push_back(e.get());
      pin->die(*L, DamageSrc{});
      for (auto& e : L->entities()) if (std::find(before.begin(), before.end(), e.get()) == before.end() && e->type == "item" && e->data.contains("stack") && e->data["stack"].value("id", "") == "arrow") arrows += e->data["stack"].value("n", 1);
    }
    check(arrows >= 5, "skeletons shot full of arrows leave their arrows (" + std::to_string(arrows) + " from 3)");
    if (sk) sk->removed = true;
  }
  // every metal mixes in a tool: the ingots the materials table doesn't list (pig iron, zinc, brass, rose gold...) by their makeup
  for (const char* ing : {"pig_iron", "zinc_ingot", "bismuth_ingot", "brass_ingot", "rose_gold_ingot", "sterling_silver_ingot", "black_bronze_ingot", "bismuth_bronze_ingot", "iron_ingot", "diamond"})
    check(reg().item(ing) == nullptr || toolMaterialOfStack(ItemStack(ing, 1)) != nullptr, std::string("a tool can be made of ") + ing);
  {   // ...and how much of each counts: more of one metal in the head, more of its stats
    auto mk = [&](const char* a, const char* b2) { const ToolMaterial* A = toolMaterialOfStack(ItemStack(a, 1)), *Bm = toolMaterialOfStack(ItemStack(b2, 1)); return std::make_pair(A, Bm); };
    for (auto [a, b2] : {std::make_pair("iron_ingot", "zinc_ingot"), std::make_pair("pig_iron", "rose_gold_ingot"), std::make_pair("brass_ingot", "steel_ingot")}) {
      auto [A, Bm] = mk(a, b2);
      if (!A || !Bm) { check(false, std::string("materials for ") + a + " + " + b2); continue; }
      ItemStack t3, t2, t1;
      const bool ok = makePartTool("pickaxe", {A->key, A->key, A->key}, "stick", t3) && makePartTool("pickaxe", {A->key, A->key, Bm->key}, "stick", t2) && makePartTool("pickaxe", {A->key, Bm->key, Bm->key}, "stick", t1);
      const int d3 = ok ? toolMaxDurability(t3) : 0, d2 = ok ? toolMaxDurability(t2) : 0, d1 = ok ? toolMaxDurability(t1) : 0;
      check(ok && d3 != d1 && ((d3 >= d2 && d2 >= d1) || (d3 <= d2 && d2 <= d1)), std::string("a pickaxe of ") + a + " and " + b2 + ": its wear follows how much of each (" + std::to_string(d3) + ", " + std::to_string(d2) + ", " + std::to_string(d1) + ")");
    }
  }
  {   // armour too: a helmet of pig iron and zinc, its protection between theirs
    const ToolMaterial* A = toolMaterialOfStack(ItemStack("pig_iron", 1)), *Z = toolMaterialOfStack(ItemStack("zinc_ingot", 1));
    const ArmorPattern* hp = armorPattern("helmet");
    ItemStack h1, h2;
    const bool ok = A && Z && hp && makePartArmor("helmet", std::vector<std::string>(hp->cells.size(), A->key), h1)
                    && makePartArmor("helmet", [&] { std::vector<std::string> v(hp->cells.size(), A->key); for (size_t i = 0; i < v.size() / 2; i++) v[i] = Z->key; return v; }(), h2);
    check(ok && armorPointsOf(h1) != armorPointsOf(h2), "a helmet of pig iron and zinc: its protection follows how much of each (" + std::to_string(ok ? armorPointsOf(h1) : 0.f) + " / " + std::to_string(ok ? armorPointsOf(h2) : 0.f) + ")");
  }
  {   // the off hand: the shield slot on the free forearm, a sword in the off hand swings (each hand its own)
    for (auto& s2 : P->inv) s2 = ItemStack();
    P->selected = 0; P->offhand[0] = ItemStack(); P->shield[0] = ItemStack("shield", 1);
    P->inv[0] = ItemStack("iron_sword", 1);
    check(shieldSide(*P) == -1, "the shield slot: a sword in the main hand, the shield on the left forearm");
    P->offhand[0] = ItemStack("torch", 1);
    check(shieldSide(*P) == -1, "  ...a torch in the off hand: the shield still there to raise");
    P->offhand[0] = ItemStack("iron_sword", 1); P->inv[0] = ItemStack();
    check(shieldSide(*P) == 1, "  ...the sword swapped to the off hand: the shield on the right forearm");
    P->inv[0] = ItemStack("iron_sword", 1);
    check(shieldSide(*P) == 0, "  ...a sword in each hand: the shield slung (nothing to raise)");
    check(!offHandSwings(*P, false, true) && !offHandSwings(*P, true, false), "  ...two swords: use stays free to block, the attack button chains both");
    P->inv[0] = ItemStack();
    check(offHandSwings(*P, true, false), "  ...the main hand empty: attack swings the off hand's sword");
    // a swing of the off hand's: the move starts, marked the off hand's, and its blow wears the off hand's sword
    P->inv[0] = ItemStack("iron_sword", 1); P->offhand[0] = ItemStack("diamond_sword", 1);
    P->atkMove = 0; P->atkT = 0; P->atkGap = 9; P->atkCombo = 0;
    PlayerInput atk; atk.attackPressed = true;
    tickMelee(*L, *P, 0.01f, atk, true, false);
    const u8 firstCut = P->atkMove;
    check(P->atkMove != 0 && !P->atkOff, "  ...the first click is the main hand");
    for (int k = 0; k < 200 && P->atkMove; k++) tickMelee(*L, *P, 0.01f, PlayerInput{}, true, false);
    P->atkGap = 0.1f;
    PlayerInput atk2; atk2.attackPressed = true;
    tickMelee(*L, *P, 0.01f, atk2, true, false);
    check(P->atkMove == firstCut && P->atkOff, "  ...the next click is the off hand, the same cut mirrored");
    for (int k = 0; k < 200 && P->atkMove; k++) tickMelee(*L, *P, 0.01f, PlayerInput{}, true, false);
    P->atkGap = 0.1f;
    PlayerInput atk3; atk3.attackPressed = true;
    tickMelee(*L, *P, 0.01f, atk3, true, false);
    check(P->atkMove != 0 && !P->atkOff, "  ...the click after that is the main hand again");
    for (int k = 0; k < 200 && P->atkMove; k++) tickMelee(*L, *P, 0.01f, PlayerInput{}, true, false);
    P->atkGap = 0.1f;
    PlayerInput atk4; atk4.attackPressed = true;
    tickMelee(*L, *P, 0.01f, atk4, true, false);
    check(P->atkMove != 0 && P->atkOff, "  ...and the one after is the off hand again");
    for (int k = 0; k < 200 && P->atkMove; k++) tickMelee(*L, *P, 0.01f, PlayerInput{}, true, false);
    check(!P->atkOff && P->inv[0].id == "iron_sword" && P->offhand[0].id == "diamond_sword", "  ...and ends with each sword still in its own hand");
    P->atkMove = 0; P->atkT = 0; P->atkGap = 9; P->atkCombo = 0; P->atkOff = false;
    PlayerInput use; use.use = true; use.usePressed = true;
    tickMelee(*L, *P, 0.01f, use, true, false);
    check(P->atkMove == 0, "  ...use does not swing either sword");
    check(attackMove(MV_AXE1).strikes == 1 && attackMove(MV_AXE1).pierce > attackMove(MV_DAGGER1).pierce && attackMove(MV_DAGGER1).impact < attackMove(MV_AXE1).impact,
          "an axe bites one foe through armour; a dagger is lighter");
    P->inv[0] = ItemStack("stone_knife", 1); P->offhand[0] = ItemStack();
    P->sneaking = true; P->onGround = true; P->sprinting = false;
    P->atkMove = 0; P->atkT = 0; P->atkGap = 9; P->atkCombo = 0;
    PlayerInput sn; sn.attackPressed = true;
    tickMelee(*L, *P, 0.01f, sn, true, false);
    check(P->atkMove == MV_DAGGER3, "sneaking with a knife lunges");
    P->atkMove = 0; P->atkGap = 9; P->inv[0] = ItemStack("iron_axe", 1);
    tickMelee(*L, *P, 0.01f, sn, true, false);
    check(P->atkMove == MV_AXE2 && P->data.value("sneakHeavy", 0.f) > 1.f, "sneaking with an axe opens with the heavy cut");
    P->sneaking = false; P->atkMove = 0; P->data.erase("sneakHeavy");
    P->offhand[0] = ItemStack(); P->shield[0] = ItemStack(); P->inv[0] = ItemStack();
  }
  {   // the crafting guide's "Lay it out": a recipe set out on a crafting table from the bag, piece by piece
    const ivec3 T(8, Y + 1, 16); w.setBlock(T.x, T.y, T.z, B("crafting_table"));
    for (auto& s2 : P->inv) s2 = ItemStack();
    P->inv[0] = ItemStack("planks", 6); P->yaw = 0;
    int ri = -1; for (int i = 0; i < (int)reg().recipes.size(); i++) if (reg().recipes[i].out == "crafting_table") { ri = i; break; }
    check(ri >= 0 && worktableLayout(*L, *P, w, T, ri).empty(), "the guide lays a crafting table's recipe out on a table");
    const json* be = w.blockEntity(T.x, T.y, T.z);
    int on = 0; if (be) for (auto& s2 : worktableGrid(*be)) on += s2.empty() ? 0 : s2.n;
    check(on == 4 && P->inv[0].n == 2 && worktableMatch(w, T, P->yaw, w.immersive).out == "crafting_table", "  ...four planks on its squares, two left in the bag, and it makes a crafting table");
    P->inv[0] = ItemStack();
    int ri2 = -1; for (int i = 0; i < (int)reg().recipes.size(); i++) if (reg().recipes[i].out == "iron_pickaxe") { ri2 = i; break; }
    check(ri2 >= 0 && !worktableLayout(*L, *P, w, T, ri2).empty(), "  ...not without what it takes");
  }
  {   // the journal's story tree: only what you've come to shows
    P->stats["story"] = json::object(); P->stats.erase("vseen"); P->stats.erase("dex");
    json saveR = L->info.data.value("rift", json::object()); L->info.data["rift"] = json::object();
    auto shown = [&](const char* id) { Story s = storyOf(*L, *P); for (auto& c : s.ch) if (c.id == id) return c.shown; return false; };
    check(shown("village") && !shown("iron") && !shown("tear") && !shown("seal") && !shown("depths"), "a new journal: only the village (the rest hidden)");
    P->stats["vseen"] = json{{"v1", 1}};
    check(shown("iron") && !shown("tear"), "  ...the village found: the next chapter shows");
    P->stats["story"]["iron"] = 1;
    check(shown("tear") && !shown("keepers") && !shown("end"), "  ...iron made: the tear's chapter, not past it");
    dexNote(*P, "enderman", false);
    check(shown("end"), "  ...an enderman met: the End's branch shows");
    L->info.data["rift"] = json{{"stage", 1}, {"frags", 1}};
    check(shown("keepers") && shown("depths") && !shown("skylands") && !shown("gateway"), "  ...the tear open, a fragment given: the keepers and the fire below");
    L->info.data["rift"] = saveR;
    dexNote(*P, "deer", true); dexNote(*P, "deer", true);
    check(dexKills(*P, "deer") == 2 && dexKills(*P, "wolf") == -1, "the Compendium counts what you've beaten");
  }
  // ------------------------------------------------------------ the Tool Forge and the inscription table worked by hand
  {
    const ivec3 F(2, Y + 1, 12), T(5, Y + 1, 12);
    w.setBlock(F.x, F.y, F.z, B("tool_forge")); w.setBlock(T.x, T.y, T.z, B("inscription_table"));
    auto count = [&](const std::string& id) { int n = 0; for (auto& s : P->inv) if (s.id == id) n += s.n; return n; };
    for (auto& s : P->inv) s = ItemStack();
    ItemStack held("iron_ingot", 8);
    check(pairWorkPut(*L, *P, w, F, -1, held) && held.empty() && pairWorkItems(w, F)[0].n == 8, "the Tool Forge: ingots laid on it");
    auto heads = forgeWorkHeads(w, F);
    check(!heads.empty() && heads[0].second >= 1, "  ...it offers the heads they make (" + std::to_string(heads.size()) + ")");
    int axe = -1; for (int k = 0; k < (int)heads.size(); k++) if (heads[k].second == 3) axe = k;
    ItemStack made;
    check(axe >= 0 && forgeWorkHead(*L, *P, w, F, axe, &made) && pairWorkItems(w, F)[0].n == 5 && count(made.id) == 1, "  ...a head forged: three ingots for it, the head in the bag");
    check(pairWorkTake(*L, *P, w, F, 0) && count("iron_ingot") == 5 && forgeWorkHeads(w, F).empty(), "  ...the rest taken back");
    ItemStack pick("iron_pickaxe", 1), gem("diamond", 2);
    check(pairWorkPut(*L, *P, w, F, -1, pick) && pick.empty(), "  a pickaxe laid on it");
    check(pairWorkPut(*L, *P, w, F, -1, gem) && gem.n == 1 && pairWorkItems(w, F)[1].id == "diamond", "  ...a diamond beside it goes in as the gem (one)");
    check(forgeWorkHeads(w, F).empty() && pairWorkResult(*L, w, F).ok, "  ...no heads offered: a gem to set");
    check(pairWorkDo(*L, *P, w, F, &made) && made.id == "iron_pickaxe" && made.tag.is_object() && made.tag.contains("gem"), "  ...set: the pickaxe back with its gem");
    check(gemSocketsLine(made).find("Diamond") != std::string::npos && gemSocketsLine(made).find("empty") == std::string::npos, "  ...its sockets name the diamond, not empty (" + gemSocketsLine(made) + ")");
    // the inscription table
    for (auto& s : P->inv) s = ItemStack();
    P->stats["spells"] = json::array({"firebolt"});
    ItemStack book("apprentice_spellbook", 1), scroll = makeScroll("frost", 2), paper("paper", 4), lapis("lapis_lazuli", 4), dirt("dirt", 1);
    check(!inscribeWorkPut(*L, *P, w, T, dirt) && dirt.n == 1, "the inscription table: dirt isn't laid on it");
    check(inscribeWorkPut(*L, *P, w, T, book) && inscribeWorkPut(*L, *P, w, T, scroll) && inscribeWorkPut(*L, *P, w, T, paper) && inscribeWorkPut(*L, *P, w, T, lapis)
          && book.empty() && scroll.empty() && paper.empty() && lapis.empty(), "  ...book, scroll, paper and lapis laid on its board");
    check(inscribeWorkInscribe(*L, *P, w, T).empty() && inscribeWorkItems(w, T)[1].empty() && bookSpells(inscribeWorkItems(w, T)[0]).size() == 1, "  ...the scroll pressed into the book");
    check(inscribeWorkExtract(*L, *P, w, T, 0).empty() && bookSpells(inscribeWorkItems(w, T)[0]).empty() && count("spell_scroll") == 1, "  ...taken back out as a scroll");
    check(inscribeWorkWrite(*L, *P, w, T, "firebolt").empty() && inscribeWorkItems(w, T)[2].n == 3 && inscribeWorkItems(w, T)[3].n == 3 && count("spell_scroll") == 2, "  ...a spell you know written out: a paper and a lapis used");
    check(!inscribeWorkWrite(*L, *P, w, T, "frost").empty(), "  ...not one you don't know");
    check(inscribeWorkTake(*L, *P, w, T, 0) && count("apprentice_spellbook") == 1, "  ...the book taken back");
  }
  lvp->close();
  printf("[boss fights]\n");
  {
    World& wb = L->world();
    const int BY = dimBase(DIM_OVERWORLD) + 200;
    auto clearMobs = [&] { for (auto& e : L->entities()) if (!e->isPlayer() && !e->removed) e->removed = true; L->tick(TICK_DT); };
    const std::string saveMode = P->mode;
    P->mode = "survival";
    // each one, given 20 seconds beside you, hurts you: standing on the floor, and up on a pillar out of reach
    for (const char* b : {"night_lord", "graveyard_lich", "dead_king", "hollow_archmage", "lich_king", "hex_matron", "grave_warden", "minotaur", "valkyrie_queen", "pirate_captain", "rot_king", "slider", "broodmother", "ember_lord", "sun_spirit"}) {
      for (int pillar = 0; pillar < 2; pillar++) {
        clearMobs();
        for (int y = BY + 1; y < BY + 8; y++) for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) wb.setBlock(x, y, z, AIR);
        P->pos = vec3(8.5f, BY + 1 + (pillar ? 4 : 0), 8.5f); P->vel = vec3(0); P->dead = false;
        for (double t0 = nowSeconds(); nowSeconds() - t0 < 30 && !(wb.isLoaded(15, BY + 1, 8) && wb.isLoaded(-8, BY + 1, 8) && wb.isLoaded(24, BY + 1, 24));) { wb.stream({{P->pos, 2, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) { wb.setBlock(x, BY, z, B("stone")); for (int y = BY + 1; y < BY + 8; y++) wb.setBlock(x, y, z, AIR); }
        if (pillar) for (int y = BY + 1; y <= BY + 4; y++) wb.setBlock(8, y, 8, B("stone"));
        P->maxHp = 100000; P->hp = 100000; P->hurtTime = 0;
        Entity* boss = spawnCreature(*L, REALM_MAIN, b, vec3(15.5f, BY + 1, 8.5f));
        if (!boss) { check(false, std::string(b) + " spawns"); continue; }
        const float before = P->hp;
        for (int k = 0; k < 400 && !boss->removed; k++) { P->pos.x = 8.5f; P->pos.z = 8.5f; P->vel.x = P->vel.z = 0; P->invuln = std::max(0.f, P->invuln - (float)TICK_DT); L->tick(TICK_DT); }   // (the player ticks in the game: its hit protection wears off)
        const float lost = before - P->hp;
        if (std::getenv("DQ_BOSS_TEST_DEBUG")) printf("  %s at %.1f %.1f %.1f player %.1f %.1f %.1f state %s removed %d age %.1f hp %.0f\n", b, boss->pos.x, boss->pos.y, boss->pos.z, P->pos.x, P->pos.y, P->pos.z, (static_cast<Creature*>(boss)->state + "/" + boss->data.value("bfState", std::string("-"))).c_str(), (int)boss->removed, boss->age, P->hp);
        check(lost > 4 && !boss->removed, std::string(b) + (pillar ? " reaches you up a pillar" : " fights you") + " (" + std::to_string((int)lost) + " damage in 20 s)");
      }
    }
    // the Night Lord in its second phase: its moves with no hit windows of their own strike too
    {
      clearMobs();
      for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) { wb.setBlock(x, BY, z, B("stone")); for (int y = BY + 1; y < BY + 8; y++) wb.setBlock(x, y, z, AIR); }
      P->pos = vec3(8.5f, BY + 1, 8.5f); P->maxHp = 100000; P->hp = 100000; P->dead = false;
      Entity* nl = spawnCreature(*L, REALM_MAIN, "night_lord", vec3(11.5f, BY + 1, 8.5f));
      if (nl) { nl->hp = nl->maxHp * 0.4f; nl->data["phase"] = 2; }
      int hits = 0; float last = P->hp;
      for (int k = 0; k < 1200 && nl && !nl->removed; k++) { P->pos.x = 8.5f; P->pos.z = 8.5f; P->vel.x = P->vel.z = 0; P->invuln = std::max(0.f, P->invuln - (float)TICK_DT); L->tick(TICK_DT); if (P->hp < last) hits++; else if (std::getenv("DQ_NL_DEBUG") && P->hurtTime > 0) printf("nl: hurt but no loss k=%d\n", k); last = P->hp; }
      if (std::getenv("DQ_NL_DEBUG")) printf("nl: level %d, player def %s\n", nl ? nl->mobLevel : -1, P->stats.dump().substr(0, 300).c_str());
      check(nl && hits >= 6, "the Night Lord's second phase lands its blows (" + std::to_string(hits) + " in 60 s)");
    }
    // a dragon placed in a hall (no lair): it keeps to where it was put (it flew off up through the roof)
    {
      clearMobs();
      for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) { wb.setBlock(x, BY, z, B("stone")); for (int y = BY + 1; y < BY + 8; y++) wb.setBlock(x, y, z, AIR); }
      P->pos = vec3(8.5f, BY + 1, 8.5f); P->maxHp = 100000; P->hp = 100000;
      Entity* dg = spawnCreature(*L, REALM_MAIN, "sand_dragon", vec3(14.5f, BY + 2, 8.5f));
      const vec3 at0 = dg ? dg->pos : vec3(0);
      float far = 0, up = 0;
      for (int k = 0; k < 600 && dg && !dg->removed; k++) { P->pos.x = 8.5f; P->pos.z = 8.5f; P->invuln = 0; L->tick(TICK_DT); far = std::max(far, std::hypot(dg->pos.x - at0.x, dg->pos.z - at0.z)); up = std::max(up, dg->pos.y - at0.y); }
      check(dg && up < 26 && far < 50, "a dragon set down in a hall keeps near where it was put (" + std::to_string((int)up) + " up, " + std::to_string((int)far) + " out)");
    }
    // a summoner keeps its servants few
    clearMobs();
    for (int y = BY + 1; y < BY + 8; y++) for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) wb.setBlock(x, y, z, AIR);
    P->pos = vec3(8.5f, BY + 1, 8.5f); P->hp = P->maxHp;
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 30 && !wb.isLoaded(15, BY + 1, 8);) { wb.stream({{P->pos, 2, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) wb.setBlock(x, BY, z, B("stone"));
    spawnCreature(*L, REALM_MAIN, "rot_king", vec3(15.5f, BY + 1, 8.5f));
    for (int k = 0; k < 1200; k++) { P->pos.x = 8.5f; P->pos.z = 8.5f; L->tick(TICK_DT); }
    int minions = 0; for (auto& e : L->entities()) if (!e->removed && e->type == "ghoul") minions++;
    check(minions >= 1 && minions <= 4, "the Rot King calls ghouls, but never more than a few (" + std::to_string(minions) + ")");
    clearMobs();
    P->mode = saveMode; P->maxHp = 20; P->hp = 20;
    for (int y = BY + 1; y < BY + 8; y++) for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) wb.setBlock(x, y, z, AIR);
    P->pos = vec3(8.5f, BY + 1, 8.5f);
  }

  printf("[treasure maps]\n");
  {
    World& wt = L->world();
    for (auto& q : P->inv) q = ItemStack();
    P->inv[0] = ItemStack("treasure_map", 1); P->selected = 0;
    json opened;
    const int hScr = bus.on("contentScreen", [&](const json& a) { if (a.is_array() && a.size() >= 3 && a[1] == "treasure_map") opened = a[2]; return true; });
    const bool used = !use("treasure_map");
    const ItemStack& tm = P->inv[0];
    const bool hasX = tm.tag.is_object() && tm.tag.contains("tx") && tm.tag.contains("tz");
    const int tx = hasX ? tm.tag["tx"].get<int>() : 0, tz = hasX ? tm.tag["tz"].get<int>() : 0;
    const float dist = std::hypot(tx - P->pos.x, tz - P->pos.z);
    check(used && hasX && dist > 150 && dist < 1000, "using a treasure map marks its X some way off (" + std::to_string((int)dist) + " blocks)");
    ColumnSample cs; if (DimGenerator* g = L->gen->forDim(DIM_OVERWORLD)) g->sample(tx, tz, cs);
    check(cs.water < 0 && cs.height > 0, "the X is on dry land");
    check(opened.is_object() && opened.value("x", 1 << 30) == tx && opened.value("z", 1 << 30) == tz && opened.value("slot", -1) == 0,
          "using it opens the treasure map's screen with its X");
    {   // the picture: the land round the X, centred on it, painted (the generator where nothing's loaded), its X marked
      const double t1 = nowSeconds();
      PaperMap* pic = treasureMapPicture(*L, tx, tz);
      const double ms = (nowSeconds() - t1) * 1000;
      const bool centred = pic && pic->x0 + pic->span() / 2 == tx && pic->z0 + pic->span() / 2 == tz && pic->scale == TREASURE_MAP_SCALE;
      check(centred && ms < 30, "its picture is the land round the X; asking for it doesn't hold the game up (" + std::to_string((int)ms) + " ms)");
      double worst = 0, waited = 0;
      for (const double t2 = nowSeconds(); pic && pic->explored() < 0.95f && nowSeconds() - t2 < 20;) {
        const double t3 = nowSeconds(); treasureMapPicture(*L, tx, tz); worst = std::max(worst, nowSeconds() - t3);
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); waited = nowSeconds() - t2;
      }
      // (done: a sample straight from the generator where nothing's loaded)
      bool exact = pic != nullptr;
      if (pic) for (int k = 0; k < 64; k++) {
        const int px = (k * 37 + 5) % MAP_N, pz = (k * 53 + 3) % MAP_N, s = 1 << pic->scale;
        const int bx = pic->x0 + px * s + (s >> 1), bz = pic->z0 + pz * s + (s >> 1);
        if (L->world().chunk(DIM_OVERWORLD, bx >> 4, bz >> 4)) continue;
        ColumnSample c; L->gen->forDim(DIM_OVERWORLD)->sample(bx, bz, c);
        const int hh = pic->h[pz * MAP_N + px] - 1;
        if (hh != c.height && hh != c.water && hh != c.treeTop) exact = false;
      }
      check(pic && pic->explored() > 0.95f && exact && worst < 0.03, "drawn on a worker, then laid in: the whole land filled in as the generator makes it (" + std::to_string((int)(waited * 1000)) + " ms; worst call " + std::to_string((int)(worst * 1000)) + " ms)");
      bool xMark = false; if (pic) for (auto& k : mapMarkers(*L, *pic, P)) if (k.cell == 16 && std::fabs(k.u - MAP_N / 2.f) < 1 && std::fabs(k.v - MAP_N / 2.f) < 1) xMark = true;
      check(xMark && treasureMapPicture(*L, tx, tz) == pic && !mapStore(*L).dirty.count(pic ? pic->id : ""), "with a red X in the middle; made once, never saved");
      ItemStack held = P->inv[0];
      check(isReadableMapStack(held) && readableMapFor(*L, *P, held) == pic && !isReadableMapStack(ItemStack("treasure_map", 1)), "held, it's read as a map (its picture)");
    }
    {   // the hint: which way from where it's read, and how far
      struct W { float px, pz; const char* dir; } ways[] = {{0, 500, "north"}, {-400, 0, "east"}, {300, 300, "north-west"}, {-300, -300, "south-east"}, {0, -250, "south"}, {350, 0, "west"}};
      bool all = true; std::string got;
      for (auto& w2 : ways) {
        const std::string h = treasureHint(*L, 0, 0, w2.px, w2.pz);
        if (h.rfind(std::string("Buried ") + w2.dir + " of here", 0) != 0) { all = false; got = h; }
      }
      check(all, "the hint gives the way to the X from where it's read (N, E, NW, SE, S, W)" + (got.empty() ? std::string() : " -- got: " + got));
      const double th = nowSeconds();
      const std::string h = treasureHint(*L, 0, 0, 0, 300);
      const int hms = (int)((nowSeconds() - th) * 1000);
      check(h.find("about 300 blocks") != std::string::npos && treasureHint(*L, 0, 0, 1, 1).find("right here") != std::string::npos && hms < 50, "and how far (" + h + "; " + std::to_string(hms) + " ms)");
    }
    bus.off(hScr);
    // the land at the X loaded: a chest is buried there, with loot
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 60 && !wt.isLoaded(tx, dimBase(DIM_OVERWORLD) + 100, tz);) { wt.stream({{vec3(tx, dimBase(DIM_OVERWORLD) + 100, tz), 1, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    tick(3);
    int chestY = -1, loot = 0;
    for (int y = dimBase(DIM_OVERWORLD) + 255; y > dimBase(DIM_OVERWORLD); y--) if (reg().block(wt.getBlock(tx, y, tz)).key == "chest") {
      chestY = y; if (const json* e = wt.blockEntity(tx, y, tz); e && e->contains("items")) for (auto& it : (*e)["items"]) if (!it.is_null()) loot++;
      break;
    }
    const int top = wt.surfaceY(tx, tz, DIM_OVERWORLD);
    check(chestY > 0 && loot >= 3 && top > chestY, "a chest of loot lies buried under the X (" + std::to_string(loot) + " stacks, " + std::to_string(top - chestY) + " down)");
    const bool again = !use("treasure_map");
    check(again && P->inv[0].tag.value("tx", 0) == tx, "using it again shows the same X");
    // an item like any other: used on a chest or crouched it's not opened (the chest, the ground take it: tools/test_gameplay.cpp)
    {
      int opens = 0;
      const int h2 = bus.on("contentScreen", [&](const json& a) { if (a.is_array() && a.size() >= 3 && a[1] == "treasure_map") opens++; return true; });
      const ivec3 ch(4, Y + 1, 4);
      for (double t0 = nowSeconds(); nowSeconds() - t0 < 30 && !wt.isLoaded(ch.x, ch.y, ch.z);) { wt.stream({{vec3(ch), 1, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
      wt.setBlock(ch.x, ch.y, ch.z, B("chest"));
      use("treasure_map", ch, 2);
      const int onChest = opens;
      wt.setBlock(ch.x, ch.y, ch.z, AIR);
      P->sneaking = true; use("treasure_map", ivec3(4, Y, 4), 2); P->sneaking = false;
      const int crouched = opens - onChest;
      use("treasure_map", ivec3(4, Y, 4), 2);
      check(onChest == 0 && crouched == 0 && opens == 1, "used on a chest, or crouched, the map isn't opened (the chest or the ground takes it); at the ground it is (" + std::to_string(onChest) + std::to_string(crouched) + std::to_string(opens) + ")");
      bus.off(h2);
    }
    for (auto& q : P->inv) q = ItemStack();
  }

  printf("[crafting from the chests beside the table]\n");
  {
    World& wc = L->world();
    const int BY2 = dimBase(DIM_OVERWORLD) + 200;
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 30 && !wc.isLoaded(4, BY2 + 1, 4);) { wc.stream({{vec3(4, BY2 + 1, 4), 1, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    const ivec3 tb(4, BY2 + 1, 4), ch = tb + ivec3(1, 0, 0);
    wc.setBlock(tb.x, tb.y, tb.z, B("crafting_table")); wc.setBlock(ch.x, ch.y, ch.z, B("chest"));
    json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr); items[5] = ItemStack("planks", 10).toJson();
    wc.setBlockEntity(ch.x, ch.y, ch.z, json{{"type", "chest"}, {"items", items}});
    for (auto& q : P->inv) q = ItemStack();
    InvOpen o; o.mode = "table"; o.hasPos = true; o.pos = tb;
    InvSession S(*P, &wc, o, false);
    int ri = -1; for (int i = 0; i < (int)reg().recipes.size(); i++) { const Recipe& r = reg().recipes[i]; if (r.out == "crafting_table" && !r.shaped() == false) { ri = i; break; } }
    if (ri < 0) for (int i = 0; i < (int)reg().recipes.size(); i++) if (reg().recipes[i].out == "crafting_table") { ri = i; break; }
    check(ri >= 0 && canCraft(S.bags(), reg().recipes[ri]), "the recipe book counts the planks in the chest beside the table");
    S.bookPick(ri, false, true);
    int onGrid = 0; for (auto& g : S.grid) if (g.id == "planks") onGrid += g.n;
    int left = 0; for (auto& it : (*wc.blockEntity(ch.x, ch.y, ch.z))["items"]) if (!it.is_null()) left += ItemStack::fromJson(it).n;
    check(onGrid == 4 && left == 6, "picking it lays 4 planks out of the chest on the grid (" + std::to_string(onGrid) + " laid, " + std::to_string(left) + " left)");
    for (auto& g : S.grid) g.clear();
    // the far half of a double chest is not beside the table; the near half is empty
    const ivec3 far = ch + ivec3(1, 0, 0);
    json empty = json::array(); for (int i = 0; i < 27; i++) empty.push_back(nullptr);
    json farItems = empty; farItems[2] = ItemStack("planks", 8).toJson();
    wc.setBlock(ch.x, ch.y, ch.z, B("chest"), 0);
    wc.setBlock(far.x, far.y, far.z, B("chest"), 0);
    wc.setBlockEntity(ch.x, ch.y, ch.z, json{{"type", "chest"}, {"items", empty}});
    wc.setBlockEntity(far.x, far.y, far.z, json{{"type", "chest"}, {"items", farItems}});
    InvSession D(*P, &wc, o, false);
    check(canCraft(D.bags(), reg().recipes[ri]), "the far half of a double chest counts too");
    D.bookPick(ri, false, true);
    int fromFar = 0; for (auto& g : D.grid) if (g.id == "planks") fromFar += g.n;
    int farLeft = 0; for (auto& it : (*wc.blockEntity(far.x, far.y, far.z))["items"]) if (!it.is_null()) farLeft += ItemStack::fromJson(it).n;
    int nearLeft = 0; for (auto& it : (*wc.blockEntity(ch.x, ch.y, ch.z))["items"]) if (!it.is_null()) nearLeft += ItemStack::fromJson(it).n;
    check(fromFar == 4 && farLeft == 4 && nearLeft == 0, "picking it takes the planks from the far half (" + std::to_string(fromFar) + " laid, " + std::to_string(farLeft) + " left)");
    for (auto& g : D.grid) g.clear();
    wc.setBlock(far.x, far.y, far.z, AIR);
    wc.setBlock(ch.x, ch.y, ch.z, AIR); wc.setBlock(tb.x, tb.y, tb.z, AIR);
  }

  printf("[stacking]\n");
  {
    auto ms = [](const char* k) { const ItemDef* d = reg().item(k); return d ? d->maxStack : -1; };
    check(ms("chest") == 64 && ms("barrel") == 64 && ms("oak_door") == 64 && ms("white_bed") == 64 && ms("cake") == 64, "chests, barrels, doors, beds and cakes stack to 64");
    check(ms("minecart") == 64 && ms("compass") == 64 && ms("tome_firebolt") == 64 && ms("music_disc_ember") == 64, "minecarts, compasses, tomes and discs stack");
    check(ms("diamond_sword") == 1 && ms("iron_chestplate") == 1 && ms("leather_backpack") == 1 && ms("water_bucket") == 1 && ms("mushroom_stew") == 1 && ms("elytra") == 1, "worn-out things, carried containers, filled buckets and stews stay single");
    ItemStack full("chest", 1); full.tag = json{{"c", json::array({ItemStack("dirt", 3).toJson()})}};
    check(!ItemStack("chest", 1).canMerge(full) && ItemStack("chest", 1).canMerge(ItemStack("chest", 5)), "a chest carried with things in it never merges with an empty one");
  }

  printf("[every item has a use]\n");
  {
    const auto dead = deadEndItems();
    std::string list; for (auto& k : dead) list += " " + k;
    check(dead.empty(), "no dead-end items (placed, worn, eaten, an ingredient, a station's input, a creature's food, melted, or handled by the sim)" + (dead.empty() ? std::string() : ":" + list));
  }

  printf("[the War Horn]\n");
  {
    auto hasRecipe = [](const char* out, const char* in) {
      for (auto& r : reg().recipes) if (r.out == out) { for (auto& k : r.items) if (k == in) return true; for (auto& [c, k] : r.key) if (k == in) return true; }
      return false;
    };
    check(reg().item("war_horn") && hasRecipe("war_horn", "minotaur_horn"), "the Minotaur's horn is made into a War Horn");
    for (auto& q : P->inv) q = ItemStack();
    Entity* we = spawnCreature(*L, "wolf", P->realm, P->pos + vec3(20, 0.5f, 0));
    auto* wolf = dynamic_cast<Creature*>(we);
    if (wolf) { wolf->tamed = true; wolf->owner = P->name; wolf->data["owner"] = P->name; wolf->data["tamed"] = true; }
    int called = 0; std::string why;
    P->stats.erase("warHornAt");
    check(warHornBlow(*L, *P, called, why) && called == 1, "blowing it: one companion answers (" + std::to_string(called) + ")");
    check(wolf && glm::distance(wolf->pos, P->pos) < 4 && rallied(*L, *wolf), "blowing it brings your tamed wolf to you, under its call");
    float k = wolf ? pathPetFactor(*L, *wolf, nullptr) : 0;   // (a blow on it: not stronger)
    Entity* zz = spawnCreature(*L, "zombie", P->realm, P->pos + vec3(3, 0.5f, 0));
    float out = zz && wolf ? pathPetFactor(*L, *static_cast<Creature*>(zz), wolf) : 0;
    check(k == 1.f && std::abs(out - RALLY_DAMAGE) < 1e-4f, "...and its blows are stronger while it lasts (x" + std::to_string(out) + ")");
    int n = 0;
    check(!warHornBlow(*L, *P, n, why) && why.find("breath") != std::string::npos, "a second blow at once waits for your breath (" + why + ")");
    if (zz) zz->removed = true;
    if (wolf) wolf->removed = true;
    for (auto& q : P->inv) q = ItemStack();
  }

  printf("[the dragons' own scales and armour]\n");
  {
    int own = 0;
    for (const char* el : {"verdant", "sand", "frost", "bog", "ember", "storm", "tide"}) {
      const json& M = reg().mobs[std::string(el) + "_dragon"];
      const std::string scale = std::string(el) + "_dragon_scale";
      bool drops = false; for (auto& d : M["drops"]) if (d[0] == scale) drops = true;
      if (drops && reg().item(scale) && reg().item(std::string(el) + "scale_chestplate")) own++;
    }
    check(own == 7, "each of the seven dragons drops its own scale, with an armour set of its own (" + std::to_string(own) + ")");
    StationResult r = stationResult("smithing", ItemStack("dragonscale_chestplate", 1), ItemStack("frost_dragon_scale", 4));
    check(r.ok && r.out.id == "frostscale_chestplate" && r.useB == 4, "at the smithing table, four frost scales make a dragonscale chestplate Frost Scale");
    check(!stationResult("smithing", ItemStack("dragonscale_chestplate", 1), ItemStack("frost_dragon_scale", 3)).ok, "...three are not enough");
    bool back = false; for (auto& rc : reg().recipes) if (rc.out == "dragon_scale" && rc.items.size() == 1 && rc.items[0] == "tide_dragon_scale") back = true;
    check(back, "a dragon's own scale still crafts into a plain Dragon Scale (the old recipes)");
    for (int i = 0; i < 4; i++) P->armor[i] = ItemStack(std::string("frostscale_") + ARMOR_PIECE[i], 1);
    DamageSource frost; frost.element = "frost";
    check(gearSet(*P) == "frostscale" && gearHarmFactor(*P, frost) < 0.25f, "the whole Frost Scale set: frost hurts a fifth");
    Entity* z = spawnCreature(*L, "zombie", P->realm, P->pos + vec3(1.5f, 0, 0));
    DamageSource blow; blow.type = "mob"; blow.attacker = z ? z->id : 0;
    P->invuln = 0; P->hp = P->maxHp;
    if (z) damagePlayer(*L, *P, 4, blow);
    check(z && mobStatus(*z, "slowT") > 0, "...and what strikes you is slowed");
    if (z) z->removed = true;
    P->armor[3] = ItemStack("dragonscale_boots", 1);
    check(gearSet(*P).empty(), "a mixed set has no set bonus");
    for (int i = 0; i < 4; i++) P->armor[i] = ItemStack(std::string("tidescale_") + ARMOR_PIECE[i], 1);
    P->air = 1; tick(0.2);
    check(P->air >= P->maxAir - 0.01f, "the Tide Scale set: your breath never runs out");
    for (auto& a : P->armor) a = ItemStack();
  }

  printf("[barley, oats and rye; the capes]\n");
  {
    auto made = [](const char* out, const char* in) { for (auto& r : reg().recipes) if (r.out == out) { for (auto& k : r.items) if (k == in) return true; for (auto& [c, k] : r.key) if (k == in) return true; } return false; };
    check(made("rye_bread", "rye") && made("barley_bread", "barley") && made("porridge", "oat") && made("oatcake", "oat"), "rye bread, barley bread, porridge and oatcakes");
    const json barrel = readJson(dataDir() / "barrel.json").value_or(json());
    bool ale = false, mead = false; for (auto& r : barrel["recipes"]) { if (r.value("item", "") == "barley" && r.value("out", "") == "ale_mug") ale = true; if (r.value("item", "") == "honey_bottle" && r.value("out", "") == "mead") mead = true; }
    check(ale && mead, "barley brews into ale and honey into mead, sealed in a barrel with water");
    auto eats = [](const char* mob, const char* f) { const json& m = reg().mobs[mob]; for (auto& x : m["food"]) if (x == f) return true; return false; };
    check(eats("cow", "oat") && eats("sheep", "barley") && eats("horse", "oat") && eats("chicken", "rye"), "farm animals eat the grains (breeding, the feeding trough)");
    for (auto& c : P->cape) c = ItemStack();
    const float bare = fireResist(*P);
    P->cape[0] = ItemStack("ember_cape", 1);
    check(fireResist(*P) > bare + 0.15f && reg().item("ember_cape")->str("desc").find("fire") != std::string::npos, "the ember cape takes a fifth off fire, and says so");
    P->cape[0] = ItemStack("frost_cape", 1);
    check(gearHarmFactor(*P, DamageSource{"", "frost"}) < 0.75f, "the frost cape: frost hurts less");
    P->cape[0] = ItemStack("royal_cape", 1);
    check(capeShopFactor(*P) < 1, "the royal cape: the shops ask less");
    P->cape[0] = ItemStack();
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
