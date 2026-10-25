// tools/test_endstory.cpp — headless checks of the End and its fight, dragons, the Deep Dark's guardian, the watcher
// and the story (sim/endstory*, end_fight, dragons, deepdark, haunt, rift).
//   test_endstory [--userdir DIR]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/endstory.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "sim/mob_combat.h"
#include "sim/player.h"
#include "sim/content.h"
#include "sim/commands.h"
#include "sim/portals.h"
#include "gen/dimgens.h"
#include <thread>
#include <cstdio>
using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }
struct RecFx : LevelFx {
  std::vector<std::string> msgs, sounds;
  void message(u32, const std::string& t, const std::string&) override { msgs.push_back(t); }
  void sound(const std::string& n, vec3, float, float, float) override { sounds.push_back(n); }
  void soundUi(u32, const std::string& n) override { sounds.push_back(n); }
  bool heard(const std::string& s) const { for (auto& m : msgs) if (m.find(s) != std::string::npos) return true; return false; }
  bool sound_(const std::string& s) const { return std::find(sounds.begin(), sounds.end(), s) != sounds.end(); }
};

static Level* L; static Player* P; static EventBus bus; static RecFx fx;
static BlockId B(const char* k) { return reg().blockId(k); }
static void tick(double secs) { for (double t = 0; t < secs; t += TICK_DT) { L->tick(TICK_DT); P->hp = P->maxHp; P->hurtTime = 0; } }
static void load(vec3 at, int r) {
  World& w = L->world(P->realm);
  double t0 = nowSeconds();
  int dim = dimOfY(ifloor(at.y));
  while (nowSeconds() - t0 < 90) {
    w.stream({{at, r, false}}, 30);
    bool all = true;
    for (int cx = -r + 1; cx < r; cx++) for (int cz = -r + 1; cz < r; cz++) if (!w.chunk(dim, (ifloor(at.x) >> 4) + cx, (ifloor(at.z) >> 4) + cz)) all = false;
    if (all) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}
static std::vector<Creature*> all(const std::string& type) {
  std::vector<Creature*> v;
  for (auto& e : L->entities()) if (!e->removed && e->type == type) if (auto* c = dynamic_cast<Creature*>(e.get())) v.push_back(c);
  return v;
}
static int countItems(const std::string& id) {
  int n = 0;
  for (auto& e : L->entities()) if (!e->removed && e->type == "item" && e->data.value("stack", json::object()).value("id", "") == id) n += e->data["stack"].value("n", 1);
  return n;
}
static void kill(Creature& c, Entity* by = nullptr) { DamageSrc s; s.type = "mob"; if (by) s.attacker = by->id; c.invulnT = 0; c.hurt(*L, 1e5f, s); }
static bool use(const std::string& item, ivec3 hit = ivec3(0), int face = -1) { return bus.emit("itemUse", json::array({P->name, item, hit.x, hit.y, hit.z, face})); }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-endstory").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(2);
  LevelInfo li; li.name = "EndTest"; li.seedText = "endstory"; li.seed = 12345;
  li.rules = json{{"mobSpawning", false}};
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  L = lvp.get(); L->fx = &fx;
  World& w = L->world();
  { auto p = std::make_unique<Player>(); p->name = "Tester"; P = static_cast<Player*>(L->spawn(std::move(p))); }

  check(!isCreatureType("phantom"), "no phantoms, ever");
  check(rigModel("ender_dragon") && rigModel("ender_dragon")->rig == "dragon" && rigModel("end_crystal"), "dragon and crystal rigs load");

  // ------------------------------------------------------------ the End: the fight sets itself out
  const EndGen* eg = dynamic_cast<const EndGen*>(L->gen->forDim(DIM_END));
  check(eg != nullptr, "the End has its generator");
  int fy = dimBase(DIM_END) + eg->fountainY();
  P->pos = vec3(20.5f, fy + 2, 0.5f);
  load(vec3(0, fy, 0), 7);
  check(w.chunk(DIM_END, 0, 0) != nullptr, "the main island loads");
  tick(2.5);
  auto crystals = all("end_crystal"), dragons = all("ender_dragon");
  check(crystals.size() == eg->pillars().size() && crystals.size() == 10, "ten end crystals on the pillars (" + std::to_string(crystals.size()) + ")");
  check(dragons.size() == 1, "the Ender Dragon is roused");
  check(fx.heard("The Ender Dragon! Break the crystals"), "the fight's opening toast");
  tick(4.5);
  check(fx.heard("Somewhere over the main island, a dragon keeps watch"), "the first visit's warning (4 s later)");
  if (!dragons.empty()) {
    Creature& d = *dragons[0];
    d.hp = 300; tick(2);
    check(d.hp > 300, "the crystals heal the dragon (" + std::to_string((int)d.hp) + ")");
    tick(20);
    check(d.pos.y > fy + 10 && d.noGravity, "the dragon flies high over the island (state " + d.state + ")");
  }
  // break a crystal
  if (!crystals.empty()) {
    int idx = crystals[0]->data.value("crystalIdx", -1);
    kill(*crystals[0], P); tick(0.5);
    const json& E = L->info.data["end"];
    bool gone = false; for (auto& g : E["crystalsGone"]) if (g == idx) gone = true;
    check(gone, "a broken crystal stays broken (crystalsGone)");
    check(fx.heard("A crystal shatters"), "a crystal shatters toast");
    tick(1.5);
    check(all("end_crystal").size() == 9, "it isn't set out again");
  }
  // the dragon falls
  if (!dragons.empty()) {
    for (auto* c : all("end_crystal")) kill(*c, P);
    tick(0.2);
    check(fx.heard("The last crystal shatters"), "the last crystal");
    int xp0 = P->xp;
    kill(*dragons[0], P);
    tick(1.2);
    const json& E = L->info.data["end"];
    check(E.value("slain", 0) == 1 && E.value("gates", 0) == 1, "slain once, one gateway");
    check(w.getBlock(1, fy, 0) == B("end_portal") && w.getBlock(-2, fy, 1) == B("end_portal"), "the exit portal opens");
    check(w.getBlock(0, fy + 4, 0) == B("dragon_egg_ender"), "its egg rests on the pillar");
    check(P->xp - xp0 >= 1200, "the dragon's experience");
    vec3 g = endGatewaySpot(0);
    load(g, 3); tick(2.5);
    check(w.getBlock((int)g.x, (int)g.y, (int)g.z) == B("end_gateway"), "a gateway opens round the main island");
    check(countItems("dragon_scale") >= 14, "dragon scales drop");
    check(fx.heard("The Ender Dragon is slain!"), "the victory toast");
    // through the gateway
    P->pos = g + vec3(0.5f, -0.4f, 0.5f);
    tick(0.2);
    check(std::hypot(P->pos.x, P->pos.z) > 600, "the gateway flings you out to the far islands (" + std::to_string((int)std::hypot(P->pos.x, P->pos.z)) + ")");
    tick(2);
    check(all("ender_dragon").empty(), "no second dragon once slain");
  }
  // ------------------------------------------------------------ endermen
  {
    P->pos = vec3(20.5f, fy + 2, 0.5f); load(P->pos, 3);
    Creature* m = spawnCreature(*L, REALM_MAIN, "enderman", vec3(26.5f, fy + 1, 0.5f));
    tick(0.5);
    DamageSrc a; a.type = "arrow"; a.attacker = P->id;
    float before = m->hp; float dealt = m->hurt(*L, 10, a);
    check(dealt == 0 && m->hp == before, "arrows never touch an enderman");
    m->angryT = 0; P->pos = vec3(m->pos.x, m->pos.y, m->pos.z + 8); P->yaw = 0;   // looking toward -z: at it
    vec3 eye = P->pos + vec3(0, EYE, 0), to = m->pos + vec3(0, m->h - 0.3f, 0) - eye;
    P->yaw = std::atan2(-to.x, -to.z); P->pitch = std::atan2(to.y, std::hypot(to.x, to.z));
    tick(1);
    check(m->angryT > 0 || m->foe == P->id, "meeting its eyes angers it");
    m->removed = true;
  }
  // ------------------------------------------------------------ pearls, eyes, chorus fruit
  {
    P->pos = vec3(20.5f, fy + 2, 0.5f); load(P->pos, 3);
    for (int x = 10; x < 40; x++) for (int z = -10; z < 10; z++) { w.setBlock(x, fy + 1, z, B("obsidian")); for (int y = fy + 2; y < fy + 8; y++) w.setBlock(x, y, z, AIR); }
    P->pos = vec3(14.5f, fy + 2, 0.5f); P->vel = vec3(0); P->yaw = -PI / 2; P->pitch = 0.1f;   // facing +x
    P->inv[0] = ItemStack("ender_pearl", 2); P->selected = 0;
    bool r = use("ender_pearl");
    check(!r && P->inv[0].n == 1, "a pearl is thrown (the use is taken)");
    tick(2);
    check(P->pos.x > 20, "the pearl carries you where it lands (x " + std::to_string((int)P->pos.x) + ")");
    P->inv[0] = ItemStack("chorus_fruit", 1); vec3 p0 = P->pos;
    use("chorus_fruit");
    check(glm::distance(P->pos, p0) > 0.5f && P->inv[0].empty(), "chorus fruit takes you somewhere nearby");
  }
  // ------------------------------------------------------------ a wild dragon in its lair, and its egg
  const int OB = dimBase(DIM_OVERWORLD), Y = OB + 200;
  {
    P->pos = vec3(1000.5f, Y + 1, 0.5f); load(P->pos, 2);
    for (int x = 970; x < 1030; x++) for (int z = -30; z < 30; z++) w.setBlock(x, Y, z, B("stone"));
    P->onGround = true; P->pos = vec3(1015.5f, Y + 1, 45.5f);   // far from it at first
    Creature* d = spawnCreature(*L, REALM_MAIN, "frost_dragon", vec3(1015.5f, Y + 1, 0.5f));
    d->state = "sleep"; d->sleeping = true; d->kept = true; d->data["lair"] = true; d->data["lairId"] = "Ltest";
    tick(1);
    check(d->state == "sleep", "a dragon asleep stays asleep while you're far");
    P->pos = vec3(1000.5f, Y + 1, 0.5f);
    d->pos = vec3(1010.5f, Y + 1, 0.5f);
    tick(0.5);
    check(d->state == "takeoff" || d->state == "circle", "it wakes when you come close");
    check(fx.heard("The Frost Dragon wakes!"), "the waking toast");
    bool chilled = false;
    for (int i = 0; i < 90 && !chilled; i++) { tick(0.5); chilled = playerEffect(*P, "chill") > 0 || playerEffect(*P, "slow") > 0; }
    check(chilled, "its frost breath chills you (state " + d->state + ")");
    int xp0 = P->xp;
    kill(*d, P); tick(1.2);
    check(L->info.data["dragons"]["Ltest"].value("slain", 0) == 1 && P->xp - xp0 >= 250, "slain, its lair is quiet for good");
    // an egg kept warm hatches a drake that is yours
    w.setBlock(1002, Y + 1, 0, B("dragon_egg_frost")); w.setBlock(1003, Y + 1, 0, B("campfire"));
    tick(1.5);
    json& eggs = L->info.data["eggs"];
    auto egg = [&]() -> json* { for (auto& e : eggs) if (e.value("x", 0) == 1002 && e.value("y", 0) == Y + 1) return &e; return nullptr; };
    check(egg() && (*egg()).value("owner", "") == "Tester", "a dragon egg set down is noticed");
    tick(4);
    check(egg() && (*egg()).value("t", 0) >= 3 && (*egg()).value("warm", false), "the campfire keeps it warm");
    if (egg()) (*egg())["t"] = 298;
    tick(3);
    auto drakes = all("frost_drake");
    check(drakes.size() == 1 && drakes[0]->owner == "Tester" && drakes[0]->baby, "it hatches: a frost drake, yours");
    check(fx.heard("A frost drake has hatched"), "the hatching toast");
    if (!drakes.empty()) {
      Creature& k = *drakes[0];
      P->inv[0] = ItemStack("beef", 3); P->selected = 0;
      float g0 = k.growT; useOnCreature(*L, *P, k, false);
      check(k.growT <= g0 - 150 && P->inv[0].n == 2, "feeding the young one makes it grow faster");
      k.growT = 0.01f; tick(0.2);
      check(!k.baby, "it grows up");
      P->inv[0] = ItemStack("saddle", 1); useOnCreature(*L, *P, k, false);
      check(k.saddled, "a grown drake takes a saddle");
      P->inv[0] = ItemStack(); useOnCreature(*L, *P, k, false);
      check(P->riding == k.id && k.riddenBy == P->id, "and you can ride it");
      rideControl(*L, *P, 1, 0, true, false, false); tick(0.4);
      check(k.noGravity, "jump: it takes to the air");
      rideControl(*L, *P, 0, 0, false, false, true); tick(0.1);
      check(P->riding == k.id, "sneaking in the air flies it down instead of throwing you off");
      dismount(*L, k, *P);
    }
    // hit volumes along the body
    Creature* b = spawnCreature(*L, REALM_MAIN, "sand_dragon", vec3(1020.5f, Y + 1, 10.5f)); b->yaw = 0;
    float t = dragonRayT(*b, vec3(1020.5f, Y + 2.5f, 0.5f), vec3(0, 0, 1));
    check(t > 0 && t < 10, "a boss dragon's hit volumes follow its body");
    b->removed = true;
  }
  // ------------------------------------------------------------ the Deep Dark: the sculk listens, the Warden comes
  {
    const int DY = OB + 30;   // well inside the Deep Dark's depth
    P->pos = vec3(2000.5f, DY + 1, 0.5f); load(P->pos, 2);
    for (int x = 1985; x < 2016; x++) for (int z = -15; z < 16; z++) { w.setBlock(x, DY, z, B("stone")); for (int y = DY + 1; y < DY + 6; y++) w.setBlock(x, y, z, AIR); }
    w.setBlock(2004, DY + 1, 0, B("sculk_sensor"));
    w.setBlock(2006, DY + 1, 2, B("sculk_shrieker"), 1);   // one that can call it
    P->onGround = true; P->sneaking = false;
    L->info.data["playTime"] = 2000.0;
    for (int i = 0; i < 4; i++) {
      vibrate(*L, REALM_MAIN, P->pos, P->id, "step");
      // the shrieker's ten seconds of quiet between screams
      tick(i < 3 ? 10.5 : 0.3);
    }
    check(P->data.value("darkT", 0.0) > 0, "a shriek brings the dark down over your eyes");
    check(fx.heard("The sculk shrieks") && fx.heard("Another shriek") && fx.heard("It is coming closer"), "the warnings, one by one");
    auto wd = all("warden");
    check(wd.size() == 1 && fx.heard("It is here."), "the fourth time, it comes");
    if (!wd.empty()) {
      Creature& W = *wd[0];
      DamageSrc s; s.type = "mob"; s.attacker = P->id;
      check(W.hurt(*L, 5, s) == 0 && W.pos.y < P->pos.y - 0.5f, "rising out of the ground it can't be hurt");
      tick(5);
      check(fx.sound_("warden_roar"), "it rises and roars");
      W.hurt(*L, 5, s);
      P->pos = W.pos + vec3(4, 0, 0);
      bool hit = false; float hp = P->hp;
      for (int i = 0; i < 40 && !hit; i++) { L->tick(TICK_DT * 3); hit = P->hp < hp || fx.sound_("warden_boom") || fx.sound_("warden_hit"); }
      for (int i = 0; i < 200 && !hit; i++) { L->tick(TICK_DT); hit = P->hp < hp || fx.sound_("warden_boom") || fx.sound_("warden_hit"); }
      check(hit, "struck, it knows who did it and comes for them");
      W.removed = true; P->hp = P->maxHp;
    }
  }
  // ------------------------------------------------------------ the watcher
  {
    auto cmds = allChatCommands();
    bool listed = false; for (auto& c : cmds) if (c.name == "herobrine") listed = true;
    check(!listed && findChatCommand("herobrine"), "the command exists but is never listed");
    P->pos = vec3(1000.5f, Y + 1, 0.5f);
    std::string r = runCommand(*L, P, "/herobrine");
    check(r == "Removed Herobrine.", "it always says the same");
    tick(0.1);
    int watchers = 0; for (auto& e : L->entities()) if (!e->removed && e->type == "watcher") watchers++;
    check(watchers <= 1, "at most one figure at a time");
    check(runCommand(*L, P, "/herobrine trace") == "Removed Herobrine.", "a trace on demand");
  }
  // ------------------------------------------------------------ the story
  {
    check(riftTier(*L) <= 0 && riftStability(*L) == 100, "before the rift: all quiet");
    check(riftRec(*L).value("frags", 0) == 2, "the Ender Dragon and a lair dragon each gave a Realm Fragment (rpg.js great foes)");
    riftRec(*L)["frags"] = 0;
    int frag0 = countItems("realm_fragment");
    riftOpenAt(*L, "vtest", "Testvale", vec3(1000.5f, Y + 1, 10.5f), 0);
    tick(1.2);
    check(riftRec(*L).value("stage", 0) == 1 && riftTier(*L) == 1, "the rift opens in the square");
    PortalSystem* ps = portalSystem(*L);
    bool tear = false; if (ps) for (auto& win : ps->windows(REALM_MAIN)) if (win.kind == PortalKind::Rift && !win.walk) tear = true;
    check(tear, "a window onto his realm (a torn virtual portal, not walkable yet)");
    check(!riftElderLine(*L).empty() && riftElderLabel(*L) == "The rift in the square\xE2\x80\xA6", "the elder knows the old tale");
    RiftLine q = riftAsk(*L, *P, "guard", {"farmer"});
    check(!q.text.empty() && !q.toast.empty(), "a villager has something to say about it");
    // a keeper falls
    Creature* k = spawnCreature(*L, REALM_MAIN, "ghoul", vec3(1005.5f, Y + 1, 0.5f)); k->data["dungeon"] = "d1_1";
    kill(*k, P); tick(1.5);
    check(riftRec(*L).value("frags", 0) == 1 && riftStability(*L) == 74 && countItems("realm_fragment") == frag0 + 1, "a keeper's Realm Fragment thins the boundary");
    tick(2);
    check(fx.heard("The world shudders. (1 of 4 fragments)"), "the world shudders");
    // the rift wants four
    P->pos = vec3(1001.5f, Y + 1, 10.5f);
    P->inv[3] = ItemStack("realm_fragment", 2);
    use("");
    check(fx.heard("but it wants 4. You have 2."), "two aren't enough");
    P->inv[3] = ItemStack("realm_fragment", 4);
    use("");
    check(riftRec(*L).value("stage", 0) == 3 && P->inv[3].empty() && riftTier(*L) == 4, "four fragments: the gateway steadies");
    tick(1.2);
    bool gate = false; if (ps) for (auto& win : ps->windows(REALM_MAIN)) if (win.kind == PortalKind::Rift && win.walk) gate = true;
    check(gate, "the gateway can be walked through");
    // his realm
    riftTravel(*L, *P, true);
    check(P->realm == REALM_HIS && fx.heard("His realm."), "through the rift: his realm");
    load(P->pos, 2);
    P->pos = vec3(1 * 112 + 30.5f, dimBase(DIM_OVERWORLD) + 41, 0.5f); tick(1.2);
    check(all("rift_ghoul").size() + all("rift_skeleton").size() + all("rift_spider").size() >= 5, "each hall has its waiting crowd");
    for (const char* t : {"rift_ghoul", "rift_skeleton", "rift_spider"}) for (auto* c : all(t)) c->removed = true;
    P->pos = vec3(4 * 112 + 20.5f, dimBase(DIM_OVERWORLD) + 41, 0.5f); load(P->pos, 3); tick(1.2);
    auto him = all("herobrine");
    check(him.size() == 1, "the last hall: him");
    if (!him.empty()) {
      him[0]->hp = him[0]->maxHp * 0.6f; tick(0.2);
      check(all("rift_ghoul").size() + all("rift_skeleton").size() + all("rift_spider").size() >= 4, "he calls them through");
      kill(*him[0], P); tick(1.2);
      check(riftRec(*L).value("sealed", 0) == 1 && riftTier(*L) == 5 && countItems("watcher_blade") == 1, "beaten: the rift is sealed, his blade yours");
    }
    check(riftStability(*L) == 100 && riftCorruptSay(*L, "farmer").empty(), "the villages at peace");
    riftTravel(*L, *P, false);
    check(P->realm == REALM_MAIN, "and home again");
  }
  check(all("phantom").empty(), "still no phantoms");
  printf("\n%d passed, %d failed\n", passes, fails);
  L->close();
  return fails ? 1 : 0;
}
