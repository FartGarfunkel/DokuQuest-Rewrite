// tools/test_npc_paths.cpp — headless audit of how townsfolk find their way. In a few generated towns (a stone-walled
// one, a palisaded one, an open village) every villager and guard is sent, one errand after another, to points about the
// town: house doors, workplaces, the square, beds, street cells. Each errand: did they get there, how long it took, did
// they stick (no progress for 10 s), were they moved there by the rescue (a jump of over 2 blocks in one tick).
//   test_npc_paths [--userdir DIR] [--towns N] [--errands N] [--ladder-only]   (DQ_NAV_TRACE=1: the climber and the miller step by step)    exit code 0 = the arrival rate is at least 80% (before this audit: 45%), the ladder and the miller pass
#include "core/init.h"
#include "core/registry.h"
#include "sim/villagers.h"
#include "sim/villagers_internal.h"
#include "gen/village_mill_lift.h"
#include "sim/mob_combat.h"
#include "gen/structures.h"
#include "gen/structures_api.h"
#include "world/dims.h"
#include "world/chunk.h"
#include "gen/generator.h"
#include "sim/moving.h"
#include "sim/physics.h"
#include <set>
#include "common/noise.h"
#include <thread>
#include <cstdio>
using namespace dq;

namespace {
struct Errand { vec3 to; std::string kind; };
struct Tally { int sent = 0, arrived = 0, stuck = 0, rescued = 0, fell = 0; double secs = 0; };
}

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-npc-paths").string();
  int townsWanted = 3, errands = 6; bool golemsOnly = false;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--userdir" && i + 1 < argc) ud = argv[++i];
    else if (a == "--towns" && i + 1 < argc) townsWanted = std::atoi(argv[++i]);
    else if (a == "--errands" && i + 1 < argc) errands = std::atoi(argv[++i]);
    else if (a == "--ladder-only") townsWanted = 0;
    else if (a == "--golems-only") golemsOnly = true;
  }
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(2);
  EventBus bus;
  LevelInfo li; li.name = "NpcPaths"; li.seedText = "villagers";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  Level* L = lvp.get();
  L->info.rules["mobSpawning"] = false;
  World& w = L->world();
  Player* P; { auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(0.5f, 700, 0.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }

  Tally all; std::map<std::string, Tally> byKind; int millerFails = 0;
  {   // the styles' own windmills: which have their stones up a floor, and which of those get a lift (gen/village_mill_lift.cpp)
    int mills = 0, upper = 0, lifted = 0; std::string without;
    const BlockId MS = reg().blockId("millstone");
    if (const VillagePieces* VP = villagePieces()) for (const auto& [pool, list] : VP->pools) for (const VPPiece& pc : list) {
      int my = -1; for (const VPBlock& k : pc.blocks) if (pc.states[k.s].id == MS) my = k.y;
      if (my < 0) continue;
      mills++;
      if (std::getenv("DQ_MILL_DUMP") && pc.name == "plains_windmill_1") {
        std::vector<char> g((size_t)pc.sx * pc.sy * pc.sz, '.');
        for (const VPBlock& k : pc.blocks) { const BlockId b = pc.states[k.s].id; const std::string& key = reg().block(b).key;
          g[((size_t)k.y * pc.sz + k.z) * pc.sx + k.x] = b == MS ? 'M' : reg().block(b).has("climb") ? 'L' : key.find("stairs") != std::string::npos ? 's' : key.find("slab") != std::string::npos ? '_' : reg().tSolid[b] ? '#' : '+'; }
        for (int y = 0; y < pc.sy; y++) { printf("   y%d\n", y); for (int z = 0; z < pc.sz; z++) { printf("    "); for (int x = 0; x < pc.sx; x++) putchar(g[((size_t)y * pc.sz + z) * pc.sx + x]); printf("\n"); } }
      }
      if (my >= 4) { upper++; if (millLiftOf(pc)) lifted++; else without += " " + pc.name + "(" + std::to_string(my) + ")"; }
    }
    printf("  (windmill pieces: %d, %d with the stones up a floor, %d of those given a lift;%s%s)\n", mills, upper, lifted, without.empty() ? "" : " none for", without.c_str());
    if (upper > 0 && lifted == 0) millerFails++;
  }
  // a ladder, by hand in the sky: a floor, a ladder up a wall to a loft; a townsperson sent up it and back down
  {
    const int Y = dimBase(DIM_OVERWORLD) + 220;
    P->pos = vec3(4.5f, Y + 1, 4.5f);
    // (all the land the scene stands on loaded first: a busy machine streams slowly)
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 900 && !(w.isLoaded(-2, Y, -2) && w.isLoaded(44, Y, 18) && w.isLoaded(-2, Y, 18) && w.isLoaded(44, Y, -2));) { w.stream({{P->pos, 2, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    const BlockId stone = reg().blockId("stone"), ladder = reg().blockId("ladder"), planks = reg().blockId("planks");
    for (int x = -2; x < 12; x++) for (int z = -2; z < 12; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 12; y++) w.setBlock(x, y, z, AIR); }
    for (int x = 0; x < 9; x++) for (int y = Y + 1; y <= Y + 9; y++) w.setBlock(x, y, 9, stone);   // (the wall)
    for (int x = 0; x < 9; x++) for (int z = 5; z <= 8; z++) w.setBlock(x, Y + 6, z, planks);     // (the loft)
    for (int y = Y + 1; y <= Y + 6; y++) w.setBlock(4, y, 8, ladder, 2);                          // (against the wall, up through the loft)
    L->tick(0.05);   // (the villages' first look round, which sends home any townsperson of no village, done first)
    Creature* ce = spawnCreature(*L, REALM_MAIN, "villager", vec3(4.5f, Y + 1, 2.5f));
    Creature* c = ce;
    bool upOk = false, downOk = false, attUp = false, attDown = false;
    if (c) {
      c->data["npc"] = json{{"v", "none"}, {"i", 0}, {"n", "Climber"}};
      NpcMind& M = vsys(*L)->mind(c->id);
      const vec3 ups[2] = {vec3(4.5f, Y + 7, 6.5f), vec3(4.5f, Y + 1, 2.5f)};
      for (int leg = 0; leg < 4; leg++) {
        if (leg == 2)   // (then torches on the shaft's side put up first: up most of it the ladder is an attachment beside one)
          for (int y = Y + 2; y <= Y + 5; y++) { w.setBlock(3, y, 8, stone); w.setBlock(4, y, 8, reg().blockId("torch"), 1); w.addAttachment(4, y, 8, ladder, 2); }
        M.sent = true; M.sentTo = ups[leg & 1];
        bool moved = false;   // (set there by the rescue, not climbed: a jump of over 2 blocks in a tick)
        for (double t = 0; t < 40 && M.sent; t += 0.05) {
          const vec3 was = c->pos;
          L->tick(0.05); P->pos = vec3(4.5f, Y + 1, -6.5f);
          if (glm::distance(was, c->pos) > 2.f) moved = true;
          if ((int)(t * 20) % 20 == 0 && std::getenv("DQ_NAV_TRACE")) printf("    (climber t%.0f: %.2f %.2f %.2f climbing %d path %zu/%zu%s)\n", t, c->pos.x, c->pos.y - Y, c->pos.z, M.climbing, M.pathI, M.path.size(), M.pathPartial ? " partial" : "");
        }
        const bool there = !moved && std::hypot(c->pos.x - ups[leg & 1].x, c->pos.z - ups[leg & 1].z) < 1 && std::fabs(c->pos.y - ups[leg & 1].y) < 0.6f;
        (leg == 0 ? upOk : leg == 1 ? downOk : leg == 2 ? attUp : attDown) = there;
        M.sent = false;
      }
      c->removed = true;
    }
    printf("%s a townsperson climbs a ladder up to a loft\n%s and back down it\n", upOk ? "ok  " : "FAIL", downOk ? "ok  " : "FAIL");
    printf("%s a townsperson climbs it with a torch on the shaft's wall, the ladder an attachment beside it\n%s and back down\n", attUp ? "ok  " : "FAIL", attDown ? "ok  " : "FAIL");
    if (!upOk || !downOk || !attUp || !attDown) millerFails++;
    // a golem (1.4 wide) by a wall with a 1-wide gap straight ahead and a 2-wide one further along: it goes round by the
    // broad one (it had walked at the narrow one and stuck in it)
    {
      const int X = 20;
      for (int x = X - 2; x < X + 24; x++) for (int z = -2; z < 18; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 6; y++) w.setBlock(x, y, z, AIR); }
      for (int x = X; x < X + 20; x++) if (x != X + 4 && x != X + 13 && x != X + 14) for (int y = Y + 1; y <= Y + 3; y++) w.setBlock(x, y, 8, stone);
      Creature* g = spawnCreature(*L, REALM_MAIN, "iron_golem", vec3(X + 4.5f, Y + 1, 3.5f));
      bool there = false; double took = 0;
      if (g) {
        g->data["npc"] = json{{"v", "none"}, {"i", 200}, {"n", "Golem"}};
        NpcMind& M = vsys(*L)->mind(g->id);
        M.sent = true; M.sentTo = vec3(X + 4.5f, Y + 1, 13.5f);
        for (double t = 0; t < 60 && M.sent; t += 0.05) { L->tick(0.05); P->pos = vec3(X + 4.5f, Y + 1, -6.5f); took = t;
          if (std::getenv("DQ_NAV_TRACE") && (int)(t * 20) % 20 == 0) printf("    (golem t%.0f: %.2f %.2f path %zu/%zu%s)\n", t, g->pos.x - X, g->pos.z, M.pathI, M.path.size(), M.pathPartial ? " partial" : ""); }
        there = std::hypot(g->pos.x - (X + 4.5f), g->pos.z - 13.5f) < 1.2f;
        M.sent = false; g->removed = true;
      }
      printf("%s a golem goes round by the gap broad enough for it (%.0f s)\n", there ? "ok  " : "FAIL", took);
      if (!there) millerFails++;
    }
    fflush(stdout);
  }
  // terrain versions: a world made before terrain 24 keeps its towns as they were (no corner tower half old, half new);
  // one made at 24 has the corner towers' town-side doors
  {
    int opened[2] = {-1, -1}; std::string where;
    for (int vi = 0; vi < 2; vi++) {
      WorldGenOptions o; o.seed = L->info.seed; o.genVersion = vi ? 24 : 23; WorldGen g(o);
      const StructurePlan* town = nullptr;
      for (int k = 0; k < 160 && !town; k++) {
        const int step = k < 40 ? 4000 : 3000, kk = k < 40 ? k : k - 40;
        const int x = k < 40 ? (kk % 7 - 3) * step : (kk % 11 - 5) * step, z = k < 40 ? (kk / 7 - 3) * step : (kk / 11 - 5) * step;
        const StructurePlan* sp = locateStructure(g, DIM_OVERWORLD, "village", x, z, 24);
        if (sp && sp->data.contains("wall") && sp->data["wall"].value("kind", "") == "stone") town = sp;
      }
      if (!town) continue;
      const int x0 = town->data["wall"].value("x0", 0), z0 = town->data["wall"].value("z0", 0);
      // (the corner tower at the wall's north-west corner: its new door is the cell in from the corner, both ways)
      const int dx = x0 + 2, dz = z0 + 2;
      auto ch = std::make_unique<Chunk>(DIM_OVERWORLD, dx >> 4, dz >> 4);
      g.forDim(DIM_OVERWORLD)->generate(*ch);
      // a doorway: two open cells with the tower's wall over them and its floor under, somewhere up the column
      int open = 0;
      const BlockId SB = reg().blockId("stone_bricks");
      for (int y = 2; y < DIM_H - 3; y++) {
        const BlockId u = ch->get(dx & 15, y - 1, dz & 15), a = ch->get(dx & 15, y, dz & 15), b = ch->get(dx & 15, y + 1, dz & 15), c = ch->get(dx & 15, y + 2, dz & 15);
        if (reg().tSolid[u] && a == AIR && b == AIR && reg().tSolid[c] && reg().block(c).key.find("brick") != std::string::npos) { open = 1; break; }
      }
      (void)SB;
      opened[vi] = open; where = town->name;
    }
    printf("%s a world made at terrain 23 keeps its corner towers as they were (no door in from the corner, %s)\n", opened[0] == 0 ? "ok  " : "FAIL", where.c_str());
    printf("%s and one made at 24 has it\n", opened[1] == 1 ? "ok  " : "FAIL");
    if (opened[0] != 0 || opened[1] != 1) millerFails++;
  }
  // the towns: one of each kind of wall, then any
  std::vector<const StructurePlan*> towns;
  auto kindOf = [](const StructurePlan* sp) { return !sp->data.contains("wall") ? std::string("open") : sp->data["wall"].value("kind", std::string("?")); };
  for (const char* want : {"stone", "palisade", "open"}) {
    for (int k = 0; k < 120 && (int)towns.size() < townsWanted; k++) {
      const int step = 3000, x = (k % 11 - 5) * step, z = (k / 11 - 5) * step;
      const StructurePlan* sp = locateStructure(*L->gen, DIM_OVERWORLD, "village", x, z, 24);
      if (!sp || kindOf(sp) != want) continue;
      bool dup = false; for (auto* t : towns) if (t == sp || t->name == sp->name) dup = true;
      if (dup) continue;
      towns.push_back(sp); break;
    }
  }
  long nodesBefore = 0; (void)nodesBefore;
  for (const StructurePlan* town : towns) {
    VillageRef v = villageById(*L, town->data.value("id", std::string()));
    if (!v) { printf("  (%s: no village record)\n", town->name.c_str()); continue; }
    printf("[%s: %s, %s wall, %zu buildings, %zu folk]\n", v->name.c_str(), v->tier.c_str(), kindOf(town).c_str(), v->blds.size(), v->folk.size());
    fflush(stdout);
    // the whole town loaded, and its people about
    P->pos = vec3(v->x + 0.5f, v->y + 2, v->z + 0.5f);
    const int rad = std::min(6, (v->r + 12) / 16 + 1);
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 900;) {   // (till it has: a busy machine streams slowly)
      w.stream({{P->pos, rad, false}}, 40);
      bool loaded = true;
      for (int dx = -rad + 1; dx < rad; dx++) for (int dz = -rad + 1; dz < rad; dz++) if (!w.isLoaded(v->x + dx * 16, v->y, v->z + dz * 16)) loaded = false;
      L->tick(0.05);
      if (loaded) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    L->info.time = 3600;   // (morning)
    for (int k = 0; k < 200; k++) { L->tick(0.05); if (k % 20 == 0) w.stream({{P->pos, rad, false}}, 40); }
    std::vector<Creature*> folk;
    for (auto& up : L->entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (const json* N = npcOf(*c))
      if (!c->removed && N->value("v", "") == v->id && N->value("i", 0) < 300) folk.push_back(c);
    // the errands: points the town was built to be walked to
    std::vector<Errand> pts;
    for (const VBld& b : v->blds) {
      if (b.door != ivec3(0)) pts.push_back({vec3(b.door) + vec3(0.5f, 0, 0.5f), "door"});
      if (b.hasWork) pts.push_back({vec3(b.work) + vec3(0.5f, 0, 0.5f), "work"});
    }
    for (const VFolk& f : v->folk) {
      pts.push_back({vec3(f.home) + vec3(0.5f, 0, 0.5f), "bed"});
      if (f.hasWork) pts.push_back({vec3(f.work) + vec3(0.5f, 0, 0.5f), "work"});
    }
    for (const VRoad& r : v->roads) for (size_t i = 0; i < r.cells.size(); i += std::max<size_t>(1, r.cells.size() / 4)) pts.push_back({vec3(r.cells[i]) + vec3(0.5f, 1, 0.5f), "street"});
    pts.push_back({vec3(v->bell) + vec3(0.5f, 0, 1.5f), "square"});
    {   // a point nobody can stand on (a street's cell out over the water): the nearest place by it that holds, or dropped
      int snappedN = 0, dropped = 0;
      std::vector<Errand> ok;
      for (Errand e : pts) {
        const int x = ifloor(e.to.x), y = ifloor(e.to.y + 0.45f), z = ifloor(e.to.z);
        auto standsAt = [&](int ax, int ay, int az) { return !std::isnan(npcStandAt(w, ax, ay, az)); };
        if (standsAt(x, y, z) || standsAt(x, y + 1, z) || standsAt(x, y - 1, z)) { ok.push_back(e); continue; }
        bool found = false;
        for (int r = 1; r <= 3 && !found; r++) for (int dz = -r; dz <= r && !found; dz++) for (int dx = -r; dx <= r && !found; dx++) {
          if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
          for (int dy : {0, 1, -1, 2, -2}) if (standsAt(x + dx, y + dy, z + dz)) {
            e.to = vec3(x + dx + 0.5f, npcStandAt(w, x + dx, y + dy, z + dz), z + dz + 0.5f); found = true; break;
          }
        }
        if (found) { ok.push_back(e); snappedN++; } else dropped++;
      }
      pts = ok;
      printf("  (%zu points; %d moved to the nearest place that holds, %d dropped: nowhere to stand by them)\n", pts.size(), snappedN, dropped);
    }
    if (folk.empty() || pts.empty()) { printf("  (nobody about: %zu folk, %zu points)\n", folk.size(), pts.size()); continue; }
    for (Creature* c : folk) if (c->T->j.value("golem", false)) {
      auto G = golemGround(*L, *v, *c);
      printf("  (the golems' ground: %zu steps a body %.1f wide reaches from the square; golem on it: %d)\n", G ? G->cells.size() : (size_t)0, c->w * 2, G ? (int)G->near(c->pos) : -1);
      break;
    }
    Mulberry32 rng((u32)v->seed);
    VillagerSystem* S = vsys(*L);
    Tally T;
    for (int round = 0; round < errands; round++) {
      struct Trip { Creature* c; Errand e; double limit = 0, t = 0, lastMove = 0; vec3 last; bool done = false, stuck = false, late = false, over = false; int rescued = 0; };
      std::vector<Trip> trips;
      std::set<size_t> taken;   // (one to a spot at a time: not a crowd round the same doorstep)
      for (Creature* c : folk) {
        if (c->removed) continue;
        const bool golem = c->T->j.value("golem", false);
        if (golemsOnly && !golem) continue;
        size_t pick = (size_t)(rng() * pts.size()) % pts.size();
        // (a golem, too broad for a house door or an alley: somewhere on the open ground a body its width can reach from
        // the square — a street, the square)
        auto broadOk = [&](size_t k) {
          if (pts[k].kind != "street" && pts[k].kind != "square") return false;
          auto G = golemGround(*L, *v, *c);
          return G && G->near(pts[k].to);
        };
        for (int tries = 0; golem && tries < 40 && !broadOk(pick); tries++) pick = (size_t)(rng() * pts.size()) % pts.size();
        if (taken.count(pick) || (golem && !broadOk(pick))) continue;
        Errand e = pts[pick];
        if (golem) e.kind = "golem";
        if (std::hypot(e.to.x - c->pos.x, e.to.z - c->pos.z) < 3) continue;
        taken.insert(pick);
        NpcMind& M = S->mind(c->id);
        M.sent = true; M.sentTo = e.to; M.hasPath = false; M.navT = 0;
        // (time enough at their walk, a street's turns and all: 25 s and 2.2 s a block as the crow flies)
        trips.push_back({c, e, 25 + 2.2 * std::hypot(e.to.x - c->pos.x, e.to.z - c->pos.z), 0, 0, c->pos});
      }
      for (double t = 0; t < 400; t += 0.05) {
        L->tick(0.05);
        // (the player walks the town with them so it stays awake: by the first one still going)
        Trip* lead = nullptr; for (auto& tr : trips) if (!tr.over) { lead = &tr; break; }
        if (!lead) break;
        P->pos = vec3(v->x + 0.5f, v->y + 40, v->z + 0.5f);   // (overhead, out of everyone's way)
        if ((int)(t * 20) % 40 == 0) w.stream({{P->pos, rad, false}}, 40);
        for (auto& tr : trips) {
          if (tr.over) continue;
          Creature* c = tr.c;
          if (c->removed) { tr.over = true; tr.stuck = true; S->mind(c->id).sent = false; continue; }
          const float jump = glm::distance(c->pos, tr.last);
          if (jump > 2.0f) tr.rescued++;
          if (glm::distance(vec3(c->pos.x, 0, c->pos.z), vec3(tr.last.x, 0, tr.last.z)) > 0.02f || std::fabs(c->pos.y - tr.last.y) > 0.05f) tr.lastMove = t;
          tr.last = c->pos;
          if (std::hypot(tr.e.to.x - c->pos.x, tr.e.to.z - c->pos.z) < 1.0f && std::fabs(tr.e.to.y - c->pos.y) < 1.6f) { tr.done = tr.over = true; tr.t = t; }
          else if (t - tr.lastMove > 10) {   // (ten seconds without a step)
            tr.stuck = tr.over = true;
            const NpcMind& M = S->mind(c->id);
            const NpcStep* nx = M.hasPath && M.pathI < M.path.size() ? &M.path[M.pathI] : nullptr;
            printf("      [stuck: path %d%s %zu/%zu next %d %.2f %d (feet %.2f) ground %d climb %d stuckGo %d lostT %.1f vel %.2f %.2f %.2f]\n", M.hasPath, M.pathPartial ? " partial" : "", M.pathI, M.path.size(),
                   nx ? nx->at.x - v->x : 0, nx ? nx->floor - v->y : 0.f, nx ? nx->at.z - v->z : 0, c->pos.y - v->y, c->onGround, M.climbing, M.stuckGo, M.lostT, c->vel.x, c->vel.y, c->vel.z);
            if (c->w > 0.5f) {   // (a golem: where its 2 x 2 stands round it, '#' none)
              const int bx = ifloor(c->pos.x + 0.5f) - 1, bz = ifloor(c->pos.z + 0.5f) - 1, by = ifloor(c->pos.y + 0.45f);
              for (int dz = -3; dz <= 3; dz++) { std::string r; for (int dx = -3; dx <= 3; dx++) r += !std::isnan(npcStandAt(w, bx + dx, by, bz + dz, c->w, c->h)) ? '.' : !std::isnan(npcStandAt(w, bx + dx, by, bz + dz)) ? 'n' : '#'; printf("        %s\n", r.c_str()); }
              std::string k; for (int dz = -1; dz <= 2; dz++) for (int dx = -1; dx <= 2; dx++) k += " " + reg().block(w.getBlock(bx + dx, by, bz + dz)).key;
              printf("        (4 x 4 round it:%s)\n", k.c_str());
            }
            if (!M.hasPath || M.path.size() <= 2) {   // (shut in? what's round them, at their feet and head)
              const int x0 = ifloor(c->pos.x), y0 = ifloor(c->pos.y + 0.45f), z0 = ifloor(c->pos.z);
              std::string r;
              for (int f = 0; f < 4; f++) r += " " + reg().block(w.getBlock(x0 + DXL[f], y0, z0 + DZL[f])).key + "/" + reg().block(w.getBlock(x0 + DXL[f], y0 + 1, z0 + DZL[f])).key;
              printf("        round them (feet/head, 4 sides):%s; under %s; in %s\n", r.c_str(), reg().block(w.getBlock(x0, y0 - 1, z0)).key.c_str(), reg().block(w.getBlock(x0, y0, z0)).key.c_str());
            }
          }
          else if (t > tr.limit) tr.late = tr.over = true;
          if (tr.over) S->mind(c->id).sent = false;
        }
      }
      for (auto& tr : trips) {
        S->mind(tr.c->id).sent = false;
        Tally& K = byKind[tr.e.kind];
        for (Tally* X : {&T, &K}) {
          X->sent++;
          if (tr.done && !tr.rescued) { X->arrived++; X->secs += tr.t; }
          if (tr.stuck) X->stuck++;
          if (tr.rescued) X->rescued++;
        }
        if (!tr.done || tr.rescued) {
          const vec3 p = tr.c->pos;
          bool part = false; const auto way = npcPath(w, p, tr.e.to, tr.c->w, tr.c->h, &part);
          if (!way.empty()) printf("      (a way from there now: %zu steps%s, ending %.0f %.0f %.0f)\n", way.size(), part ? " partial" : "", way.back().at.x - (float)v->x, way.back().floor - v->y, way.back().at.z - (float)v->z);
          else printf("      (no way from there now)\n");
          if (std::hypot(tr.e.to.x - p.x, tr.e.to.z - p.z) < 4) {   // (close and not there: what's about the spot)
            const int tx = ifloor(tr.e.to.x), ty = ifloor(tr.e.to.y + 0.45f), tz = ifloor(tr.e.to.z);
            std::string r;
            for (int dy = -1; dy <= 1; dy++) { r += " y" + std::to_string(dy) + ":"; for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) r += " " + reg().block(w.getBlock(tx + dx, ty + dy, tz + dz)).key; }
            printf("      (about the spot:%s; stands there %.2f)\n", r.c_str(), npcStandAt(w, tx, ty, tz) - v->y);
          }
          printf("    miss: %s %s to %s (%.0f %.0f %.0f) from %.1f %.1f %.1f%s%s\n", tr.c->type.c_str(), npcOf(*tr.c)->value("j", std::string("")).c_str(), tr.e.kind.c_str(),
                 tr.e.to.x - v->x, tr.e.to.y - v->y, tr.e.to.z - v->z, p.x - v->x, p.y - v->y, p.z - v->z, tr.stuck ? " stuck" : tr.late ? " late" : "", tr.rescued ? " moved" : "");
        }
      }
      fflush(stdout);
    }
    // the miller and the windmill's lift: with wheat in the stores, of a morning, up to the stones on it and back down
    for (const VBld& b : v->blds) {
      if (b.type != "windmill") continue;
      ivec3 sl; if (!npcLiftIn(w, std::min(b.x0, b.x1), b.y - 1, std::min(b.z0, b.z1), std::max(b.x0, b.x1), b.y + 1, std::max(b.z0, b.z1), sl)) { printf("  (the windmill at %d %d has no lift)\n", b.x0 - v->x, b.z0 - v->z); continue; }
      Creature* miller = nullptr;
      for (Creature* c : folk) if (!c->removed && npcOf(*c)->value("j", std::string()) == "miller") miller = c;
      if (!miller) { printf("  (no miller)\n"); continue; }
      villageStock(*L, *v)["wheat"] = 40;
      L->info.time = std::floor(L->info.time / DAY_LEN) * DAY_LEN + 0.1 * DAY_LEN;   // (the morning's work)
      S->mind(miller->id).rideS = ivec3(INT32_MIN); S->mind(miller->id).rideStage = 0;
      bool rode = false, up = false; int trips = 0; double midT = 0;
      const bool trace = std::getenv("DQ_NAV_TRACE") != nullptr;
      for (double t = 0; t < 240 && trips < 2; t += 0.05) {
        L->tick(0.05);
        if ((int)(t * 20) % 40 == 0) w.stream({{P->pos, rad, false}}, 40);
        NpcMind& M = S->mind(miller->id);
        if (M.rideStage == 1 && M.rideS == sl && std::fabs(miller->pos.y - sl.y - 1.5f) > 0.6f) rode = true;   // (on it, off the ground floor)
        if (rode && miller->pos.y > b.y + 7.5f) up = true;
        if (trace && (int)(t * 20) % 100 == 0) printf("    (miller t%.0f: %.1f %.1f %.1f from the lift, ride %d to %d, lift at %d, carry %s, chore %.1f, path %zu/%zu%s)\n", t, miller->pos.x - sl.x, miller->pos.y - sl.y, miller->pos.z - sl.z, M.rideStage, M.rideTo,
                                            machineState(w, sl).value("at", 0), M.carry.c_str(), M.choreT, M.pathI, M.path.size(), M.pathPartial ? " partial" : "");
        if (trace && (int)(t * 20) % 400 == 0) {   // (the column they're in, block by block, and what's solid in it)
          const int cx = ifloor(miller->pos.x), cz = ifloor(miller->pos.z);
          std::string col;
          for (int y = sl.y; y <= sl.y + 10; y++) {
            std::vector<AABB> bx; blockBoxes(w, cx, y, cz, bx);
            char buf[160]; snprintf(buf, sizeof buf, " %d:%s", y - sl.y, reg().block(w.getBlock(cx, y, cz)).key.c_str()); col += buf;
            for (auto& q : bx) { snprintf(buf, sizeof buf, "[%.2f-%.2f x%.2f-%.2f z%.2f-%.2f]", q.lo.y - y, q.hi.y - y, q.lo.x - cx, q.hi.x - cx, q.lo.z - cz, q.hi.z - cz); col += buf; }
          }
          printf("      (column:%s)\n", col.c_str());
        }
        if (trace && (int)(t * 20) % 100 == 0 && M.pathI < M.path.size()) printf("      (next %d %.2f %d climb %d; climbing %d ground %d nograv %d vel %.2f %.2f %.2f stuckGo %d)\n", M.path[M.pathI].at.x - sl.x, M.path[M.pathI].floor - sl.y, M.path[M.pathI].at.z - sl.z, M.path[M.pathI].climb, M.climbing, miller->onGround, miller->noGravity, miller->vel.x, miller->vel.y, miller->vel.z, M.stuckGo);
        if (up && miller->pos.y < b.y + 2.5f) { trips++; up = false; }
        if (miller->pos.y > b.y + 4.5f && miller->pos.y < b.y + 7.f) midT += 0.05; else midT = 0;   // (on the floor between)
        if (midT > 20) break;
      }
      printf("  %s miller and the windmill lift: rode %d, up to the stones and back down %d times%s\n", (rode && trips >= 2) ? "ok  " : "FAIL", rode, trips, midT > 20 ? " (left on the floor between)" : "");
      if (!(rode && trips >= 2)) millerFails++;
      break;
    }
    printf("  %s: %d errands, %d there on foot (%.0f%%), mean %.1f s; %d stuck, %d moved by the rescue\n", v->name.c_str(), T.sent, T.arrived, T.sent ? 100.0 * T.arrived / T.sent : 0.0,
           T.arrived ? T.secs / T.arrived : 0.0, T.stuck, T.rescued);
    all.sent += T.sent; all.arrived += T.arrived; all.stuck += T.stuck; all.rescued += T.rescued; all.secs += T.secs;
  }
  printf("\nby kind of place:\n");
  for (auto& [k, T] : byKind) printf("  %-7s %3d errands, %3d there (%.0f%%), mean %.1f s, %d stuck, %d moved\n", k.c_str(), T.sent, T.arrived, T.sent ? 100.0 * T.arrived / T.sent : 0.0, T.arrived ? T.secs / T.arrived : 0.0, T.stuck, T.rescued);
  const double rate = all.sent ? (double)all.arrived / all.sent : 0;
  { const NpcNavStats& st = npcNavStats(); printf("\nway finding: %ld plans (%ld partial, %ld no way), %ld ladders climbed, %ld rescues\n", st.plans, st.partial, st.failed, st.climbs, st.rescues); }
  printf("\nall: %d errands, %d there on foot (%.1f%%), mean %.1f s; %d stuck, %d moved by the rescue\n", all.sent, all.arrived, 100 * rate, all.arrived ? all.secs / all.arrived : 0.0, all.stuck, all.rescued);
  lvp->close();
  return (townsWanted == 0 || (all.sent > 0 && rate >= 0.8)) && !millerFails ? 0 : 1;
}
