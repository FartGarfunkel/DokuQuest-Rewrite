// tools/test_content.cpp — headless checks of the content features (sim only): paper maps (records, exploring,
// markers, pins, crafting, saving), fishing, campfires / rope / the grappling hook / the sleeping bag, waystones
// and mounts, elytra, the stable, finding places, the watering can and rice.
//   test_content [--userdir DIR]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/maps.h"
#include "sim/fishing.h"
#include "sim/camping.h"
#include "sim/travel.h"
#include "sim/elytra.h"
#include "sim/stable.h"
#include "sim/places.h"
#include "sim/farming.h"
#include <thread>
#include <cstdio>
using namespace dq;

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
      check(P->held().d == 1, "the rod wears a little");
    }
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
    check(P->glideBoost > 2 && P->held().n == 1, "a rocket in flight is a boost");
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
  lvp->close();
  printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
