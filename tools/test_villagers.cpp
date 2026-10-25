// tools/test_villagers.cpp — headless checks of the villagers (sim only): the data, the original's hashes and skin
// painter, a village coming to life near a player and going when they leave, the day (work, sleep in their beds),
// shops (prices, standing, stock, restock), errands and the board (deterministic per day, take, hunt, hand in,
// lapse), standing, the guard's word and the sword, the conversation, paths, the missing villager.
//   test_villagers [--userdir DIR]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/villagers.h"
#include "sim/content.h"
#include "sim/skills.h"
#include "sim/rpg.h"
#include "sim/mob_combat.h"
#include "world/dims.h"
#include <thread>
#include <cstdio>
using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }
struct RecFx : LevelFx {
  std::vector<std::string> msgs; int voices = 0;
  void message(u32, const std::string& t, const std::string&) override { msgs.push_back(t); }
  void voice(const std::string&, const std::string&, vec3, float) override { voices++; }
  bool heard(const std::string& s) const { for (auto& m : msgs) if (m.find(s) != std::string::npos) return true; return false; }
};
static Level* L; static Player* P; static EventBus bus; static RecFx fx;
static void tick(double secs) { for (double t = 0; t < secs; t += TICK_DT) L->tick(TICK_DT); }
static std::vector<Creature*> villagers(const std::string& vid) {
  std::vector<Creature*> out;
  for (auto& e : L->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && npcOf(*c) && npcOf(*c)->value("v", "") == vid) out.push_back(c);
  return out;
}
static Creature* byJob(const std::string& vid, const std::string& job) { for (auto* c : villagers(vid)) if (npcOf(*c)->value("j", "") == job) return c; return nullptr; }
static void setPhase(double ph) { L->info.time = std::floor(L->info.time / DAY_LEN) * DAY_LEN + ph * DAY_LEN; }
static int count(const std::string& id) { int n = 0; for (auto& s : P->inv) if (s.id == id) n += s.n; return n; }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-villagers").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }

  // ------------------------------------------------------------ the data and the original's numbers
  check(VD().contains("jobs") && VD()["shops"].size() == 16 && VD()["riftLore"].size() > 30, "data/villagers.json: jobs, 16 shops, the rift lore");
  check(VD()["shops"]["librarian"]["buy"].size() > 20, "the shops carry the other modules' wares (tomes, robes, potions...)");
  { Village vv; vv.seed = 12345; check(npcSeedOf(vv, 2) == 45406 && npcName(45406) == "Bram", "npcSeedOf / npcName match the original"); }
  { Standing s = standingOf(36); check(s.k == 4 && s.name == "Trusted" && s.tier == 2 && s.next == 70, "standingOf(36) = Trusted, tier 2, next 70"); }
  { NpcVoice v = npcVoice(4242); check(std::fabs(v.pitch - 254.1957f) < 0.01f && !v.triangle && v.seed == 71, "npcVoice matches the original"); }
  {
    std::vector<u8> px; npcSkinPixels(777, "farmer", px);
    u32 h = 0; for (u8 b : px) h = h * 31 + b;
    const u8* p = px.data() + (8 * 64 + 8) * 4;
    check(h == 2483321190u, "npcSkinCanvas(777, farmer): every pixel the same as the original (hash " + std::to_string(h) + ")");
    check(p[0] == 210 && p[4] == 212 && px[(11 * 64 + 10) * 4 + 0] == 60, "the skin's face pixels");
  }

  ThreadPool pool(2);
  LevelInfo li; li.name = "VillagerTest"; li.seedText = "villagers";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  L = lvp.get(); L->fx = &fx;
  L->info.rules["mobSpawning"] = false;   // no night crowd on the test floor
  World& w = L->world();
  const int base = dimBase(DIM_OVERWORLD), Y = base + 200, ly = 200;   // a test floor high in the sky
  { auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 20.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }
  double t0 = nowSeconds();
  while (nowSeconds() - t0 < 60) {
    w.stream({{P->pos, 3, false}}, 20);
    bool all = true;
    for (int cx = -2; cx <= 2; cx++) for (int cz = -2; cz <= 2; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
    if (all) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(w.chunk(DIM_OVERWORLD, 0, 0) != nullptr, "the test chunks load");
  BlockId stone = reg().blockId("stone");
  for (int x = -24; x < 40; x++) for (int z = -24; z < 40; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 8; y++) w.setBlock(x, y, z, AIR); }
  P->onGround = true;

  // ------------------------------------------------------------ a village by hand (the structures' plan shape, y band-local)
  auto spot = [&](int x, int z, int f) { return json{{"at", json::array({x, ly + 1, z})}, {"face", f}}; };
  auto bed = [&](int x, int z) { return json{{"at", json::array({x, ly + 1, z})}, {"f", 0}}; };
  json folk = json::array({
    {{"home", bed(-6, -6)}, {"job", "farmer"}, {"work", spot(14, -6, 0)}},
    {{"home", bed(-6, 0)}, {"job", "librarian"}, {"work", spot(4, 4, 2)}},
    {{"home", bed(-6, 6)}, {"job", "elder"}, {"work", spot(8, 6, 0)}},
  });
  json plan = {{"kind", "village"}, {"id", "t5000_5000"}, {"name", "Testbury"}, {"style", "plains"}, {"tier", "village"}, {"x", 8}, {"z", 0}, {"y", ly}, {"P", 7}, {"hw", 1}, {"r", 20},
               {"board", json::array({10, ly + 1, 2})}, {"bell", json::array({8, ly + 1, 2})}, {"folk", folk}, {"guards", 1}, {"golems", 0}, {"seats", json::array()},
               {"blds", json::array()}, {"roads", json::array({{{"d", 1}, {"L", 12}, {"hw", 1}}})}, {"seed", 12345}};
  addVillage(*L, plan);
  VillageRef v = villageById(*L, "t5000_5000");
  check(v && v->name == "Testbury" && v->y == Y && v->folk.size() == 3 && v->folk[0].work == ivec3(14, Y + 1, -6), "a village plan is read (band-local y made global)");
  check(villagesNear(*L, 8, 20, 72).size() == 1 && villageAtPos(*L, REALM_MAIN, P->pos, 4) != nullptr, "villagesNear / villageAtPos find it");
  check(contentHooks().villageNear && contentHooks().villageNear(*L, P->pos, 30).has_value(), "the content hook villageNear answers from the villages");

  // ------------------------------------------------------------ coming to life near a player
  setPhase(0.1);   // morning: off to work
  tick(2);
  auto folkNow = villagers(v->id);
  check(folkNow.size() == 4, "the villagers come out when someone is near (3 folk + 1 guard): " + std::to_string(folkNow.size()));
  Creature* farmer = byJob(v->id, "farmer"); Creature* lib = byJob(v->id, "librarian"); Creature* elder = byJob(v->id, "elder"); Creature* guard = byJob(v->id, "guard");
  check(farmer && lib && elder && guard, "farmer, librarian, elder and a guard");
  check(guard && guard->heldItem == "iron_sword" && guard->data["npcArmor"].size() == 4, "the guard carries a sword and wears iron");
  check(farmer && npcOf(*farmer)->value("n", "") == npcName(npcSeedOf(*v, 0)), "names from their look seed");
  tick(25);
  check(farmer && std::hypot(farmer->pos.x - 14.5f, farmer->pos.z + 5.5f) < 1.5f, "in the morning the farmer walks to work (" + std::to_string(farmer ? farmer->pos.x : 0) + ", " + std::to_string(farmer ? farmer->pos.z : 0) + ")");
  check(lib && (npcOf(*lib)->value("p", 0) & NP_READ), "the librarian reads at work");
  setPhase(0.7);   // night: home to bed
  tick(30);
  check(farmer && std::hypot(farmer->pos.x + 5.5f, farmer->pos.z + 5.5f) < 1.2f && (npcOf(*farmer)->value("p", 0) & NP_SLEEP), "at night the farmer goes home to bed and sleeps");
  check(farmer && std::fabs(farmer->pos.y - (Y + 1 + 0.5625f)) < 0.05f, "lying on the bed");
  {
    fx.msgs.clear();
    bool used = useOnCreature(*L, *P, *farmer, false);
    check(used && fx.heard("fast asleep"), "using a sleeping villager: \"is fast asleep\"");
  }
  setPhase(0.2);
  tick(3);

  // ------------------------------------------------------------ the conversation and the shop
  json opened; int hScr = bus.on("villagerScreen", [&](const json& a) { opened = a; return true; });
  P->pos = lib->pos + vec3(0, 0, 2);
  check(useOnCreature(*L, *P, *lib, false) && opened.is_array() && opened[1] == "talk" && opened[2]["npc"] == lib->id, "using a villager opens the conversation (villagerScreen talk)");
  Talk t; talkOpen(*L, *P, *lib, t);
  check(t.title == "Librarian of Testbury" && !t.text.empty() && t.opts.size() >= 3 && t.opts[0].act == "shop" && t.opts.back().label == "Goodbye", "the librarian greets you; wares first, goodbye last");
  bool hasPath = false; for (auto& o : t.opts) if (o.act == "path") hasPath = o.label.find("Mage") != std::string::npos;
  check(hasPath, "the librarian offers the Mage's ways (paths.js)");
  tick(0.5);
  check(npcOf(*lib)->value("p", 0) & NP_TALK, "they stand and talk while you talk with them");
  for (auto& o : t.opts) if (o.act == "shop") { talkChoose(*L, *P, t, (int)(&o - &t.opts[0])); break; }
  check(t.mode == "shop", "\"Let me see your wares\" opens the shop");
  auto lines = shopLines(*L, *P, *v, npcOf(*lib)->value("i", 0), "librarian", "buy");
  check(!lines.empty() && lines[0].id == "book" && lines[0].price == 1 && lines[0].left == 12 && !lines[0].can, "the book for 1 emerald, 12 a day; can't afford it yet");
  P->give(ItemStack("emerald", 30));
  int books = count("book"), em = count("emerald");
  talkShopTrade(*L, *P, t, 0);
  check(count("book") == books + 1 && count("emerald") == em - 1, "buying a book: an emerald for a book");
  int locked = -1; lines = shopLines(*L, *P, *v, 1, "librarian", "buy"); for (auto& l : lines) if (l.locked) { locked = l.i; break; }
  std::string said; check(locked >= 0 && !shopTrade(*L, *P, *v, 1, "librarian", "buy", locked, said) && said.find("Help the village") != std::string::npos, "a Friend-only ware is kept back for strangers");
  P->give(ItemStack("paper", 24));
  em = count("emerald");
  check(shopTrade(*L, *P, *v, 1, "librarian", "sell", 0, said) && count("emerald") == em + 1 && count("paper") == 0, "selling 24 paper for an emerald");
  for (int i = 0; i < 11; i++) shopTrade(*L, *P, *v, 1, "librarian", "buy", 0, said);
  check(!shopTrade(*L, *P, *v, 1, "librarian", "buy", 0, said) && said.find("All gone") != std::string::npos, "twelve a day, then sold out");
  L->info.day++;
  check(shopLines(*L, *P, *v, 1, "librarian", "buy")[0].left == 12, "they restock each morning");
  talkShopBack(*L, *P, t);
  check(t.mode == "home" && t.text == "Anything else?", "back from the shop: \"Anything else?\"");

  // ------------------------------------------------------------ standing
  double r0 = repOf(*P, v->id);
  check(r0 > 0 && r0 <= 6, "trading earns a little standing (capped per day): " + std::to_string(r0));
  fx.msgs.clear();
  npcRep(*L, *P, v->id, 20);
  check(fx.heard("Testbury now counts you a Friend"), "a new standing is told: \"now counts you a Friend\"");
  lines = shopLines(*L, *P, *v, 1, "librarian", "buy");
  check(locked >= 0 && !lines[locked].locked, "as a Friend the Friend wares open");

  // ------------------------------------------------------------ errands and the board
  json q1 = npcQuestFor(*L, *v, 0), q2 = npcQuestFor(*L, *v, 0);
  check(q1 == q2, "today's errand is the same each time you ask");
  json b1 = boardQuests(*L, *v), b2 = boardQuests(*L, *v);
  check(b1 == b2 && b1.size() >= 3 && b1[0]["kind"] == "hunt" && b1[2]["kind"] == "fetch", "the board: two bounties and a supply job, the same all day");
  L->info.day++;
  check(boardQuests(*L, *v)[0]["id"] != b1[0]["id"], "new bounties every morning");
  json hunt = boardQuests(*L, *v)[0];
  check(questTake(*L, *P, hunt) && findQuest(*P, hunt["id"]) != nullptr, "taking a bounty");
  std::string fam = hunt["fam"], mob = VD()["huntFamilies"][fam][0];
  int need = hunt["need"];
  for (int i = 0; i < need; i++) {
    Creature* m = spawnCreature(*L, REALM_MAIN, mob, P->pos + vec3(3, 0, 0));
    if (m) { m->persistent = true; DamageSrc s; s.attacker = P->id; m->hurt(*L, 500, s); }
  }
  json* hq = findQuest(*P, hunt["id"]);
  check(hq && (*hq)["have"].get<int>() == need && questReady(*L, *P, *hq), "slaying the bounty's monsters counts (" + mob + ")");
  em = count("emerald");
  int rew = hunt["reward"]["em"];
  check(questFinish(*L, *P, hunt["id"]) && count("emerald") == em + rew && !findQuest(*P, hunt["id"]) && npcStats(*P)["qdone"].contains(hunt["id"].get<std::string>()), "handing it in: emeralds, and it's done");
  json sup = boardQuests(*L, *v)[2];
  questTake(*L, *P, sup);
  L->info.day += 7;
  fx.msgs.clear(); questLapse(*L, *P);
  check(!findQuest(*P, sup["id"]) && fx.heard("has lapsed"), "an unfinished board job lapses after seven days");
  for (int i = 0; i < 5; i++) questTake(*L, *P, {{"id", "x" + std::to_string(i)}, {"kind", "fetch"}, {"item", "stone"}, {"need", 1}, {"vid", v->id}, {"giver", -1}, {"title", "t"}});
  fx.msgs.clear();
  check(!questTake(*L, *P, {{"id", "x9"}, {"kind", "fetch"}, {"item", "stone"}, {"need", 1}, {"title", "t"}}) && fx.heard("five jobs"), "five jobs at most");
  for (int i = 0; i < 5; i++) questDrop(*L, *P, "x" + std::to_string(i));
  // an errand through the conversation
  int giver = -1; json errand;
  for (int d = 0; d < 20 && giver < 0; d++) { for (int i = 0; i < 3; i++) { json q = npcQuestFor(*L, *v, i); if (q.is_object()) { giver = i; errand = q; break; } } if (giver < 0) L->info.day++; }
  check(giver >= 0, "someone has an errand for you within a few days");
  if (giver >= 0) {
    Creature* who = giver == 0 ? farmer : giver == 1 ? lib : elder;
    P->pos = who->pos + vec3(0, 0, 2);
    talkOpen(*L, *P, *who, t);
    int oi = -1; for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == "offer") oi = i;
    check(oi >= 0 && t.opts[oi].label == "Any work for me?", "\"Any work for me?\"");
    talkChoose(*L, *P, t, oi);
    check(t.text.find("I can pay") != std::string::npos, "they tell you what they need and what they pay");
    talkChoose(*L, *P, t, 0);
    check(findQuest(*P, errand["id"]) && t.text == "Thank you! Come back when you have it.", "\"I'll do it\": the errand is yours");
    P->give(ItemStack(errand["item"].get<std::string>(), errand["need"].get<int>()));
    talkOpen(*L, *P, *who, t);
    oi = -1; for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == "errand") oi = i;
    check(oi >= 0 && t.opts[oi].label.find("Here's what you asked for") == 0, "\"Here's what you asked for\" once you have it");
    talkChoose(*L, *P, t, oi);
    check(!findQuest(*P, errand["id"]) && t.text.find("Wonderful!") == 0, "handing in an errand");
  }

  // ------------------------------------------------------------ accidents happen: the guard's word, then the sword
  {
    tick(1);
    P->pos = farmer->pos + vec3(1.5f, 0, 0);
    double rep0 = repOf(*P, v->id);
    opened = nullptr;
    DamageSrc s; s.attacker = P->id; s.weapon = true;
    farmer->hurt(*L, 1, s);
    check(repOf(*P, v->id) == rep0 - 1, "hitting a villager costs a little standing");
    tick(12);
    check(opened.is_array() && opened[1] == "talk" && opened[2].value("confront", false), "the guard comes over to have a word");
    Talk c; talkOpen(*L, *P, *guard, c, true, opened[2].value("victim", ""));
    check(c.mode == "confront" && c.text.find("What do you think you're doing") != std::string::npos && c.opts.size() == 3, "\"Hey! I saw that.\" — sorry, sorry, or mind your own business");
    talkChoose(*L, *P, c, 0);
    check(c.text.find("Accidents happen") != std::string::npos, "saying sorry makes up for it");
    farmer->hurt(*L, 1, s);
    tick(0.5);
    check(repOf(*P, v->id) <= rep0 - 1 - 4 + 1 + 0.01, "doing it again while they remember: no accident (-4)");
    fx.msgs.clear();
    guard->removed = true;   // a new guard (with nothing against you) comes out
    tick(2);
    guard = byJob(v->id, "guard");
    check(guard != nullptr, "someone new takes the watch");
  }

  // ------------------------------------------------------------ paths
  {
    talkOpen(*L, *P, *guard, t);
    int pi = -1; for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == "path") pi = i;
    check(pi >= 0 && t.opts[pi].label.find("Knight") != std::string::npos, "a guard teaches the Knight's ways");
    talkChoose(*L, *P, t, pi);
    check(t.text.find("Come back when you've seen more fighting") == 0, "not before combat level 20");
    for (const char* k : {"melee", "defence", "archery"}) P->stats["skills"][k] = skillXpFor(20);
    check(combatLevel(*P) == 23, "combat level from melee, defence and archery (sheet.js)");
    float hp0 = P->maxHp;
    check(pathPick(*L, *P, "knight") && onPath(*P, "knight"), "walking the Knight's path");
    tick(6);
    check(P->maxHp == hp0 + 4, "a Knight has two more hearts");
    check(pathPoints(*P) == 1 && pathLearn(*L, *P, "heavy") && pathMeleeFactor(*P) > 1.09f, "a talent point: Heavy Blows");
    check(!pathLearn(*L, *P, "ironwill"), "the second tier opens after two talents");
    P->give(ItemStack("emerald", 20));
    check(pathForget(*L, *P) && !onPath(*P, "knight"), "forgetting it all for 20 emeralds");
    tick(6);
    check(P->maxHp == hp0, "and the hearts go with it");
  }

  // ------------------------------------------------------------ the missing villager
  {
    json lost; i64 d0 = L->info.day;
    for (int d = 0; d < 40 && lost.is_null(); d++, L->info.day++) for (auto& q : boardQuests(*L, *v)) if (q["kind"] == "rescue") { lost = q; break; }
    check(lost.is_object() && lost["title"].get<std::string>().rfind("Missing: ", 0) == 0, "now and then the board asks after someone who never came back");
    (void)d0;
  }

  // ------------------------------------------------------------ going when everyone has left
  P->pos = vec3(8.5f, Y + 1, 400.5f);
  tick(3);
  check(villagers(v->id).empty(), "the villagers go when everyone has left");
  bus.off(hScr);

  printf("\n%d passed, %d failed\n", passes, fails);
  lvp->close();
  return fails ? 1 : 0;
}
