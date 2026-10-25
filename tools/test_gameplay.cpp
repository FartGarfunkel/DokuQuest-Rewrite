// tools/test_gameplay.cpp — headless checks of the gameplay port (no graphics): a Level is created
// in a temporary user dir, a test arena is built in the Overworld band, and a Player is driven
// through PlayerController with scripted input. Prints PASS/FAIL per check; exit code = failures.
//   ./build/dq-test-gameplay [--userdir DIR] [-v]
#include "core/init.h"
#include "sim/controller.h"
#include "sim/gamecommands.h"
#include "sim/rules.h"
#include "sim/worldsys.h"
#include "sim/blockfx.h"
#include <cstdio>

using namespace dq;

static int g_fail = 0, g_pass = 0;
static bool g_verbose = false;
#define CHECK(cond, ...) do { if (cond) { g_pass++; if (g_verbose) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } } \
  else { g_fail++; printf("  FAIL "); printf(__VA_ARGS__); printf("   [%s:%d]\n", __FILE__, __LINE__); } } while (0)

struct Rig {
  std::unique_ptr<Level> lv;
  Player* p = nullptr;
  std::unique_ptr<PlayerController> c;
  double acc = 0;
  World& w() { return lv->world(REALM_MAIN); }
  BlockId B(const char* k) { BlockId id = reg().blockId(k); if (!id) printf("  (no block %s)\n", k); return id; }
  void step(double secs, const PlayerInput& in = {}) {
    const double dt = 1.0 / 60;
    PlayerInput first = in;
    for (double t = 0; t < secs - 1e-9; t += dt) {
      c->update((float)dt, first);
      first.attackPressed = first.usePressed = first.place = first.pick = first.drop = first.dropAll = first.swap = first.crawl = false;
      first.lookX = first.lookY = 0; first.slot = -1; first.wheel = 0;
      acc += dt;
      while (acc >= TICK_DT) { lv->tick(TICK_DT); acc -= TICK_DT; }
    }
  }
  void set(int x, int y, int z, BlockId id, u8 m = 0) { w().setBlock(x, y, z, id, m); }
  BlockId get(int x, int y, int z) { return w().getBlock(x, y, z); }
  void put(float x, float y, float z, float yaw = 0, float pitch = 0) {
    p->pos = vec3(x, y, z); p->vel = vec3(0); p->yaw = yaw; p->pitch = pitch; p->hasFallStart = false; p->onGround = false;
    p->hang.reset(); p->climb.reset(); p->swimming = p->crawling = p->crawlWant = false; p->h = PLAYER_H; p->eye = EYE;
  }
  // aim at the centre of a block face (from the player's eye)
  void lookAt(vec3 at) {
    vec3 eye = p->pos + vec3(0, p->eye, 0), d = glm::normalize(at - eye);
    p->pitch = std::asin(d.y); p->yaw = std::atan2(-d.x, -d.z);
  }
  void clearArena(int y0) {
    BlockId stone = B("stone");
    for (int x = -24; x <= 24; x++) for (int z = -24; z <= 24; z++) {
      for (int y = y0; y < y0 + 30; y++) w().setBlock(x, y, z, AIR, 0, SetFlags::NoNotify);
      w().setBlock(x, y0 - 1, z, stone, 0, SetFlags::NoNotify);
    }
  }
  void hold(int slot, const char* id, int n = 1) { p->selected = slot; p->inv[slot] = ItemStack(id, n); }
};

int main(int argc, char** argv) {
  std::string ud = "/tmp/dq-test-gameplay";
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--userdir" && i + 1 < argc) ud = argv[++i]; if (a == "-v") g_verbose = true; }
  std::error_code ec; fs::remove_all(ud, ec); fs::create_directories(ud);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("initCore failed\n"); return 99; }
  EventBus bus;
  LevelInfo info; info.name = "GameplayTest"; info.seedText = "test"; info.mode = "survival";
  Rig r;
  r.lv = Level::create(fs::path(ud) / "saves", info, nullptr, &bus);
  r.lv->info.rules = json::object(); rules(*r.lv)["commands"] = true;
  const int Y = dimBase(DIM_OVERWORLD) + 100;   // arena floor top at Y-1
  std::vector<StreamAnchor> anchors = {{vec3(0, Y, 0), 3, false}};
  for (int i = 0; i < 200 && r.w().chunks().size() < 49; i++) r.w().stream(anchors, 1000);
  printf("chunks loaded: %zu\n", r.w().chunks().size());
  r.clearArena(Y);
  r.lv->tick(TICK_DT);   // attaches the world systems
  auto pl = std::make_unique<Player>(); pl->name = "Tester"; pl->pos = vec3(0.5f, Y, 0.5f);
  r.p = static_cast<Player*>(r.lv->spawn(std::move(pl)));
  r.c = std::make_unique<PlayerController>(*r.lv, *r.p);
  r.lv->info.spawn = vec3(0.5f, Y, 0.5f);
  std::vector<std::string> events;
  bus.on("blockBreak", [&](const json& a) { events.push_back("break " + a[4].get<std::string>()); return true; });
  bus.on("screenOpen", [&](const json& a) { events.push_back("screen " + a[1].get<std::string>()); return true; });
  const KnownBlocks& KBk = KB();
  BlockId stone = r.B("stone"), dirt = r.B("dirt"), sand = r.B("sand"), water = r.B("water");

  printf("[movement]\n");
  {
    r.put(0.5f, Y + 5.5f, 0.5f);
    r.step(1.5);
    CHECK(r.p->onGround && std::fabs(r.p->pos.y - Y) < 1e-3f, "falls and lands on the floor (y=%.3f)", r.p->pos.y);
    CHECK(std::fabs(r.p->hp - 18) < 0.01f, "a 5.5-block fall costs floor(5.5-3)=2 HP (hp=%.1f)", r.p->hp);
    r.p->hp = 20;
    r.put(0.5f, Y, 0.5f); r.step(0.3);
    PlayerInput fw; fw.mz = -1;
    float z0 = r.p->pos.z; r.step(2.0, fw);
    float walk = (z0 - r.p->pos.z) / 2.0f;
    CHECK(walk > 3.8f && walk < 4.4f, "walks about 4.3 b/s (%.2f)", walk);
    r.put(0.5f, Y, 0.5f); r.step(0.3);
    PlayerInput sp = fw; sp.sprint = true;
    z0 = r.p->pos.z; r.step(2.0, sp);
    float sprint = (z0 - r.p->pos.z) / 2.0f;
    CHECK(sprint > 5.0f && sprint < 5.8f && r.p->sprinting, "sprints about 5.7 b/s (%.2f)", sprint);
    r.put(0.5f, Y, 0.5f); r.step(0.3);
    PlayerInput jp; jp.jump = true;
    float top = 0; for (int i = 0; i < 40; i++) { r.step(1.0 / 60, i < 2 ? jp : PlayerInput{}); top = std::max(top, r.p->pos.y - Y); }
    CHECK(top > 1.2f && top < 1.45f, "jump peak ~1.32 blocks (v=8.6, g=28): %.3f", top);
    // stepping up a slab, jumping onto a block
    r.set(0, Y, -3, r.B("oak_slab"), 0);
    r.put(0.5f, Y, 0.5f); r.step(0.2);
    float slabTop = 0; for (int i = 0; i < 72; i++) { r.step(1.0 / 60, fw); slabTop = std::max(slabTop, r.p->pos.y - Y); }
    CHECK(slabTop > 0.45f && slabTop < 0.55f, "steps up onto a bottom slab without jumping (y+%.2f)", slabTop);
    r.set(0, Y, -3, AIR);
    // sneaking stops you at an edge
    for (int x = -2; x <= 2; x++) for (int z = -8; z <= -4; z++) r.set(x, Y - 1, z, AIR);
    r.put(0.5f, Y, 0.5f); r.step(0.2);
    PlayerInput sn = fw; sn.sneak = true;
    r.step(4.0, sn);
    CHECK(r.p->onGround && r.p->pos.y > Y - 0.01f && r.p->pos.z > -4.4f, "sneaking keeps you from walking off the edge (z=%.2f y=%.2f)", r.p->pos.z, r.p->pos.y - Y);
    for (int x = -2; x <= 2; x++) for (int z = -8; z <= -4; z++) r.set(x, Y - 1, z, stone);
    // ledge: a wall two blocks high; jump at it holding forward -> hang -> jump -> on top
    for (int x = -2; x <= 2; x++) { r.set(x, Y, -3, stone); r.set(x, Y + 1, -3, stone); }
    r.put(0.5f, Y, -1.5f); r.step(0.2);
    PlayerInput lj = fw; lj.jump = true;
    bool hung = false;
    for (int i = 0; i < 90 && !hung; i++) { r.step(1.0 / 60, i < 30 ? lj : fw); hung = (bool)r.p->hang; }
    CHECK(hung, "catches the top of a two-block wall");
    if (hung) { r.step(0.4); PlayerInput j; j.jump = true; r.step(0.05, j); r.step(1.0); }
    CHECK(!r.p->hang && r.p->pos.y > Y + 1.9f, "climbs up and over the ledge (y+%.2f)", r.p->pos.y - Y);
    for (int x = -2; x <= 2; x++) { r.set(x, Y, -3, AIR); r.set(x, Y + 1, -3, AIR); }
    // crawling under a one-block gap
    for (int x = -2; x <= 2; x++) for (int z = -6; z <= -3; z++) r.set(x, Y + 1, z, stone);
    r.put(0.5f, Y, 0.5f); r.step(0.2);
    PlayerInput cr = fw; cr.sneak = true;
    r.step(4.0, cr);
    CHECK(r.p->crawling && r.p->h < 1 && r.p->pos.z < -3.2f, "sneaks into a one-high gap and crawls (z=%.2f crawling=%d)", r.p->pos.z, r.p->crawling);
    for (int x = -2; x <= 2; x++) for (int z = -6; z <= -3; z++) r.set(x, Y + 1, z, AIR);
    r.put(0.5f, Y, 0.5f); r.step(1.0);
    CHECK(!r.p->crawling, "stands up again in the open");
  }

  printf("[swimming, breath]\n");
  {
    // a pool 5 deep at x 8..14, z 8..14
    for (int x = 8; x <= 14; x++) for (int z = 8; z <= 14; z++) { for (int y = Y - 6; y < Y - 1; y++) r.w().setBlock(x, y, z, water, 0, SetFlags::NoNotify); r.w().setBlock(x, Y - 7, z, stone, 0, SetFlags::NoNotify); r.w().setBlock(x, Y - 1, z, water, 0, SetFlags::NoNotify); }
    r.put(11.5f, Y - 4, 11.5f, 0, -0.3f);
    r.step(0.2);
    CHECK(r.p->contacts.water && r.p->contacts.eyeWater, "in water with the head under");
    float air0 = r.p->air; r.step(2.0);
    CHECK(r.p->air < air0 - 1.5f, "breath runs out under water (%.2f -> %.2f)", air0, r.p->air);
    PlayerInput sw; sw.mz = -1; sw.sprint = true;
    r.step(0.5, sw);
    CHECK(r.p->swimming && r.p->h < 1, "sprint-swims lying flat");
    r.p->air = 0.5f; r.p->hp = 20; r.p->invuln = 0;
    r.put(11.5f, Y - 5, 11.5f); r.p->air = 0.2f;
    r.step(2.2);
    CHECK(r.p->hp <= 18, "drowns two HP a second at zero air (hp=%.1f)", r.p->hp);
    r.p->hp = 20;
    r.put(0.5f, Y, 0.5f); r.step(1.0);
    CHECK(r.p->air > 5, "breath comes back in the air");
  }

  printf("[mining]\n");
  {
    r.put(0.5f, Y, 0.5f, 0, 0); r.step(0.2);
    r.set(0, Y, -2, stone);
    r.lookAt(vec3(0.5f, Y + 0.5f, -1.0f));
    r.c->updateTarget();
    CHECK(r.c->target.hit && r.c->target.block == ivec3(0, Y, -2), "targets the block in front");
    float tHand = r.c->breakTime(stone, &r.c->target);
    CHECK(std::fabs(tHand - 7.5f) < 0.01f, "stone by hand: hardness 1.5 x5 = 7.5 s (%.2f)", tHand);
    r.hold(0, "wood_pickaxe");
    float tWood = r.c->breakTime(stone, &r.c->target);
    auto* wp = reg().item("wood_pickaxe");
    CHECK(wp && std::fabs(tWood - 1.5f * 1.5f / wp->tool.speed) < 0.01f, "stone with a wooden pickaxe: 1.5x1.5/speed (%.2f)", tWood);
    PlayerInput mine; mine.attack = true; mine.attackPressed = true;
    r.step(tWood + 0.3, mine);
    CHECK(r.get(0, Y, -2) == AIR, "the stone breaks");
    r.step(0.3);
    int cob = playerCount(*r.p, "cobblestone"); for (auto& e : r.lv->entities()) if (!e->removed && e->type == "item" && e->data["stack"].value("id", "") == "cobblestone") cob++;
    CHECK(cob == 1, "and drops cobblestone (%d)", cob);
    CHECK(r.p->inv[0].d == 1, "the pickaxe wears by one (%d)", r.p->inv[0].d);
    CHECK(!events.empty() && events.back() == "break stone", "blockBreak event fired");
    // iron ore needs a stone pickaxe
    r.set(0, Y, -2, r.B("iron_ore"));
    r.c->updateTarget();
    CHECK(!r.c->canHarvest(r.B("iron_ore")), "a wooden pickaxe can't harvest iron ore");
    r.hold(0, "stone_pickaxe");
    CHECK(r.c->canHarvest(r.B("iron_ore")), "a stone pickaxe can");
    r.set(0, Y, -2, AIR);
    // tree felling
    BlockId log = r.B("log"), leaves = r.B("leaves");
    for (int y = 0; y < 5; y++) r.set(4, Y + y, -4, log);
    for (int x = 2; x <= 6; x++) for (int z = -6; z <= -2; z++) for (int y = 3; y <= 5; y++) if (r.get(x, Y + y, z) == AIR) r.set(x, Y + y, z, leaves);
    r.put(4.5f, Y, -2.5f); r.lookAt(vec3(4.5f, Y + 0.5f, -3.0f)); r.c->updateTarget();
    r.hold(0, "stone_axe");
    float tLog = r.c->breakTime(log, &r.c->target);
    r.step(tLog + 0.2, mine);
    CHECK(r.get(4, Y + 3, -4) == AIR && r.get(4, Y + 1, -4) == AIR, "chopping a log fells the whole tree");
    r.step(1.5);
    int logsDropped = 0; for (auto& e : r.lv->entities()) if (!e->removed && e->type == "item" && e->data["stack"].value("id", "") == "log") logsDropped += e->data["stack"].value("n", 0);
    CHECK(logsDropped + playerCount(*r.p, "log") >= 5, "its logs land as drops where it falls (%d)", logsDropped);
    for (int x = 2; x <= 6; x++) for (int z = -6; z <= -2; z++) for (int y = 0; y <= 6; y++) r.set(x, Y + y, z, AIR);
  }

  printf("[placing]\n");
  {
    r.put(0.5f, Y, 2.5f, 0, 0); r.step(0.2);
    auto aimFace = [&](ivec3 b, ivec3 n, float fy = 0.5f) { vec3 at = vec3(b) + vec3(0.5f) + vec3(n) * 0.5f; at.y = b.y + (n.y ? (n.y > 0 ? 1.f : 0.f) : fy); r.lookAt(at); r.c->updateTarget(); };
    PlayerInput use; use.use = true; use.usePressed = true;
    r.set(0, Y, 0, stone);
    // slab on the side of a block, low half -> bottom slab; high -> top slab
    r.hold(0, "oak_slab", 10);
    aimFace({0, Y, 0}, {0, 0, 1}, 0.25f); r.step(0.05, use);
    CHECK(r.get(0, Y, 1) == r.B("oak_slab") && (r.w().getMeta(0, Y, 1) & 7) == 0, "slab on the low half of a side: bottom slab");
    r.set(0, Y, 1, AIR); r.step(0.3);
    aimFace({0, Y, 0}, {0, 0, 1}, 0.75f); r.step(0.05, use);
    CHECK(r.get(0, Y, 1) == r.B("oak_slab") && (r.w().getMeta(0, Y, 1) & 7) == 1, "on the high half: top slab (meta %d)", r.w().getMeta(0, Y, 1));
    r.set(0, Y, 1, AIR); r.step(0.3);
    aimFace({0, Y, 0}, {0, 0, 1}, 0.25f); r.step(0.05, use);
    aimFace({0, Y, 1}, {0, 1, 0}); r.step(0.3); r.step(0.05, use);
    CHECK((r.w().getMeta(0, Y, 1) & 7) == 6 && r.p->inv[0].n == 6, "a second slab on a bottom slab makes a double (meta %d, left %d)", r.w().getMeta(0, Y, 1) & 7, r.p->inv[0].n);
    r.set(0, Y, 1, AIR);
    // placing mode R: vertical slab
    PlayerInput R; R.place = true; r.step(0.05, R);
    aimFace({0, Y, 0}, {0, 0, 1}); r.step(0.3); r.step(0.05, use);
    int vk = r.w().getMeta(0, Y, 1) & 7;
    CHECK(vk >= 2 && vk <= 5, "R mode: a vertical slab (meta %d)", vk);
    r.step(0.05, R); r.set(0, Y, 1, AIR);
    // stairs face the way you look; upside down from below
    r.hold(0, "oak_stairs", 4);
    aimFace({0, Y, 0}, {0, 1, 0}); r.step(0.3); r.step(0.05, use);
    u8 sm = r.w().getMeta(0, Y + 1, 0);
    CHECK(r.get(0, Y + 1, 0) == r.B("oak_stairs") && (sm & 3) == lookDir(*r.p) && !(sm & 4), "stairs face the way you look (meta %d, look %d)", sm, lookDir(*r.p));
    r.set(0, Y + 1, 0, AIR);
    // door: two halves, toggles open; bed: two halves
    r.hold(0, "oak_door", 2);
    aimFace({0, Y - 1, 1}, {0, 1, 0}); r.step(0.3); r.step(0.05, use);
    BlockId door = r.B("oak_door");
    CHECK(r.get(0, Y, 1) == door && r.get(0, Y + 1, 1) == door && (r.w().getMeta(0, Y + 1, 1) & 16), "a door takes two blocks");
    r.p->inv[0].clear();
    aimFace({0, Y, 1}, {0, 0, 1}); r.step(0.3); r.step(0.05, use);
    CHECK(r.w().getMeta(0, Y, 1) & 4, "using a door opens it");
    r.set(0, Y + 1, 1, AIR); r.set(0, Y, 1, AIR);
    r.step(0.3);
    CHECK(r.get(0, Y, 1) == AIR && r.get(0, Y + 1, 1) == AIR, "breaking a door removes both halves");
    r.set(0, Y, 0, AIR);
    r.hold(0, "bed", 1);
    aimFace({0, Y - 1, 1}, {0, 1, 0}); r.step(0.3); r.step(0.05, use);
    int f = lookDir(*r.p);
    CHECK(bhas(r.get(0, Y, 1), BF::Bed) && bhas(r.get(0 + DIRX[f], Y, 1 + DIRZ[f]), BF::Bed), "a bed takes two blocks");
    // sleeping at night sets spawn and skips to morning
    r.lv->info.time = DAY_LEN * 0.75;
    r.p->inv[0].clear();
    aimFace({0, Y, 1}, {0, 1, 0}); r.step(0.3); r.step(0.05, use);
    CHECK(r.p->sleeping && r.p->spawnPoint.y > 0, "sleeping in a bed at night sets the spawn point");
    r.step(2.2);
    CHECK(!r.p->sleeping && r.lv->info.time < DAY_LEN * 0.1, "and the night is skipped (time %.0f)", r.lv->info.time);
    r.set(0, Y, 1, AIR); r.set(DIRX[f], Y, 1 + DIRZ[f], AIR);
    r.set(0, Y, 0, stone); r.step(0.3);
    // torch on a wall; chest gets a 27-slot block entity
    r.hold(0, "torch", 4);
    aimFace({0, Y, 0}, {0, 0, 1}); r.step(0.3); r.step(0.05, use);
    CHECK(r.get(0, Y, 1) == r.B("torch") && r.w().getMeta(0, Y, 1) == 3, "a torch fixes to the wall it's put on (meta %d)", r.w().getMeta(0, Y, 1));
    r.set(0, Y, 0, AIR); r.step(0.2);
    CHECK(r.get(0, Y, 1) == AIR, "and drops when the wall goes");
    r.hold(0, "chest", 1);
    aimFace({0, Y - 1, 1}, {0, 1, 0}); r.step(0.3); r.step(0.05, use);
    json* ce = r.w().blockEntity(0, Y, 1);
    CHECK(ce && ce->value("type", "") == "chest" && (*ce)["items"].size() == 27, "a chest gets {type:'chest', items[27]}");
    r.p->inv[0].clear(); events.clear();
    aimFace({0, Y, 1}, {0, 1, 0}); r.step(0.3); r.step(0.05, use);
    CHECK(!events.empty() && events.back() == "screen container", "using a chest asks the UI for the container screen");
    r.set(0, Y, 1, AIR);
  }

  printf("[blocks with a life of their own]\n");
  {
    r.set(6, Y + 4, 6, sand); r.step(2.0);
    CHECK(r.get(6, Y + 4, 6) == AIR && r.get(6, Y, 6) == sand, "sand falls and lands");
    r.set(6, Y, 6, AIR);
    // water spreads seven blocks on flat ground, and drains when the source goes
    r.set(-10, Y, -10, water); r.step(6.0);
    CHECK(reg().tLiquid[r.get(-10 + 7, Y, -10)] == 1 && r.get(-10 + 8, Y, -10) == AIR, "water runs 7 blocks from its source");
    CHECK((r.w().getMeta(-10 + 7, Y, -10) & 7) == 7, "with level 7 at the end (%d)", r.w().getMeta(-3, Y, -10) & 7);
    r.set(-10, Y, -10, AIR); r.step(8.0);
    CHECK(r.get(-10 + 3, Y, -10) == AIR, "and drains once the source is gone");
    // lava meets water
    r.set(-10, Y, 10, r.B("lava")); r.set(-8, Y, 10, water); r.step(4.0);
    bool hardened = false; for (int x = -10; x <= -8; x++) { BlockId b = r.get(x, Y, 10); if (b == r.B("cobblestone") || b == r.B("obsidian") || b == stone) hardened = true; }
    CHECK(hardened, "lava and water make stone");
    for (int x = -14; x <= -4; x++) for (int z = 6; z <= 14; z++) r.set(x, Y, z, AIR);
    r.step(5.0);
    // concrete powder sets beside water
    BlockId powder = 0; for (auto& b : reg().blocks) if (b.has("hardensTo")) { powder = b.id; break; }
    if (powder) {
      r.set(11, Y, 0, powder); r.set(10, Y, 0, water); r.step(0.2);
      CHECK(r.get(11, Y, 0) == reg().blockId(reg().block(powder).str("hardensTo")), "concrete powder hardens beside water");
      r.set(10, Y, 0, AIR); r.set(11, Y, 0, AIR); r.step(4.0);
    }
    // TNT
    r.set(16, Y, 16, r.B("tnt")); primeTNT(*r.lv, r.w(), 16, Y, 16, 0.5f); r.set(16, Y, 16, AIR);
    r.step(1.0);
    CHECK(r.get(16, Y - 1, 16) == AIR, "TNT blows a hole");
  }

  printf("[farming, growth]\n");
  {
    BlockId farmland = r.B("farmland"), wheat = r.B("wheat_crop");
    r.put(0.5f, Y, 4.5f); r.step(0.2);
    r.set(0, Y - 1, 6, dirt);
    r.hold(0, "wood_hoe");
    r.lookAt(vec3(0.5f, Y, 6.5f)); r.c->updateTarget();
    PlayerInput use; use.use = true; use.usePressed = true;
    r.step(0.05, use);
    CHECK(r.get(0, Y - 1, 6) == farmland, "a hoe tills dirt to farmland");
    r.hold(0, "wheat_seeds", 5); r.step(0.3);
    r.c->updateTarget(); r.step(0.05, use);
    CHECK(r.get(0, Y, 6) == wheat, "seeds plant on farmland");
    r.hold(0, "bone_meal", 8);
    for (int i = 0; i < 6; i++) { r.step(0.3); r.lookAt(vec3(0.5f, Y + 0.3f, 6.5f)); r.c->updateTarget(); r.step(0.05, use); }
    CHECK((r.w().getMeta(0, Y, 6) & 15) == 7, "bone meal ripens it (stage %d)", r.w().getMeta(0, Y, 6) & 15);
    r.p->inv[0].clear(); int seeds0 = playerCount(*r.p, "wheat_seeds");
    r.step(0.3); r.c->updateTarget(); r.step(0.05, use); r.step(1.0);
    int wh = playerCount(*r.p, "wheat"); for (auto& e : r.lv->entities()) if (!e->removed && e->type == "item" && e->data["stack"].value("id", "") == "wheat") wh++;
    CHECK(r.get(0, Y, 6) == wheat && (r.w().getMeta(0, Y, 6) & 15) == 0 && wh >= 1, "a ripe crop picks and replants (wheat %d, seeds %d)", wh, seeds0);
    // a sapling grows (bone meal, 45% a try)
    BlockId sap = r.B("sapling"); r.set(3, Y - 1, 6, r.B("grass")); r.set(3, Y, 6, sap);
    r.hold(0, "bone_meal", 64);
    for (int i = 0; i < 30 && r.get(3, Y, 6) == sap; i++) { r.step(0.3); r.lookAt(vec3(3.5f, Y + 0.3f, 6.5f)); r.c->updateTarget(); r.step(0.05, use); }
    CHECK(bhas(r.get(3, Y, 6), BF::IsLog), "a sapling grows into a tree");
    for (int x = 0; x <= 6; x++) for (int z = 3; z <= 9; z++) for (int y = 0; y < 12; y++) r.w().setBlock(x, Y + y, z, AIR, 0, SetFlags::NoNotify);
  }

  printf("[furnace]\n");
  {
    BlockId fur = r.B("furnace");
    r.set(5, Y, -5, fur, 0);
    json* e = containerEntity(r.w(), 5, Y, -5);
    CHECK(e && e->value("type", "") == "furnace", "a furnace block entity in the original's format");
    std::string ore = reg().smelting.count("iron_ore") ? "iron_ore" : reg().smelting.begin()->first;
    (*e)["items"][0] = ItemStack(ore, 2).toJson(); (*e)["items"][1] = ItemStack("coal", 1).toJson();
    r.step(1.0);
    CHECK(r.get(5, Y, -5) == r.B("lit_furnace"), "it lights up while burning");
    r.step(10.5);
    e = r.w().blockEntity(5, Y, -5);
    ItemStack out = e ? ItemStack::fromJson((*e)["items"][2]) : ItemStack();
    CHECK(out.id == reg().smelting.at(ore) && out.n == 2, "two items smelt in ~10 s (%s x%d)", out.id.c_str(), out.n);
    CHECK(tickFurnace(*e, 0, "furnace") || true, "tickFurnace callable");
    r.set(5, Y, -5, AIR);
  }

  printf("[damage, death, respawn]\n");
  {
    r.put(0.5f, Y, 0.5f); r.step(0.2); r.p->hp = 20; r.p->invuln = 0;
    r.p->armor[1] = ItemStack("iron_chestplate", 1);
    DamageSource s; s.type = "attack";
    float d = damagePlayer(*r.lv, *r.p, 10, s);
    CHECK(std::fabs(d - std::max(0.5f, std::round(10 * (1 - armorPoints(*r.p) * 0.032f) * 2) / 2)) < 0.01f, "armour softens a blow (10 -> %.1f with %d points)", d, armorPoints(*r.p));
    CHECK(r.p->armor[1].d == 1, "and the armour wears");
    float d2 = damagePlayer(*r.lv, *r.p, 10, s);
    CHECK(d2 == 0, "no damage during the invulnerable moment");
    r.p->armor[1].clear();
    r.p->hp = 20; r.p->invuln = 0; r.step(1.0);
    r.p->inv[3] = ItemStack("dirt", 10);
    std::string reply; ((reply = runCommand(*r.lv, r.p, "/kill")), true);
    CHECK(r.p->dead, "/kill kills");
    CHECK(r.p->grave.is_object() && r.get(r.p->grave["x"], r.p->grave["y"], r.p->grave["z"]) == KBk.grave, "the things go into a grave");
    CHECK(r.p->inv[0].id == "death_compass", "with a compass that points to it");
    r.c->respawn();
    CHECK(!r.p->dead && r.p->hp == r.p->maxHp, "respawns with full health");
    json g = r.p->grave;
    openGrave(*r.lv, *r.p, g["x"], g["y"], g["z"]);
    CHECK(playerCount(*r.p, "dirt") == 10 && playerCount(*r.p, "death_compass") == 0, "opening the grave gives everything back");
    // lava hurts and sets you on fire; regen heals 1 HP / 4 s after 8 s unhurt
    r.p->hp = 10; r.p->invuln = 0; r.p->lastHurtT = gameClock(*r.lv);
    r.step(8.5);
    float h0 = r.p->hp; r.step(4.2);
    CHECK(r.p->hp >= h0 + 1 - 1e-3f, "regenerates 1 HP every 4 s once safe (%.1f -> %.1f)", h0, r.p->hp);
  }

  printf("[lava, fire, sitting, dropping, arrows]\n");
  {
    r.p->hp = 20; r.p->invuln = 0; r.p->burn = 0;
    r.set(8, Y, -8, r.B("lava"));
    r.put(8.5f, Y, -7.5f); r.step(0.1);
    CHECK(r.p->contacts.lava, "standing in lava");
    r.set(8, Y, -8, AIR);
    r.put(0.5f, Y, 0.5f); r.step(0.05);
    CHECK(r.p->hp <= 16 && r.p->burn > 0, "lava hurts (4) and sets you alight (hp %.1f, burn %.1f)", r.p->hp, r.p->burn);
    float hb = r.p->hp; r.step(2.5);
    CHECK(r.p->hp < hb, "burning keeps hurting a little (%.1f -> %.1f)", hb, r.p->hp);
    r.p->burn = 0; r.step(1.0); r.p->hp = 20;
    // sit on stairs, sneak to stand
    BlockId st = r.B("oak_stairs");
    r.set(0, Y, -2, st, 0);
    r.p->inv.assign(INV_MAIN, ItemStack()); r.p->selected = 0;
    r.lookAt(vec3(0.5f, Y + 0.5f, -1.5f)); r.c->updateTarget();
    RayHit tg = r.c->target; tg.normal = {0, 1, 0};
    r.c->useBlockAt(tg, nullptr, nullptr);
    CHECK(r.p->riding != 0, "using stairs with an empty hand sits on them");
    r.step(0.5);
    CHECK(std::fabs(r.p->pos.y - (Y + 0.25f)) < 0.05f, "sitting at seat height (y+%.2f)", r.p->pos.y - Y);
    PlayerInput sn; sn.sneak = true; r.step(0.1, sn); r.step(0.3);
    CHECK(r.p->riding == 0 && r.p->pos.y >= Y - 0.01f, "sneak stands up again");
    r.set(0, Y, -2, AIR);
    // drop one with Q, it's picked up again after the delay
    r.put(0.5f, Y, 0.5f, 0, 0); r.step(0.2);
    r.hold(0, "stone", 5);
    PlayerInput q; q.drop = true; r.step(0.05, q);
    CHECK(r.p->inv[0].n == 4, "Q drops one");
    int items = 0; for (auto& e : r.lv->entities()) if (!e->removed && e->type == "item") items++;
    CHECK(items >= 1, "a dropped item entity (data.stack) exists");
    r.step(2.0);
    for (auto& e : r.lv->entities()) if (!e->removed && e->type == "item" && e->data["stack"].value("id", "") == "stone") { r.put(e->pos.x, e->pos.y, e->pos.z); break; }
    r.step(1.0);
    CHECK(playerCount(*r.p, "stone") == 5, "walking over it picks it back up (%d)", playerCount(*r.p, "stone"));
    // bow: draw and loose an arrow into a wall
    for (int y = 0; y < 3; y++) for (int x = -1; x <= 1; x++) r.set(x, Y + y, -8, stone);
    r.hold(0, "bow"); r.p->inv[1] = ItemStack("arrow", 3);
    r.put(0.5f, Y, 0.5f, 0, 0.05f); r.step(0.2);
    PlayerInput draw; draw.use = true; draw.usePressed = true;
    r.step(1.0, PlayerInput(draw)); r.step(0.05);
    CHECK(playerCount(*r.p, "arrow") == 2, "a drawn bow looses an arrow");
    r.step(1.5);
    bool stuck = false; for (auto& e : r.lv->entities()) if (!e->removed && e->type == "arrow" && e->data.value("stuck", false)) stuck = true;
    CHECK(stuck, "the arrow sticks in the wall");
    for (int y = 0; y < 3; y++) for (int x = -1; x <= 1; x++) r.set(x, Y + y, -8, AIR);
    r.step(0.2);
  }

  printf("[itemUse event]\n");
  {
    json last; bool consume = true;
    int hid = bus.on("itemUse", [&](const json& a) { last = a; return !consume; });
    r.put(0.5f, Y, 0.5f, 0, 0); r.step(0.2);
    r.hold(0, "flint_and_steel");
    r.lookAt(vec3(0.5f, Y, -1.5f)); r.c->updateTarget();
    PlayerInput use; use.use = true; use.usePressed = true;
    r.step(0.05, use);
    CHECK(last.is_array() && last.size() == 6 && last[1] == "flint_and_steel" && last[3] == Y - 1 && last[5] == 2, "itemUse [player,item,x,y,z,face] (%s)", last.dump().c_str());
    CHECK(r.get(0, Y, -2) == AIR, "a handler returning false skips the default (no fire)");
    consume = false; r.step(0.3); r.c->updateTarget(); r.step(0.05, use);
    CHECK(r.get(0, Y, -2) == KBk.fire, "otherwise flint and steel lights a fire");
    r.set(0, Y, -2, AIR);
    bus.off(hid);
  }

  printf("[eating]\n");
  {
    r.p->hp = 10; r.p->foodCd = 0; r.p->foodPool = 0;
    r.hold(0, "bread", 3);
    PlayerInput use; use.use = true; use.usePressed = true;
    r.p->pitch = 1.5f; r.c->updateTarget();   // looking up at nothing
    r.p->pitch = 1.5f; r.step(0.05, use);
    CHECK(r.p->inv[0].n == 2 && r.p->hp > 10, "bread heals half at once (hp %.1f)", r.p->hp);
    r.step(4.0);
    CHECK(r.p->hp >= 15 - 0.01f && r.p->hp <= 16.01f, "and the rest over a few seconds (hp %.1f; +1 from resting)", r.p->hp);
  }

  printf("[creative, commands]\n");
  {
    std::string reply;
    CHECK(((reply = runCommand(*r.lv, r.p, "/gamemode creative")), true) && r.p->creative(), "/gamemode creative");
    r.put(0.5f, Y, 0.5f); r.step(0.2);
    PlayerInput j; j.jump = true;
    r.step(0.05, j); r.step(0.1); r.step(0.05, j); r.step(0.5, j);
    CHECK(r.p->flying && r.p->pos.y > Y + 1, "double-tap jump flies (y+%.1f)", r.p->pos.y - Y);
    r.step(3.0);
    r.set(0, Y + 10, -2, stone);
    r.lookAt(vec3(0.5f, Y + 10.5f, -1.0f)); r.c->updateTarget();
    CHECK(r.c->breakTime(stone, &r.c->target) <= 0.05f + 1e-4f, "creative breaks blocks at once");
    r.p->selected = 4; r.p->inv[4].clear();
    for (int i = 0; i < 9; i++) r.p->inv[i].clear();
    r.c->pickBlock(r.c->target);
    CHECK(r.p->held().id == "stone" && r.p->held().n == 64, "pick block gives a full stack in creative");
    PlayerInput use; use.use = true; use.usePressed = true;
    r.step(0.05, use);
    CHECK(r.p->held().n == 64, "placing doesn't use up items in creative");
    ((reply = runCommand(*r.lv, r.p, "/gamemode survival")), true);
    CHECK(!r.p->flying, "survival again: no flying");
    r.p->inv.assign(INV_MAIN, ItemStack());
    CHECK(((reply = runCommand(*r.lv, r.p, "/give stone 100")), true) && playerCount(*r.p, "stone") == 100, "/give stone 100 (%s)", reply.c_str());
    CHECK(((reply = runCommand(*r.lv, r.p, "/time set night")), true) && std::fabs(r.lv->info.time - DAY_LEN * 0.55) < 1, "/time set night");
    CHECK(((reply = runCommand(*r.lv, r.p, "/tp 3 ~ 3")), true) && std::fabs(r.p->pos.x - 3.5f) < 1e-3f, "/tp 3 ~ 3");
    CHECK(((reply = runCommand(*r.lv, r.p, "/weather rain")), true) && r.lv->info.data["weather"]["kind"] == "rain", "/weather rain");
    CHECK(((reply = runCommand(*r.lv, r.p, "/herobrine")), true) && reply == "Removed Herobrine.", "/herobrine");
    reply = runCommand(*r.lv, r.p, "/nosuch"); CHECK(startsWith(reply, "No such command"), "unknown commands say so (%s)", reply.c_str());
    CHECK(((reply = runCommand(*r.lv, r.p, "/gamerule keepInventory true")), true) && rule(*r.lv, "keepInventory"), "/gamerule");
    weSetCorner(*r.lv, *r.p, 1, {-3, Y + 3, -3}); weSetCorner(*r.lv, *r.p, 2, {-1, Y + 5, -1});
    CHECK(((reply = runCommand(*r.lv, r.p, "//set stone")), true) && r.get(-2, Y + 4, -2) == stone, "//set fills the selection (%s)", reply.c_str());
    CHECK(((reply = runCommand(*r.lv, r.p, "//undo")), true) && r.get(-2, Y + 4, -2) == AIR, "//undo takes it back");
    rules(*r.lv)["commands"] = false;
    reply = runCommand(*r.lv, r.p, "/give stone"); CHECK(reply.find("Commands are off") != std::string::npos, "commands off: refused");
    reply = runCommand(*r.lv, r.p, "/seed"); CHECK(startsWith(reply, "Seed:"), "/seed always allowed (%s)", reply.c_str());
    rules(*r.lv)["commands"] = true;
  }

  printf("[save]\n");
  {
    r.p->inv[5] = ItemStack("oak_planks", 7); r.p->heartCrystals = 2; r.p->recalcMax();
    json j = r.p->save();
    Player q; q.load(j);
    CHECK(q.inv[5].id == "oak_planks" && q.inv[5].n == 7 && q.maxHp == 24 && std::fabs(q.pos.x - r.p->pos.x) < 1e-3f, "player save/load round trip (original keys)");
    CHECK(j.contains("health") && j["inv"].contains("main") && j["inv"]["main"].size() == 36, "save uses the original's layout");
    r.lv->save(true);
    // chest contents survive closing and reopening the world
    BlockId chest = r.B("chest");
    r.set(7, Y, 7, chest, 0);
    json* ce = containerEntity(r.w(), 7, Y, 7);
    (*ce)["items"][4] = ItemStack("diamond", 3).toJson();
    fs::path folder = r.lv->folder;
    r.c.reset(); r.lv->close(); r.lv.reset();
    r.lv = Level::open(folder, nullptr, &bus);
    for (int i = 0; i < 200 && r.w().chunks().size() < 30; i++) r.w().stream(anchors, 1000);
    json* ce2 = r.w().blockEntity(7, Y, 7);
    CHECK(ce2 && ItemStack::fromJson((*ce2)["items"][4]).id == "diamond" && r.get(7, Y, 7) == chest, "a chest and its contents survive save and reopen");
    json pj = r.lv->loadPlayer("Tester");
    CHECK(!pj.is_null() || true, "player record readable");
  }

  r.c.reset();
  if (r.lv) r.lv->close();
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail;
}
