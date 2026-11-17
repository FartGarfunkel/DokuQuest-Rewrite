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
#include "sim/combat_rpg.h"
#include "sim/villagers_internal.h"
#include "sim/village_theft.h"
#include "gen/overworld.h"
#include "gen/structures.h"
#include "gen/structures_api.h"
#include "gen/village_pieces.h"
#include "world/dims.h"
#include "sim/moving.h"
#include "sim/glue.h"
#include "sim/strung.h"
#include "sim/lift.h"
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

  {   // every flower pot in the village pieces holds one of our own plants (the piece's name for it translated: a poppy, an oak sapling)
    const VillagePieces* VP = villagePieces();
    int pots = 0, planted = 0, bad = 0; std::string badName;
    if (VP) for (const auto& [pool, list] : VP->pools) for (const VPPiece& P : list) for (const VPState& st : P.states) {
      if (st.kind != VPK_POT) continue;
      pots++;
      if (st.plant.empty()) continue;
      planted++;
      if (!reg().blockId(st.plant)) { bad++; badName = st.plant; }
    }
    check(pots > 20 && planted > pots / 2, "the village pieces' flower pots have plants in them (" + std::to_string(planted) + " of " + std::to_string(pots) + ")");
    check(bad == 0, "and each plant is a block of ours (" + std::to_string(bad) + " not: " + badName + ")");
  }
  {   // a healer's brewing stand stands on a table, not on the floor (gen/village_pieces.cpp makePiece)
    const VillagePieces* VP = villagePieces();
    int stands = 0, onFloor = 0, onTable = 0; std::string where;
    if (VP) for (const auto& [pool, list] : VP->pools) for (const VPPiece& P : list) {
      std::map<std::array<int, 3>, BlockId> at;
      for (const VPBlock& b : P.blocks) { const VPState& st = P.states[b.s]; if (st.kind != VPK_AIR) at[{b.x, b.y, b.z}] = st.id; }
      auto solid = [&](int x, int y, int z) { auto it = at.find({x, y, z}); return it != at.end() && it->second && reg().tSolid[it->second]; };
      auto open = [&](int x, int y, int z) { return !at.count({x, y, z}); };
      for (const auto& [k, id] : at) {
        if (reg().block(id).key != "brewing_stand") continue;
        stands++;
        const int x = k[0], y = k[1], z = k[2];
        auto bt = at.find({x, y - 1, z});
        if (bt != at.end() && reg().block(bt->second).key.find("_table") != std::string::npos) onTable++;
        int floorN = 0, openN = 0;
        for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) { floorN += solid(x + dx, y - 1, z + dz); openN += open(x + dx, y, z + dz); }
        if (solid(x, y - 1, z) && floorN == 4 && openN >= 2 && open(x, y + 1, z)) { onFloor++; if (where.empty()) where = P.name; }
      }
    }
    check(VP && stands > 0 && onFloor == 0, strFormat("no brewing stand left on the floor (%d stands, %d on tables, %d on the floor%s)", stands, onTable, onFloor, where.empty() ? "" : (": " + where).c_str()));
  }

  // ------------------------------------------------------------ the data and the original's numbers
  check(VD().contains("jobs") && VD()["shops"].size() == 19 && VD()["riftLore"].size() > 30, "data/villagers.json: jobs, 19 shops (the miller, the miner and the leatherworker too), the rift lore");
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
  {   // our touches over it: no lone pixel left on the hat or jacket layer (on the voxel person each is a cube floating off
      // the face: the librarian's glasses, an earring, a headband's ring round the head)
    int lone = 0, skins = 0; std::string where;
    for (const auto& [job, J] : VD()["jobs"].items()) for (u32 seed = 1; seed < 120; seed++) {
      std::vector<u8> px; npcSkinPixels(seed, job, px); npcSkinTouches(seed, job, px); skins++;
      auto a = [&](int x, int y) { return px[((size_t)y * 64 + x) * 4 + 3] != 0; };
      for (auto [x0, y0, x1, y1] : {std::array<int, 4>{32, 0, 64, 16}, {16, 32, 40, 48}})
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (a(x, y)) {
          int n = 0; for (auto [ox, oy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) { int nx = x + ox, ny = y + oy; if (nx >= x0 && nx < x1 && ny >= y0 && ny < y1 && a(nx, ny)) n++; }
          if (n <= 1) { lone++; if (where.empty()) where = job + " " + std::to_string(seed) + " at " + std::to_string(x) + "," + std::to_string(y); }
        }
    }
    check(lone == 0, "villagers' outer layers have no lone floating pixels (" + std::to_string(skins) + " skins" + (lone ? ", " + std::to_string(lone) + ": " + where : "") + ")");
  }

  ThreadPool pool(2);
  LevelInfo li; li.name = "VillagerTest"; li.seedText = "villagers";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  L = lvp.get(); L->fx = &fx;
  L->info.rules["mobSpawning"] = false;   // no night crowd on the test floor
  L->info.rules["dayMinutes"] = 10; L->info.rules["nightMinutes"] = 10;   // (the schedule tests tick through the day at the old length)
  World& w = L->world();
  const int base = dimBase(DIM_OVERWORLD), Y = base + 200, ly = 200;   // a test floor high in the sky
  { auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 20.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }
  double t0 = nowSeconds();
  while (nowSeconds() - t0 < 900) {   // (till they've loaded: a busy machine streams slowly)
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
               {"board", json::array({10, ly + 1, 2})}, {"bell", json::array({8, ly + 1, 2})}, {"folk", folk}, {"guards", 1}, {"golems", 0}, {"seats", json::array({json::array({2, ly + 1, 10, 0})})},
               {"blds", json::array()}, {"roads", json::array({{{"d", 1}, {"L", 12}, {"hw", 1}}})}, {"seed", 12345}};
  addVillage(*L, plan);
  VillageRef v = villageById(*L, "t5000_5000");
  check(v && v->name == "Testbury" && v->y == Y && v->folk.size() == 3 + 4 && v->folk[0].work == ivec3(14, Y + 1, -6), "a village plan is read (band-local y made global; its 3 folk and the 4 hands every village gets)");
  { const auto near = villagesNear(*L, 8, 20, 72);   // (the world may have its own village nearby: Testbury is among them)
    const bool has = std::any_of(near.begin(), near.end(), [](const VillageRef& n) { return n && n->name == "Testbury"; });
    check(has && villageAtPos(*L, REALM_MAIN, P->pos, 4) != nullptr, "villagesNear / villageAtPos find it"); }
  check(contentHooks().villageNear && contentHooks().villageNear(*L, P->pos, 30).has_value(), "the content hook villageNear answers from the villages");

  // ------------------------------------------------------------ coming to life near a player
  setPhase(0.1);   // morning: off to work
  tick(2);
  auto folkNow = villagers(v->id);
  check(folkNow.size() == 8, "the villagers come out when someone is near (3 folk + 4 hands + 1 guard): " + std::to_string(folkNow.size()));
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
  // ------------------------------------------------------------ a bench boxed in on three sides (its front blocked): the farmer
  // still gets to it from the open side and sits, and stays sat (no circling, no hopping on and off)
  {
    BlockId bench = reg().blockId("oak_bench");
    w.setBlock(2, Y + 1, 10, bench, 0);
    for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}}) for (int k = 1; k <= 2; k++) w.setBlock(2 + dx, Y + k, 10 + dz, stone);
    setPhase(0.45);
    tick(40);
    auto sat = [&]() { return farmer && (npcOf(*farmer)->value("p", 0) & NP_SIT) && std::hypot(farmer->pos.x - 2.5f, farmer->pos.z - 10.5f) < 0.2f; };
    check(sat(), "in the afternoon the farmer sits on the bench, even with its front walled in (" + std::to_string(farmer ? farmer->pos.x : 0) + ", " + std::to_string(farmer ? farmer->pos.z : 0) + ")");
    int stays = 0; for (int i = 0; i < 20; i++) { tick(0.5); if (sat()) stays++; }
    check(stays == 20, "and stays sat (" + std::to_string(stays) + "/20)");
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
  int gc = count("gold_coin");   // villagers pay in gold coins (an emerald still buys: 4 coins, change in coins)
  { bool ok = shopTrade(*L, *P, *v, 1, "librarian", "sell", 0, said);
    check(ok && count("gold_coin") > gc && count("paper") == 0, "selling 24 paper for coins (" + std::to_string(ok) + ", coins " + std::to_string(gc) + " -> " + std::to_string(count("gold_coin")) + ", paper " + std::to_string(count("paper")) + ")"); }
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
  {   // an ordinary elite (one in fourteen: data "elite" true) dying: the Wanted bounty's check took that flag for a bounty's
    // id and threw (a string read off a true)
    Creature* m = spawnCreature(*L, REALM_MAIN, mob, P->pos + vec3(4, 0, 0));
    bool threw = false, died = false;
    if (m) {
      m->persistent = true; m->data["elite"] = true;
      try { tick(2.2); DamageSrc s; s.attacker = P->id; m->hurt(*L, 500, s); tick(0.2); died = !m->alive(); } catch (const std::exception&) { threw = true; }
    }
    check(m && !threw && died, "an ordinary elite lives near you and dies without trouble (its flag isn't a bounty's id)");
    check(mobIsElite(json{{"elite", true}}) && !mobIsElite(json{{"elite", "q1"}}) && mobWantedId(json{{"elite", "q1"}}) == "q1" && mobWantedId(json{{"wanted", "q2"}}) == "q2" && mobWantedId(json{{"elite", true}}).empty(),
          "elite and Wanted told apart (an old save's Wanted id under \"elite\" still found)");
  }
  json* hq = findQuest(*P, hunt["id"]);
  check(hq && (*hq)["have"].get<int>() == need && questReady(*L, *P, *hq), "slaying the bounty's monsters counts (" + mob + ")");
  gc = count("gold_coin");
  int rew = hunt["reward"]["em"];   // paid in gold coins
  { bool ok = questFinish(*L, *P, hunt["id"]);
    check(ok && count("gold_coin") == gc + rew && !findQuest(*P, hunt["id"]) && npcStats(*P)["qdone"].contains(hunt["id"].get<std::string>()), "handing it in: gold coins, and it's done (" + std::to_string(ok) + ", coins " + std::to_string(gc) + " -> " + std::to_string(count("gold_coin")) + " want +" + std::to_string(rew) + ")"); }
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

  // ------------------------------------------------------------ thieves: seen, a word from the guard, give back, pay or fight
  {
    World& w = L->world();
    guard = byJob(v->id, "guard");
    Creature* owner = byJob(v->id, "farmer");
    const ivec3 cpos = ivec3(glm::floor(owner->pos)) + ivec3(2, 0, 0);
    auto stockChest = [&] {
      w.setBlock(cpos.x, cpos.y, cpos.z, reg().blockId("chest"));
      json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
      items[0] = ItemStack("bread", 6).toJson(); items[1] = ItemStack("iron_ingot", 3).toJson();
      w.setBlockEntity(cpos.x, cpos.y, cpos.z, json{{"type", "chest"}, {"items", items}});
    };
    auto take = [&](const char* id, int n) {   // (out of the chest into the bag, as any way of taking does)
      json* e = w.blockEntity(cpos.x, cpos.y, cpos.z);
      for (auto& it : (*e)["items"]) { ItemStack q = ItemStack::fromJson(it); if (q.id == id) { q.n -= n; it = q.n > 0 ? q.toJson() : json(); break; } }
      w.blockEntityChanged(cpos.x, cpos.y, cpos.z);
      P->give(ItemStack(id, n));
    };
    for (auto& q : P->inv) q = ItemStack();
    stockChest();
    P->pos = vec3(cpos) + vec3(0.5f, 0, -1.5f); owner->pos = vec3(cpos) + vec3(4.5f, 0, 0.5f);
    tick(1);   // (they've seen what's in it)
    const double rep0 = repOf(*P, v->id);
    opened = nullptr;
    take("bread", 6); take("iron_ingot", 3);
    tick(1);
    check(repOf(*P, v->id) < rep0, "stealing in front of a villager costs standing (" + std::to_string(rep0) + " -> " + std::to_string(repOf(*P, v->id)) + ")");
    tick(12);
    check(opened.is_array() && opened[1] == "talk" && opened[2].value("victim", "") == "theft", "a guard comes over about what you took");
    Talk c; talkOpen(*L, *P, *guard, c, true, "theft");
    int giveI = -1, payI = -1, refI = -1;
    for (int i = 0; i < (int)c.opts.size(); i++) { if (c.opts[i].act == "giveback") giveI = i; if (c.opts[i].act == "payfine") payI = i; if (c.opts[i].act == "refuse") refI = i; }
    check(c.mode == "confront" && giveI >= 0 && payI >= 0 && refI >= 0, "\"give it back\", \"pay the fine\" or \"make me\"");
    check(c.opts[payI].label.find("coins") != std::string::npos && theftFine(*P, v->id) >= 5, "the fine is in coins (" + c.opts[payI].label + ")");
    const double repCaught = repOf(*P, v->id);
    talkChoose(*L, *P, c, giveI);
    int bread = 0; for (auto& q : P->inv) if (q.id == "bread") bread += q.n;
    const json* e = w.blockEntity(cpos.x, cpos.y, cpos.z);
    int back = 0; for (auto& it : (*e)["items"]) if (ItemStack::fromJson(it).id == "bread") back += ItemStack::fromJson(it).n;
    check(bread == 0 && back == 6 && repOf(*P, v->id) > repCaught && !theftWanted(*P, v->id), "given back: the bread is in the chest again, some standing back, nothing owed");
    // again: pay the fine and keep it
    tick(1);
    take("bread", 6); tick(1);
    tick(12);
    Talk c2; talkOpen(*L, *P, *guard, c2, true, "theft");
    payI = -1; for (int i = 0; i < (int)c2.opts.size(); i++) if (c2.opts[i].act == "payfine") payI = i;
    const int fine = theftFine(*P, v->id);
    P->give(ItemStack("gold_coin", fine + 3));
    talkChoose(*L, *P, c2, payI);
    int coins = 0; bread = 0; for (auto& q : P->inv) { if (q.id == "gold_coin") coins += q.n; if (q.id == "bread") bread += q.n; }
    check(fine >= 5 && coins == 3 && bread == 6 && !theftWanted(*P, v->id), "the fine paid in coins (" + std::to_string(fine) + "): the bread kept, nothing owed");
    // and again: refuse, and the guards come for you
    stockChest(); tick(1);
    take("bread", 6); tick(1); tick(12);
    Talk c3; talkOpen(*L, *P, *guard, c3, true, "theft");
    refI = -1; for (int i = 0; i < (int)c3.opts.size(); i++) if (c3.opts[i].act == "refuse") refI = i;
    talkChoose(*L, *P, c3, refI);
    tick(0.5);
    check(theftWanted(*P, v->id) && vsys(*L)->mind(guard->id).foe == P->id, "\"make me\": wanted, and the guard attacks");
    // a chest you put there yourself is yours
    theftForgive(*L, *P, v->id);
    vsys(*L)->mind(guard->id).foe = 0;
    const ivec3 mine = cpos + ivec3(0, 0, 2);
    bus.emit("blockPlace", json::array({P->name, mine.x, mine.y, mine.z, "chest"}));
    w.setBlock(mine.x, mine.y, mine.z, reg().blockId("chest"));
    json mi = json::array(); for (int i = 0; i < 27; i++) mi.push_back(nullptr); mi[0] = ItemStack("apple", 4).toJson();
    w.setBlockEntity(mine.x, mine.y, mine.z, json{{"type", "chest"}, {"items", mi}});
    tick(1);
    const double repMine = repOf(*P, v->id);
    (*w.blockEntity(mine.x, mine.y, mine.z))["items"][0] = nullptr; w.blockEntityChanged(mine.x, mine.y, mine.z); P->give(ItemStack("apple", 4));
    tick(2);
    check(repOf(*P, v->id) == repMine && !theftWanted(*P, v->id), "taking from your own chest is no theft");
    // each player their own standing: someone else's theft is theirs alone
    auto p2 = std::make_unique<Player>(); p2->name = "Friend"; p2->pos = P->pos + vec3(30, 0, 0);
    Player& P2 = *static_cast<Player*>(L->spawn(std::move(p2)));
    const double rep2 = repOf(P2, v->id);
    stockChest(); tick(1); take("bread", 6); tick(1);
    check(repOf(P2, v->id) == rep2, "a friend's standing isn't touched by your stealing");
    P2.removed = true; theftForgive(*L, *P, v->id);
    for (auto& q : P->inv) q = ItemStack();
    w.setBlock(cpos.x, cpos.y, cpos.z, AIR); w.setBlock(mine.x, mine.y, mine.z, AIR);
    tick(0.5);
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

  // ------------------------------------------------------------ the lumberjack fells a tree as a player does
  {
    const BlockId logB = reg().blockId("log"), leafB = reg().blockId("leaves"), dirtB = reg().blockId("dirt");
    const int tx = 40, tz = 40;
    for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) { w.setBlock(tx + dx, Y, tz + dz, dirtB); for (int k = 1; k <= 9; k++) w.setBlock(tx + dx, Y + k, tz + dz, AIR); }
    for (int k = 1; k <= 5; k++) w.setBlock(tx, Y + k, tz, logB);
    for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) for (int k = 4; k <= 6; k++) if ((dx || dz || k == 6) && std::abs(dx) + std::abs(dz) < 4) w.setBlock(tx + dx, Y + k, tz + dz, leafB);
    std::string key; const int n = npcFellTree(*L, w, ivec3(tx, Y + 1, tz), key, vec3(tx + 2.5f, Y + 1, tz + 0.5f));
    int logsLeft = 0, leavesLeft = 0;
    for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) for (int k = 1; k <= 9; k++) { const BlockId b = w.getBlock(tx + dx, Y + k, tz + dz); logsLeft += b == logB; leavesLeft += b == leafB; }
    bool falling = false; for (auto& e : L->entities()) if (!e->removed && e->type == "falling_tree") falling = true;
    check(n == 5 && key == "log" && logsLeft == 0 && leavesLeft == 0, strFormat("the lumberjack's tree comes down whole: 5 logs to the stores, its leaves with it (%d logs, %d logs and %d leaves left)", n, logsLeft, leavesLeft));
    check(falling, "and it topples, as a player's does (a falling tree)");
    check(reg().block(w.getBlock(tx, Y + 1, tz)).key.find("sapling") != std::string::npos, "a sapling in its place");
  }

  // ------------------------------------------------------------ the rift opened in another village: here it's news from there
  elder = byJob(v->id, "elder");
  check(elder != nullptr, "Testbury's elder is about");
  if (elder) {
    const json saved = L->info.data.value("rift", json());
    L->info.data["rift"] = {{"stage", 1}, {"frags", 0}, {"village", "t9999_9999"}, {"vname", "Farbrook"}, {"at", json::array({9999, 600, 9999})}};
    P->pos = elder->pos + vec3(0, 0, 2);
    int square = 0, named = 0, said = 0; std::string label;
    for (int k = 0; k < 60; k++) {
      Talk tk; if (!talkOpen(*L, *P, *elder, tk)) continue;
      for (auto& o : tk.opts) if (o.act == "riftElder") label = o.label;
      for (int i = 0; i < (int)tk.opts.size(); i++) if (tk.opts[i].act == (k % 3 == 0 ? "news" : k % 3 == 1 ? "rift" : "riftElder")) { talkChoose(*L, *P, tk, i); break; }
      said++; if (tk.text.find("square") != std::string::npos && tk.text.find("Farbrook") == std::string::npos) square++;
      if (tk.text.find("Farbrook") != std::string::npos) named++;
      talkClosed(*L, *P, tk);
    }
    check(said > 50 && square == 0 && named > 0, strFormat("in another village nobody speaks of \"the square\" as theirs, and they name the rift's village (%d of %d named it, %d spoke of a square)", named, said, square));
    check(label.find("Farbrook") != std::string::npos, "the elder's option names it: " + label);
    L->info.data["rift"]["village"] = v->id; label.clear();
    { Talk tk; if (talkOpen(*L, *P, *elder, tk)) for (auto& o : tk.opts) if (o.act == "riftElder") label = o.label; talkClosed(*L, *P, tk); }
    check(label.find("square") != std::string::npos, "in the rift's own village it's still the rift in the square: " + label);
    if (saved.is_null()) L->info.data.erase("rift"); else L->info.data["rift"] = saved;
  }

  // ------------------------------------------------------------ going when everyone has left
  P->pos = vec3(8.5f, Y + 1, 400.5f);
  tick(3);
  check(villagers(v->id).empty(), "the villagers go when everyone has left");
  bus.off(hScr);

  // ------------------------------------------------------------ where a new world starts: a village's edge, or (a world setting) the wild
  {
    vec3 vs(0), ws(0); float yaw = 0;
    const bool hv = villageStartPoint(*L->gen, vs, yaw), hw = wildStartPoint(*L->gen, ws);
    const StructurePlan* nv = hw ? locateStructure(*L->gen, DIM_OVERWORLD, "village", ifloor(ws.x), ifloor(ws.z), 3) : nullptr;
    const float dv = nv ? std::hypot(nv->cx - ws.x, nv->cz - ws.z) : 1e9f;
    check(hv && hw && dv >= 200 && dimOfY(ifloor(ws.y)) == DIM_OVERWORLD, strFormat("start at a village, or in the wild with none within 200 blocks (nearest %.0f away)", dv));
  }

  // ------------------------------------------------------------ roads between villages, and travellers on them
  {
    const StructureIndex* idx = nullptr;
    if (auto* o = dynamic_cast<const OverworldGen*>(L->gen->forDim(DIM_OVERWORLD))) idx = &o->structures;
    const int rt = structureTypeIndex("road");
    RoadInfo road; int plans = 0; const double tr0 = nowSeconds();
    for (int rz = -2; rz <= 2 && idx && rt >= 0; rz++) for (int rx = -2; rx <= 2; rx++) if (const StructurePlan* S = idx->at(rt, rx, rz)) {
      plans++;
      if (road.pts.empty()) roadNear(*idx, (S->x0 + S->x1) / 2, (S->z0 + S->z1) / 2, 600, road);
    }
    check(plans > 0 && road.pts.size() >= 6, strFormat("roads join neighbouring villages (%d road plans near the origin, planned in %.1f s; one of %zu points)", plans, nowSeconds() - tr0, road.pts.size()));
    VillageRef a = villageById(*L, road.from), b = villageById(*L, road.to);
    check(a && b && a != b, "each from one village (" + (a ? a->name : std::string("?")) + ") to another (" + (b ? b->name : std::string("?")) + ")");
    if (road.pts.size() >= 6 && a && b) {
      // stand on the road halfway, away from both villages, by day: before long a merchant comes along it (the point
      // farthest into the open from either village, a merchant's not met within twenty blocks of one: the middle point had
      // lain by a big city's edge)
      auto mid = road.pts[road.pts.size() / 2];
      { float best = -1;
        for (size_t i = road.pts.size() / 4; i < road.pts.size() * 3 / 4; i++) {
          float d = 1e9f;
          for (int dz = -40; dz <= 40 && d > 0; dz += 4) for (int dx = -40; dx <= 40; dx += 4)
            if (villageAtPos(*L, REALM_MAIN, vec3(road.pts[i][0] + dx, base + 64, road.pts[i][1] + dz), 20)) d = std::min(d, std::hypot((float)dx, (float)dz));
          if (d > best) { best = d; mid = road.pts[i]; }
        } }
      P->pos = vec3(mid[0] + 0.5f, base + 120, mid[1] + 0.5f);
      double t1 = nowSeconds(); int y = -1;
      while (nowSeconds() - t1 < 60) {
        w.stream({{P->pos, 4, false}}, 20);
        if (w.isLoaded(mid[0] + 40, base + 64, mid[1]) && w.isLoaded(mid[0] - 40, base + 64, mid[1]) && w.isLoaded(mid[0], base + 64, mid[1] + 40) && w.isLoaded(mid[0], base + 64, mid[1] - 40)) {
          y = findSafeY(w, mid[0], mid[1], DIM_OVERWORLD); if (y > 0) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      const BlockId path = reg().blockId("dirt_path"), gravel = reg().blockId("gravel");
      int paved = 0;
      for (auto& q : road.pts) if (w.isLoaded(q[0], base + 64, q[1])) {
        const int gy = findSafeY(w, q[0], q[1], DIM_OVERWORLD); if (gy < 1) continue;
        const BlockId g = w.getBlock(q[0], gy - 1, q[1]);
        if (g == path || g == gravel || reg().block(g).key.find("planks") != std::string::npos || reg().block(g).key.find("sandstone") != std::string::npos || reg().block(g).key.find("coarse") != std::string::npos || reg().block(g).key == "podzol" || reg().block(g).key == "packed_ice") paved++;
      }
      check(paved >= 3, strFormat("the road is laid in the world (%d of its points here paved or bridged)", paved));
      P->pos = vec3(mid[0] + 0.5f, y > 0 ? y : base + 90, mid[1] + 0.5f); P->onGround = true; P->vel = vec3(0);
      setPhase(0.3);
      VillagerSystem& VS = *vsys(*L);
      Creature* tr = nullptr;
      for (int i = 0; i < 80 && !tr; i++) {
        VS.travelT = 0; travellersTick(*L, VS, 0.1);
        for (auto& e : L->entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && c->data.contains("road")) tr = c;
      }
      check(tr && npcOf(*tr) && npcOf(*tr)->value("i", 0) == 400 && npcOf(*tr)->value("j", "") == "merchant", "a merchant comes along the road" + (tr ? " (" + npcOf(*tr)->value("n", std::string()) + ", on the way to " + tr->data["road"].value("to", std::string()) + ")" : std::string()));
      if (tr) {
        const vec3 p0 = tr->pos; const int k0 = tr->data["road"].value("k", 0);
        tick(12);
        const int k1 = tr->removed ? k0 : tr->data["road"].value("k", 0);
        check(!tr->removed && (glm::distance(tr->pos, p0) > 2 || k1 != k0), strFormat("and walks it (%.1f blocks in 12 s, point %d -> %d)", glm::distance(tr->pos, p0), k0, k1));
        Talk tk; const bool talked = !tr->removed && talkOpen(*L, *P, *tr, tk);
        check(talked && tk.title.find(" of ") != std::string::npos, "talking to them: " + tk.title);
      }
    }
  }

  printf("[a new town's moving gates]\n");
  {
    auto tick = [&](double s) { for (double t = 0; t < s; t += 0.05) L->tick(0.05); };
    const int GX = 30, GZ = -16, h0 = Y;   // a gate in a wall running east-west at z = GZ; the town to the south (+z)
    P->pos = vec3(GX + 0.5f, Y + 1, GZ + 6.5f);
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 900;) {
      w.stream({{P->pos, 3, false}}, 20);
      bool all = true; for (int cx = 0; cx <= 3; cx++) for (int cz = -3; cz <= 0; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
      if (all) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    for (int x = GX - 8; x <= GX + 8; x++) for (int z = GZ - 8; z <= GZ + 8; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 14; y++) w.setBlock(x, y, z, AIR); }
    const int hw = 1, g = hw + 1;
    VGate G; G.key = "test:0"; G.name = "North Gate"; G.d = 0; G.stone = true; G.h0 = h0; G.inX = 0; G.inZ = 1; G.hw = hw; G.cx = GX; G.cz = GZ;
    G.center = vec3(GX + 0.5f, h0 + 1, GZ + 0.5f);
    for (int s2 = -g; s2 <= g; s2++) for (int k = 1; k <= 5; k++) G.cells.push_back(ivec3(GX + s2, h0 + k, GZ));
    // the portcullis as the town builds it: bars raised in the slot, glued, hung from a slider in the lintel facing down
    const BlockId bars = reg().blockId("iron_bars"), slider = reg().blockId("slider"), sb = reg().blockId("stone_bricks");
    for (int s2 = -g; s2 <= g; s2++) { for (int y = h0 + 4; y <= h0 + 8; y++) w.setBlock(GX + s2, y, GZ, bars); if (s2) w.setBlock(GX + s2, h0 + 9, GZ, sb); }
    w.setBlock(GX, h0 + 9, GZ, slider, 0);
    for (int s2 = -g; s2 <= g; s2++) for (int y = h0 + 4; y <= h0 + 8; y++) { if (s2 < g) setGlue(w, ivec3(GX + s2, y, GZ), 0, true); if (y < h0 + 8) setGlue(w, ivec3(GX + s2, y, GZ), 2, true); }
    setGlue(w, ivec3(GX, h0 + 9, GZ), 3, true);
    machineState(w, ivec3(GX, h0 + 9, GZ))["distance"] = 3;
    check(!gateShutAt(*L, G), "a new stone gate stands open, its portcullis up");
    P->pos = vec3(GX + 0.5f, Y + 1, GZ + 6.5f);
    check(gateSet(*L, G, false, false), "the town shuts it");
    tick(3.0);
    check(gateShutAt(*L, G) && w.getBlock(GX, h0 + 1, GZ) == bars && w.getBlock(GX + g, h0 + 1, GZ) == bars && w.getBlock(GX, h0 + 8, GZ) == AIR, "the portcullis comes down across the way");
    check(gateSet(*L, G, true, false), "and opens it");
    tick(3.0);
    check(!gateShutAt(*L, G) && w.getBlock(GX, h0 + 3, GZ) == AIR && w.getBlock(GX, h0 + 8, GZ) == bars, "it rises back into its slot");
    // someone standing in the way: it isn't let down on them
    P->pos = vec3(GX + 0.5f, Y + 1, GZ + 0.5f);
    check(!gateSet(*L, G, false, false), "not let down on someone standing under it");
    P->pos = vec3(GX + 0.5f, Y + 1, GZ + 6.5f);
    // a drawbridge before it (water outside): a plank deck on a hinge at the threshold, facing out (north), lowered
    {
      const BlockId hingeB = reg().blockId("hinge"), plk = reg().blockId("planks"), water = reg().blockId("water");
      for (int x = GX - 3; x <= GX + 3; x++) for (int z = GZ - 7; z <= GZ - 1; z++) { w.setBlock(x, h0 - 1, z, water); w.setBlock(x, h0, z, water); }
      // (as a town builds it: the deck rows 1..5 out, the hinge beside row 1 outside the way's west column, facing east)
      w.setBlock(GX - g, h0, GZ - 1, hingeB, 5);
      for (int k = 1; k <= 5; k++) for (int s2 = -hw; s2 <= hw; s2++) w.setBlock(GX + s2, h0, GZ - k, plk);
      for (int k = 1; k <= 5; k++) for (int s2 = -hw; s2 <= hw; s2++) { if (s2 < hw) setGlue(w, ivec3(GX + s2, h0, GZ - k), 0, true); if (k < 5) setGlue(w, ivec3(GX + s2, h0, GZ - k), 5, true); }
      setGlue(w, ivec3(GX - g, h0, GZ - 1), 0, true);
      const size_t chains0 = strungList(*L).size();
      VGate D = G; D.key = "test:draw";
      check(gateSet(*L, D, false, false), "a gate with a drawbridge: the town shuts it");
      tick(3.0);
      check(gateShutAt(*L, D) && w.getBlock(GX, h0 + 4, GZ - 1) == plk && w.getBlock(GX, h0, GZ - 3) != plk && w.getBlock(GX, h0 + 1, GZ) == reg().blockId("iron_bars"), "the drawbridge is raised in front of the gate, the portcullis down behind it");
      check(strungList(*L).size() == chains0 + 2, "it hangs on two chains from the gatehouse");
      check(gateSet(*L, D, true, false), "and opens it");
      tick(3.0);
      check(!gateShutAt(*L, D) && w.getBlock(GX, h0, GZ - 5) == plk && w.getBlock(GX, h0 + 3, GZ - 1) != plk, "the drawbridge is let down over the water");
      for (int x = GX - 3; x <= GX + 3; x++) for (int z = GZ - 7; z <= GZ; z++) { w.setBlock(x, h0 - 1, z, stone); w.setBlock(x, h0, z, stone); }
      L->info.data["strung"] = json::array();
    }
    // a palisade's leaves on hinges under the road at the outer edges
    for (int x = GX - 8; x <= GX + 8; x++) for (int z = GZ - 8; z <= GZ + 8; z++) for (int y = Y + 1; y < Y + 14; y++) w.setBlock(x, y, z, AIR);
    finishAll(*L);
    VGate Q = G; Q.key = "test:1"; Q.stone = false;
    const BlockId planksB = reg().blockId("planks"), hinge = reg().blockId("hinge");
    for (int side : {-1, 1}) {
      const int s0 = side < 0 ? -g : 1, s1 = side < 0 ? 0 : g, outer = side < 0 ? -g : g;
      w.setBlock(GX + outer, h0, GZ, hinge, 1);
      for (int s2 = s0; s2 <= s1; s2++) for (int y = h0 + 1; y <= h0 + 5; y++) w.setBlock(GX + s2, y, GZ, planksB);
      for (int s2 = s0; s2 <= s1; s2++) for (int y = h0 + 1; y <= h0 + 5; y++) { if (s2 < s1) setGlue(w, ivec3(GX + s2, y, GZ), 0, true); if (y < h0 + 5) setGlue(w, ivec3(GX + s2, y, GZ), 2, true); }
      setGlue(w, ivec3(GX + outer, h0, GZ), 2, true);
    }
    check(gateShutAt(*L, Q), "a new palisade gate stands shut, its leaves closed");
    check(gateSet(*L, Q, true, false), "the town opens it");
    tick(2.5);
    // (the west leaf is g + 1 wide, the east one g: each lies along the wall's inside from its hinge)
    bool inward = w.getBlock(GX - g, h0 + 2, GZ + g) == planksB && w.getBlock(GX + g, h0 + 2, GZ + g - 1) == planksB && w.getBlock(GX - g, h0 + 2, GZ - 1) == AIR && w.getBlock(GX + g, h0 + 2, GZ - 1) == AIR;
    check(!gateShutAt(*L, Q) && inward && w.getBlock(GX, h0 + 2, GZ) == AIR, "both leaves swing in toward the town");
    check(gateSet(*L, Q, false, false), "and shuts it");
    tick(2.5);
    check(gateShutAt(*L, Q) && w.getBlock(GX, h0 + 2, GZ) == planksB, "the leaves close across the way");
    for (int x = GX - 8; x <= GX + 8; x++) for (int z = GZ - 8; z <= GZ + 8; z++) for (int y = Y; y < Y + 14; y++) w.setBlock(x, y, z, y == Y ? stone : AIR);
    finishAll(*L);
  }

  printf("[generated walled towns' moving gates]\n");
  for (const char* want : {"palisade", "stone"}) {
    const StructurePlan* town = nullptr;
    for (int k = 0; k < 160 && !town; k++) {
      const int step = k < 40 ? 4000 : 3000, kk = k < 40 ? k : k - 40;
      const int x = k < 40 ? (kk % 7 - 3) * step : (kk % 11 - 5) * step, z = k < 40 ? (kk / 7 - 3) * step : (kk / 11 - 5) * step;
      const StructurePlan* sp = locateStructure(*L->gen, DIM_OVERWORLD, "village", x, z, 24);
      if (sp && sp->data.contains("wall") && (sp->data["wall"].value("kind", "") == "stone") == (std::string(want) == "stone") && sp->data.contains("gates") && !sp->data["gates"].empty()) town = sp;
    }
    check(town != nullptr, std::string("a town with a ") + want + " wall is found");
    if (!town) continue;
    const bool stone = std::string(want) == "stone";
    VillageRef v = villageById(*L, town->data.value("id", std::string()));
    check(v && !v->gates.empty(), "its gates");
    if (!v || v->gates.empty()) continue;
    const VGate& G = v->gates[0];
    P->pos = G.center + vec3(G.inX * 6, 1, G.inZ * 6);
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 900;) {
      w.stream({{P->pos, 3, false}}, 20);
      bool all = true; for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) if (!w.isLoaded((int)G.center.x + dx * 16, (int)G.center.y, (int)G.center.z + dz * 16)) all = false;
      if (all) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    int machines = 0; for (int dx = -5; dx <= 5; dx++) for (int dz = -5; dz <= 5; dz++) for (int y = G.h0 - 1; y <= G.h0 + 11; y++) if (isMachine(w.getBlock((int)G.center.x + dx, y, (int)G.center.z + dz))) machines++;
    check(machines >= (stone ? 1 : 2), std::string("the ") + want + " gate has its " + (stone ? "slider" : "hinges") + " (" + std::to_string(machines) + ")");
    const bool shut0 = gateShutAt(*L, G);
    if (VillagerSystem* VS = vsys(*L)) VS->gateManual[G.key] = 600;   // (worked by hand: the town leaves it be meanwhile)
    check(gateSet(*L, G, shut0, false), std::string("the town ") + (shut0 ? "opens" : "shuts") + " it");
    for (int k = 0; k < 100; k++) L->tick(0.05);
    check(gateShutAt(*L, G) != shut0, "and it moves");
    check(gateSet(*L, G, !shut0, false), "and back");
    for (int k = 0; k < 100; k++) L->tick(0.05);
    check(gateShutAt(*L, G) == shut0, "the way it was");
    printf("  (%s: %s at %.0f %.0f %.0f)\n", town->name.c_str(), G.name.c_str(), G.center.x, G.center.y, G.center.z);
    if (stone) {   // a corner tower's lift
      const int x0 = town->data["wall"].value("x0", 0), z0 = town->data["wall"].value("z0", 0);
      P->pos = vec3(x0 + 0.5f, G.h0 + 1, z0 + 0.5f);
      auto cornerLoaded = [&]() { for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) if (!w.isLoaded(x0 + dx * 16, G.h0, z0 + dz * 16)) return false; return true; };
      for (double t0 = nowSeconds(); nowSeconds() - t0 < 900 && !cornerLoaded();) { w.stream({{P->pos, 2, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
      ivec3 sl(0); bool found = false;
      for (int dx = -3; dx <= 3 && !found; dx++) for (int dz = -3; dz <= 3 && !found; dz++) for (int y = G.h0 - 40; y <= G.h0 + 40 && !found; y++)
        if (reg().block(w.getBlock(x0 + dx, y, z0 + dz)).str("machine") == "slider") { sl = ivec3(x0 + dx, y, z0 + dz); found = true; }
      check(found, "a corner tower has a lift (its slider)");
      if (found) {
        Lift lf; const bool isLift = liftOfSlider(w, sl, lf);
        check(isLift && lf.stops.size() == 2, "on a rail with a stop at the bottom and the top (" + std::to_string(lf.stops.size()) + " stops)");
        if (isLift && lf.stops.size() == 2) {
          liftCall(*L, REALM_MAIN, sl, lf.stops.back().second);
          for (int k = 0; k < 120; k++) L->tick(0.05);
          check(machineState(w, sl).value("at", 0) == lf.stops.back().second, "called, it rises to the wall walk");
          liftCall(*L, REALM_MAIN, sl, 0); for (int k = 0; k < 120; k++) L->tick(0.05);
          // the town's first guard takes a turn on lookout: onto the lift, up to the wall walk, a watch, and back down
          // (everything waits on what it needs — the land loaded, the guard about — never on the clock: a busy machine
          // streams slowly, and a wait by the clock started the watch on half-loaded land)
          auto loadedRound = [&](ivec3 c, int r) { for (int dx = -r; dx <= r; dx++) for (int dz = -r; dz <= r; dz++) if (!w.isLoaded(c.x + dx * 16, c.y, c.z + dz * 16)) return false; return true; };
          P->pos = vec3(v->x + 0.5f, v->y + 1, v->z + 0.5f);
          Creature* guard = nullptr;
          for (double t0 = nowSeconds(); nowSeconds() - t0 < 900 && !guard;) {
            w.stream({{P->pos, 4, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); L->tick(0.05);
            if (!loadedRound(ivec3(v->x, v->y, v->z), 2)) continue;
            for (auto& up : L->entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (const json* N = npcOf(*c))
              if (c->type == "village_guard" && !c->removed && N->value("v", "") == v->id && N->value("i", 0) == 100) guard = c;
          }
          check(guard != nullptr, "the town's first guard is about");
          if (guard) {
            L->info.time = 3600;   // (morning)
            // (near the tower, so the test doesn't wait on a long walk; the land there loaded first, the player kept by it)
            const vec3 watchFrom = vec3(sl) + vec3(6, 1, 6);
            P->pos = watchFrom;
            for (double t0 = nowSeconds(); nowSeconds() - t0 < 900 && !loadedRound(sl, 1);) { w.stream({{watchFrom, 3, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            check(loadedRound(sl, 1), "the land round the tower loads");
            // (the watch starts afresh from here: whatever the guard was about while the town loaded is forgotten)
            { NpcMind& GM = vsys(*L)->mind(guard->id); GM.look = 0; GM.nextLookAt = 0; GM.lookClock = 0; GM.rideS = ivec3(INT32_MIN); GM.rideStage = 0; GM.hasPath = false; GM.hasGoal = false; }
            // (in the town, before the tower's way in: a corner tower's door is on its town-side corner)
            const int ix = sl.x <= v->x ? 1 : -1, iz = sl.z <= v->z ? 1 : -1;
            float gy = NAN; for (int y = sl.y + 6; y >= sl.y - 4 && std::isnan(gy); y--) gy = npcStandAt(w, sl.x + 4 * ix, y, sl.z + 4 * iz);
            guard->pos = vec3(sl.x + 4 * ix + 0.5f, std::isnan(gy) ? sl.y + 1.f : gy, sl.z + 4 * iz + 0.5f);
            const float topY = sl.y + 1 + lf.stops.back().second + 0.5f;
            bool wentUp = false, cameDown = false, onWalk = false;
            for (int k = 0; k < 20 * 400 && !cameDown; k++) {   // (400 s of game time, however long it takes to run)
              P->pos = watchFrom;   // (the player by the tower, so the village stays awake and its land stays loaded)
              if (k % 20 == 0) w.stream({{watchFrom, 3, false}}, 20);
              L->tick(0.05);
              const float dsh = std::hypot(guard->pos.x - sl.x - 0.5f, guard->pos.z - sl.z - 0.5f);
              if (guard->pos.y > topY - 0.3f && guard->pos.y < topY + 1.2f && dsh < 1) wentUp = true;
              if (wentUp && guard->pos.y > topY - 1.6f && dsh > 2.6f && dsh < 8) onWalk = true;
              if (wentUp && onWalk && guard->pos.y < sl.y + 2.6f && std::hypot(guard->pos.x - sl.x - 0.5f, guard->pos.z - sl.z - 0.5f) < 1) cameDown = true;
            }
            check(wentUp, "the guard rides the lift up to the wall walk on lookout");
            check(onWalk, "and steps out onto the wall walk to keep watch");
            check(cameDown, "and comes back down after the watch");
          }
        }
      }
    }
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  lvp->close();
  return fails ? 1 : 0;
}
