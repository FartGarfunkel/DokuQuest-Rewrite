// tools/test_life.cpp — headless checks of the living world (sim/world_life.h): lightning charging a fuseling and
// turning a pig, the enderman lifting, carrying and setting down blocks (never a player's), mushrooms spreading in the
// dark up to their cap and withering in bright light, bone meal growing a huge mushroom, vines creeping and hanging.
//   test_life [--userdir DIR]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/weather.h"
#include "sim/world_life.h"
#include "sim/skills.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include "world/world.h"
#include <thread>
#include <cstdio>
#include <algorithm>
using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }
static Level* L; static Player* P; static EventBus bus; static LevelFx fx;
static BlockId B(const char* k) { return reg().blockId(k); }
static void tick(double secs) { for (double t = 0; t < secs; t += TICK_DT) L->tick(TICK_DT); }
static int count(const std::string& type) { int n = 0; for (auto& e : L->entities()) if (!e->removed && e->type == type) n++; return n; }
static Creature* first(const std::string& type) { for (auto& e : L->entities()) if (!e->removed && e->type == type) return dynamic_cast<Creature*>(e.get()); return nullptr; }
static int countBox(World& w, BlockId id, ivec3 a, ivec3 b) { int n = 0; for (int y = a.y; y <= b.y; y++) for (int z = a.z; z <= b.z; z++) for (int x = a.x; x <= b.x; x++) if (w.getBlock(x, y, z) == id) n++; return n; }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-life").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(2);
  LevelInfo li; li.name = "LifeTest"; li.seedText = "life";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  L = lvp.get(); L->fx = &fx;
  L->info.rules["fireSpread"] = false; L->info.rules["mobSpawning"] = false;
  World& w = L->world();
  const int Y = dimBase(DIM_OVERWORLD) + 200;   // a test floor high in the sky
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
  const BlockId stone = B("stone"), grass = B("grass"), myc = B("mycelium"), brownM = B("brown_mushroom"), redM = B("red_mushroom"), vine = B("vine");
  const BlockId stem = B("mushroom_stem"), redCap = B("red_mushroom_block");
  for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) w.setBlock(x, Y, z, stone);
  P->onGround = true;
  WeatherSystem* W = weatherOf(*L);
  check(W != nullptr, "the weather runs");

  // ------------------------------------------------------------ lightning
  if (W) {
    Creature* f = spawnCreature(*L, REALM_MAIN, "fuseling", vec3(20.5f, Y + 1, 0.5f));
    f->calm = true;
    W->strike(*L, 21, Y + 1, 0);
    check(fuselingCharged(*f) && chargedBlastScale(*f) == 2.f, "a bolt by a fuseling charges it (twice the blast)");
    check(f->save()["data"].value("charged", false), "  ...and the charge is saved with it");
    Creature* pig = spawnCreature(*L, REALM_MAIN, "pig", vec3(20.5f, Y + 1, 10.5f));
    pig->petName = "Bacon"; pig->calm = true;
    const int pm0 = count("zombie_pigman");
    W->strike(*L, 21, Y + 1, 10);
    Creature* pm = first("zombie_pigman");
    check(pig->removed && count("zombie_pigman") == pm0 + 1 && pm && pm->petName == "Bacon" && glm::distance(pm->pos, vec3(20.5f, Y + 1, 10.5f)) < 0.1f,
          "a bolt by a pig turns it into a zombie pigman (its name and place kept)");
    W->strike(*L, 21, Y + 1, 10);
    check(count("zombie_pigman") == pm0 + 1 && count("pig") == 0, "  ...and a second bolt changes nothing more");
    Creature* vil = spawnCreature(*L, REALM_MAIN, "villager", vec3(20.5f, Y + 1, 18.5f));
    if (vil) {
      vil->calm = true;
      vil->data["npc"] = {{"v", "tvx"}, {"i", 2}, {"n", "Ada"}, {"j", "farmer"}};
      L->info.data["villages"]["tvx"] = {{"dead", json::object()}, {"couples", {{"2", 5}, {"5", 2}}}};
      W->strike(*L, 21, Y + 1, 18);
      Creature* wi = first("witch");
      check(vil->removed && count("witch") == 1 && wi && wi->petName == "Ada", "a bolt by a villager makes a witch (its name kept)");
      const json& rec = L->info.data["villages"]["tvx"];
      check(rec["dead"].contains("2") && rec["couples"].empty() && !wi->data.contains("npc"), "  ...its village counts it gone, its partner single again");
    }
  }
  // the charged blast is bigger: two fuselings go off by you, one charged; count the floor each takes
  {
    auto blast = [&](vec3 at, bool charged) {
      for (int x = -6; x <= 6; x++) for (int z = -6; z <= 6; z++) w.setBlock(ifloor(at.x) + x, Y, ifloor(at.z) + z, stone);
      Creature* f = spawnCreature(*L, REALM_MAIN, "fuseling", at);
      if (charged) f->data["charged"] = true;
      P->pos = P->prevPos = at + vec3(1.6f, 0, 0); P->vel = vec3(0); P->hp = P->maxHp = 1000;
      for (int i = 0; i < 400 && !f->removed; i++) { P->pos = at + vec3(1.6f, 0, 0); P->vel = vec3(0); tick(TICK_DT); }
      int gone = 0; for (int x = -8; x <= 8; x++) for (int z = -8; z <= 8; z++) if (w.getBlock(ifloor(at.x) + x, Y, ifloor(at.z) + z) == AIR) gone++;
      for (int x = -8; x <= 8; x++) for (int z = -8; z <= 8; z++) w.setBlock(ifloor(at.x) + x, Y, ifloor(at.z) + z, stone);
      return f->removed ? gone : -1;
    };
    int plain = blast(vec3(-4.5f, Y + 1, 2.5f), false), big = blast(vec3(-4.5f, Y + 1, 18.5f), true);
    check(plain > 0 && big > plain * 2, "a charged fuseling's blast is far bigger (" + std::to_string(plain) + " vs " + std::to_string(big) + " floor blocks)");
    P->pos = P->prevPos = vec3(8.5f, Y + 1, 8.5f); P->hp = P->maxHp = 20;
  }

  // ------------------------------------------------------------ the enderman's block
  {
    for (int x = 0; x <= 4; x++) for (int z = 20; z <= 24; z++) w.setBlock(x, Y, z, grass);
    for (int x = 10; x <= 14; x++) for (int z = 20; z <= 24; z++) { w.setBlock(x, Y, z, grass); skillNotePlaced(*L, REALM_MAIN, x, Y, z); }
    Creature* e = spawnCreature(*L, REALM_MAIN, "enderman", vec3(2.5f, Y + 1, 22.5f));
    e->calm = true;
    bool took = false;
    for (int i = 0; i < 2000 && !took; i++) { e->pos = vec3(2.5f, Y + 1, 22.5f); took = enderTryLift(*L, *e); }
    check(took && carriedBlock(*e) == "grass" && countBox(w, grass, ivec3(0, Y, 20), ivec3(4, Y, 24)) == 24, "the enderman lifts a natural block near it");
    check(e->save()["data"].value("carry", std::string()) == "grass", "  ...carries it (kept in its saved data)");
    bool put = false;
    for (int i = 0; i < 4000 && !put; i++) { e->pos = vec3(2.5f, Y + 1, 22.5f); put = enderTrySetDown(*L, *e); }
    check(put && carriedBlock(*e).empty() && countBox(w, grass, ivec3(-2, Y - 1, 18), ivec3(6, Y + 3, 26)) == 25, "  ...and sets it down somewhere it fits");
    Creature* e2 = spawnCreature(*L, REALM_MAIN, "enderman", vec3(12.5f, Y + 1, 22.5f));
    e2->calm = true;
    bool stole = false;
    for (int i = 0; i < 3000; i++) { e2->pos = vec3(12.5f, Y + 1, 22.5f); if (enderTryLift(*L, *e2) && carriedBlock(*e2) == "grass") stole = true; }
    check(!stole && countBox(w, grass, ivec3(10, Y, 20), ivec3(14, Y, 24)) == 25, "it never takes a block a player put down");
    // the record is kept in the chunk and saved with it
    {
      Chunk* c = w.chunk(DIM_OVERWORLD, 0, 1);
      std::vector<u8> bytes = c ? encodeChunk(*c) : std::vector<u8>();
      Chunk back(DIM_OVERWORLD, 0, 1);
      const bool ok = c && decodeChunk(back, bytes.data(), bytes.size());
      bool all = ok; for (int x = 10; x <= 14; x++) for (int z = 20; z <= 24; z++) if (!back.placed.count((u16)colIdx(x & 15, Y & 255, z & 15))) all = false;
      check(all && back.placed.size() == c->placed.size(), "the player-placed record survives saving and loading the chunk (" + std::to_string(back.placed.size()) + " cells)");
      w.setBlock(14, Y, 24, AIR);
      check(!w.placedAt(14, Y, 24) && w.placedAt(13, Y, 24), "  ...and forgets a placed block once it's broken");
      w.setBlock(14, Y, 24, grass); skillNotePlaced(*L, REALM_MAIN, 14, Y, 24);
      c->placed.clear();   // an old save: no record at all
      Chunk old(DIM_OVERWORLD, 0, 1); std::vector<u8> ob = encodeChunk(*c);
      check(decodeChunk(old, ob.data(), ob.size()) && old.placed.empty(), "  ...a chunk saved without one loads with none");
      for (int x = 10; x <= 14; x++) for (int z = 20; z <= 24; z++) skillNotePlaced(*L, REALM_MAIN, x, Y, z);
    }
    // a world with no record: a block within two of anything made (planks) is left alone, one further off may go
    {
      const BlockId planks = B("planks") ? B("planks") : B("oak_planks");
      for (int x = 18; x <= 26; x++) for (int z = 19; z <= 27; z++) w.setBlock(x, Y, z, grass);
      w.setBlock(22, Y + 1, 25, planks);
      Creature* e3 = spawnCreature(*L, REALM_MAIN, "enderman", vec3(22.5f, Y + 1, 23.5f));
      e3->calm = true;
      int near = 0, far = 0;
      for (int i = 0; i < 4000; i++) {
        e3->pos = vec3(22.5f, Y + 1, 23.5f);
        const int before = countBox(w, grass, ivec3(18, Y, 19), ivec3(26, Y, 27));
        if (!enderTryLift(*L, *e3)) continue;
        for (int x = 18; x <= 26; x++) for (int z = 19; z <= 27; z++) if (w.getBlock(x, Y, z) == AIR) {
          (std::max(std::abs(x - 22), std::abs(z - 25)) <= 2 ? near : far)++;
          w.setBlock(x, Y, z, grass);
        }
        e3->data.erase("carry"); e3->data.erase("carryMeta");
        (void)before;
      }
      check(planks && near == 0 && far > 0, "with no record, it leaves blocks next to planks alone (" + std::to_string(far) + " taken further off)");
      e3->removed = true;
    }
    L->info.rules["mobGriefing"] = false;
    bool grief = false; for (int i = 0; i < 2000; i++) { e->pos = vec3(2.5f, Y + 1, 22.5f); if (enderTryLift(*L, *e)) grief = true; }
    check(!grief, "with creatures breaking blocks turned off, it leaves them be");
    L->info.rules["mobGriefing"] = true;
    // killed while carrying: the block falls
    e2->data["carry"] = "sand";
    int items0 = 0; for (auto& q : L->entities()) if (!q->removed && q->type == "item") items0++;
    DamageSrc s; s.type = "mob"; e2->hurt(*L, 1000, s);
    int sand = 0; for (auto& q : L->entities()) if (!q->removed && q->type == "item" && q->data.value("stack", json::object()).value("id", "") == "sand") sand++;
    check(sand == 1, "killed, it drops what it carried");
    // its own brain, left to itself on the grass a while, picks something up
    for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) if (w.getBlock(x, Y, z) == stone) w.setBlock(x, Y, z, grass);   // (a meadow to wander)
    e->calm = false;
    bool brain = false;
    for (int i = 0; i < 1200 && !brain && !e->removed; i++) {
      tick(0.5);
      if (!carriedBlock(*e).empty()) brain = true;
      // (it teleports now and then: off the meadow it has nothing it may lift, and the check waited in vain)
      if (e->pos.x < -10 || e->pos.x > 26 || e->pos.z < -10 || e->pos.z > 26 || std::fabs(e->pos.y - (Y + 1)) > 3) { e->pos = vec3(8.5f, Y + 1, 8.5f); e->vel = vec3(0); }
    }
    check(brain, "its own brain lifts a block now and then");
    // carrying, both hands are held out before it (its front is -z at yaw 0), up off its hips
    if (const RigModel* RM = rigModel(e->modelKey())) {
      e->data["carry"] = "grass"; e->yaw = 0; e->walkAmt = 0;
      rigPose(*e, *RM, creatureBase(*e, true));
      auto tip = [&](const char* n) { int i = RM->find(n); return i >= 0 && rigPartPosed(*e, i) ? vec3(e->mats[i] * vec4(0, RM->parts[i].min.y, 0, 1)) : vec3(0); };
      auto joint = [&](const char* n) { int i = RM->find(n); return i >= 0 && rigPartPosed(*e, i) ? vec3(e->mats[i] * vec4(0, 0, 0, 1)) : vec3(0); };
      const vec3 sh = joint("armR"), hr = tip("foreR"), hl = tip("foreL");
      printf("     shoulder %.2f %.2f %.2f  hands %.2f %.2f %.2f / %.2f %.2f %.2f\n", sh.x, sh.y, sh.z, hr.x, hr.y, hr.z, hl.x, hl.y, hl.z);
      check(hr.z < sh.z - 0.5f && hl.z < sh.z - 0.5f && hr.y > 0.9f, "carrying, its hands are held out in front of it");
    }
    e->removed = true;
  }

  // ------------------------------------------------------------ mushrooms
  {
    // a sealed dark room, 5 x 5 inside
    for (int x = -11; x <= -5; x++) for (int z = -11; z <= -5; z++) for (int y = Y + 1; y <= Y + 4; y++) {
      const bool wall = x == -11 || x == -5 || z == -11 || z == -5 || y == Y + 4;
      w.setBlock(x, y, z, wall ? stone : AIR);
    }
    // (the light engine does a few milliseconds of its work a call: wait till the room's light has settled)
    auto litAt = [&] { const u8 l = w.getLight(-8, Y + 1, -8); return std::max(l >> 4, l & 15); };
    for (int k = 0; k < 30 && litAt() > 12; k++) tick(1);
    const int lit = litAt();
    check(lit <= 12, "the room is dark (light " + std::to_string(lit) + ")");
    w.setBlock(-8, Y + 1, -8, brownM);
    int most = 0;
    for (int it = 0; it < 3000; it++) {
      for (int x = -10; x <= -6; x++) for (int z = -10; z <= -6; z++) for (int y = Y + 1; y <= Y + 3; y++) if (w.getBlock(x, y, z) == brownM) mushroomTick(*L, w, x, y, z, brownM, true);
      most = std::max(most, countBox(w, brownM, ivec3(-10, Y + 1, -10), ivec3(-6, Y + 3, -6)));
    }
    const int n = countBox(w, brownM, ivec3(-10, Y + 1, -10), ivec3(-6, Y + 3, -6));
    check(n == 5 && most == 5, "mushrooms spread in the dark up to the cap, and no further (" + std::to_string(n) + ")");
    // bright light: off stone it withers, on mycelium it stays
    w.setBlock(-10, Y + 1, 4, brownM); w.setBlock(-8, Y, 4, myc); w.setBlock(-8, Y + 1, 4, redM);
    tick(1);
    mushroomTick(*L, w, -10, Y + 1, 4, brownM, true); mushroomTick(*L, w, -8, Y + 1, 4, redM, true);
    check(w.getBlock(-10, Y + 1, 4) == AIR, "a mushroom in bright light off mycelium pops off");
    check(w.getBlock(-8, Y + 1, 4) == redM, "  ...one on mycelium stays in the sun");
    // bone meal: a huge one
    w.setBlock(-6, Y, 14, myc); w.setBlock(-6, Y + 1, 14, redM);
    check(lifeBonemeal(*L, w, -6, Y + 1, 14, redM, true) && w.getBlock(-6, Y + 1, 14) == stem && countBox(w, redCap, ivec3(-9, Y + 1, 11), ivec3(-3, Y + 9, 17)) > 10,
          "bone meal on a mushroom grows a huge one");
    w.setBlock(-6, Y, 4, myc); w.setBlock(-6, Y + 1, 4, brownM); w.setBlock(-6, Y + 3, 4, stone);
    check(!growHugeMushroom(w, -6, Y + 1, 4, brownM) && w.getBlock(-6, Y + 1, 4) == brownM, "  ...but not without room");
  }

  // ------------------------------------------------------------ vines
  {
    // a row: wall behind (z 27), floor below, a ceiling above: it can only creep along
    for (int x = -12; x <= 6; x++) { w.setBlock(x, Y + 1, 27, stone); w.setBlock(x, Y + 2, 26, stone); }
    w.setBlock(-4, Y + 1, 26, vine, 2);
    int most = 0;
    for (int it = 0; it < 3000; it++) {
      for (int x = -12; x <= 7; x++) if (w.getBlock(x, Y + 1, 26) == vine) vineTick(*L, w, x, Y + 1, 26, w.getMeta(x, Y + 1, 26), true);
      most = std::max(most, countBox(w, vine, ivec3(-12, Y + 1, 26), ivec3(8, Y + 1, 27)));
    }
    const int n = countBox(w, vine, ivec3(-12, Y + 1, 26), ivec3(8, Y + 1, 27));
    bool held = true; for (int x = -12; x <= 7; x++) if (w.getBlock(x, Y + 1, 26) == vine && (w.getMeta(x, Y + 1, 26) & 3) != 2) held = false;
    check(n == 5 && most == 5 && held, "a vine creeps along its wall up to the cap (" + std::to_string(n) + ")");
    // hanging: one block of wall high up, the strand grows down so far and no further
    w.setBlock(24, Y + 18, 26, stone); w.setBlock(24, Y + 18, 25, vine, 2);
    for (int it = 0; it < 4000; it++) for (int y = Y + 1; y <= Y + 18; y++) if (w.getBlock(24, y, 25) == vine) vineTick(*L, w, 24, y, 25, w.getMeta(24, y, 25), true);
    int lowest = Y + 18; while (w.getBlock(24, lowest - 1, 25) == vine) lowest--;
    check(lowest < Y + 18 && lowest == Y + 2, "a vine hangs down further, so far below its wall (bottom " + std::to_string(lowest - Y) + ")");
    // bone meal: a few blocks down
    w.setBlock(16, Y + 12, 27, stone); w.setBlock(16, Y + 12, 26, vine, 2);
    check(lifeBonemeal(*L, w, 16, Y + 12, 26, vine) && w.getBlock(16, Y + 11, 26) == vine && (w.getMeta(16, Y + 11, 26) & 3) == 2, "bone meal grows a vine down");
    check(!lifeRandomTick(*L, w, 0, Y, 0, stone, 0), "other blocks aren't ours to tick");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
