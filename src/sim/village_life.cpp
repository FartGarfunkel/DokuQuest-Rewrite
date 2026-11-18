// sim/village_life.cpp — see village_life.h.
#include "sim/village_life.h"
#include "sim/villagers_internal.h"
#include "sim/village_theft.h"
#include "sim/places.h"
#include "sim/skills.h"
#include "sim/skilltree.h"
#include "sim/rpg.h"
#include "world/dims.h"
#include "world/dirs.h"
#include "core/registry.h"

namespace dq {

namespace {
BlockId Bk(const char* k) { return reg().blockId(k); }
std::string posKey(ivec3 p) { return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z); }
Standing standIn(Player& p, const std::string& vid) { return standingOf(vid.empty() ? 0 : repOf(p, vid)); }
constexpr int ST_FRIEND = 3, ST_HONOURED = 5, ST_HERO = 6;
const json& tradeWants() { static const json E = json::object(); const json& D = VD(); return D.contains("tradeWants") && D["tradeWants"].is_object() ? D["tradeWants"] : E; }
bool onOverworld(const Player& p) { return p.realm == REALM_MAIN && dimOfY(ifloor(p.pos.y)) == DIM_OVERWORLD; }
}

// ------------------------------------------------------------------ the trades' requests
std::string tradeSkill(const std::string& job) { const json& T = tradeWants(); return T.contains(job) ? T[job].value("skill", std::string()) : std::string(); }

json tradeRequestFor(Level& lv, const Village& v, int i, Player& p) {
  if (i < 0 || i >= (int)v.folk.size()) return nullptr;
  const std::string& job = v.folk[i].job;
  const json& T = tradeWants();
  if (!T.contains(job)) return nullptr;
  const std::string skill = T[job].value("skill", std::string());
  const int lvl = skillLv(p, skill);
  const i64 day = npcDay(lv);
  Mulberry32 r(hash32((i32)v.seed, i, (i32)day, 35));
  // one of the trade's wants, drawn the same all day; one waiting on more skill than you have gives way to the best you're up to
  std::vector<const json*> all;
  for (auto& w : jarr(T[job], "wants")) if (w.is_array() && w.size() >= 3 && w[0].is_string() && reg().item(w[0].get<std::string>())) all.push_back(&w);
  if (all.empty()) return nullptr;
  auto minLv = [](const json& w) { return w.size() > 3 && w[3].is_number() ? w[3].get<int>() : 1; };
  const json* pick = all[(size_t)std::floor(r() * all.size()) % all.size()];
  if (minLv(*pick) > lvl) { const json* best = nullptr; for (const json* w : all) if (minLv(*w) <= lvl && (!best || minLv(*w) > minLv(*best))) best = w; if (!best) return nullptr; pick = best; }
  const json& w = *pick;
  const std::string item = w[0];
  const int a = w[1][0], b = w[1][1];
  int need = a + (int)std::floor(r() * (b - a + 1));
  const ItemDef* D = reg().item(item);
  const bool stacks = D && D->maxStack > 1;
  // the better you are at the trade, the more they ask of you (and the more they teach)
  if (stacks) need = std::min(std::max(1, D->maxStack) * 2, (int)std::lround(need * (1 + (lvl - 1) / 40.0)));
  else need += lvl >= 40 ? 1 : 0;
  const double teach = std::round((stacks ? need * 15.0 : need * 150.0) * (1 + (lvl - 1) / 25.0));
  const int em = 3 + (stacks ? (need + 3) / 4 : need * 4) + lvl / 12;
  const std::string name = npcName(npcSeedOf(v, i)), why = w[2].is_string() ? w[2].get<std::string>() : std::string();
  return {{"id", v.id + ":tr" + std::to_string(i) + ":" + std::to_string(day)}, {"vid", v.id}, {"giver", i}, {"from", name}, {"kind", "fetch"}, {"trade", true}, {"item", item}, {"need", need},
          {"title", name + "'s request"}, {"text", why + " Bring " + std::to_string(need) + " " + itemName(item) + "."},
          {"reward", {{"em", em}, {"xp", 15 + std::min(need, 40)}, {"rep", 4}, {"skill", json::array({skill, teach})}}}};
}

json tradeRequests(Level& lv, const Village& v, Player& p) {
  json all = json::array();
  for (int i = 0; i < (int)v.folk.size(); i++) if (json q = tradeRequestFor(lv, v, i, p); q.is_object()) all.push_back(q);
  if (all.size() <= 4) return all;
  json out = json::array();   // four of them a day, a different four each day
  const size_t k0 = (size_t)(npcDay(lv) % (i64)all.size());
  for (size_t k = 0; k < 4; k++) out.push_back(all[(k0 + k) % all.size()]);
  return out;
}

// ------------------------------------------------------------------ a Friend's lessons
int pathLessons(const Player& p) {
  int n = 0;
  if (p.stats.is_object() && p.stats.contains("pathLessons") && p.stats["pathLessons"].is_object())
    for (auto& [k, x] : p.stats["pathLessons"].items()) if (x.is_number()) n += x.get<int>();
  return n;
}
static int lessonsAt(const Player& p, const std::string& vid) {
  return p.stats.is_object() && p.stats.contains("pathLessons") && p.stats["pathLessons"].is_object() ? p.stats["pathLessons"].value(vid, 0) : 0;
}
static constexpr int LESSONS_MAX = 3;
bool pathLessonOffered(Level& lv, Player& p, const Village& v, const std::string& job, int* price) {
  (void)lv;
  const json& TR = VD()["trains"];
  if (!TR.contains(job) || !p.stats.is_object() || !p.stats.contains("path") || !p.stats["path"].is_object()) return false;
  const std::string want = TR[job].is_string() ? TR[job].get<std::string>() : "", mine = p.stats["path"].value("k", std::string());
  if (mine.empty() || (!want.empty() && want != mine)) return false;   // (the elder teaches any path; the others their own)
  const Standing st = standIn(p, v.id);
  if (st.k < ST_FRIEND || lessonsAt(p, v.id) >= (st.k >= ST_HONOURED ? 2 : 1) || pathLessons(p) >= LESSONS_MAX) return false;
  if (price) *price = 30 + 30 * pathLessons(p);
  return true;
}
bool pathLessonTake(Level& lv, Player& p, const Village& v, const std::string& job, std::string& say) {
  int price = 0;
  if (!pathLessonOffered(lv, p, v, job, &price)) { say = "I've taught you all I can for now."; return false; }
  if (!payCoins(lv, p, price)) { say = "A lesson like that is " + std::to_string(price) + " gold coins. Come back when you have them."; sfxUi(lv, p, "click_off"); return false; }
  json& L = p.stats["pathLessons"]; if (!L.is_object()) L = json::object();
  L[v.id] = L.value(v.id, 0) + 1;
  say = "Watch closely. No, closer. There: that's the trick of it. You'll find it comes easier now.";
  toast(lv, p, "An advanced lesson: a talent point more on your path", "epic");
  sfxUi(lv, p, "levelup");
  return true;
}
int pathForgetPrice(Level& lv, Player& p) {
  VillageRef v = villageAtPos(lv, p.realm, p.pos, 4);
  if (!v || std::hypot(p.pos.x - v->x, p.pos.z - v->z) > v->r + 4) return 20;
  const int k = standIn(p, v->id).k;
  return k >= ST_HONOURED ? 5 : k >= ST_FRIEND ? 10 : 20;
}

// ------------------------------------------------------------------ an Honoured guest's house
bool homeOffered(Player& p, const std::string& vid) { return standIn(p, vid).k >= ST_HONOURED; }

namespace {
bool doorNear(World& w, int x, int y, int z) {
  for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (reg().block(w.getBlock(x + dx, y + dy, z + dz)).has("door")) return true;
  return false;
}
bool freeCell(World& w, int x, int y, int z) {
  const Registry& R = reg();
  return w.getBlock(x, y, z) == AIR && w.getBlock(x, y + 1, z) == AIR && R.tSolid[w.getBlock(x, y - 1, z)] && !w.blockEntity(x, y, z) && !doorNear(w, x, y, z);
}
const VBld* bldAt(const Village& v, ivec3 at) {
  for (auto& b : v.blds) if (at.x >= b.x0 && at.x <= b.x1 && at.z >= b.z0 && at.z <= b.z1) return &b;
  return nullptr;
}
}

bool homeClaim(Level& lv, Player& p, const Village& v, HeroHouse& out) {
  World& w = lv.world(REALM_MAIN);
  const Registry& R = reg();
  json& claims = lv.info.data["homeClaims"]; if (!claims.is_object()) claims = json::object();
  for (const VBld& b : v.blds) {
    if (b.type.rfind("house", 0) != 0 || !b.job.empty() || b.x1 - b.x0 < 3 || b.z1 - b.z0 < 3) continue;
    const std::string ck = v.id + ":" + std::to_string(b.x0) + "," + std::to_string(b.z0);
    if (claims.contains(ck)) continue;   // (someone else's now)
    bool lived = false;
    for (const VFolk& f : v.folk) if (!f.noBed && f.home.x >= b.x0 && f.home.x <= b.x1 && f.home.z >= b.z0 && f.home.z <= b.z1) lived = true;
    if (lived || !w.isLoaded(b.x0, b.y, b.z0) || !w.isLoaded(b.x1, b.y, b.z1)) continue;
    const int top = b.y + std::max(4, b.H);
    // a bed it has already (a house of the pieces' may), else one put in on the ground floor
    ivec3 bed(INT32_MIN); bool newBed = false;
    for (int y = b.y + 1; y <= top && bed.x == INT32_MIN; y++) for (int z = b.z0; z <= b.z1 && bed.x == INT32_MIN; z++) for (int x = b.x0; x <= b.x1; x++)
      if (R.tShape[w.getBlock(x, y, z)] == ShapeKind::Bed) { bed = ivec3(x, y, z); break; }
    int bedK = 0;
    if (bed.x == INT32_MIN) {
      for (int y = b.y + 1; y <= b.y + 3 && bed.x == INT32_MIN; y++) for (int z = b.z0 + 1; z < b.z1 && bed.x == INT32_MIN; z++) for (int x = b.x0 + 1; x < b.x1 && bed.x == INT32_MIN; x++)
        for (int k = 0; k < 4; k++) {
          const int hx = x + DIRX[k], hz = z + DIRZ[k];
          if (hx <= b.x0 || hx >= b.x1 || hz <= b.z0 || hz >= b.z1 || !freeCell(w, x, y, z) || !freeCell(w, hx, y, hz)) continue;
          bed = ivec3(x, y, z); bedK = k; newBed = true; break;
        }
      if (bed.x == INT32_MIN) continue;
    }
    // a chest by a wall, on the bed's floor
    ivec3 chest(INT32_MIN);
    const ivec3 head = newBed ? bed + ivec3(DIRX[bedK], 0, DIRZ[bedK]) : bed;
    for (int z = b.z0 + 1; z < b.z1 && chest.x == INT32_MIN; z++) for (int x = b.x0 + 1; x < b.x1; x++) {
      const ivec3 c(x, bed.y, z);
      if (c == bed || c == head || !freeCell(w, x, bed.y, z)) continue;
      bool wall = false; for (int k = 0; k < 4; k++) { const BlockId n = w.getBlock(x + DIRX[k], bed.y, z + DIRZ[k]); if (R.tSolid[n] && R.tOpaque[n]) wall = true; }
      if (wall) { chest = c; break; }
    }
    if (chest.x == INT32_MIN) continue;
    if (newBed) {
      BlockId bb = Bk("red_bed"); if (!bb) bb = Bk("bed");
      w.setBlock(bed.x, bed.y, bed.z, bb, (u8)bedK); w.setBlock(head.x, head.y, head.z, bb, (u8)(bedK | 4));
    }
    w.setBlock(chest.x, chest.y, chest.z, Bk("chest"), 0);
    json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
    int n = 0;
    for (auto& [k, c] : std::vector<std::pair<const char*, int>>{{COIN, 16}, {"bread", 8}, {"torch", 16}, {"oak_sapling", 2}}) if (reg().item(k)) items[n++] = ItemStack(k, c).toJson();
    w.setBlockEntity(chest.x, chest.y, chest.z, json{{"type", "chest"}, {"items", items}, {"owner", p.name}});
    claims[ck] = p.name;
    out = HeroHouse{}; out.ok = true; out.bed = bed; out.door = b.door; out.name = v.name;
    return true;
  }
  return false;
}

void homeOwn(Level& lv, Player& p, const Village& v, ivec3 bed) {
  World& w = lv.world(REALM_MAIN);
  const Registry& R = reg();
  const VBld* b = bldAt(v, bed);
  const int x0 = b ? b->x0 : bed.x - 6, x1 = b ? b->x1 : bed.x + 6, z0 = b ? b->z0 : bed.z - 6, z1 = b ? b->z1 : bed.z + 6;
  json& placed = villageRec(lv, v.id)["placed"]; if (!placed.is_object()) placed = json::object();
  for (int y = bed.y - 1; y <= bed.y + 2; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    if (!w.isLoaded(x, y, z)) continue;
    const BlockId id = w.getBlock(x, y, z);
    if (!id) continue;
    const bool cont = R.block(id).has("container"), isBed = R.tShape[id] == ShapeKind::Bed;
    if (!cont && !isBed) continue;
    if (isBed && std::abs(x - bed.x) + std::abs(z - bed.z) > 1) continue;   // (only your own bed: a house of the pieces' may have more)
    placed[posKey(ivec3(x, y, z))] = p.name;
    if (cont) if (json* e = w.blockEntity(x, y, z, true)) {
      if (!e->is_object()) *e = json{{"type", "chest"}};
      (*e)["owner"] = p.name; w.blockEntityChanged(x, y, z);
    }
  }
}

static void homeRevoke(Level& lv, Player& p, const std::string& vid) {
  const json* H = homeOf(p, vid);
  if (!H) return;
  const json& bd = (*H)["bed"];
  const vec3 bed(bd[0].get<float>(), bd[1].get<float>(), bd[2].get<float>());
  if (p.spawnPoint.y >= 0 && std::hypot(p.spawnPoint.x - bed.x - 0.5f, p.spawnPoint.z - bed.z - 0.5f) < 2.5f) { p.spawnPoint = vec3(0, -1, 0); p.spawnRealm = REALM_MAIN; }
  json& claims = lv.info.data["homeClaims"];
  if (claims.is_object()) for (auto it = claims.begin(); it != claims.end(); ) { if (it.key().rfind(vid + ":", 0) == 0 && it.value() == p.name) it = claims.erase(it); else ++it; }
  p.stats["homes"].erase(vid);
  VillageRef v = villageById(lv, vid);
  toast(lv, p, (v ? v->name : std::string("The village")) + " has taken back the house it gave you. (What you left in it is still yours.)", "warn");
}

// ------------------------------------------------------------------ the watch: a patrol, an escort
json watchJobs(Level& lv, Player& p, const Village& v) {
  json out = json::array();
  if (standIn(p, v.id).k < ST_HONOURED) return out;
  const i64 day = npcDay(lv);
  Mulberry32 r(hash32((i32)v.seed, 9, (i32)day, 36));
  {   // the round: posts at the village's edge, walked in turn
    const int n = 4 + (v.r > 30 ? 2 : 0);
    const float rad = std::max(8.f, v.r * 0.8f), a0 = (float)(r() * TAU);
    json pts = json::array();
    for (int k = 0; k < n; k++) { const float a = a0 + k * (float)TAU / n; pts.push_back(json::array({(int)std::floor(v.x + std::cos(a) * rad + 0.5f), (int)std::floor(v.z + std::sin(a) * rad + 0.5f)})); }
    out.push_back({{"id", v.id + ":pt:" + std::to_string(day)}, {"vid", v.id}, {"giver", -1}, {"from", "The Watch"}, {"kind", "patrol"}, {"pts", pts}, {"need", n}, {"have", 0},
                   {"at", pts[0]}, {"title", "Patrol: " + v.name},
                   {"text", "Walk the rounds of " + v.name + ": " + std::to_string(n) + " posts at its edge, in turn, the first near x " + std::to_string(pts[0][0].get<int>()) + ", z " + std::to_string(pts[0][1].get<int>()) + ". Then report to a guard."},
                   {"reward", {{"em", 4 + n}, {"xp", 40}, {"rep", 6}, {"skill", json::array({"defence", 250})}}}});
  }
  float nd = 1e9f;
  if (VillageRef nb = villageNeighbour(lv, v, &nd); nb && nd < 900) {   // someone with business in the next village
    const int seed = (int)std::floor(r() * 99999);
    const std::string who = npcName((u32)seed), dir = dirWord(std::atan2(nb->z - v.z, nb->x - v.x));
    out.push_back({{"id", v.id + ":es:" + nb->id + ":" + std::to_string(day)}, {"vid", v.id}, {"giver", -1}, {"from", "The Watch"}, {"kind", "escort"}, {"to", nb->id}, {"toName", nb->name},
                   {"lname", who}, {"ls", seed}, {"need", 1}, {"have", 0}, {"at", json::array({nb->x, nb->z})}, {"title", "Escort: " + who + " to " + nb->name},
                   {"text", who + " has business in " + nb->name + ", some " + std::to_string(roundTo(nd, 10)) + " blocks to the " + dir + " (near x " + std::to_string(nb->x) + ", z " + std::to_string(nb->z) + "). The roads aren't safe. See them there."},
                   {"reward", {{"em", 6 + (int)std::floor(nd / 100 + 0.5)}, {"xp", 60}, {"rep", 10}, {"skill", json::array({"exploring", 300})}}}});
  }
  return out;
}

// the escorted one: walks with you from the village (as the missing do), done once they're in the other village
static void escortTick(Level& lv, Player& p, const json& q) {
  const std::string id = q.value("id", std::string());
  Creature* who = nullptr;
  for (auto& up : lv.entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (!c->removed && c->alive() && c->data.value("escortQ", std::string()) == id) { who = c; break; }
  VillageRef from = villageById(lv, q.value("vid", std::string())), to = villageById(lv, q.value("to", std::string()));
  if (!from || !to) return;
  if (!who) {   // (none yet, or gone with a save: they wait for you in their village)
    if (!onOverworld(p) || std::hypot(p.pos.x - from->x, p.pos.z - from->z) > from->r + 30) return;
    World& w = lv.world(REALM_MAIN);
    const int x = ifloor(p.pos.x + 1.5f), z = ifloor(p.pos.z), y = findSafeY(w, x, z, DIM_OVERWORLD, ifloor(p.pos.y) + 3);
    if (y < 1) return;
    who = spawnCreature(lv, REALM_MAIN, "villager", vec3(x + 0.5f, y, z + 0.5f));
    if (!who) return;
    const u32 seed = (u32)(q.value("ls", 0) % 100000);
    std::string nm = q.value("lname", npcName(seed)); if (nm.size() > 30) nm.resize(30);
    who->data["npc"] = {{"v", from->id}, {"i", 300}, {"n", nm}, {"j", "merchant"}, {"s", seed}, {"p", 0}};
    who->data["escortQ"] = id; who->persistent = true; who->kept = false;
    if (VillagerSystem* S = vsys(lv)) S->mind(who->id).follow = p.id;
    toast(lv, p, nm + " walks with you. Lead on to " + to->name + ".", "good");
    return;
  }
  if (std::hypot(who->pos.x - to->x, who->pos.z - to->z) < std::max(14, to->r)) {
    if (json* m = findQuest(p, id)) { (*m)["have"] = 1; questFinish(lv, p, id); }
    npcSay(lv, *who, "Here we are, safe and sound. Thank you, friend.");
    who->removed = true;
  }
}

// ------------------------------------------------------------------ a Hero's statue
namespace {
const char* STATUE_SIGN0 = "In honour of";
struct StatueBlock { ivec3 at; std::string key; u8 meta = 0; };
bool statueSite(Level& lv, const Village& v, ivec3& at, int& f) {
  World& w = lv.world(REALM_MAIN);
  const Registry& R = reg();
  const BlockId way = Bk("waystone"), wayTop = Bk("waystone_upper");
  for (int rr = 4; rr <= std::max(6, v.r - 3); rr++) for (int k = 0; k < 24; k++) {
    const float a = k / 24.f * (float)TAU + rr * 0.7f;
    const int cx = ifloor(v.x + std::cos(a) * rr + 0.5f), cz = ifloor(v.z + std::sin(a) * rr + 0.5f);
    // the figure faces the square's middle
    const int tx = v.x - cx, tz = v.z - cz;
    const int ff = std::abs(tx) > std::abs(tz) ? (tx > 0 ? 1 : 3) : (tz > 0 ? 2 : 0);
    const int sx = cx + DIRX[ff] * 2, sz = cz + DIRZ[ff] * 2;
    bool ok = true; int gy = INT32_MIN;
    auto cell = [&](int x, int z) {
      if (!w.isLoaded(x, v.y, z)) return false;
      for (auto& b : v.blds) if (x >= b.x0 - 1 && x <= b.x1 + 1 && z >= b.z0 - 1 && z <= b.z1 + 1) return false;
      if (std::abs(x - v.board.x) <= 2 && std::abs(z - v.board.z) <= 2) return false;
      if (std::abs(x - v.bell.x) <= 2 && std::abs(z - v.bell.z) <= 2) return false;
      const int y = findSafeY(w, x, z, DIM_OVERWORLD, v.y + 6);
      if (y < 1 || std::abs(y - (v.y + 1)) > 2) return false;
      if (gy == INT32_MIN) gy = y; else if (y != gy) return false;
      const BlockId g = w.getBlock(x, y - 1, z);
      if (R.tLiquid[g] || !R.tSolid[g] || !R.tOpaque[g] || R.tShape[g] != ShapeKind::None) return false;
      for (int yy = y; yy <= y + 4; yy++) { const BlockId id = w.getBlock(x, yy, z); if (id != AIR || w.blockEntity(x, yy, z)) return false; }
      for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) for (int yy = y - 1; yy <= y + 1; yy++) { const BlockId id = w.getBlock(x + dx, yy, z + dz); if (id == way || id == wayTop) return false; }
      return true;
    };
    for (int dz = -1; dz <= 1 && ok; dz++) for (int dx = -1; dx <= 1 && ok; dx++) ok = cell(cx + dx, cz + dz);
    if (ok) ok = cell(sx, sz);
    if (!ok) continue;
    at = ivec3(cx, gy, cz); f = ff;
    return true;
  }
  return false;
}
bool statueRaise(Level& lv, Player& p, const Village& v) {
  ivec3 at; int f = 0;
  if (!statueSite(lv, v, at, f)) return false;
  World& w = lv.world(REALM_MAIN);
  std::vector<StatueBlock> L;
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) L.push_back({at + ivec3(dx, 0, dz), dx || dz ? "stone_bricks" : "chiseled_stone_bricks"});
  const int lx = -DIRZ[f], lz = DIRX[f];   // the figure's left-right
  L.push_back({at + ivec3(0, 1, 0), "stone_brick_wall"});    // legs
  L.push_back({at + ivec3(0, 2, 0), "polished_andesite"});   // body
  L.push_back({at + ivec3(lx, 2, lz), "andesite_wall"});     // arms
  L.push_back({at + ivec3(-lx, 2, -lz), "andesite_wall"});
  L.push_back({at + ivec3(0, 3, 0), "polished_diorite"});    // head
  json rec = json::array();
  for (auto& b : L) { BlockId id = Bk(b.key.c_str()); if (!id) { id = Bk("stone_bricks"); b.key = "stone_bricks"; } w.setBlock(b.at.x, b.at.y, b.at.z, id, b.meta); rec.push_back(json::array({b.at.x, b.at.y, b.at.z, b.key})); }
  // the sign in front, facing whoever comes to look
  const ivec3 s = at + ivec3(DIRX[f] * 2, 0, DIRZ[f] * 2);
  BlockId sign = Bk("oak_sign"); std::string sk = "oak_sign"; if (!sign) { sign = Bk("spruce_sign"); sk = "spruce_sign"; }
  if (sign) {
    const float yaw = std::atan2((float)DIRX[f], (float)DIRZ[f]);
    w.setBlock(s.x, s.y, s.z, sign, (u8)((int)std::round(((PI - yaw) / TAU) * 16) & 15));
    std::string nm = p.name.empty() ? std::string("our Hero") : p.name; if (nm.size() > 15) nm.resize(15);
    std::string vn = v.name; if (vn.size() > 15) vn.resize(15);
    w.setBlockEntity(s.x, s.y, s.z, json{{"type", "sign"}, {"lines", json::array({STATUE_SIGN0, nm, "Hero of", vn})}});
    rec.push_back(json::array({s.x, s.y, s.z, sk}));
  }
  json& S = lv.info.data["statues"]; if (!S.is_object()) S = json::object();
  S[v.id + ":" + p.name] = {{"at", json::array({at.x, at.y, at.z})}, {"f", f}, {"blocks", rec}};
  toast(lv, p, v.name + " raises a statue of you in the square!", "epic");
  sfxUi(lv, p, "levelup");
  return true;
}
// taken down once its blocks are loaded (those changed since are left as they are)
bool statueTakeDown(Level& lv, const std::string& key) {
  json& S = lv.info.data["statues"];
  if (!S.is_object() || !S.contains(key)) return true;
  World& w = lv.world(REALM_MAIN);
  for (auto& b : S[key]["blocks"]) if (!w.isLoaded(b[0].get<int>(), b[1].get<int>(), b[2].get<int>())) return false;
  for (auto& b : S[key]["blocks"]) {
    const int x = b[0], y = b[1], z = b[2];
    if (reg().block(w.getBlock(x, y, z)).key != b[3].get<std::string>()) continue;
    w.setBlockEntity(x, y, z, nullptr);
    w.setBlock(x, y, z, AIR);
  }
  S.erase(key);
  return true;
}
}

// ------------------------------------------------------------------ a Hero's waystone
bool wayFreeFor(Level& lv, Player& p, const std::string& wayId) {
  (void)lv;
  return wayId.rfind("v:", 0) == 0 && standIn(p, wayId.substr(2)).k >= ST_HERO;
}

// ------------------------------------------------------------------ the Shrine of Renewal
int respecPoints(const Player& p) { return treePointsSpent(p) + p.statCon + p.statStr + p.statAgi + p.statInt + p.statLck + p.statFoc; }
int respecCost(Player& p, const std::string& vid) {
  const int pts = respecPoints(p);
  if (pts <= 0) return 0;
  double c = 10 + 8.0 * pts;
  if (!vid.empty()) {
    static const double K[7] = {1.5, 1, 1, 0.9, 0.8, 0.65, 0.5};
    c *= K[std::clamp(standIn(p, vid).k, 0, 6)] * shopPriceFactor(p, vid);
  }
  return std::max(1, (int)std::lround(c));
}
bool respecDo(Level& lv, Player& p, const std::string& vid, std::string& say) {
  const int pts = respecPoints(p), cost = respecCost(p, vid);
  if (pts <= 0) { say = "There's nothing to take back: you've spent no talents or stat points."; return false; }
  if (!payCoins(lv, p, cost)) { say = "The shrine asks " + std::to_string(cost) + " gold coins. You haven't enough."; sfxUi(lv, p, "click_off"); return false; }
  treeReset(p);
  p.statCon = p.statStr = p.statAgi = p.statInt = p.statLck = p.statFoc = 0;
  p.recalcMax();
  if (p.hp > p.maxHp) p.hp = p.maxHp;
  say = "The light in the basin rises and settles. What you learnt is yours to choose again: " + std::to_string(pts) + " point" + (pts == 1 ? "" : "s") + " back.";
  toast(lv, p, "Renewed: your talents and stat points are free to spend again", "epic");
  sfxUi(lv, p, "enchant");
  return true;
}

// ------------------------------------------------------------------ now and then, for each player
void villageLifeTick(Level& lv, Player& p, double dt) {
  if (lv.isRemote()) return;
  float t = p.data.value("vlifeT", 0.f) - (float)dt;
  if (t > 0) { p.data["vlifeT"] = t; return; }
  p.data["vlifeT"] = 0.5f;
  json& S = npcStats(p);
  // the watch's jobs in hand
  bool changed = false;
  for (auto& q : S["quests"]) {
    const std::string k = q.value("kind", std::string());
    if (k == "patrol" && onOverworld(p)) {
      const int have = q.value("have", 0), need = q.value("need", 1);
      if (have >= need || !q.contains("pts") || have >= (int)q["pts"].size()) continue;
      const json& pt = q["pts"][have];
      if (std::hypot(p.pos.x - (pt[0].get<float>() + 0.5f), p.pos.z - (pt[1].get<float>() + 0.5f)) > 3.5f) continue;
      q["have"] = have + 1; changed = true;
      if (have + 1 >= need) toast(lv, p, q.value("title", std::string()) + ": the round is walked. Report to a guard.", "good");
      else { q["at"] = q["pts"][have + 1]; toast(lv, p, "Post " + std::to_string(have + 1) + " of " + std::to_string(need) + ". On to the next (near x " + std::to_string(q["pts"][have + 1][0].get<int>()) + ", z " + std::to_string(q["pts"][have + 1][1].get<int>()) + ")."); }
    }
  }
  if (changed) questHudChanged(lv, p);
  { json L = S["quests"]; for (auto& q : L) if (q.value("kind", "") == "escort" && !q.value("have", 0)) escortTick(lv, p, q); }
  // one walking with you whose job is gone (dropped, lapsed): they go their own way
  if (const float sweep = p.data.value("escortSweep", 0.f) - 0.5f; sweep > 0) p.data["escortSweep"] = sweep;
  else {
    p.data["escortSweep"] = 5.f;
    for (auto& up : lv.entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (!c->removed && c->data.contains("escortQ")) {
      const std::string qid = c->data.value("escortQ", std::string());
      bool held = false; for (Player* o : levelPlayers(lv)) if (findQuest(*o, qid)) held = true;
      if (!held) c->removed = true;
    }
  }
  // standing's doors: a Hero's statue and waystone, perks lost
  if (!S["vrep"].is_object()) return;
  std::vector<std::string> vids; for (auto& [vid, x] : S["vrep"].items()) vids.push_back(vid);
  const json& HM = p.stats.value("homes", json::object());
  if (HM.is_object()) for (auto& [vid, x] : HM.items()) if (std::find(vids.begin(), vids.end(), vid) == vids.end()) vids.push_back(vid);
  for (const std::string& vid : vids) {
    const Standing st = standIn(p, vid);
    const std::string key = vid + ":" + p.name;
    const bool up = lv.info.data.contains("statues") && lv.info.data["statues"].is_object() && lv.info.data["statues"].contains(key);
    if (st.k < ST_HERO && !up && !(st.k == 0 && homeOf(p, vid))) continue;   // (nothing for this village to do)
    VillageRef v = villageById(lv, vid);
    if (st.k >= ST_HERO && !up && v && onOverworld(p) && std::hypot(p.pos.x - v->x, p.pos.z - v->z) < v->r + 24) {
      json& wait = p.data["statueWait"]; if (!wait.is_object()) wait = json::object();
      const double now = vsys(lv) ? vsys(lv)->now : 0;
      if (wait.value(vid, -1.0) <= now && !statueRaise(lv, p, *v)) wait[vid] = now + 30;   // (no room in the square yet: looked for again in a while)
    }
    if (st.k < ST_HONOURED && up && statueTakeDown(lv, key) && v) toast(lv, p, v->name + " has taken down your statue.", "warn");
    if (st.k >= ST_HERO) {   // the village's waystone, linked to its Hero
      static const json NONE = json::object();
      const json& W = lv.info.data.contains("waystones") ? lv.info.data["waystones"] : NONE;
      const std::string wid = "v:" + vid;
      json& ways = p.stats["ways"]; if (!ways.is_object()) ways = json::object();
      if (W.is_object() && W.contains(wid) && !ways.contains(wid)) {
        ways[wid] = W[wid]; ways[wid]["hero"] = vid;
        toast(lv, p, "The " + W[wid].value("name", std::string("village's waystone")) + " is linked to you: as its Hero you travel there free", "epic");
      }
    }
    if (st.k == 0 && homeOf(p, vid)) homeRevoke(lv, p, vid);
  }
}

} // namespace dq
