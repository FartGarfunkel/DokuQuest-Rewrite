// tools/test_npc_paths.cpp — headless audit of how townsfolk find their way. In a few generated towns (a stone-walled
// one, a palisaded one, an open village) every villager and guard is sent, one errand after another, to points about the
// town: house doors, workplaces, the square, beds, street cells. Each errand: did they get there, how long it took, did
// they stick (no progress for 10 s), were they moved there by the rescue (a jump of over 2 blocks in one tick).
//   test_npc_paths [--userdir DIR] [--towns N] [--errands N]    exit code 0 = the arrival rate is at least 90%
#include "core/init.h"
#include "sim/villagers.h"
#include "sim/villagers_internal.h"
#include "gen/structures.h"
#include "gen/structures_api.h"
#include "world/dims.h"
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
  int townsWanted = 3, errands = 6;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--userdir" && i + 1 < argc) ud = argv[++i];
    else if (a == "--towns" && i + 1 < argc) townsWanted = std::atoi(argv[++i]);
    else if (a == "--errands" && i + 1 < argc) errands = std::atoi(argv[++i]);
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
  Tally all; std::map<std::string, Tally> byKind; int millerFails = 0;
  long nodesBefore = 0; (void)nodesBefore;
  for (const StructurePlan* town : towns) {
    VillageRef v = villageById(*L, town->data.value("id", std::string()));
    if (!v) { printf("  (%s: no village record)\n", town->name.c_str()); continue; }
    printf("[%s: %s, %s wall, %zu buildings, %zu folk]\n", v->name.c_str(), v->tier.c_str(), kindOf(town).c_str(), v->blds.size(), v->folk.size());
    fflush(stdout);
    // the whole town loaded, and its people about
    P->pos = vec3(v->x + 0.5f, v->y + 2, v->z + 0.5f);
    const int rad = std::min(6, (v->r + 12) / 16 + 1);
    for (double t0 = nowSeconds(); nowSeconds() - t0 < 240;) {
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
    if (folk.empty() || pts.empty()) { printf("  (nobody about: %zu folk, %zu points)\n", folk.size(), pts.size()); continue; }
    Mulberry32 rng((u32)v->seed);
    VillagerSystem* S = vsys(*L);
    Tally T;
    for (int round = 0; round < errands; round++) {
      struct Trip { Creature* c; Errand e; double t = 0, lastMove = 0; vec3 last; bool done = false, stuck = false; int rescued = 0; };
      std::vector<Trip> trips;
      for (Creature* c : folk) {
        if (c->removed) continue;
        const Errand& e = pts[(size_t)(rng() * pts.size()) % pts.size()];
        if (std::hypot(e.to.x - c->pos.x, e.to.z - c->pos.z) < 3) continue;
        NpcMind& M = S->mind(c->id);
        M.sent = true; M.sentTo = e.to; M.hasPath = false; M.navT = 0;
        trips.push_back({c, e, 0, 0, c->pos});
      }
      for (double t = 0; t < 75; t += 0.05) {
        L->tick(0.05);
        // (the player walks the town with them so it stays awake: by the first one still going)
        Trip* lead = nullptr; for (auto& tr : trips) if (!tr.done) { lead = &tr; break; }
        if (!lead) break;
        P->pos = vec3(v->x + 0.5f, v->y + 40, v->z + 0.5f);   // (overhead, out of everyone's way)
        if ((int)(t * 20) % 40 == 0) w.stream({{P->pos, rad, false}}, 40);
        for (auto& tr : trips) {
          if (tr.done) continue;
          Creature* c = tr.c;
          if (c->removed) { tr.done = true; tr.stuck = true; continue; }
          const float jump = glm::distance(c->pos, tr.last);
          if (jump > 2.0f) tr.rescued++;
          if (glm::distance(vec3(c->pos.x, 0, c->pos.z), vec3(tr.last.x, 0, tr.last.z)) > 0.02f || std::fabs(c->pos.y - tr.last.y) > 0.05f) tr.lastMove = t;
          tr.last = c->pos;
          if (std::hypot(tr.e.to.x - c->pos.x, tr.e.to.z - c->pos.z) < 1.0f && std::fabs(tr.e.to.y - c->pos.y) < 1.6f) { tr.done = true; tr.t = t; }
          else if (t - tr.lastMove > 10) tr.stuck = true;
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
          printf("    miss: %s %s to %s (%.0f %.0f %.0f) from %.1f %.1f %.1f%s%s\n", tr.c->type.c_str(), npcOf(*tr.c)->value("j", std::string("")).c_str(), tr.e.kind.c_str(),
                 tr.e.to.x - v->x, tr.e.to.y - v->y, tr.e.to.z - v->z, p.x - v->x, p.y - v->y, p.z - v->z, tr.stuck ? " stuck" : "", tr.rescued ? " moved" : "");
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
      L->info.time = 3600;
      bool rode = false, up = false, down = false;
      for (double t = 0; t < 240 && !down; t += 0.05) {
        L->tick(0.05);
        if ((int)(t * 20) % 40 == 0) w.stream({{P->pos, rad, false}}, 40);
        NpcMind& M = S->mind(miller->id);
        if (M.rideStage == 1 && M.rideS == sl) rode = true;
        if (rode && miller->pos.y > b.y + 7.5f) up = true;
        if (up && miller->pos.y < b.y + 2.5f) down = true;
      }
      printf("  %s miller and the windmill lift: rode %d, up at the stones %d, back down %d\n", (rode && up && down) ? "ok  " : "FAIL", rode, up, down);
      if (!(rode && up && down)) millerFails++;
      break;
    }
    printf("  %s: %d errands, %d there on foot (%.0f%%), mean %.1f s; %d stuck, %d moved by the rescue\n", v->name.c_str(), T.sent, T.arrived, T.sent ? 100.0 * T.arrived / T.sent : 0.0,
           T.arrived ? T.secs / T.arrived : 0.0, T.stuck, T.rescued);
    all.sent += T.sent; all.arrived += T.arrived; all.stuck += T.stuck; all.rescued += T.rescued; all.secs += T.secs;
  }
  printf("\nby kind of place:\n");
  for (auto& [k, T] : byKind) printf("  %-7s %3d errands, %3d there (%.0f%%), mean %.1f s, %d stuck, %d moved\n", k.c_str(), T.sent, T.arrived, T.sent ? 100.0 * T.arrived / T.sent : 0.0, T.arrived ? T.secs / T.arrived : 0.0, T.stuck, T.rescued);
  const double rate = all.sent ? (double)all.arrived / all.sent : 0;
  printf("\nall: %d errands, %d there on foot (%.1f%%), mean %.1f s; %d stuck, %d moved by the rescue\n", all.sent, all.arrived, 100 * rate, all.arrived ? all.secs / all.arrived : 0.0, all.stuck, all.rescued);
  lvp->close();
  return all.sent > 0 && rate >= 0.9 && !millerFails ? 0 : 1;
}
