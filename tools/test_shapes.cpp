// tools/test_shapes.cpp — the hitbox audit (headless): for every block, every meta value it can hold and a few
// neighbourhoods (alone on the ground, in a run, at a crossing, hung under a ceiling; chiseled blocks: every carve
// pattern), the union bounds of what is drawn, what you bump into and what the crosshair picks (world/shapes.cpp
// shapeBoxes Draw / Collide / Select, as sim/physics.cpp and the mesher use them). A difference of more than a
// sixteenth is a mismatch: floating on top, sinking in, walking through what you see, an invisible wall, an outline
// that isn't the model. Deliberate ones are allowlisted below, each with its reason.
//   build/test_shapes            (exit code = mismatches not allowlisted)
//   build/test_shapes --all      (also print the allowlisted ones)
#include "core/init.h"
#include "core/registry.h"
#include "world/shapes.h"
#include "world/carve_faces.h"
#include "sim/fallen.h"
#include "sim/blockinfo.h"
#include "common/fs.h"
#include <map>
#include <set>
#include <cstdio>
#include <cmath>
#include <functional>

using namespace dq;

namespace {
struct U { float b[6] = {1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f}; bool any = false;
  void add(const float* q) { for (int i = 0; i < 3; i++) { b[i] = std::min(b[i], q[i]); b[i + 3] = std::max(b[i + 3], q[i + 3]); } any = true; }
  void full() { const float f[6] = {0, 0, 0, 16, 16, 16}; add(f); } };

// a block's neighbourhood: 3x3x3 ids round it (with its own meta for its own kind)
struct Nb : ShapeNb {
  BlockId c[3][3][3] = {}; u8 m[3][3][3] = {}; u8 cv[3][3][3] = {};
  BlockId id(int dx, int dy, int dz) const override { return std::abs(dx) > 1 || std::abs(dy) > 1 || std::abs(dz) > 1 ? 0 : c[dx + 1][dy + 1][dz + 1]; }
  u8 meta(int dx, int dy, int dz) const override { return std::abs(dx) > 1 || std::abs(dy) > 1 || std::abs(dz) > 1 ? 0 : m[dx + 1][dy + 1][dz + 1]; }
  u8 carve(int dx, int dy, int dz) const override { return std::abs(dx) > 1 || std::abs(dy) > 1 || std::abs(dz) > 1 ? 0 : cv[dx + 1][dy + 1][dz + 1]; }
  ivec3 pos{0, 600, 0};   // (somewhere in the Overworld: the remains lie by where they are)
  bool at(ivec3& p) const override { p = pos; return true; }
};
const char* NB_NAMES[] = {"alone on the ground", "in a north-south run", "at a crossing", "under a ceiling"};
Nb makeNb(int k, BlockId self, u8 meta, BlockId stone) {
  Nb n; n.c[1][1][1] = self; n.m[1][1][1] = meta; n.pos = ivec3(meta * 7, 600, k * 13);
  if (k != 3) n.c[1][0][1] = stone; else n.c[1][2][1] = stone;
  if (k == 1 || k == 2) { n.c[1][1][0] = n.c[1][1][2] = self; n.m[1][1][0] = n.m[1][1][2] = meta; }
  if (k == 2) { n.c[0][1][1] = n.c[2][1][1] = self; n.m[0][1][1] = n.m[2][1][1] = meta; }
  return n;
}

// the stations drawn from their hands-on models (assets/local_models.cpp: the same list it draws; local files only)
struct Station { std::vector<std::array<float, 6>> boxes[4]; int turn[4] = {0, 0, 0, 0}; int facingKind = 0; bool faceBack = false; };
std::map<BlockId, Station> g_stations;
bool handsPorted(const std::string& key) {
  if (key.size() > 13 && key.compare(key.size() - 13, 13, "painted_chest") == 0) return true;
  if (key == "ender_chest" || key == "party_chest" || key == "jukebox" || key == "enchanting_table") return true;
  static const std::set<std::string> DONE = {"smithing_table", "cartography_table", "furnace", "lit_furnace", "smoker", "lit_smoker", "blast_furnace", "lit_blast_furnace", "anvil", "chipped_anvil", "damaged_anvil", "grindstone", "chest", "brewing_stand"};
  return DONE.count(key) > 0;
}
void loadStations() {
  const fs::path dir = dataDir() / "local_models" / "hands";
  auto man = readJson(dir / "stations.json");
  if (!man || !man->is_object()) return;
  for (auto& [key, v] : man->items()) {
    const BlockId id = reg().blockId(key);
    if (!id || !handsPorted(key) || !v.contains("model")) continue;
    Station st;
    for (int k = 0; k < 4; k++) {
      std::string name = v["model"].is_array() ? (k < (int)v["model"].size() ? v["model"][k].get<std::string>() : std::string()) : v["model"].get<std::string>();
      if (v.value("state", std::string()) == "chest") name = "chest_full";
      auto mj = readJson(dir / "models" / (name + ".json"));
      if (!mj || !mj->contains("elements")) continue;
      for (auto& e : (*mj)["elements"]) {
        std::array<float, 6> b;
        for (int i = 0; i < 3; i++) { float a = e["from"][i].get<float>(), c = e["to"][i].get<float>(); b[i] = std::min(a, c); b[i + 3] = std::max(a, c); }
        st.boxes[k].push_back(b);
      }
    }
    if (st.boxes[0].empty()) continue;
    const BlockDef& bd = reg().block(id);
    st.facingKind = bd.props.contains("tex") && bd.props["tex"].is_object() && bd.props["tex"].contains("front") ? 1 : bd.has("facingModel") ? 2 : 0;
    st.faceBack = key == "brewing_stand" || key == "enchanting_table";
    if (v.contains("y") && v["y"].is_array()) for (int k = 0; k < 4 && k < (int)v["y"].size(); k++) st.turn[k] = ((v["y"][k].get<int>() / 90) % 4 + 4) % 4;
    g_stations[id] = st;
  }
}
bool stationDraw(BlockId id, u8 meta, U& d) {
  auto it = g_stations.find(id);
  if (it == g_stations.end()) return false;
  const Station& st = it->second;
  static const int FM[4] = {2, 1, 0, 3};
  int k = st.facingKind == 1 ? meta & 3 : st.facingKind == 2 ? FM[meta & 3] : 2;
  if (st.faceBack) k = (k + 2) & 3;
  const auto& bs = st.boxes[k].empty() ? st.boxes[0] : st.boxes[k];
  for (auto b : bs) {
    for (int q = 0; q < st.turn[k]; q++) {   // a quarter turn clockwise seen from above, about the block's middle
      float x0 = 16 - b[5], x1 = 16 - b[2], z0 = b[0], z1 = b[3];
      b[0] = x0; b[3] = x1; b[2] = z0; b[5] = z1;
    }
    float f[6] = {std::max(0.f, b[0]), std::max(0.f, b[1]), std::max(0.f, b[2]), std::min(16.f, b[3]), std::min(16.f, b[4]), std::min(16.f, b[5])};
    d.add(f);
  }
  return true;
}

enum Src { DRAWN, NOT_DRAWN_HERE };   // NOT_DRAWN_HERE: drawn by code we can't measure (planes, a render layer)
Src drawUnion(BlockId id, u8 meta, const Nb& nb, U& d, U* solidOnly = nullptr) {
  const Registry& R = reg();
  const Render r = (Render)R.tRender[id];
  const BlockTraits& T = traits(id);
  if (r == Render::None || r == Render::Liquid || r == Render::Cross || r == Render::Torch) return NOT_DRAWN_HERE;
  if (stationDraw(id, meta, d)) return DRAWN;
  if (T.cloth) {   // (render/mesh_shapes.cpp emitModel: turned with the way it faces, lifted by its sag)
    std::vector<ShapeBox> cb; clothBoxes(id, meta, cb);
    for (ShapeBox b : cb) { b.b[1] += b.lift; b.b[4] += b.lift; rotBox(b, T.facingModel ? meta & 3 : 0); d.add(b.b); }
    return DRAWN;
  }
  std::vector<ShapeBox> bx;
  if (!shapeBoxes(id, meta, nb, ShapePurpose::Draw, bx)) {
    if (r == Render::Cube || r == Render::Cutout) { d.full(); return DRAWN; }
    return NOT_DRAWN_HERE;
  }
  for (auto& b : bx) if (b.only && !b.spin) { d.add(b.b); if (!b.nocol && solidOnly) solidOnly->add(b.b); }
  if (R.tShape[id] == ShapeKind::Lever) {   // the handle: an upright stick drawn on its own (render/mesh_shapes.cpp emitLeverHandle)
    const float a = 0.7f * ((meta & 8) ? 1 : -1);
    const vec3 dir(0, std::cos(a), std::sin(a)), ctr = attachPoint(vec3(8, 1.5f, 8) + dir * 5.f, meta, false);
    const vec3 ay = attachPoint(dir, meta, true), ax = attachPoint(vec3(1, 0, 0), meta, true), az = glm::cross(ax, ay);
    for (int c = 0; c < 8; c++) {
      const vec3 p = ctr + ax * ((c & 1) ? 1.f : -1.f) + ay * ((c & 2) ? 5.f : -5.f) + az * ((c & 4) ? 1.f : -1.f);
      const float f[6] = {p.x, p.y, p.z, p.x, p.y, p.z};
      d.add(f);
    }
  }
  return d.any ? DRAWN : NOT_DRAWN_HERE;
}
void collideUnion(BlockId id, u8 meta, const Nb& nb, U& c) {
  const Registry& R = reg();
  if (!R.tSolid[id] || bhas(id, BF::IsLeaves)) return;
  if (!hasShapeBoxes(id)) { c.full(); return; }
  std::vector<ShapeBox> bx;
  if (!shapeBoxes(id, meta, nb, ShapePurpose::Collide, bx)) { c.full(); return; }
  for (auto& b : bx) c.add(b.b);
}
void selectUnion(BlockId id, u8 meta, const Nb& nb, U& s) {
  const Registry& R = reg();
  const Render r = (Render)R.tRender[id];
  float sb[6];
  if (((r == Render::Cube || r == Render::Cutout) && !stationBounds(id, meta, sb)) || r == Render::Liquid) { s.full(); return; }   // (sim/physics.cpp raycastBlocks)
  std::vector<ShapeBox> bx;
  if (!shapeBoxes(id, meta, nb, ShapePurpose::Select, bx)) { s.full(); return; }
  for (auto& b : bx) s.add(b.b);
}

// what's wrong between drawn d and other o (pixels); "" = within a sixteenth
std::string diff(const U& d, const U& o, bool collide) {
  const float TOL = 1.01f;
  std::string w;
  auto add = [&](const char* s) { if (w.find(s) == std::string::npos) { if (!w.empty()) w += ", "; w += s; } };
  if (!o.any) return d.any ? (collide ? "" : "nothing to pick") : "";
  if (o.b[4] - d.b[4] > TOL) add(collide ? "floats over the top" : "outline over the top");
  if (d.b[4] - o.b[4] > TOL) add(collide ? "sinks into the top" : "outline below the top");
  if (o.b[1] < d.b[1] - TOL) add(collide ? "solid under the drawn bottom" : "outline under the bottom");
  if (o.b[1] > d.b[1] + TOL) add(collide ? "walk under the drawn bottom" : "outline above the bottom");
  for (int a : {0, 2}) {
    if (o.b[a] < d.b[a] - TOL || o.b[a + 3] > d.b[a + 3] + TOL) add(collide ? "invisible wall beside it" : "outline wider than the model");
    if (o.b[a] > d.b[a] + TOL || o.b[a + 3] < d.b[a + 3] - TOL) add(collide ? "walk into the drawn sides" : "outline narrower than the model");
  }
  return w;
}
std::string fmt(const U& u) { if (!u.any) return "none"; char s[96]; snprintf(s, sizeof s, "%g,%g,%g..%g,%g,%g", u.b[0], u.b[1], u.b[2], u.b[3], u.b[4], u.b[5]); return s; }

// the deliberate differences, each with its reason (what = "collide" / "select"; true = allowlisted)
std::string allowed(BlockId id, u8 meta, const std::string& what, const std::string& problem) {
  const Registry& R = reg();
  const BlockDef& b = R.block(id);
  const int k = R.tShape[id];
  const BlockTraits& T = traits(id);
  const bool col = what == "collide";
  if (k == ShapeKind::Scaffold && col) return "scaffolding: you climb inside it and stand on its top deck";
  if (k == ShapeKind::Ladder && col) return "ladders: three pixels deep to cling to (one drawn), so you don't slip off the wall";
  if (k == ShapeKind::Gate && col) return "shut gates reach the ground: nothing crawls under the bottom bar";
  if (k == ShapeKind::Fence && col) return "fence rails: a plane down to the ground between posts, so nothing slips under them";
  if (k == ShapeKind::Cactus) return "cactus: a pixel in from its spines all round (you're pricked against it, as it was)";
  if ((k == ShapeKind::Candle || T.pickle) && col) return "candles and pickles: one small box to bump, not each thin stick";
  if (k == ShapeKind::Hook) return "tripwire hooks: one box round the hook and its plate";
  if ((k == ShapeKind::Repeater || k == ShapeKind::Comparator) && col) return "repeaters and comparators: stood on at the slab, over the little torches";
  if (k == ShapeKind::Bed && col && (meta & 4)) return "beds: you stand on the mattress; the soft pillow (2 px) gives under you";
  if (k == ShapeKind::PistonHead) return "piston heads: the rod reaches back into its base when drawn (no gap); its own cell's box is what you hit";
  if (b.key == "spike_trap") return "spike traps: physics gives extended spikes a 3 px plate on their face (sim/physics.cpp spikeDBox): you step onto the points";
  if (b.key == "end_gateway") return "end gateways: you walk into them to travel";
  if (b.has("remains") && col) return "fallen adventurers: bones lying past their own cell are picked there, but bumped only inside it (the next cell may be a wall)";
  (void)problem;
  return "";
}
} // namespace

int main(int argc, char** argv) {
  bool all = false;
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--all") all = true;
  CoreOptions co; co.server = true; co.userDirOverride = (fs::temp_directory_path() / "dq-test-shapes").string();
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  initBlockTraits();
  loadStations();
  const Registry& R = reg();
  const BlockId stone = R.blockId("stone");
  int blocks = 0, mismatched = 0, fixedNone = 0, allowN = 0, real = 0;
  struct Hit { std::string block, what, problem, detail, why; };
  std::vector<Hit> hits;
  for (const BlockDef& def : R.blocks) {
    const BlockId id = def.id;
    if (!id) continue;
    const Render r = (Render)R.tRender[id];
    if ((r == Render::Cube || r == Render::Cutout) && !hasShapeBoxes(id) && !g_stations.count(id)) continue;   // plain cubes are cubes
    blocks++;
    std::map<std::string, Hit> seen;   // one line per block and problem
    // every meta it can hold (the waterlogged bit aside), every neighbourhood
    for (int meta = 0; meta < 128; meta++) for (int k = 0; k < 4; k++) {
      // (levers and buttons are fixed 0 floor, 1 ceiling, 2-5 a wall: 6 and 7 are never set)
      if ((R.tShape[id] == ShapeKind::Lever || R.tShape[id] == ShapeKind::Button) && (meta & 7) > 5) continue;
      Nb nb = makeNb(k, id, (u8)meta, stone);
      U d, c, s;
      U dSolid;   // the drawn parts that aren't marked "nocol" (a sensor's tendrils, a brazier's flame, a blade)
      const Src src = drawUnion(id, (u8)meta, nb, d, &dSolid);
      collideUnion(id, (u8)meta, nb, c);
      selectUnion(id, (u8)meta, nb, s);
      auto report = [&](const char* what, const U& o) {
        std::string p = diff(d, o, std::string(what) == "collide");
        if (p.empty()) return;
        std::string key = std::string(what) + ": " + p;
        if (seen.count(key)) return;
        char det[256]; snprintf(det, sizeof det, "meta %d, %s: drawn %s, %s %s", meta, NB_NAMES[k], fmt(d).c_str(), what, fmt(o).c_str());
        std::string why = allowed(id, (u8)meta, what, p);
        if (why.empty() && std::string(what) == "collide" && dSolid.any && diff(dSolid, o, true).empty())
          why = "thin drawn decorations marked nocol in data/shapes.json (tendrils, flames, a blade, a wick): nothing to bump";
        seen[key] = {def.key, what, p, det, why};
      };
      if (src == DRAWN) {
        if (R.tSolid[id] && !bhas(id, BF::IsLeaves)) report("collide", c);
        report("select", s);
        // something big you walk straight through: not solid, but drawn waist high over most of the cell
        if (!R.tSolid[id] && d.any && d.b[4] - d.b[1] > 8 && (d.b[3] - d.b[0]) * (d.b[5] - d.b[2]) > 100) {
          std::string key = "collide: not solid";
          if (!seen.count(key)) {
            char det[256]; snprintf(det, sizeof det, "meta %d: drawn %s, not solid", meta, fmt(d).c_str());
            seen[key] = {def.key, "collide", "not solid (walk through it)", det, allowed(id, (u8)meta, "collide", "not solid")};
          }
        }
      }
    }
    if (!seen.empty()) mismatched++;
    for (auto& [key, h] : seen) hits.push_back(h);
  }
  // not-solid things you walk through on purpose
  auto passThrough = [&](const std::string& key) -> std::string {
    const BlockId id = R.blockId(key);
    const BlockDef& b = R.block(id);
    if (b.has("door") || R.tShape[id] == ShapeKind::Door) return "";
    if (b.has("hangs") || b.has("flower") || b.has("crop") || bhas(id, BF::NeedsSupport)) return "plants and hanging growth: you push through them";
    if (traits(id).cloth) return "flags, banners and bunting: cloth you walk through";
    if (b.has("cobweb") || key.find("web") != std::string::npos) return "cobwebs: you're caught in them, not stopped";
    return "";
  };
  for (auto& h : hits) {
    if (h.why.empty() && h.problem.rfind("not solid", 0) == 0) h.why = passThrough(h.block);
    (h.why.empty() ? real : allowN)++;
  }
  for (auto& h : hits) {
    if (!h.why.empty() && !all) continue;
    printf("%s %-28s %-7s %s  [%s]%s%s\n", h.why.empty() ? "FAIL" : "ok  ", h.block.c_str(), h.what.c_str(), h.problem.c_str(), h.detail.c_str(),
           h.why.empty() ? "" : "  -- allowed: ", h.why.c_str());
  }
  (void)fixedNone;
  printf("\n%d shaped blocks audited, %d with differences: %zu differences, %d allowlisted, %d mismatches\n", blocks, mismatched, hits.size(), allowN, real);
  return real;
}
