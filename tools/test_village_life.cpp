// tools/test_village_life.cpp — headless checks of what a village gives back (sim/village_life.h): each trade's requests
// (the fisher wants fish, the smith iron; the trade's own skill learnt by it; bigger asks as you grow), the doors standing
// opens (a Friend's advanced lessons and cheaper forgetting; an Honoured guest's house, one nobody lived in, and the
// watch's patrols and escorts; a Hero's statue in the square and the village's waystone linked for free), standing lost
// taking them back, the Shrine of Renewal (the talents and stat points taken back for coins) and the shrine in a new
// world's village square.
//   test_village_life [--userdir DIR]       exit code 0 = everything passed
#include "core/init.h"
#include "sim/villagers.h"
#include "sim/villagers_internal.h"
#include "sim/village_life.h"
#include "sim/village_theft.h"
#include "sim/content.h"
#include "sim/travel.h"
#include "sim/skills.h"
#include "sim/skilltree.h"
#include "sim/rpg.h"
#include "gen/structures.h"
#include "gen/structures_api.h"
#include "world/dims.h"
#include <thread>
#include <cstdio>
#include <functional>
using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); fflush(stdout); (ok ? passes : fails)++; }
struct RecFx : LevelFx {
  std::vector<std::string> msgs;
  void message(u32, const std::string& t, const std::string&) override { msgs.push_back(t); }
  bool heard(const std::string& s) const { for (auto& m : msgs) if (m.find(s) != std::string::npos) return true; return false; }
};
static Level* L; static Player* P; static EventBus bus; static RecFx fx;
static void tick(double secs) { for (double t = 0; t < secs; t += TICK_DT) L->tick(TICK_DT); }
static Creature* byJob(const std::string& vid, const std::string& job) {
  for (auto& e : L->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && npcOf(*c) && npcOf(*c)->value("v", "") == vid && npcOf(*c)->value("j", "") == job) return c;
  return nullptr;
}
static int count(const std::string& id) { int n = 0; for (auto& s : P->inv) if (s.id == id) n += s.n; return n; }
static int optOf(const Talk& t, const std::string& act) { for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == act) return i; return -1; }
static void setLevel(const std::string& skill, int lvl) { P->stats["skills"][skill] = skillXpFor(lvl); }
static void setRep(const std::string& vid, double r) { npcRep(*L, *P, vid, r - repOf(*P, vid)); }
static double teachOf(const json& R) { return R.is_object() && R.contains("skill") && R["skill"].is_array() && R["skill"].size() > 1 && R["skill"][1].is_number() ? R["skill"][1].get<double>() : 0; }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-village-life").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }

  // ------------------------------------------------------------ the data
  {
    const BlockId sh = reg().blockId("respec_shrine");
    check(sh && reg().block(sh).has("respecShrine") && reg().block(sh).str("shape") == "respec_shrine" && reg().item("respec_shrine"), "the Shrine of Renewal: a block with its own model, and an item");
    const json& TW = VD().value("tradeWants", json::object());
    int jobs = 0; for (auto& [j, x] : VD()["shops"].items()) if (TW.contains(j)) jobs++;
    check(TW.size() >= 19 && jobs >= 17, strFormat("each trade has its wants (%d trades; %d of the shops' trades)", (int)TW.size(), jobs));
    check(tradeSkill("fisherman") == "fishing" && tradeSkill("farmer") == "farming" && tradeSkill("armorer") == "smithing" && tradeSkill("baker") == "cooking" &&
          tradeSkill("cleric") == "alchemy" && tradeSkill("librarian") == "magic" && tradeSkill("shepherd") == "taming" && tradeSkill("guard").empty(),
          "each trade teaches its own skill (fisher fishing, farmer farming, smith smithing, baker cooking, healer alchemy, librarian magic, shepherd taming)");
  }

  ThreadPool pool(2);
  LevelInfo li; li.name = "VillageLifeTest"; li.seedText = "village life";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  L = lvp.get(); L->fx = &fx;
  L->info.rules["mobSpawning"] = false;
  World& w = L->world();
  const int base = dimBase(DIM_OVERWORLD), Y = base + 200, ly = 200;   // a test floor high in the sky
  { auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 20.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }
  for (double t0 = nowSeconds(); nowSeconds() - t0 < 900;) {
    w.stream({{vec3(8, Y, 20), 5, false}}, 20);
    bool all = true;
    for (int cx = -3; cx <= 4; cx++) for (int cz = -2; cz <= 4; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
    if (all) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(w.chunk(DIM_OVERWORLD, 0, 0) && w.chunk(DIM_OVERWORLD, 4, 4), "the test chunks load");
  const BlockId stone = reg().blockId("stone"), planks = reg().blockId("planks");
  for (int x = -40; x < 72; x++) for (int z = -30; z < 76; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 10; y++) w.setBlock(x, y, z, AIR); }
  // a house nobody lives in: walls x 20..26, z 8..14, its door on the north side at x 23
  for (int x = 20; x <= 26; x++) for (int z = 8; z <= 14; z++) {
    const bool edge = x == 20 || x == 26 || z == 8 || z == 14;
    for (int y = Y + 1; y <= Y + 3; y++) if (edge && !(x == 23 && z == 8 && y <= Y + 2)) w.setBlock(x, y, z, planks);
    w.setBlock(x, Y + 4, z, planks);
  }
  P->onGround = true;

  // ------------------------------------------------------------ the village: a fisher, a farmer, an armourer, the librarian, a baker, a healer, a shepherd, the elder
  auto spot = [&](int x, int z, int f) { return json{{"at", json::array({x, ly + 1, z})}, {"face", f}}; };
  auto bed = [&](int x, int z) { return json{{"at", json::array({x, ly + 1, z})}, {"f", 0}}; };
  const char* JOBS[] = {"fisherman", "farmer", "armorer", "librarian", "baker", "cleric", "shepherd", "elder"};
  json folk = json::array();
  for (int i = 0; i < 8; i++) folk.push_back({{"home", bed(-12 + i * 2, -14)}, {"job", JOBS[i]}, {"work", spot(-12 + i * 2, -4, 0)}});
  json plan = {{"kind", "village"}, {"id", "t6000_6000"}, {"name", "Hollowmere"}, {"style", "plains"}, {"tier", "village"}, {"x", 8}, {"z", 0}, {"y", ly}, {"P", 7}, {"hw", 1}, {"r", 20},
               {"board", json::array({10, ly + 1, 2})}, {"bell", json::array({6, ly + 1, 2})}, {"folk", folk}, {"guards", 1}, {"golems", 0}, {"seats", json::array()},
               {"blds", json::array({{{"type", "house_s"}, {"x0", 20}, {"z0", 8}, {"x1", 26}, {"z1", 14}, {"y", ly}, {"W", 7}, {"D", 7}, {"H", 4}, {"door", json::array({23, ly + 1, 8})}}})},
               {"roads", json::array()}, {"seed", 4242}};
  addVillage(*L, plan);
  json plan2 = {{"kind", "village"}, {"id", "t6001_6000"}, {"name", "Brookfield"}, {"style", "plains"}, {"tier", "village"}, {"x", 8}, {"z", 60}, {"y", ly}, {"P", 7}, {"hw", 1}, {"r", 10},
                {"board", json::array({10, ly + 1, 62})}, {"bell", json::array({6, ly + 1, 62})}, {"folk", json::array()}, {"guards", 0}, {"golems", 0}, {"seats", json::array()},
                {"blds", json::array()}, {"roads", json::array()}, {"seed", 777}};
  addVillage(*L, plan2);
  VillageRef v = villageById(*L, "t6000_6000"), v2 = villageById(*L, "t6001_6000");
  check(v && v2, "two villages by hand: Hollowmere and Brookfield");
  if (!v || !v2) { printf("%d passed, %d failed\n", passes, fails); return 1; }
  const std::string vid = v->id;
  auto idxOf = [&](const std::string& job) { for (int i = 0; i < (int)v->folk.size(); i++) if (v->folk[i].job == job) return i; return -1; };
  L->info.time = std::floor(L->info.time / DAY_LEN) * DAY_LEN + 0.2 * DAY_LEN;
  tick(3);
  Creature* fisher = byJob(vid, "fisherman"); Creature* lib = byJob(vid, "librarian"); Creature* elder = byJob(vid, "elder"); Creature* guard = byJob(vid, "guard");
  check(fisher && lib && elder && guard, "the fisher, the librarian, the elder and a guard come out");
  if (!fisher || !lib || !elder || !guard) { printf("%d passed, %d failed\n", passes, fails); return 1; }
  P->give(ItemStack("gold_coin", 64)); P->give(ItemStack("gold_coin", 64));

  // ------------------------------------------------------------ the trades' requests
  const int fi = idxOf("fisherman");
  {
    json q = tradeRequestFor(*L, *v, fi, *P), q2 = tradeRequestFor(*L, *v, fi, *P);
    check(q.is_object() && q == q2, "the fisher has a request today, the same each time you ask");
    if (!q.is_object()) q = json::object();
    const json R = q.is_object() ? q.value("reward", json::object()) : json::object();
    const std::string item = q.is_object() ? q.value("item", std::string()) : "";
    check(q.is_object() && q.value("kind", "") == "fetch" && jflag(q, "trade") && q.value("giver", -1) == fi && (item == "cod" || item == "salmon" || item == "tropical_fish" || item == "pufferfish"),
          "it's for fish (" + item + ")");
    check(R.contains("skill") && R["skill"].is_array() && R["skill"][0] == "fishing" && teachOf(R) > 0 && R.value("em", 0) > 0 && R.value("rep", 0) > 0,
          "and pays coins, standing and Fishing experience (" + R.dump() + ")");
    setLevel("fishing", 60);
    json q3 = tradeRequestFor(*L, *v, fi, *P);
    if (!q3.is_object()) q3 = json::object();
    check(q3.is_object() && teachOf(q3.value("reward", json::object())) > teachOf(R) && (q3.value("item", "") != item || q3.value("need", 0) > q.value("need", 0)),
          strFormat("a better fisher is asked for more (or finer fish) and taught more (%d %s -> %d %s, xp %.0f -> %.0f)", q.value("need", 0), item.c_str(), q3.value("need", 0),
                    q3.value("item", std::string()).c_str(), teachOf(R), teachOf(q3.value("reward", json::object()))));
    setLevel("fishing", 1);
    std::map<std::string, std::string> skillOf;
    for (int i = 0; i < (int)v->folk.size(); i++) { json t = tradeRequestFor(*L, *v, i, *P); if (t.is_object()) skillOf[v->folk[i].job] = t["reward"]["skill"][0]; }
    check(skillOf["farmer"] == "farming" && skillOf["armorer"] == "smithing" && skillOf["baker"] == "cooking" && skillOf["cleric"] == "alchemy" && skillOf["librarian"] == "magic" && skillOf["shepherd"] == "taming",
          "the farmer's requests teach Farming, the armourer's Smithing, the baker's Cooking, the healer's Alchemy, the librarian's Magic, the shepherd's Taming");
    json b = boardQuests(*L, *v, P), b0 = boardQuests(*L, *v);
    int trades = 0, trades0 = 0; for (auto& x : b) trades += jflag(x, "trade"); for (auto& x : b0) trades0 += jflag(x, "trade");
    check(trades >= 1 && trades <= 4 && trades0 == 0, strFormat("the board posts the trades' requests for whoever reads it (%d of them)", trades));
    // through the conversation
    P->pos = fisher->pos + vec3(0, 0, 2);
    Talk t; talkOpen(*L, *P, *fisher, t);
    int oi = optOf(t, "trade");
    check(oi >= 0, "the fisher asks: anything for my trade? (" + (oi >= 0 ? t.opts[oi].label : std::string("none")) + ")");
    if (oi >= 0) {
      talkChoose(*L, *P, t, oi);
      check(t.text.find("I can pay") != std::string::npos, "they say what they need and pay");
      talkChoose(*L, *P, t, 0);
      check(findQuest(*P, q["id"]) != nullptr, "taken");
      P->give(ItemStack(item, q.value("need", 1)));
      talkOpen(*L, *P, *fisher, t);
      int hi = -1; for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == "errand" && t.opts[i].arg == q["id"]) hi = i;
      check(hi >= 0 && t.opts[hi].label.find("Here's what you asked for") == 0, "\"Here's what you asked for\"");
      const double fx0 = skillXp(*P, "fishing"); const int gc = count("gold_coin"); const double r0 = repOf(*P, vid);
      if (hi >= 0) talkChoose(*L, *P, t, hi);
      check(!findQuest(*P, q["id"]) && count(item) == 0 && skillXp(*P, "fishing") >= fx0 + teachOf(R) - 1 && count("gold_coin") == gc + R.value("em", 0) && repOf(*P, vid) > r0,
            strFormat("handed in: the fish go, Fishing grows (%.0f -> %.0f), coins and standing", fx0, skillXp(*P, "fishing")));
    }
  }

  // ------------------------------------------------------------ a Friend: the trainers' advanced lessons, cheaper forgetting
  for (const char* s : {"melee", "defence", "magic"}) setLevel(s, 40);
  P->stats["path"] = {{"k", "mage"}, {"t", json::object()}};
  setRep(vid, 8);
  {
    P->pos = lib->pos + vec3(0, 0, 2);
    Talk t; talkOpen(*L, *P, *lib, t);
    check(optOf(t, "lesson") < 0 && pathForgetPrice(*L, *P) == 20, "a Visitor gets no advanced lesson, and forgetting costs the full 20");
    setRep(vid, 20);
    talkOpen(*L, *P, *lib, t);
    const int pts0 = pathPoints(*P), oi = optOf(t, "lesson");
    check(oi >= 0, "a Friend: the librarian offers the Mage an advanced lesson (" + (oi >= 0 ? t.opts[oi].label : std::string("none")) + ")");
    if (oi >= 0) talkChoose(*L, *P, t, oi);
    check(pathLessons(*P) == 1 && pathPoints(*P) == pts0 + 1, strFormat("the lesson: a talent point more (%d -> %d)", pts0, pathPoints(*P)));
    talkOpen(*L, *P, *lib, t);
    check(optOf(t, "lesson") < 0, "one lesson a village till you're Honoured there");
    check(pathForgetPrice(*L, *P) < 20, strFormat("a trainer here lets a Friend forget their path for less (%d gold coins)", pathForgetPrice(*L, *P)));
    P->pos = vec3(8.5f, Y + 1, 40.5f);   // out of the village
    check(pathForgetPrice(*L, *P) == 20, "out of the village, the full price");
  }

  // ------------------------------------------------------------ Honoured: a house of your own, the watch's work
  {
    P->pos = elder->pos + vec3(0, 0, 2);
    Talk t; talkOpen(*L, *P, *elder, t);
    check(optOf(t, "house") < 0, "a Friend isn't offered a house");
    P->pos = guard->pos + vec3(0, 0, 2);
    talkOpen(*L, *P, *guard, t);
    check(optOf(t, "watch") < 0, "nor the watch's work");
    setRep(vid, 75);
    P->pos = elder->pos + vec3(0, 0, 2);
    talkOpen(*L, *P, *elder, t);
    int oi = optOf(t, "house");
    check(oi >= 0, "Honoured: the elder has a word about a house");
    if (oi >= 0) talkChoose(*L, *P, t, oi);
    const json* H = homeOf(*P, vid);
    ivec3 bp(0);
    if (H) bp = ivec3((*H)["bed"][0].get<int>(), (*H)["bed"][1].get<int>(), (*H)["bed"][2].get<int>());
    check(H && bp.x > 20 && bp.x < 26 && bp.z > 8 && bp.z < 14 && reg().tShape[w.getBlock(bp.x, bp.y, bp.z)] == ShapeKind::Bed,
          strFormat("the house nobody lived in is yours: a bed in it (%d, %d, %d)", bp.x, bp.y - Y, bp.z));
    check(std::fabs(P->spawnPoint.x - (bp.x + 0.5f)) < 0.1f && std::fabs(P->spawnPoint.z - (bp.z + 0.5f)) < 0.1f, "its bed is your spawn");
    ivec3 chest(0); bool owned = false;
    for (int x = 21; x <= 25; x++) for (int z = 9; z <= 13; z++) for (int y = Y + 1; y <= Y + 2; y++) if (reg().block(w.getBlock(x, y, z)).has("container")) {
      chest = ivec3(x, y, z); const json* e = w.blockEntity(x, y, z); owned = e && e->value("owner", std::string()) == P->name;
    }
    check(chest != ivec3(0) && owned, "and a chest that's yours");
    const json& placed = villageRec(*L, vid)["placed"];
    const std::string ck = std::to_string(chest.x) + "," + std::to_string(chest.y) + "," + std::to_string(chest.z);
    check(placed.is_object() && placed.value(ck, std::string()) == P->name, "the village counts it yours (taking from it is no theft)");
    // the watch
    P->pos = guard->pos + vec3(0, 0, 2);
    talkOpen(*L, *P, *guard, t);
    oi = optOf(t, "watch");
    check(oi >= 0, "Honoured: the guard has work for the watch");
    if (oi >= 0) talkChoose(*L, *P, t, oi);
    int pi = -1, ei = -1;
    for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == "watchTake") { if (t.opts[i].arg.value("kind", "") == "patrol") pi = i; if (t.opts[i].arg.value("kind", "") == "escort") ei = i; }
    check(pi >= 0 && ei >= 0, "a patrol of the village, and someone to see safely to Brookfield");
    json patrol = pi >= 0 ? t.opts[pi].arg : json(), escort = ei >= 0 ? t.opts[ei].arg : json();
    if (pi >= 0) talkChoose(*L, *P, t, pi);
    json* pq = pi >= 0 ? findQuest(*P, patrol["id"]) : nullptr;
    check(pq && (*pq)["pts"].size() >= 4, "the patrol taken: its posts round the village");
    if (pq) {
      const json pts = (*pq)["pts"];
      for (auto& pt : pts) { P->pos = vec3(pt[0].get<float>() + 0.5f, Y + 1, pt[1].get<float>() + 0.5f); tick(1.2); }
      pq = findQuest(*P, patrol["id"]);
      check(pq && questReady(*L, *P, *pq), "walking each post in turn finishes the round");
      P->pos = guard->pos + vec3(0, 0, 2);
      const double r0 = repOf(*P, vid);
      talkOpen(*L, *P, *guard, t);
      oi = optOf(t, "bounties");
      if (oi >= 0) talkChoose(*L, *P, t, oi);
      check(oi >= 0 && !findQuest(*P, patrol["id"]) && repOf(*P, vid) > r0, "handed in to the guard");
    }
    if (ei >= 0) {
      P->pos = guard->pos + vec3(0, 0, 2);
      talkOpen(*L, *P, *guard, t);
      oi = optOf(t, "watch");
      if (oi >= 0) talkChoose(*L, *P, t, oi);
      ei = -1; for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == "watchTake" && t.opts[i].arg.value("kind", "") == "escort") ei = i;
      if (ei >= 0) talkChoose(*L, *P, t, ei);
      tick(2);
      Creature* who = nullptr;
      for (auto& e : L->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && c->data.value("escortQ", std::string()) == escort.value("id", std::string())) who = c;
      check(findQuest(*P, escort["id"]) && who, "the escort taken: someone comes along with you");
      if (who) {
        check(vsys(*L)->mind(who->id).follow == P->id, "and follows you");
        P->pos = vec3(8.5f, Y + 1, 58.5f); who->pos = vec3(9.5f, Y + 1, 58.5f);
        tick(3);
        check(!findQuest(*P, escort["id"]) && npcStats(*P)["qdone"].contains(escort["id"].get<std::string>()), "brought safely to Brookfield: done, and paid");
      }
    }
  }

  // ------------------------------------------------------------ Hero: a statue in the square, the waystone linked
  {
    P->pos = vec3(8.5f, Y + 1, 4.5f);
    tick(5);   // (the village gets its waystone)
    const json& W = L->info.data.value("waystones", json::object());
    check(W.is_object() && W.contains("v:" + vid), "Hollowmere has its waystone");
    check(!wayKnown(*P, "v:" + vid), "you never attuned to it");
    fx.msgs.clear();
    setRep(vid, 130);
    tick(4);
    const json S = L->info.data.value("statues", json::object());
    const std::string sk = vid + ":" + P->name;
    check(S.is_object() && S.contains(sk), "Hero: a statue of you is raised in the square");
    bool named = false, carved = false;
    if (S.contains(sk)) {
      const json& r = S[sk];
      for (auto& b : r["blocks"]) {
        const ivec3 at(b[0].get<int>(), b[1].get<int>(), b[2].get<int>());
        if (w.getBlock(at.x, at.y, at.z) == reg().blockId("chiseled_stone_bricks")) carved = true;
        if (const json* e = w.blockEntity(at.x, at.y, at.z); e && e->value("type", "") == "sign") for (auto& l : (*e)["lines"]) if (l.is_string() && l.get<std::string>().find(P->name) != std::string::npos) named = true;
      }
    }
    check(carved && named, "built of stone, your name on its sign");
    check(wayKnown(*P, "v:" + vid), "the village's waystone is linked to you");
    P->pos = vec3(8.5f, Y + 1, 66.5f); P->level = 0;
    bool freeGo = false; for (auto& e : wayList(*L, *P, "")) if (e.id == "v:" + vid) freeGo = e.can && e.cost == 0;
    check(freeGo, "and travel to it is free for its Hero");
  }

  // ------------------------------------------------------------ losing standing takes them back
  {
    P->pos = vec3(8.5f, Y + 1, 4.5f);
    setRep(vid, 80);   // Honoured
    tick(3);
    bool cost = false; for (auto& e : wayList(*L, *P, "")) if (e.id == "v:" + vid) cost = e.cost > 0;
    check(cost, "no longer the Hero: the waystone costs again");
    check(L->info.data["statues"].contains(vid + ":" + P->name), "Honoured still: the statue stands");
    setRep(vid, 10);   // a Visitor
    tick(3);
    bool gone = !L->info.data["statues"].contains(vid + ":" + P->name);
    check(gone, "fallen to a Visitor: the statue is taken down");
    Talk t; P->pos = guard->pos + vec3(0, 0, 2); talkOpen(*L, *P, *guard, t);
    check(optOf(t, "watch") < 0 && homeOf(*P, vid), "no watch work; the house stays yours");
    setRep(vid, -20);   // a thief: Distrusted
    tick(3);
    check(!homeOf(*P, vid) && P->spawnPoint.y < 0, "Distrusted: the village takes the house back, and its bed is no spawn of yours");
  }

  // ------------------------------------------------------------ the Shrine of Renewal
  {
    P->stats["tree"] = {{"nodes", json::array({"a", "b", "c"})}};
    P->statStr = 2; P->statCon = 1;
    check(respecPoints(*P) == 6, "six points spent (three talents, three stats)");
    setRep(vid, -20); const int dear = respecCost(*P, vid);
    setRep(vid, 130); const int cheap = respecCost(*P, vid);
    check(dear > cheap && cheap > 0, strFormat("it costs by what you've spent, less for a village's Hero (%d vs %d gold coins)", cheap, dear));
    P->stats["tree"]["nodes"].push_back("d");
    check(respecCost(*P, vid) > cheap, "more spent, more to pay");
    P->stats["tree"]["nodes"].erase(3);
    const int coins = count("gold_coin");
    for (auto& s : P->inv) if (s.id == "gold_coin" || s.id == "emerald") s.clear();
    std::string say;
    const bool poor = respecDo(*L, *P, vid, say);
    check(!poor && P->statStr == 2 && treePointsSpent(*P) == 3, "without the coins, nothing changes (" + say + ")");
    P->give(ItemStack("gold_coin", coins));
    const bool paid = respecDo(*L, *P, vid, say);
    check(paid && treePointsSpent(*P) == 0 && P->statStr == 0 && P->statCon == 0 && count("gold_coin") == coins - cheap,
          strFormat("paid: the talents and stats are yours to choose again (%d coins)", coins - count("gold_coin")));
    P->statAgi = 1;
    P->pos = elder->pos + vec3(0, 0, 2);
    Talk t; talkOpen(*L, *P, *elder, t);
    check(optOf(t, "respec") >= 0, "the elder can do it too");
    json opened; int h = bus.on("villagerScreen", [&](const json& a) { opened = a; return true; });
    w.setBlock(14, Y + 1, 4, reg().blockId("respec_shrine"));
    P->pos = vec3(14.5f, Y + 1, 6.5f);
    bus.emit("itemUse", json::array({P->name, "", 14, Y + 1, 4, 1}));
    check(opened.is_array() && opened[1] == "shrine" && opened[2].value("vid", "") == vid, "using a shrine opens its window (villagerScreen shrine)");
    bus.off(h);
  }

  // ------------------------------------------------------------ a new world's villages keep a shrine in the square
  {
    const StructurePlan* sp = locateStructure(*L->gen, DIM_OVERWORLD, "village", 0, 0, 6);
    check(sp && sp->data.contains("shrine"), "a new world's village plan has its shrine" + (sp ? " (" + sp->name + ")" : std::string()));
    if (sp && sp->data.contains("shrine")) {
      const json& s = sp->data["shrine"];
      const ivec3 at(s[0].get<int>(), s[1].get<int>() + base, s[2].get<int>());
      P->pos = vec3(at.x + 0.5f, at.y + 30, at.z + 0.5f);
      for (double t0 = nowSeconds(); nowSeconds() - t0 < 600 && !w.isLoaded(at.x, at.y, at.z);) { w.stream({{P->pos, 2, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
      check(w.getBlock(at.x, at.y, at.z) == reg().blockId("respec_shrine"), strFormat("and it stands there (%d, %d, %d: %s)", at.x, at.y - base, at.z, reg().block(w.getBlock(at.x, at.y, at.z)).key.c_str()));
    }
  }

  printf("%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
