// tools/test_mobs.cpp — every creature's place in the world, headless (dqcore only): what can be carried, tamed and
// ridden (mobs.json / data/mob_traits.json "carry", "tame", "mount": sim/creature.h, sim/mob_carry.h), and the water
// creatures swimming for real ("aquatic" / "amphibious": sim/creature_beasts.cpp aquaticSteer).
// Run: ./build/test_mobs [--userdir DIR] [--table] [--drops]   (exit code 0 = all passed; --table prints every creature's
// row, --drops what each one drops)
#include "core/init.h"
#include "core/registry.h"
#include "sim/creature.h"
#include "sim/mob_carry.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/combat.h"
#include <thread>
#include <cstdio>
#include <map>

using namespace dq;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static void run(Level& lv, std::vector<Player*> ps, float secs) {
  for (int i = 0; i < (int)(secs / TICK_DT); i++) { lv.tick(TICK_DT); for (Player* p : ps) { p->hp = p->maxHp; p->dead = false; } }
}
static void clearCreatures(Level& lv) { for (auto& e : lv.entities()) if (!e->isPlayer()) e->removed = true; lv.tick(TICK_DT); }
static Creature* findLive(Level& lv, const std::string& type) {
  for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && c->type == type) return c;
  return nullptr;
}

int main(int argc, char** argv) {
  CoreOptions co; co.server = true; co.userDirOverride = "/tmp/dq-test-mobs";
  bool table = false, dropsReport = false;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--userdir" && i + 1 < argc) co.userDirOverride = argv[++i];
    else if (std::string(argv[i]) == "--table") table = true;
    else if (std::string(argv[i]) == "--drops") dropsReport = true;
  }
  fs::remove_all(co.userDirOverride);
  if (!initCore(co)) { printf("initCore failed\n"); return 2; }

  // ------------------------------------------------------------------ the data
  printf("every creature's flags\n");
  int n = 0, tameable = 0, arms = 0, pets = 0, basket = 0, aquatic = 0, amphib = 0, mounts = 0, bad = 0;
  std::vector<std::string> aquatics, amphibians, tameables;
  for (auto& [k, v] : reg().mobs.items()) {
    const CreatureType* T = creatureType(k);
    if (!T) continue;
    n++;
    std::string why;
    auto flag = [&](bool wrong, const char* what) { if (wrong) { why += what; why += "; "; } };
    const std::string& cm = T->carryMode;
    flag(!(cm.empty() || cm == "any" || cm == "pet" || cm == "basket"), "unknown carry mode");
    flag(cm == "pet" && !T->tameable(), "a pet to carry that can't be tamed");
    flag(T->boss && (T->tameable() || !cm.empty() || T->mount), "a boss tamed, carried or ridden");
    flag(T->undead && T->tameable(), "an undead thing tamed");
    flag(T->hostile && cm == "any", "a monster anyone may pick up");
    flag(T->mount && !T->tameable() && k != "pig", "a mount that can't be tamed");
    flag(T->mount && cm == "pet", "a mount carried in the arms");
    flag(T->aquatic && T->amphibious, "both aquatic and amphibious");
    flag(T->aquatic && (!T->swims || T->walks), "aquatic but walks or doesn't swim");
    flag(T->water && T->swims && !T->walks && !T->aquatic, "a water creature not marked aquatic");
    flag(T->ai == "amphibian" && !T->amphibious, "an amphibian not marked amphibious");
    flag(T->amphibious && !T->swims, "amphibious but doesn't swim");
    if (v.contains("tame") && v["tame"].is_object() && v["tame"].contains("foods"))
      flag(v["tame"]["foods"].size() != T->tameFoods.size(), "a taming food that isn't an item");
    if (!why.empty()) { bad++; printf("  FAIL %s: %s\n", k.c_str(), why.c_str()); }
    tameable += T->tameable(); arms += cm == "any"; pets += cm == "pet"; basket += cm == "basket"; aquatic += T->aquatic; amphib += T->amphibious; mounts += T->mount;
    if (T->aquatic) aquatics.push_back(k);
    if (T->amphibious) amphibians.push_back(k);
    if (T->tameable()) tameables.push_back(k);
    if (table) printf("  %-26s %-8s w%.2f h%.2f %-10s carry:%-6s %s%s%s\n", k.c_str(), T->boss ? "boss" : T->hostile ? "hostile" : (T->ai == "neutral" || T->angersWhenHurt || T->retaliates) ? "neutral" : "passive",
                      T->w, T->h, T->aquatic ? "water" : T->amphibious ? "amphibious" : (T->fly > 0 || T->flyer || T->ai == "flyer") ? "air" : "land", cm.empty() ? "-" : cm.c_str(),
                      T->tameable() ? "tame " : "", T->mount ? "mount " : "", T->farm ? "breeds" : "");
  }
  CHECK(bad == 0, "%d creatures' flags agree with one another", n - bad);
  printf("  (%d creatures: %d tameable, %d mounts, %d carried by anyone, %d carried as pets, %d basket-only, %d aquatic, %d amphibious)\n",
         n, tameable, mounts, arms, pets, basket, aquatic, amphib);
  for (const char* k : {"wolf", "cat", "fox", "parrot", "rabbit", "maned_wolf", "toucan", "terrapin"}) CHECK(creatureType(k) && creatureType(k)->tameable(), "%s can be tamed", k);
  for (const char* k : {"wolf", "cat", "fox", "parrot"}) CHECK(creatureType(k) && creatureType(k)->carryMode == "pet", "a tamed %s is carried by its owner", k);
  for (const char* k : {"ghoul", "skeleton", "herobrine", "ender_dragon", "warden", "night_lord"}) if (creatureType(k)) CHECK(!creatureType(k)->tameable() && creatureType(k)->carryMode.empty(), "%s: never tamed or carried", k);
  CHECK(!isCreatureType("phantom"), "no phantoms, ever");
  for (const char* k : {"cod", "squid", "shark", "orca", "hammerhead_shark", "giant_squid", "blobfish"}) if (creatureType(k)) CHECK(creatureType(k)->aquatic, "%s lives in the water", k);
  for (const char* k : {"frog", "turtle", "axolotl", "crocodile", "seal", "terrapin"}) if (creatureType(k)) CHECK(creatureType(k)->amphibious, "%s lives in and out of it", k);

  // ------------------------------------------------------------------ a place
  ThreadPool pool(1);
  EventBus events;
  LevelInfo info; info.name = "MobsTest"; info.seedText = "hello"; info.time = DAY_LEN * 0.25;
  fs::create_directories(userDir() / "saves");
  auto lv = Level::create(userDir() / "saves", info, &pool, &events);
  World& w = lv->world();
  const int Y = dimBase(DIM_OVERWORLD) + 200;   // high in the sky, away from the terrain
  std::vector<StreamAnchor> anchors = {{vec3(16, Y, 16), 4, false}};
  for (int i = 0; i < 6000 && !(w.isLoaded(-8, Y, -8) && w.isLoaded(56, Y, 56) && w.pendingGeneration() == 0); i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  CHECK(w.isLoaded(0, Y, 0) && w.isLoaded(56, Y, 56), "terrain streamed in");
  const BlockId stone = reg().blockId("stone"), glass = reg().blockId("glass"), water = reg().blockId("water");
  // a stone floor (x -6..17) with glass round it, and beside it a water tank (x 20..51, 16 deep) under glass walls
  for (int x = -6; x < 56; x++) for (int z = -6; z < 40; z++) { w.setBlock(x, Y - 1, z, stone); for (int y = Y; y < Y + 20; y++) w.setBlock(x, y, z, AIR); }
  for (int x = -6; x < 18; x++) for (int z = -6; z < 40; z++) if (x == -6 || z == -6 || x == 17 || z == 39) for (int y = Y; y < Y + 3; y++) w.setBlock(x, y, z, glass);
  const int TX0 = 20, TX1 = 51, TZ0 = 2, TZ1 = 33, TOP = Y + 16;   // water fills y Y..TOP-1
  for (int x = TX0 - 1; x <= TX1 + 1; x++) for (int z = TZ0 - 1; z <= TZ1 + 1; z++) for (int y = Y; y < TOP + 2; y++) {
    const bool wall = x < TX0 || x > TX1 || z < TZ0 || z > TZ1;
    w.setBlock(x, y, z, wall ? glass : y < TOP ? water : AIR);
  }
  auto mk = [&](const char* name, vec3 at) {
    auto pl = std::make_unique<Player>(); pl->name = name; pl->pos = at; pl->mode = "creative";
    return static_cast<Player*>(lv->spawn(std::move(pl)));
  };
  Player& p = *mk("Tester", vec3(4.5f, Y, 4.5f));
  Player& other = *mk("Other", vec3(12.5f, Y, 4.5f));

  // ------------------------------------------------------------------ taming
  printf("taming: each creature's own food wins it over\n");
  int tamedOk = 0, tamedBad = 0, wrongOk = 0;
  for (const std::string& k : tameables) {
    const CreatureType* T = creatureType(k);
    const bool wet = T->aquatic;
    Creature* c = spawnCreature(*lv, 0, k, wet ? vec3(36.5f, Y + 6, 18.5f) : vec3(8.5f, Y, 20.5f));
    if (!c) { printf("  FAIL %s didn't spawn\n", k.c_str()); tamedBad++; continue; }
    if (k == "slime" || k == "magma_slime") c->size = 1;
    p.selected = 0; p.inv[0] = ItemStack("stick", 1);
    useOnCreature(*lv, p, *c, true);
    if (!c->owner.empty()) wrongOk++;
    p.inv[0] = ItemStack(T->tameFoods[0], 1);
    useOnCreature(*lv, p, *c, true);
    if (c->owner == p.name && c->tamed) tamedOk++; else { tamedBad++; printf("  FAIL %s wasn't tamed with %s\n", k.c_str(), T->tameFoods[0].c_str()); }
    c->removed = true; lv->tick(TICK_DT);
  }
  CHECK(tamedBad == 0, "%d of %zu tameable creatures tamed with their food", tamedOk, tameables.size());
  CHECK(wrongOk == 0, "a stick tames nothing");
  {   // once tamed, the new ones follow, sit and stand up like any pet
    Creature* mw = spawnCreature(*lv, 0, "maned_wolf", vec3(4.5f, Y, 30.5f));
    p.inv[0] = ItemStack("raw_meat", 1); useOnCreature(*lv, p, *mw, false);
    p.inv[0] = ItemStack();
    float d0 = glm::distance(mw->pos, p.pos);
    run(*lv, {&p, &other}, 8);
    float d1 = glm::distance(mw->pos, p.pos);
    CHECK(mw->owner == p.name && d1 < d0 - 10 && d1 < 8, "a tamed maned wolf comes to its owner (%.1f -> %.1f)", d0, d1);
    useOnCreature(*lv, p, *mw, false);
    CHECK(mw->stay, "  ...and sits when told");
    mw->removed = true;
  }
  clearCreatures(*lv);

  // ------------------------------------------------------------------ drops
  printf("drops: every creature you can kill leaves something\n");
  lv->info.rules["mobGriefing"] = false;   // (a hydrogen jellyfish bursts as it dies: not through the floor)
  {
    // (special: the townsfolk, a summoned spirit, the End's crystals and him keep their own ways)
    auto special = [](const std::string& k, const CreatureType* T) { return T->npc || T->spirit || k == "end_crystal" || k == "herobrine" || k == "villager" || k == "village_guard"; };
    int empty = 0, killed = 0, gave = 0;
    std::vector<std::string> none;
    for (auto& [k, v] : reg().mobs.items()) {
      const CreatureType* T = creatureType(k);
      if (!T || special(k, T)) continue;
      if (T->drops.empty() && !T->boss) { empty++; printf("  FAIL %s has nothing to drop\n", k.c_str()); }
      if (dropsReport) {
        std::string d; for (auto& e : T->drops) { char b[96]; std::snprintf(b, sizeof b, "%s%s %d-%d (%.0f%%)", d.empty() ? "" : ", ", e.id.c_str(), e.lo, e.hi, e.chance * 100); d += b; }
        printf("  %-26s %s%s\n", k.c_str(), d.empty() ? "-" : d.c_str(), T->boss ? "  [boss: its fight's own loot too]" : "");
      }
      if (T->boss || T->drops.empty()) continue;   // (a boss's own death: its fight's code)
      // the real death: killed by a player, up to 100 times until something falls (a fly: one kill in ten gives its string)
      bool any = false;
      for (int t = 0; t < 100 && !any; t++) {
        clearCreatures(*lv);
        Creature* c = spawnCreature(*lv, 0, k, T->aquatic ? vec3(36.5f, Y + 6, 18.5f) : vec3(8.5f, Y, 20.5f));
        if (!c) break;
        c->baby = false; c->data.erase("noDrops");
        DamageSrc src; src.type = "mob"; src.attacker = p.id;
        c->die(*lv, src);
        for (auto& e : lv->entities()) if (!e->removed && e->type == "item") { any = true; e->removed = true; }
      }
      killed++; gave += any;
      if (!any) none.push_back(k);
    }
    clearCreatures(*lv);
    CHECK(empty == 0, "every creature but the special ones has a drop table");
    // where the creatures with places of their own come out (sim/creature_spawn.cpp: spawnDim's sixth field)
    {
      auto livesIn = [&](const char* k) { std::string s; const json& m = reg().mobs.value(k, json());
        if (m.is_object() && m.contains("spawnDim")) for (auto& e : m["spawnDim"]) if (e.size() >= 6) for (auto& p : e[5]) s += p.get<std::string>() + " ";
        return s; };
      CHECK(!reg().mobs.contains("farseer") && !creatureType("farseer"), "the farseer is not in the game");
      CHECK(!reg().mobs.contains("banana_slug") && !creatureType("banana_slug"), "the banana slug is not in the game");
      CHECK(livesIn("guster").find("desert") != std::string::npos && livesIn("guster").find("forest") == std::string::npos, "the guster comes out only in deserts and the badlands (%s)", livesIn("guster").c_str());
      CHECK(livesIn("crimson_mosquito") == "crimson " && livesIn("warped_toad") == "warped " && livesIn("soul_vulture") == "soul ", "the Ember Depths' creatures each in their own woods and valleys");
      const json& lv = reg().mobs.value("laviathan", json());
      CHECK(lv.is_object() && lv["spawnDim"][0][4] == "l", "the laviathan comes up in lava, not on the ground");
    }
    std::string nl; for (auto& k : none) nl += " " + k;
    CHECK(none.empty(), "%d of %d killed creatures dropped something%s%s", gave, killed, none.empty() ? "" : ": none from", nl.c_str());
    // a tamed companion leaves nothing; a kept farm animal still gives its meat
    Creature* dog = spawnCreature(*lv, 0, "wolf", vec3(8.5f, Y, 20.5f));
    dog->owner = p.name; dog->tamed = true;
    for (int t = 0; t < 6; t++) { dog->deathTime = 0; DamageSrc src; src.attacker = p.id; dog->die(*lv, src); }
    int fell = 0; for (auto& e : lv->entities()) if (!e->removed && e->type == "item") { fell++; e->removed = true; }
    CHECK(fell == 0, "a tamed dog leaves nothing behind");
    clearCreatures(*lv);
  }

  // ------------------------------------------------------------------ carrying
  printf("picking up a pet\n");
  {
    PlayerController pc(*lv, p), pc2(*lv, other);
    PlayerInput sneak; sneak.sneak = true;
    p.selected = 0; for (auto& s : p.inv) s = ItemStack();
    other.selected = 0; for (auto& s : other.inv) s = ItemStack();
    Creature* dog = spawnCreature(*lv, 0, "wolf", vec3(6.5f, Y, 6.5f));
    pc.targetEnt = dog->id; pc.target = RayHit{};
    pc.use(false, sneak);
    CHECK(p.carrying.is_null() && !dog->removed, "a wild wolf won't be picked up");
    p.inv[0] = ItemStack("bone", 1); useOnCreature(*lv, p, *dog, false); p.inv[0] = ItemStack();
    dog->petName = "Rex";
    CHECK(dog->owner == p.name, "tamed");
    pc2.targetEnt = dog->id; pc2.target = RayHit{};
    pc2.use(false, sneak);
    CHECK(other.carrying.is_null() && !dog->removed, "someone else can't pick up your dog");
    pc.use(false, sneak);
    CHECK(p.carrying.is_object() && p.carrying.value("type", "") == "wolf" && dog->removed, "its owner picks it up (sneak + use, empty hand)");
    CHECK(!dog->stay, "  ...rather than telling it to sit");
    lv->tick(TICK_DT);
    pc.targetEnt = 0; pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(9, Y - 1, 9); pc.target.normal = ivec3(0, 1, 0); pc.target.id = stone;
    pc.placeCarried();
    Creature* back = findLive(*lv, "wolf");
    CHECK(p.carrying.is_null() && back && back->owner == p.name && back->tamed && back->petName == "Rex", "set down again: the same dog, still yours, still Rex");
    CHECK(back && std::fabs(back->pos.x - 9.5f) < 0.6f && std::fabs(back->pos.z - 9.5f) < 0.6f, "  ...where you put it");
    // carried when you fall: dropped as itself
    if (back) { pc.targetEnt = back->id; pc.target = RayHit{}; pc.use(false, sneak); }
    dropCarried(*lv, p);
    Creature* dropped = findLive(*lv, "wolf");
    CHECK(dropped && dropped->owner == p.name && dropped->petName == "Rex", "dropped when you fall, it's still your dog");
    clearCreatures(*lv);

    Creature* cat = spawnCreature(*lv, 0, "cat", vec3(6.5f, Y, 6.5f));
    CHECK(!canCarryInArms(*cat, p) && fitsBasket(*cat, &p), "a wild cat: not in your arms, but in a basket");
    Creature* cow = spawnCreature(*lv, 0, "cow", vec3(8.5f, Y, 12.5f));
    CHECK(canCarryInArms(*cow, other), "a cow: anyone lifts it");
    Creature* fish = spawnCreature(*lv, 0, "cod", vec3(36.5f, Y + 6, 18.5f));
    CHECK(!canCarryInArms(*fish, p) && fitsBasket(*fish, &p), "a fish: a basket, not your arms");
    Creature* ghoul = spawnCreature(*lv, 0, "ghoul", vec3(10.5f, Y, 12.5f));
    CHECK(!canCarryInArms(*ghoul, p) && !fitsBasket(*ghoul, &p), "a ghoul: neither");
    // the wicker basket: in, and out again as it was
    p.inv[0] = ItemStack("wicker_basket", 1);
    cat->petName = "Tibbles";
    pc.targetEnt = cat->id; pc.target = RayHit{};
    pc.use(false, PlayerInput{});
    CHECK(cat->removed && p.inv[0].tag.is_object() && p.inv[0].tag.value("mob", "") == "cat", "a wild cat goes in the basket");
    lv->tick(TICK_DT);
    pc.targetEnt = 0; pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(3, Y - 1, 12); pc.target.normal = ivec3(0, 1, 0); pc.target.id = stone;
    pc.use(false, PlayerInput{});
    Creature* out = findLive(*lv, "cat");
    CHECK(out && out->petName == "Tibbles" && !(p.inv[0].tag.is_object() && p.inv[0].tag.contains("mob")), "and comes out again");
    p.inv[0] = ItemStack();
    clearCreatures(*lv);
  }

  // ------------------------------------------------------------------ swimming
  printf("water creatures swim (spawned mid-water, 20 s each)\n");
  p.pos = vec3(TX0 - 3.5f, TOP + 1, 18.5f); other.pos = vec3(2.5f, Y, 2.5f);
  int swamOk = 0;
  for (const std::string& k : aquatics) {
    clearCreatures(*lv);
    const CreatureType* T = creatureType(k);
    const float midY = std::max((float)Y + 0.2f, Y + (16 - T->h) * 0.5f);
    Creature* c = spawnCreature(*lv, 0, k, vec3(36.5f, midY, 18.5f));
    if (!c) { CHECK(false, "%s spawned", k.c_str()); continue; }
    c->kept = true;
    int ticks = 0, wet = 0, poking = 0; float ylo = 1e9f, yhi = -1e9f, path = 0, sumY = 0; vec3 last = c->pos;
    for (int i = 0; i < (int)(20 / TICK_DT); i++) {
      lv->tick(TICK_DT); p.hp = p.maxHp; other.hp = other.maxHp;
      if (c->removed || !c->alive()) break;
      ticks++; wet += c->inWater;
      if (c->pos.y + c->h > TOP + 0.05f) poking++;
      ylo = std::min(ylo, c->pos.y); yhi = std::max(yhi, c->pos.y); path += glm::distance(c->pos, last); last = c->pos; sumY += c->pos.y;
    }
    const bool breathes = T->needsAir;   // (a dolphin, an orca: up for air now and then)
    const bool ok = ticks >= (int)(19.9f / TICK_DT) && wet >= ticks * 0.97f && (breathes || poking <= ticks / 50) && path > 3 && yhi - ylo > 0.3f;
    swamOk += ok;
    printf("  %s %-20s in water %3.0f%%, at the top %3.0f%%, depth %.1f..%.1f (mean %.1f under), swam %.1f blocks%s\n", ok ? "ok  " : "FAIL", k.c_str(),
           ticks ? 100.f * wet / ticks : 0.f, ticks ? 100.f * poking / ticks : 0.f, TOP - yhi - T->h, TOP - ylo - T->h, ticks ? TOP - sumY / ticks - T->h : 0.f, path, breathes ? " (breathes air)" : "");
    if (!ok) fails++;
  }
  CHECK(swamOk == (int)aquatics.size(), "%d of %zu aquatic creatures stay under, in the water, and get about in three dimensions", swamOk, aquatics.size());
  {   // a school keeps together
    clearCreatures(*lv);
    std::vector<Creature*> sc;
    for (int i = 0; i < 5; i++) sc.push_back(spawnCreature(*lv, 0, "cod", vec3(30.5f + i, Y + 6, 10.5f)));
    for (auto* c : sc) c->kept = true;
    run(*lv, {&p, &other}, 15);
    vec3 mid(0); for (auto* c : sc) mid += c->pos; mid /= (float)sc.size();
    float spread = 0; for (auto* c : sc) spread = std::max(spread, glm::distance(c->pos, mid));
    CHECK(spread < 8, "a school of cod keeps together (%.1f blocks from its middle at most)", spread);
  }
  printf("on land\n");
  {
    clearCreatures(*lv);
    Creature* fish = spawnCreature(*lv, 0, "cod", vec3(6.5f, Y, 26.5f));
    Creature* frog = spawnCreature(*lv, 0, "frog", vec3(10.5f, Y, 26.5f));
    fish->kept = frog->kept = true;
    const float hp0 = fish->hp, fhp0 = frog->hp;
    float moved = 0; vec3 last = fish->pos;
    for (int i = 0; i < (int)(14 / TICK_DT) && !fish->removed; i++) { lv->tick(TICK_DT); p.hp = p.maxHp; moved += glm::distance(fish->pos, last); last = fish->pos; }
    CHECK(moved > 1, "a fish on land flops about (%.1f blocks of thrashing)", moved);
    CHECK(fish->removed || fish->hp < hp0, "and suffocates (%.0f -> %.0f hp)", hp0, fish->hp);
    CHECK(!frog->removed && frog->hp >= fhp0, "a frog on land is fine");
    // an amphibian in the water swims under it too
    clearCreatures(*lv);
    for (const std::string& k : amphibians) {
      if (k == "laviathan") continue;   // (a lava dweller)
      const CreatureType* T = creatureType(k);
      Creature* c = spawnCreature(*lv, 0, k, vec3(36.5f, Y + 8, 18.5f));
      if (!c) continue;
      c->kept = true;
      int ticks = 0, wet = 0; float ylo = 1e9f, yhi = -1e9f, path = 0; vec3 last = c->pos;
      for (int i = 0; i < (int)(10 / TICK_DT) && !c->removed; i++) { lv->tick(TICK_DT); p.hp = p.maxHp; ticks++; wet += c->inWater; ylo = std::min(ylo, c->pos.y); yhi = std::max(yhi, c->pos.y); path += glm::distance(c->pos, last); last = c->pos; }
      // (in the water the whole time, never up at the top bobbing, and getting about)
      CHECK(wet >= ticks * 0.95f && path > 2 && ylo < TOP - T->h - 0.5f, "%s swims in the water, not floating on it (%.1f..%.1f under, swam %.1f)", k.c_str(), TOP - yhi - T->h, TOP - ylo - T->h, path);
      c->removed = true; lv->tick(TICK_DT);
    }
  }

  // ------------------------------------------------------------------ the stable
  printf("the stable: tamed creatures join it, and a fallen companion goes home to rest\n");
  {
    clearCreatures(*lv);
    p.pos = vec3(4.5f, Y, 4.5f);
    lv->info.data.erase("stable");   // (a fresh book: the taming checks above filled it)
    auto book = [&]() -> json { const json& D = lv->info.data; return D.contains("stable") && D["stable"].contains(p.name) ? D["stable"][p.name] : json::array(); };
    Creature* dog = spawnCreature(*lv, 0, "wolf", vec3(6.5f, Y, 8.5f));
    p.selected = 0; p.inv[0] = ItemStack("bone", 1);
    useOnCreature(*lv, p, *dog, false);
    dog->petName = "Rex";
    run(*lv, {&p, &other}, 2.5f);
    json b = book();
    CHECK(b.is_array() && b.size() == 1 && b[0].value("type", "") == "wolf" && b[0].value("state", "") == "out", "a dog tamed with a bone joins your stable");
    CHECK(!dog->data.contains("owner") && dog->owner == p.name, "  ...its owner kept in one place (the creature's own, not its data)");
    CHECK(b.size() == 1 && b[0].value("name", json()) == "Rex", "  ...by its name");
    const u32 dogId = dog->id;
    DamageSrc src; src.type = "mob"; src.attacker = other.id;
    dog->hurt(*lv, 1000, src);
    lv->tick(TICK_DT);
    int items = 0; for (auto& e : lv->entities()) if (!e->removed && e->type == "item") { items++; e->removed = true; }
    b = book();
    CHECK(!lv->entity(dogId) || lv->entity(dogId)->removed, "struck down, it leaves");
    CHECK(b.size() == 1 && b[0].value("state", "") == "resting" && items == 0, "  ...goes home to the stable to rest, and leaves nothing behind");
    // an old save: a companion the stable had called kept its owner in data; loaded, it's the creature's own
    auto e = createEntity("cat"); auto* oc = dynamic_cast<Creature*>(e.get());
    oc->data["owner"] = "Tester"; oc->data["petName"] = "Tibbles"; oc->data["tamed"] = true; oc->data["uid"] = "c1234";
    json saved = oc->save();
    auto e2 = createEntity("cat"); auto* nc = dynamic_cast<Creature*>(e2.get());
    nc->load(saved);
    CHECK(nc->owner == "Tester" && nc->tamed && nc->petName == "Tibbles" && !nc->data.contains("owner") && nc->data.value("uid", "") == "c1234", "an old save's companion: owner and name move onto the creature, its stable place stays");
    CHECK(petOwnerOf(*nc) == "Tester", "  ...and everything that asks whose it is hears the same");
    clearCreatures(*lv);
  }

  // ------------------------------------------------------------------ lava
  printf("a lava dweller swims in lava\n");
  if (creatureType("laviathan")) {
    clearCreatures(*lv);
    const BlockId lava = reg().blockId("lava");
    for (int x = TX0; x <= TX1; x++) for (int z = TZ0; z <= TZ1; z++) for (int y = Y; y < TOP; y++) w.setBlock(x, y, z, lava);
    lv->tick(TICK_DT);
    const CreatureType* T = creatureType("laviathan");
    CHECK(T->medium == 2 && T->amphibious, "the laviathan's element is lava");
    Creature* c = spawnCreature(*lv, 0, "laviathan", vec3(36.5f, Y + 6, 18.5f));
    if (c) {
      c->kept = true;
      const float hp0 = c->hp;
      int ticks = 0, in = 0; float ylo = 1e9f, yhi = -1e9f, path = 0; vec3 last = c->pos;
      for (int i = 0; i < (int)(10 / TICK_DT) && !c->removed; i++) { lv->tick(TICK_DT); p.hp = p.maxHp; ticks++; in += c->inWater; ylo = std::min(ylo, c->pos.y); yhi = std::max(yhi, c->pos.y); path += glm::distance(c->pos, last); last = c->pos; }
      CHECK(in >= ticks * 0.95f && path > 2 && ylo < TOP - T->h - 0.5f, "it swims through the lava, not floating on it nor sunk (%.1f..%.1f under, swam %.1f)", TOP - yhi - T->h, TOP - ylo - T->h, path);
      CHECK(!c->removed && c->hp >= hp0, "  ...unharmed");
    } else CHECK(false, "a laviathan spawned");
    clearCreatures(*lv);
  }

  printf(fails ? "\n%d check(s) FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
