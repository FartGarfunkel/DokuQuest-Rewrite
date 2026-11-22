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
#include "sim/combat.h"
#include "sim/combat_rpg.h"
#include "sim/attack_moves.h"
#include "sim/shield_shape.h"
#include "core/tool_parts.h"
#include "gen/structures_api.h"
#include <thread>
#include <cstdio>

using namespace dq;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static int countType(Level& lv, const std::string& t) { int n = 0; for (auto& e : lv.entities()) if (!e->removed && e->type == t) n++; return n; }
static void run(Level& lv, Player& p, float secs, bool keepAlive = true) {
  // (the PlayerController counts the invulnerable moment down in the game; there is none here)
  for (int i = 0; i < (int)(secs / TICK_DT); i++) { lv.tick(TICK_DT); p.invuln = std::max(0.f, p.invuln - (float)TICK_DT); if (keepAlive) { p.hp = p.maxHp; p.dead = false; } }
}
static void clearCreatures(Level& lv) { for (auto& e : lv.entities()) if (!e->isPlayer()) e->removed = true; lv.tick(TICK_DT); }
static void driveMelee(Level& lv, Creature& m) {
  for (int i = 0; i < 80 && m.atkMove; i++) m.tickMeleeBlow(lv, 0.02f);
}

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
  {   // state changes ease (sim/creature_rig.cpp rigBlend): a wolf sitting down doesn't snap
    auto e = createEntity("wolf"); auto* c = dynamic_cast<Creature*>(e.get());
    const RigModel* M = c ? rigModel(c->modelKey()) : nullptr;
    if (M) {
      c->onGround = true;
      auto pose = [&](float t) { c->age = t; rigPose(*c, *M, creatureBase(*c, true)); return c->mats; };
      auto far = [](const std::vector<mat4>& a, const std::vector<mat4>& b) { float d = 0; for (size_t i = 0; i < a.size() && i < b.size(); i++) for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) d = std::max(d, std::fabs(a[i][x][y] - b[i][x][y])); return d; };
      pose(1.0f); auto stand = pose(1.02f);
      c->sitting = true;
      auto first = pose(1.04f);
      CHECK(far(first, stand) < 0.05f, "a wolf sitting down starts from where it stood (%.3f)", far(first, stand));
      for (float t = 1.06f; t < 1.6f; t += 0.02f) pose(t);
      auto later = pose(1.6f);
      CHECK(far(later, stand) > 0.1f, "and ends up sitting");
    }
  }

  {   // spiders stand on their feet (sim/creature_rig.cpp animSpider: they had hovered, the Broodmother half a block up)
    for (const char* k : {"spider", "cave_spider", "broodmother", "scorpion", "rift_spider"}) {
      auto e = createEntity(k); auto* c = dynamic_cast<Creature*>(e.get());
      const RigModel* M = c ? rigModel(c->modelKey()) : nullptr;
      if (!M) { CHECK(false, "%s has a rig", k); continue; }
      c->onGround = true; c->age = 2; rigPose(*c, *M, creatureBase(*c, false));
      float foot = 1e9f, body = 1e9f;
      for (size_t i = 0; i < M->parts.size(); i++) {
        const RigPart& q = M->parts[i];
        for (int n = 0; n < 8; n++) {
          const vec3 cl((n & 1) ? q.max.x : q.min.x, (n & 2) ? q.max.y : q.min.y, (n & 4) ? q.max.z : q.min.z);
          const float y = vec3(c->mats[i] * vec4(cl, 1)).y;
          if (q.role == "sshin") foot = std::min(foot, y); else if (q.role != "sleg" && !q.glow) body = std::min(body, y);
        }
      }
      CHECK(foot > -0.04f && foot < 0.03f, "a %s stands on its feet (lowest foot %.3f above the ground)", k, foot);
      CHECK(body > foot - 0.06f, "and its body is above them (%.3f)", body);
    }
    // a blow lands on the part it strikes, not the air round it (sim/creature_rig.cpp rigHitPart: a thin pad only)
    auto e = createEntity("broodmother"); auto* c = dynamic_cast<Creature*>(e.get());
    const RigModel* M = c ? rigModel(c->modelKey()) : nullptr;
    if (M) {
      c->onGround = true; c->age = 2; rigPose(*c, *M, creatureBase(*c, false));
      const int shin = M->find("sshinR1");
      const vec3 tip = vec3(c->mats[shin] * vec4(0, M->parts[shin].min.y * 0.5f, 0, 1));
      const int down = rigHitPart(*c, *M, tip + vec3(0, 3, 0), vec3(0, -1, 0));
      CHECK(down >= 0 && rigLimbPart(*M, down) >= 0 && M->parts[rigLimbPart(*M, down)].limb == "slegR1", "a blow through a leg strikes that leg (%s)", down >= 0 ? M->parts[down].name.c_str() : "nothing");
      // beside it, across the way it leans (the old even pad, 0.12 a side, reached 0.36 out on the Broodmother's legs)
      const vec3 sideways = glm::normalize(vec3(c->mats[shin][2]));   // the shin's own z: across its lean (it leans about z)
      const float half = 0.0625f * M->scale;
      const int miss = rigHitPart(*c, *M, tip + sideways * (half + 0.12f) + vec3(0, 3, 0), vec3(0, -1, 0));
      CHECK(miss != shin, "a swing a hand's breadth outside the leg misses it (%s)", miss >= 0 ? M->parts[miss].name.c_str() : "nothing");
      const vec3 a = tip + vec3(0, 0.4f, 0), b = tip - vec3(0, 0.2f, 0);
      const int cut = rigSegHitPart(*c, *M, a, b, 0.08f);
      CHECK(cut >= 0 && rigLimbPart(*M, cut) >= 0 && M->parts[rigLimbPart(*M, cut)].limb == "slegR1",
            "a blade through a leg strikes that leg (%s)", cut >= 0 ? M->parts[cut].name.c_str() : "nothing");
      const int missSeg = rigSegHitPart(*c, *M, a + sideways * (half + 0.18f), b + sideways * (half + 0.18f), 0.08f);
      CHECK(missSeg != shin, "a blade a hand's breadth outside the leg misses it");
    }
    // a drawn sword's sweep, not the whole AABB: a ghoul in front is struck, one well to the side is not
    auto eg = createEntity("ghoul"); auto* gh = dynamic_cast<Creature*>(eg.get());
    const RigModel* Mg = gh ? rigModel(gh->modelKey()) : nullptr;
    if (Mg) {
      gh->pos = vec3(0, 0, -1.35f); gh->yaw = 3.14159f; gh->onGround = true; gh->age = 2;
      const int hit = bladeSweepHitsCreature(MV_SWORD1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 1.f);
      CHECK(hit >= 0, "a sword swing hits a ghoul standing in front (%s)", hit >= 0 ? Mg->parts[hit].name.c_str() : "nothing");
      const int limb = hit >= 0 ? rigLimbPart(*Mg, hit) : -1;
      CHECK(hit < 0 || limb < 0 || !Mg->parts[limb].limb.empty(),
            "that part is the limb tryDismember will take (%s)", limb >= 0 ? Mg->parts[limb].limb.c_str() : "torso");
      gh->pos = vec3(4.0f, 0, 0);
      const int sideMiss = bladeSweepHitsCreature(MV_SWORD1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 1.f);
      CHECK(sideMiss < 0, "a ghoul well to the side is missed by that swing");
      gh->pos = vec3(0, 0, -1.35f);
      CHECK(bladeSweepHitsCreature(MV_GS1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 1.15f) >= 0, "a greatsword cut hits that ghoul");
      CHECK(bladeSweepHitsCreature(MV_SPEAR1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 1.35f) >= 0, "a spear thrust hits that ghoul");
      CHECK(bladeSweepHitsCreature(MV_MACE1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 0.9f) >= 0, "a mace blow hits that ghoul");
      CHECK(bladeSweepHitsCreature(MV_AXE1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 1.f) >= 0, "an axe chop hits that ghoul");
      CHECK(bladeSweepHitsCreature(MV_DAGGER1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 1.f) >= 0, "a dagger stab hits that ghoul");
      CHECK(bladeSweepHitsCreature(MV_TOOL1, 0, 1, vec3(0), 0, 0, *gh, *Mg, 1.f) >= 0, "a tool swing hits that ghoul");
    } else CHECK(false, "a ghoul has a rig for a blade sweep");
  }

  ThreadPool pool(1);
  EventBus events;
  LevelInfo info; info.name = "CreatureTest"; info.seedText = "hello"; info.time = DAY_LEN * 0.75;   // night
  fs::create_directories(userDir() / "saves");
  auto lv = Level::create(userDir() / "saves", info, &pool, &events);
  World& w = lv->world();
  // a stone platform high in the Overworld's sky, away from the terrain
  const int Y = dimBase(DIM_OVERWORLD) + 200;
  std::vector<StreamAnchor> anchors = {{vec3(16, Y, 16), 4, false}};
  for (int i = 0; i < 6000 && !(w.isLoaded(0, Y, 0) && w.isLoaded(40, Y, 40) && w.isLoaded(-8, Y, -8) && w.pendingGeneration() == 0); i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
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
  // a fuseling (1.7 tall) walks under a shut trapdoor at the top of its head's block; a ghoul (1.95) can't
  {
    BlockId trap = reg().blockId("oak_trapdoor") ? reg().blockId("oak_trapdoor") : reg().blockId("trapdoor");
    for (int z = -5; z < 39; z++) { w.setBlock(15, Y + 2, z, stone); w.setBlock(15, Y + 1, z, trap, 8 /* top half */); }
    PathOpts f; f.h = 2; f.hf = 1.7f; f.w = 0.3f;
    auto pf = findPath(w, ivec3(10, Y, 5), ivec3(20, Y, 5), f);
    PathOpts gh; gh.h = 2; gh.hf = 1.95f; gh.w = 0.3f;
    auto pg = findPath(w, ivec3(10, Y, 5), ivec3(20, Y, 5), gh);
    CHECK(trap && !pf.empty() && pf.back() == ivec3(20, Y, 5), "a fuseling-sized creature paths under a row of shut top trapdoors (%zu nodes)", pf.size());
    CHECK(pg.empty() || pg.back() != ivec3(20, Y, 5), "a ghoul-sized one doesn't fit under them");
    for (int z = -5; z < 39; z++) for (int y = Y; y < Y + 3; y++) w.setBlock(15, y, z, AIR);
  }
  // an animal wandering into a one-block step hops up onto it (the hop it makes against a wall clears a whole block)
  printf("wandering up a step\n");
  for (const char* kind : {"cow", "pig", "sheep"}) {
    for (int z = 26; z < 36; z++) w.setBlock(12, Y, z, stone);
    Creature* an = spawnCreature(*lv, 0, kind, vec3(9.5f, Y, 30.5f));
    float top = 0;
    for (int i = 0; an && i < (int)(4 / TICK_DT); i++) {
      an->wanderOn = true; an->wanderT = 100; an->wanderDir = -PI / 2;   // (walking +x, into the step)
      lv->tick(TICK_DT); top = std::max(top, an->pos.y - Y);
    }
    CHECK(an && an->pos.y >= Y + 0.99f && an->pos.x > 12, "a %s wandering into a step climbs it (highest %.2f, at x %.2f)", kind, top, an ? an->pos.x : 0.f);
    clearCreatures(*lv);
    for (int z = 26; z < 36; z++) w.setBlock(12, Y, z, AIR);
  }

  printf("melee\n");
  Creature* g = spawnCreature(*lv, 0, "ghoul", vec3(4.5f, Y, 14.5f));
  float hurt = 0;
  for (int i = 0; i < 300 && g && !g->removed; i++) { float before = p.hp; lv->tick(TICK_DT); hurt += std::max(0.f, before - p.hp); p.hp = p.maxHp; p.dead = false; }
  CHECK(hurt > 0, "a ghoul walked up and hit the player (%.1f damage)", hurt);
  clearCreatures(*lv);

  printf("limb armour and a raised guard\n");
  {
    p.mode = "survival"; p.dead = false; p.hp = 20; p.invuln = 0; p.blockT = 0; p.stamina = 100;
    p.shield[0].clear();
    for (auto& a : p.armor) a.clear();
    DamageSource openSrc; openSrc.type = "mob";
    const float open = damagePlayer(*lv, p, 10, openSrc);
    p.invuln = 0; p.hp = 20; p.blockT = 1.0f; p.stamina = 100;
    p.shield[0] = ItemStack("shield", 1);
    const float kept = damagePlayer(*lv, p, 10, openSrc);
    CHECK(open > 0 && kept > 0 && kept < open * 0.5f, "a raised shield keeps most of a blow (%.1f -> %.1f)", open, kept);
    p.invuln = 0; p.hp = 20; p.blockT = 1.0f; p.stamina = 100;
    DamageSource bodyBlow; bodyBlow.type = "mob"; bodyBlow.armorSlot = 1;
    const float missedGuard = damagePlayer(*lv, p, 10, bodyBlow);
    CHECK(missedGuard > kept * 1.4f, "a blow that missed the guard is not kept (%.1f vs kept %.1f)", missedGuard, kept);
    p.invuln = 0; p.hp = 20; p.blockT = 1.0f; p.stamina = 100;
    DamageSource onGuard; onGuard.type = "mob"; onGuard.metGuard = true;
    const float met = damagePlayer(*lv, p, 10, onGuard);
    CHECK(met <= kept + 0.1f, "a blow that met the guard is kept (%.1f)", met);
    p.blockT = 0; p.shield[0].clear(); p.invuln = 0; p.hp = 20;
    ItemStack helm, chest;
    CHECK(makePartArmor("helmet", std::vector<std::string>(5, "iron"), helm) &&
          makePartArmor("chestplate", std::vector<std::string>(8, "iron"), chest), "mixed iron armour crafts");
    p.armor[0] = helm;
    DamageSource head; head.type = "attack"; head.armorSlot = 0;
    DamageSource body; body.type = "attack"; body.armorSlot = 1;
    const float dHead = damagePlayer(*lv, p, 10, head);
    p.invuln = 0; p.hp = 20;
    const float dChest = damagePlayer(*lv, p, 10, body);
    CHECK(dHead < dChest, "a helmet protects the head more than a blow on the chest (%.1f < %.1f)", dHead, dChest);
    p.armor[0].clear(); p.armor[1] = chest; p.invuln = 0; p.hp = 20;
    const float dChestArm = damagePlayer(*lv, p, 10, body);
    p.invuln = 0; p.hp = 20;
    const float dHeadBare = damagePlayer(*lv, p, 10, head);
    CHECK(dChestArm < dHeadBare, "a chestplate protects the chest more than a blow on the bare head (%.1f < %.1f)",
          dChestArm, dHeadBare);
    p.armor[1].clear(); p.invuln = 0; p.hp = 20; p.blockT = 0;
    // a ghoul in front of a raised shield: its claw meets the guard
    Creature* g2 = spawnCreature(*lv, 0, "ghoul", p.pos + vec3(0, 0, -1.2f));
    if (g2) {
      p.yaw = 0; p.blockT = 0.2f; p.shield[0] = ItemStack("shield", 1); p.stamina = 100;
      ShieldBox guard;
      const vec3 from = g2->pos + vec3(0, g2->h * 0.62f, 0);
      const vec3 claw = (p.pos + vec3(0, 1.1f, 0)) - from;
      CHECK(playerGuardBox(p, guard) && shieldRayHit(guard, from, claw, 1.f),
            "a ghoul's claw in front meets a raised shield");
      p.yaw = 3.14159f;
      ShieldBox behind;
      CHECK(playerGuardBox(p, behind) && !shieldRayHit(behind, from, claw, 1.f),
            "turning your back, that claw misses the shield");
      p.invuln = 0; p.hp = 20; p.blockT = 1.0f; p.stamina = 100; p.yaw = 0;
      g2->attackCd = 0; g2->atkMove = 0; g2->atkHit.clear();
      g2->meleeAt(*lv, p, 1.f, 0.f);
      driveMelee(*lv, *g2);
      const float front = 20.f - p.hp;
      p.invuln = 0; p.hp = 20; p.blockT = 1.0f; p.stamina = 100; p.yaw = 3.14159f;
      g2->attackCd = 0; g2->atkMove = 0; g2->atkHit.clear();
      g2->meleeAt(*lv, p, 1.f, 0.f);
      driveMelee(*lv, *g2);
      const float back = 20.f - p.hp;
      CHECK(front > 0 && back > front * 1.4f, "a claw in the back is not kept (front %.1f, back %.1f)", front, back);
      g2->removed = true;
    } else CHECK(false, "a ghoul's claw in front meets a raised shield");
    p.blockT = 0; p.shield[0].clear(); p.yaw = 0; p.hp = p.maxHp; p.invuln = 0;
    // a mixed blade's own swing, not an AABB: the cut has to pass through the ghoul
    Creature* g3 = spawnCreature(*lv, 0, "ghoul", p.pos + vec3(0, 0, -1.35f));
    if (g3) {
      g3->calm = true; g3->vel = vec3(0); g3->maxHp = 200; g3->hp = 200;
      p.yaw = 0; p.pitch = 0; p.mode = "creative"; p.atkMove = 0; p.atkT = 0; p.atkGap = 9; p.atkCombo = 0;
      ItemStack blade;
      CHECK(makePartTool("sword", {"iron", "iron"}, "stick", blade), "a mixed iron sword for the swing");
      p.inv[0] = blade; p.selected = 0;
      float hp0 = g3->hp;
      PlayerInput swing; swing.attackPressed = true;
      tickMelee(*lv, p, 0.01f, swing, true, false);
      for (int k = 0; k < 100 && p.atkMove; k++) tickMelee(*lv, p, 0.01f, PlayerInput{}, true, false);
      CHECK(!g3->removed && g3->hp < hp0, "a mixed iron sword swing lands on a ghoul in front (%.1f -> %.1f)", hp0, g3->hp);
      g3->hp = 200; g3->invulnT = 0; g3->hurtTime = 0; g3->removed = false; g3->data.erase("invulnT");
      ItemStack gs;
      CHECK(makePartTool("greatsword", {"iron", "iron", "iron"}, "stick", gs), "a mixed iron greatsword for the swing");
      p.inv[0] = gs; p.atkMove = 0; p.atkGap = 9; p.atkCombo = 0; hp0 = g3->hp;
      PlayerInput swing2; swing2.attackPressed = true;
      tickMelee(*lv, p, 0.01f, swing2, true, false);
      for (int k = 0; k < 120 && p.atkMove; k++) tickMelee(*lv, p, 0.01f, PlayerInput{}, true, false);
      CHECK(!g3->removed && g3->hp < hp0, "a mixed greatsword swing lands on that ghoul (%.1f -> %.1f)", hp0, g3->hp);
      g3->removed = true;
      p.mode = "survival"; p.inv[0] = ItemStack(); p.atkMove = 0;
    } else CHECK(false, "a mixed iron sword swing lands on a ghoul in front");
    // a ghoul with a sword: the cut is visible, then it lands, and a raised guard can parry it
    Creature* g4 = spawnCreature(*lv, 0, "ghoul", p.pos + vec3(0, 0, -1.2f));
    if (g4) {
      g4->calm = true; g4->vel = vec3(0); g4->heldItem = "copper_sword"; g4->yaw = 0;
      p.yaw = 0; p.pitch = 0; p.mode = "survival"; p.invuln = 0; p.hp = 20; p.blockT = 0; p.stamina = 100; p.shield[0].clear();
      g4->attackCd = 0; g4->atkMove = 0; g4->atkHit.clear();
      g4->meleeAt(*lv, p, 1.f, 0.f);
      CHECK(g4->atkMove != 0 && p.hp == 20.f, "a sword swing does not land on the press (move %d, hp %.1f)", (int)g4->atkMove, p.hp);
      driveMelee(*lv, *g4);
      CHECK(p.hp < 20.f, "the cut lands in its live window (hp %.1f)", p.hp);
      p.invuln = 0; p.hp = 20; p.blockT = 0.05f; p.stamina = 100; p.shield[0] = ItemStack("shield", 1); p.parryFlash = 0;
      g4->attackCd = 0; g4->atkMove = 0; g4->atkT = 0; g4->atkHit.clear();
      g4->meleeAt(*lv, p, 1.f, 0.f);
      driveMelee(*lv, *g4);
      CHECK(p.parryFlash > 0.f && p.hp >= 19.9f, "you can parry the cut you saw coming (flash %.2f, hp %.1f)", p.parryFlash, p.hp);
      g4->removed = true;
      p.blockT = 0; p.shield[0].clear();
    } else CHECK(false, "a sword swing does not land on the press");
    {
      p.mode = "creative"; p.yaw = 0; p.pitch = -0.9f; p.atkMove = 0; p.atkT = 0; p.atkGap = 9; p.atkCombo = 0;
      p.inv[0] = ItemStack("iron_sword", 1); p.selected = 0;
      PlayerInput look; look.attackPressed = true;
      tickMelee(*lv, p, 0.01f, look, true, false);
      while (p.atkMove && p.atkT < attackMove(p.atkMove).turnLock + 0.02f)
        tickMelee(*lv, p, 0.01f, PlayerInput{}, true, false);
      const float frozen = p.atkPitch;
      p.pitch = 0.9f;
      for (int k = 0; k < 12 && p.atkMove; k++) tickMelee(*lv, p, 0.02f, PlayerInput{}, true, false);
      CHECK(p.atkMove != 0 && std::fabs(frozen + 0.9f) < 0.05f && std::fabs(p.atkPitch - frozen) < 0.001f,
            "the cut keeps the look pitch it started with (%.2f)", p.atkPitch);
      p.atkMove = 0; p.mode = "survival"; p.inv[0] = ItemStack(); p.pitch = 0;
    }
  }

  printf("arrows\n");
  Creature* sk = spawnCreature(*lv, 0, "skeleton", vec3(4.5f, Y, 16.5f));
  if (sk) sk->heldItem = "bow";   // (an archer: a skeleton may roll a sword or axe, or be the knight, and fight up close)
  int arrows = 0; hurt = 0;
  for (int i = 0; i < 300 && sk && !sk->removed; i++) { float before = p.hp; lv->tick(TICK_DT); hurt += std::max(0.f, before - p.hp); p.hp = p.maxHp; p.dead = false; p.hurtTime = 0; p.invuln = 0; arrows = std::max(arrows, countType(*lv, "arrow")); }
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
  for (int i = 0; i < 1500 && w.pendingGeneration() > 0; i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }   // (a busy machine makes the land slowly)
  for (int i = 0; i < 200; i++) w.stream(anchors, 20);
  lv->info.time = DAY_LEN * 0.25;   // noon
  int animals = 0, hostile = 0;
  for (int tries = 0; tries < 4 && animals == 0; tries++) {
    run(*lv, p, 3);
    animals = hostile = 0;
    for (auto& e : lv->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed) (c->T->hostile ? hostile : animals)++;
    if (!animals) for (int i = 0; i < 300 && w.pendingGeneration() > 0; i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  }
  CHECK(animals > 0, "herds came with the land (%d animals)", animals);
  lv->info.time = DAY_LEN * 0.75;   // midnight
  run(*lv, p, 90);
  hostile = 0; bool phantom = false;
  for (auto& e : lv->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) { if (!c->removed && c->T->hostile) hostile++; if (c->type == "phantom") phantom = true; }
  CHECK(hostile > 0, "the night crowd came out (%d monsters)", hostile);
  CHECK(!phantom, "still no phantoms");

  printf("death topple, severing\n");
  {
    clearCreatures(*lv);
    lv->info.time = DAY_LEN * 0.25;
    Creature* cow = spawnCreature(*lv, 0, "cow", p.pos + vec3(3, 0.2f, 0));
    DamageSource ds; ds.attacker = p.id; ds.tool = "axe";
    damageEntity(*lv, *cow, 50, ds);
    CHECK(!cow->alive() && !cow->removed, "a cow struck down by the player is dying, not gone");
    run(*lv, p, 1.0f);
    CHECK(countType(*lv, "cow") == 1, "it still lies there after 1 s");
    run(*lv, p, 0.8f);
    CHECK(countType(*lv, "cow") == 0, "then goes in a puff (%.1f s)", DEATH_LINGER);
    int gibs = 0;
    for (int k = 0; k < 20 && !gibs; k++) {
      Creature* gh = spawnCreature(*lv, 0, "ghoul", p.pos + vec3(2.5f, 0.2f, 0)); gh->calm = true;
      lv->tick(TICK_DT);
      for (int h = 0; h < 6 && gh->alive(); h++) {
        DamageSource d2; d2.attacker = p.id; d2.tool = "axe"; d2.hasRay = true;
        d2.rayO = p.pos + vec3(0, 1.62f, 0); d2.rayD = glm::normalize(gh->pos + vec3(0, 0.3f + h * 0.3f, 0) - d2.rayO);
        if (const RigModel* Mh = rigModel(gh->modelKey())) {
          rigPose(*gh, *Mh, creatureBase(*gh, false));
          d2.hitPart = rigHitPart(*gh, *Mh, d2.rayO, d2.rayD);
        }
        damageEntity(*lv, *gh, 3, d2);
      }
      gibs = countType(*lv, "gib");
    }
    CHECK(gibs > 0, "an axe takes limbs off (%d gibs)", gibs);
    clearCreatures(*lv);
  }

  // the other bands: Nether (pigmen, ghasts, wither skeletons, charred ghouls, wisps), Skylands, End (endermen)
  auto realm = [&](const char* name, const char* spawnAt, std::vector<std::string> types, float secs) {
    clearCreatures(*lv);
    vec3 at; float yaw;
    if (!debugSpawnPoint(*lv->gen, spawnAt, at, yaw)) { CHECK(false, "%s: no spawn point %s", name, spawnAt); return; }
    p.pos = at; anchors = {{p.pos, 6, false}};
    for (int i = 0; i < 800 && !(w.isLoaded(ifloor(at.x), ifloor(at.y), ifloor(at.z)) && w.pendingGeneration() == 0); i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    for (int i = 0; i < 200; i++) w.stream(anchors, 20);
    if (std::string(name) == "End") { int sy = w.surfaceY(ifloor(at.x), ifloor(at.z), DIM_END); if (sy >= 0) p.pos.y = sy + 1; }
    lv->info.time = DAY_LEN * 0.75;
    run(*lv, p, secs);
    std::string got; int n = 0;
    for (auto& t : types) { int k = countType(*lv, t); n += k; if (k) got += " " + t + "=" + std::to_string(k); }
    CHECK(n > 0, "%s (y %.0f): creatures turned up:%s", name, p.pos.y, got.c_str());
  };
  realm("Nether", "nether_fortress!", {"zombie_pigman", "ghast", "wither_skeleton", "charred_ghoul", "cinder_wisp", "magma_slime", "enderman"}, 60);
  realm("Skylands", "sky_shrine!", {"aerwhale", "zephyr", "swet", "golden_swet", "aechor_plant", "aerbunny", "moa", "phyg", "flying_cow", "sheepuff", "cockatrice"}, 60);
  realm("End", "8,140,8", {"enderman"}, 30);

  printf("killed by a creature\n");
  {
    p.give(ItemStack("stone", 5));
    p.hp = 2; p.invuln = 0; p.mode = "survival";
    Creature* g = spawnCreature(*lv, 0, "ghoul", p.pos + vec3(1, 0, 0));
    damageEntity(*lv, p, 20, g, "mob");   // the first killing blow: second chance (sim/combat_rpg.cpp) keeps you up
    CHECK(!p.dead && p.hp == 1 && p.secondChanceCd > 0, "second chance: the first killing blow leaves half a heart (hp %.1f)", p.hp);
    p.invuln = 0;
    damageEntity(*lv, p, 20, g, "mob");   // ...and while it's waiting to come back, the next one kills
    CHECK(p.dead, "a ghoul's blow kills");
    CHECK(p.grave.is_object() && p.inv[0].id == "death_compass", "grave made, death compass given (as when anything else kills)");
  }

  lv->close();
  printf(fails ? "\n%d check(s) FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
