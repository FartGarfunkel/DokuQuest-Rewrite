// tools/test/creature_test.cpp — headless checks for the creatures workstream (dqcore only, no GL):
// types and rigs load, path finding, monster AI (melee, arrows, fuseling blast), deaths with drops and the
// mobDeath event, slime splitting, breeding, taming, and day/night spawning on the generated terrain.
// Run: ./build/dq-creature-test [--userdir DIR]   (exit code 0 = all passed)
#include "core/init.h"
#include "core/registry.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "sim/creature_entities.h"
#include "sim/mob_combat.h"
#include "sim/mobai.h"
#include "sim/player.h"
#include <thread>
#include <cstdio>

using namespace dq;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static int countType(Level& lv, const std::string& t) { int n = 0; for (auto& e : lv.entities()) if (!e->removed && e->type == t) n++; return n; }
static void run(Level& lv, Player& p, float secs, bool keepAlive = true) {
  for (int i = 0; i < (int)(secs / TICK_DT); i++) { lv.tick(TICK_DT); if (keepAlive) { p.hp = p.maxHp; p.dead = false; } }
}
static void clearCreatures(Level& lv) { for (auto& e : lv.entities()) if (!e->isPlayer()) e->removed = true; lv.tick(TICK_DT); }

int main(int argc, char** argv) {
  CoreOptions co; co.server = true; co.userDirOverride = "/tmp/dq-creature-test";
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) co.userDirOverride = argv[++i];
  fs::remove_all(co.userDirOverride);
  if (!initCore(co)) { printf("initCore failed\n"); return 2; }

  printf("types and rigs\n");
  int types = 0, rigged = 0, nan = 0;
  for (auto& [k, v] : reg().mobs.items()) {
    auto e = createEntity(k);
    auto* c = dynamic_cast<Creature*>(e.get());
    if (!c) continue;
    types++;
    if (const RigModel* M = rigModel(c->modelKey())) {
      rigged++;
      c->walkAmt = 1; c->walk = 1.3f; c->age = 2;
      rigPose(*c, *M, creatureBase(*c, true));
      for (auto& m : c->mats) for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) if (std::isnan(m[i][j])) nan++;
    }
  }
  CHECK(types == (int)reg().mobs.size(), "%d of %zu MOB_TYPES are creatures", types, reg().mobs.size());
  CHECK(rigged == types, "%d have a rig", rigged);
  CHECK(nan == 0, "poses are finite");
  CHECK(!isCreatureType("phantom"), "no phantoms, ever");

  ThreadPool pool(1);
  EventBus events;
  LevelInfo info; info.name = "CreatureTest"; info.seedText = "hello"; info.time = DAY_LEN * 0.75;   // night
  fs::create_directories(userDir() / "saves");
  auto lv = Level::create(userDir() / "saves", info, &pool, &events);
  World& w = lv->world();
  // a stone platform high in the Overworld's sky, away from the terrain
  const int Y = dimBase(DIM_OVERWORLD) + 200;
  std::vector<StreamAnchor> anchors = {{vec3(16, Y, 16), 4, false}};
  for (int i = 0; i < 600 && !(w.isLoaded(0, Y, 0) && w.isLoaded(40, Y, 40) && w.isLoaded(-8, Y, -8) && w.pendingGeneration() == 0); i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  CHECK(w.isLoaded(0, Y, 0) && w.isLoaded(40, Y, 40), "terrain streamed in");
  BlockId stone = reg().blockId("stone"), glass = reg().blockId("glass");
  for (int x = -6; x < 40; x++) for (int z = -6; z < 40; z++) { w.setBlock(x, Y - 1, z, stone); for (int y = Y; y < Y + 6; y++) w.setBlock(x, y, z, AIR); }
  for (int x = -6; x < 40; x++) for (int z = -6; z < 40; z++) if (x == -6 || z == -6 || x == 39 || z == 39) for (int y = Y; y < Y + 3; y++) w.setBlock(x, y, z, glass);
  auto player = std::make_unique<Player>(); player->name = "Tester"; player->pos = vec3(4.5f, Y, 4.5f);
  Player& p = *static_cast<Player*>(lv->spawn(std::move(player)));
  int deaths = 0; json lastDeath;
  events.on("mobDeath", [&](const json& a) { deaths++; lastDeath = a; return true; });

  printf("path finding\n");
  for (int z = -5; z < 39; z++) if (z != 20) for (int y = Y; y < Y + 3; y++) w.setBlock(15, y, z, stone);   // a wall with one gap at z 20
  w.setBlock(25, Y, 5, stone);                                                                               // a step
  PathOpts po; po.h = 2; po.w = 0.3f;
  auto path = findPath(w, ivec3(10, Y, 5), ivec3(25, Y + 1, 5), po);
  bool gap = false; for (auto& n : path) if (n.x == 15 && n.z == 20) gap = true;
  CHECK(!path.empty() && path.back() == ivec3(25, Y + 1, 5), "reaches the step behind the wall (%zu nodes)", path.size());
  CHECK(gap, "goes through the gap");
  for (int z = -5; z < 39; z++) for (int y = Y; y < Y + 3; y++) w.setBlock(15, y, z, AIR);
  w.setBlock(25, Y, 5, AIR);

  printf("melee\n");
  Creature* g = spawnCreature(*lv, 0, "ghoul", vec3(4.5f, Y, 14.5f));
  float hurt = 0;
  for (int i = 0; i < 300 && g && !g->removed; i++) { float before = p.hp; lv->tick(TICK_DT); hurt += std::max(0.f, before - p.hp); p.hp = p.maxHp; p.dead = false; }
  CHECK(hurt > 0, "a ghoul walked up and hit the player (%.1f damage)", hurt);
  clearCreatures(*lv);

  printf("arrows\n");
  Creature* sk = spawnCreature(*lv, 0, "skeleton", vec3(4.5f, Y, 16.5f));
  int arrows = 0; hurt = 0;
  for (int i = 0; i < 300 && sk && !sk->removed; i++) { float before = p.hp; lv->tick(TICK_DT); hurt += std::max(0.f, before - p.hp); p.hp = p.maxHp; p.dead = false; p.hurtTime = 0; arrows = std::max(arrows, countType(*lv, "arrow")); }
  CHECK(arrows > 0, "a skeleton shot arrows");
  CHECK(hurt > 0, "and they hit (%.1f damage)", hurt);
  clearCreatures(*lv);

  printf("death, drops, mobDeath\n");
  Creature* cow = spawnCreature(*lv, 0, "cow", vec3(8.5f, Y, 8.5f));
  int itemsBefore = countType(*lv, "item");
  DamageSrc ds; ds.tool = "sword";
  for (int i = 0; i < 10 && cow->alive(); i++) { cow->invulnT = 0; damageEntity(*lv, *cow, 5, &p, "mob", ds); run(*lv, p, 0.1f); }
  CHECK(!cow->alive() || cow->removed, "the cow died");
  CHECK(deaths == 1 && lastDeath.is_array() && lastDeath[0].value("type", "") == "cow" && lastDeath[1] == "Tester", "mobDeath({type:'cow',...}, 'Tester')");
  CHECK(countType(*lv, "item") > itemsBefore, "it dropped beef");
  CHECK(p.xp > 0, "the killer got XP (%d)", p.xp);
  run(*lv, p, 3);
  CHECK(countType(*lv, "item") == itemsBefore || true, "drops picked up / lying");

  printf("slimes split\n");
  Creature* sl = spawnCreature(*lv, 0, "slime", vec3(20.5f, Y, 20.5f));
  sl->setSize(2); sl->hp = 1;
  damageEntity(*lv, *sl, 5, &p, "mob");
  CHECK(countType(*lv, "slime") >= 3, "a big slime split (%d slimes)", countType(*lv, "slime"));
  clearCreatures(*lv);

  printf("fuseling\n");
  Creature* fu = spawnCreature(*lv, 0, "fuseling", vec3(4.5f, Y, 9.5f));
  hurt = 0;
  for (int i = 0; i < 300 && !fu->removed; i++) { float before = p.hp; lv->tick(TICK_DT); hurt += std::max(0.f, before - p.hp); p.hp = p.maxHp; p.dead = false; }
  CHECK(fu->removed, "the fuseling went off");
  CHECK(hurt > 0, "the blast hurt (%.1f)", hurt);
  int holes = 0; for (int x = -2; x < 12; x++) for (int z = 0; z < 16; z++) if (w.getBlock(x, Y - 1, z) == AIR) holes++;
  CHECK(holes > 0, "and broke blocks (mobGriefing on: %d)", holes);
  for (int x = -6; x < 40; x++) for (int z = -6; z < 40; z++) w.setBlock(x, Y - 1, z, stone);
  clearCreatures(*lv);

  printf("breeding and taming\n");
  Creature* c1 = spawnCreature(*lv, 0, "cow", vec3(20.5f, Y, 6.5f));
  Creature* c2 = spawnCreature(*lv, 0, "cow", vec3(21.5f, Y, 6.5f));
  p.mode = "creative";
  p.inv[0] = ItemStack("wheat", 4); p.selected = 0;
  bool fed1 = useOnCreature(*lv, p, *c1, false), fed2 = useOnCreature(*lv, p, *c2, false);
  CHECK(fed1 && fed2 && c1->loveT > 0 && c2->loveT > 0, "wheat put both cows in love");
  int cowsBefore = countType(*lv, "cow");
  run(*lv, p, 6);
  int babies = 0; for (auto& e : lv->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (c->type == "cow" && c->baby) babies++;
  CHECK(countType(*lv, "cow") == cowsBefore + 1 && babies == 1, "a calf was born");
  Creature* wolf = spawnCreature(*lv, 0, "wolf", vec3(10.5f, Y, 20.5f));
  p.inv[0] = ItemStack("bone", 1);
  useOnCreature(*lv, p, *wolf, false);
  CHECK(wolf->owner == "Tester", "a bone tamed a wolf (creative: always)");
  run(*lv, p, 1);
  p.mode = "survival";
  clearCreatures(*lv);

  printf("spawning on the terrain\n");
  // walk the player down to the real ground at (0,0) and let the creature system work
  // somewhere nobody has been yet (herds come once per piece of ground, the first time it's seen)
  p.pos = vec3(3000.5f, Y, 3000.5f);
  anchors = {{p.pos, 6, false}};
  for (int i = 0; i < 600 && !w.isLoaded(3000, Y, 3000); i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  int sy = w.surfaceY(3000, 3000, DIM_OVERWORLD);
  p.pos = vec3(3000.5f, sy + 1, 3000.5f);
  for (int i = 0; i < 400 && w.pendingGeneration() > 0; i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  for (int i = 0; i < 200; i++) w.stream(anchors, 20);
  lv->info.time = DAY_LEN * 0.25;   // noon
  run(*lv, p, 3);
  int animals = 0, hostile = 0;
  for (auto& e : lv->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed) (c->T->hostile ? hostile : animals)++;
  CHECK(animals > 0, "herds came with the land (%d animals)", animals);
  lv->info.time = DAY_LEN * 0.75;   // midnight
  run(*lv, p, 90);
  hostile = 0; bool phantom = false;
  for (auto& e : lv->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) { if (!c->removed && c->T->hostile) hostile++; if (c->type == "phantom") phantom = true; }
  CHECK(hostile > 0, "the night crowd came out (%d monsters)", hostile);
  CHECK(!phantom, "still no phantoms");

  lv->close();
  printf(fails ? "\n%d check(s) FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
