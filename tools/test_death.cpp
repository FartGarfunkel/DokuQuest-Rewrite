// tools/test_death.cpp — headless checks of dying (sim only): the player's body keeps everything with its slot and
// gives it back to the same slots, the death compass points to the first body when you die twice, bodies survive a
// save and reload and are carried out of lava and the void, and a creature that dies goes limp and settles.
//   test_death [--userdir DIR]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/corpse.h"
#include "sim/ragdoll.h"
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/gameplay.h"
#include "sim/rules.h"
#include <thread>
#include <cstdio>
using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }
static EventBus bus;
static Level* L; static Player* P;
static void tick(double secs) { for (double t = 0; t < secs; t += TICK_DT) L->tick(TICK_DT); }
static std::vector<Entity*> corpses(Level& lv) { std::vector<Entity*> v; for (auto& e : lv.entities()) if (!e->removed && e->type == "corpse") v.push_back(e.get()); return v; }
static Entity* corpseById(Level& lv, const std::string& cid) { for (Entity* e : corpses(lv)) if (e->data.value("cid", std::string()) == cid) return e; return nullptr; }
static int countOf(Player& p, const std::string& id) {
  int n = 0;
  for (auto* v : {&p.inv, &p.armor, &p.offhand, &p.back, &p.hook, &p.charm, &p.cape, &p.quiver, &p.shield}) for (auto& s : *v) if (s.id == id) n += s.n;
  return n;
}
static void die(Level& lv, Player& p) { DamageSource s; s.type = "attack"; p.hp = 0; playerDie(lv, p, s); }
static void up(Level& lv, Player& p, vec3 at) { respawnPlayer(lv, p); p.pos = at; p.realm = REALM_MAIN; p.hasLastSafe = true; p.lastSafe = at; }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-death").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(2);
  LevelInfo li; li.name = "DeathTest"; li.seedText = "death";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  L = lvp.get();
  const fs::path folder = L->folder;
  World& w = L->world();
  const int Y = dimBase(DIM_OVERWORLD) + 200;   // a floor high in the sky
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
  const BlockId stone = reg().blockId("stone");
  for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) w.setBlock(x, Y, z, stone);
  L->info.spawn = vec3(8.5f, Y + 1, 8.5f);
  P->hasLastSafe = true; P->lastSafe = P->pos;
  check(rule(*L, "graves"), "the graves rule is on by default");

  // ------------------------------------------------------------ one death: every slot kept, every slot given back
  struct Put { std::vector<ItemStack> Player::* v; int i; ItemStack s; };
  std::vector<Put> puts = {
    {&Player::inv, 0, ItemStack("iron_sword", 1, 7)}, {&Player::inv, 3, ItemStack("dirt", 10)}, {&Player::inv, 8, ItemStack("torch", 5)},
    {&Player::inv, 20, ItemStack("cobblestone", 33)}, {&Player::inv, 35, ItemStack("stick", 2)},
    {&Player::armor, 0, ItemStack("iron_helmet", 1)}, {&Player::armor, 1, ItemStack("iron_chestplate", 1, 12)}, {&Player::armor, 2, ItemStack("iron_leggings", 1)}, {&Player::armor, 3, ItemStack("iron_boots", 1)},
    {&Player::offhand, 0, ItemStack("torch", 3)}, {&Player::back, 0, ItemStack("leather_backpack", 1)}, {&Player::hook, 0, ItemStack("lantern", 1)},
    {&Player::charm, 0, ItemStack("vigor_charm", 1)}, {&Player::cape, 0, ItemStack("white_cape", 1)}, {&Player::quiver, 0, ItemStack("quiver", 1)}, {&Player::shield, 0, ItemStack("shield", 1, 4)},
  };
  puts[10].s.tag = {{"c", json::array({json{{"id", "apple"}, {"n", 4}}, nullptr})}};   // the backpack's own contents ride inside it
  int placed = 0;
  for (auto& u : puts) if (reg().item(u.s.id)) { ((*P).*u.v)[u.i] = u.s; placed++; }
  const vec3 deathAt = P->pos;
  die(*L, *P);
  auto cs = corpses(*L);
  check(P->dead && cs.size() == 1, "dying leaves a body (no grave block)");
  Entity* body = cs.empty() ? nullptr : cs[0];
  check(body && glm::distance(body->pos, deathAt) < 1.5f, "  ...where they fell");
  check(body && (int)body->data.value("items", json::array()).size() == placed, "  ...holding every item (" + std::to_string(placed) + ")");
  int left = 0; for (auto* v : {&P->inv, &P->armor, &P->offhand, &P->back, &P->hook, &P->charm, &P->cape, &P->quiver, &P->shield}) for (auto& s : *v) if (!s.empty() && s.id != "death_compass") left++;
  check(left == 0, "nothing is left on the player");
  check(P->inv[0].id == "death_compass", "a death compass in the hand");
  const std::string cid1 = body ? body->data.value("cid", std::string()) : "";
  check(P->grave.is_object() && P->grave.value("cid", std::string()) == cid1 && P->stats["grave"] == P->grave, "the compass (Player::grave and stats grave) points at the body");
  tick(1);
  check(body && !body->removed && body->hp > 0 && damageEntity(*L, *body, 1000, DamageSource{}) == 0 && !body->removed, "a body can't be hurt or destroyed");
  up(*L, *P, vec3(4.5f, Y + 1, 4.5f));
  P->inv[3] = ItemStack("gravel", 1);   // a slot taken since: its thing goes to a free slot
  check(body && retrieveCorpse(*L, *P, *body), "using the body takes it back");
  bool exact = true;
  for (size_t k = 0; k < puts.size(); k++) {
    auto& u = puts[k];
    if (!reg().item(u.s.id) || (u.v == &Player::inv && u.i == 3)) continue;
    const ItemStack& s = ((*P).*u.v)[u.i];
    if (s.id != u.s.id || s.n != u.s.n || s.d != u.s.d || s.tag != u.s.tag) { exact = false; printf("     slot %zu holds %s x%d\n", k, s.id.c_str(), s.n); }
  }
  check(exact, "every item is back in its exact slot (damage and contents too)");
  check(P->inv[3].id == "gravel" && countOf(*P, "dirt") == 10, "an item whose slot was taken goes to a free slot");
  check(countOf(*P, "death_compass") == 0 && P->grave.is_null() && !P->stats.contains("grave"), "the compass is gone with the last body");
  tick(0.2);
  check(corpses(*L).empty(), "the body is gone");

  // ------------------------------------------------------------ walking into your body takes it back too
  for (auto* v : {&P->inv, &P->armor, &P->offhand, &P->back, &P->hook, &P->charm, &P->cape, &P->quiver, &P->shield}) for (auto& s : *v) s.clear();
  P->inv[4] = ItemStack("apple", 3);
  die(*L, *P);
  up(*L, *P, vec3(14.5f, Y + 1, 14.5f));
  tick(2.5);
  check(corpses(*L).size() == 1, "a body lies there while nobody touches it");
  P->pos = P->lastSafe = vec3(4.5f, Y + 1, 4.5f);
  tick(0.5);
  check(corpses(*L).empty() && P->inv[4].id == "apple" && P->inv[4].n == 3, "walking into your own body takes it back");

  // ------------------------------------------------------------ two deaths: the compass keeps to the first body
  for (auto* v : {&P->inv, &P->armor, &P->offhand, &P->back, &P->hook, &P->charm, &P->cape, &P->quiver, &P->shield}) for (auto& s : *v) s.clear();
  P->inv[2] = ItemStack("iron_sword", 1); P->armor[0] = ItemStack("iron_helmet", 1);
  P->pos = P->lastSafe = vec3(0.5f, Y + 1, 0.5f);
  die(*L, *P);
  const std::string first = P->grave.value("cid", std::string());
  up(*L, *P, vec3(20.5f, Y + 1, 20.5f));
  P->inv[1] = ItemStack("stick", 6);   // gathered on the way back (the compass in inv[0] too)
  die(*L, *P);
  auto two = corpses(*L);
  check(two.size() == 2, "dying again leaves a second body");
  check(P->grave.value("cid", std::string()) == first && deathBodies(*P).size() == 2, "the compass still points at the FIRST body");
  check(countOf(*P, "death_compass") == 1, "and you still have it");
  Entity* second = nullptr; for (Entity* e : two) if (e->data.value("cid", std::string()) != first) second = e;
  bool noCompass = second && second->data.value("items", json::array()).dump().find("death_compass") == std::string::npos;
  check(noCompass && second && second->data["items"].size() == 1, "the second body holds what you had since, not the compass");
  up(*L, *P, vec3(10.5f, Y + 1, 10.5f));
  check(second && retrieveCorpse(*L, *P, *second) && P->inv[1].id == "stick", "the second body can be taken back first");
  check(P->grave.value("cid", std::string()) == first && countOf(*P, "death_compass") == 1, "  ...and the compass keeps pointing at the first");
  // ------------------------------------------------------------ bodies survive a save and a reload
  P->pos = vec3(10.5f, Y + 1, 10.5f);
  L->savePlayer(P->name, P->save());
  L->save(true);
  lvp.reset(); L = nullptr;
  lvp = Level::open(folder, &pool, &bus);
  L = lvp.get();
  check(L != nullptr, "the world opens again");
  Entity* again = L ? corpseById(*L, first) : nullptr;
  check(again && again->data["items"].size() == 2, "the first body is still there after the reload, with its things");
  { auto p = std::make_unique<Player>(); p->load(L->loadPlayer("Tester")); P = static_cast<Player*>(L->spawn(std::move(p))); }
  check(P->grave.value("cid", std::string()) == first && deathBodies(*P).size() == 1, "the player still knows where it lies");
  World& w2 = L->world();
  t0 = nowSeconds();
  while (nowSeconds() - t0 < 60 && !w2.chunk(DIM_OVERWORLD, 0, 0)) { w2.stream({{P->pos, 2, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  for (int x = -12; x < 28; x++) for (int z = -12; z < 28; z++) w2.setBlock(x, Y, z, stone);   // (the test floor was never saved with a chunk)
  // ------------------------------------------------------------ safe from the void and from lava
  if (again) {
    again->pos.y = -40; tick(0.1);
    check(again->pos.y > Y - 2 && !again->removed, "a body that falls out of the world is carried back to safety");
    const BlockId lava = reg().blockId("lava");
    const ivec3 b(ifloor(again->pos.x), Y + 1, ifloor(again->pos.z));
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) w2.setBlock(b.x + dx, Y + 1, b.z + dz, lava);
    again->pos = vec3(b) + vec3(0.5f, 0.1f, 0.5f); tick(0.3);
    check(!again->removed && glm::distance(again->pos, vec3(b) + vec3(0.5f, 0.1f, 0.5f)) > 3, "a body in lava is carried out of it");
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) w2.setBlock(b.x + dx, Y + 1, b.z + dz, AIR);
    tick(0.6);
    check(P->grave.is_object() && P->grave.value("cid", std::string()) == first && std::abs(P->grave.value("x", 0) - ifloor(again->pos.x)) <= 1, "the compass follows the body where it was carried");
    P->pos = again->pos + vec3(3, 0, 0);
    check(retrieveCorpse(*L, *P, *again) && P->inv[2].id == "iron_sword" && P->armor[0].id == "iron_helmet", "the first body gives back its things into their slots");
    check(countOf(*P, "death_compass") == 0 && P->grave.is_null(), "with every body taken back, the compass is gone");
  }

  // ------------------------------------------------------------ a creature goes limp and settles
  {
    Creature* c = spawnCreature(*L, REALM_MAIN, "pig", vec3(6.5f, Y + 1, 12.5f));
    check(c != nullptr, "a pig to kill");
    if (c) {
      tick(0.3);
      DamageSource s; s.attacker = P->id; s.type = "attack";
      damageEntity(*L, *c, 1000, s);
      check(c->deathTime > 0, "the pig dies");
      RagdollSet rs;
      const u32 id = c->id;
      for (int i = 0; i < 6; i++) { tick(TICK_DT); rs.update(*L, REALM_MAIN, P->pos, (float)TICK_DT); }
      const CreatureRagdoll* r = rs.find(id);
      check(r && r->rd.parts() > 1 && !r->rd.frozen, "its death makes a ragdoll of its parts");
      std::vector<mat4> start; if (r) for (int i = 0; i < r->rd.parts(); i++) start.push_back(r->rd.partMatrix(i));
      for (int i = 0; i < 60 * 10 && rs.find(id) && !rs.find(id)->rd.frozen; i++) { tick(TICK_DT); rs.update(*L, REALM_MAIN, P->pos, (float)TICK_DT); }
      r = rs.find(id);
      check(r && r->rd.frozen, "the ragdoll settles and freezes" + std::string(r ? " (" + std::to_string(r->rd.age) + " s)" : ""));
      check(r && r->rd.lowest() > -0.25f && r->rd.lowest() < 0.6f, "  ...lying on the ground, not through it (lowest point " + std::string(r ? std::to_string(r->rd.lowest()) : "-") + ")");
      for (int i = 0; i < 60; i++) { tick(TICK_DT); rs.update(*L, REALM_MAIN, P->pos, (float)TICK_DT); }
      check((!L->entity(id) || L->entity(id)->removed) && rs.find(id), "the creature itself is gone, its limp body still lies there (only drawn)");
      bool moved = false;
      if (r) for (int i = 0; i < r->rd.parts() && i < (int)start.size(); i++) if (r->rd.posed(i)) { mat4 a = r->rd.partMatrix(i); moved |= glm::length(vec3(a[3]) - vec3(start[i][3])) > 0.05f; }
      check(moved, "  ...its parts moved: drawn from the simulated frames, not the old topple");
      for (int i = 0; i < 60 * 15; i++) rs.update(*L, REALM_MAIN, P->pos, (float)TICK_DT);
      check(!rs.find(id), "after a while it is gone");
    }
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  lvp.reset();
  fs::remove_all(ud, ec);
  return fails ? 1 : 0;
}
