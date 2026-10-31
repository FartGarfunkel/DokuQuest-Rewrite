// world/shapes.cpp — port of world/shapes.js (+ the chorus override of game/theend.js, the SHAPES
// model table, and clothBoxes of game/townlife.js).
//
// Meta bits (the per-block byte), as in the original:
//   bit 7 (0x80)  waterlogged (any block marked wlog)
//   slab          0 bottom, 1 top, 2..5 vertical against N/E/S/W, 6 double
//   stairs        0-1 facing (N,E,S,W: the high side), 2 upside-down
//   door          0-1 facing, 2 open, 3 hinge on the right, 4 upper half
//   trapdoor      0-1 facing (hinge side), 2 open, 3 top half
//   gate          0-1 facing, 2 open
//   ladder        0-1 side of the wall it hangs on
//   bed           0-1 facing (toward the head), 2 head half
//   log / axle    0-1 axis (0 y, 1 x, 2 z)
//   candle        0-1 how many (1-4), 2 lit       sea pickle: 0-1 how many
//   cake          0-2 slices eaten (0-6)          composter: 0-3 fill level (0-8)
//   end/lightning rod 0-1 axis                    item frame: 0-1 wall side
//   cocoa         0-1 growth, 4-5 side of the log it hangs on
//   redstone dust 0-3 power                       pressure plate: 0-3 power
//   lever, button 0-2 where it's fixed (0 floor, 1 ceiling, 2-5 wall N/E/S/W), 3 on, 4 turned
//   repeater      0-1 facing (output), 2-3 delay, 4 powered, 5 locked
//   comparator    0-1 facing, 2 subtract, 3 lit, 4-7 output strength
//   piston, observer, dispenser, dropper: 0-2 facing in 3D (0 down 1 up 2 N 3 S 4 W 5 E), 3 powered
//   piston head   0-2 facing, 3 sticky            hopper: 0-2 spout (0 down, 2-5 sideways), 3 locked
//   rail          0-3 shape (0-9)                 powered/detector/activator rail: 0-2 shape (0-5), 3 powered
//   daylight      0-3 power, 4 inverted           tripwire hook: 0-1 wall side, 2 strung, 3 powered
//   tripwire      0 powered, 1 strung
#include "world/shapes.h"
#include <mutex>

namespace dq {

const int D6X[6] = {0, 0, 0, 0, -1, 1}, D6Y[6] = {-1, 1, 0, 0, 0, 0}, D6Z[6] = {0, 0, -1, 1, 0, 0};
const int OPP6[6] = {1, 0, 3, 2, 5, 4};
const int H2D6[4] = {2, 5, 3, 4};
const int D62H[6] = {-1, -1, 0, 2, 3, 1};
const int D6_FACE[6] = {3, 2, 5, 4, 1, 0};
const int FACE_D6[6] = {5, 4, 1, 0, 3, 2};
const int RAIL_EXITS[10][2][3] = {
  {{0, 0, -1}, {0, 0, 1}}, {{-1, 0, 0}, {1, 0, 0}},
  {{-1, 0, 0}, {1, 1, 0}}, {{-1, 1, 0}, {1, 0, 0}}, {{0, 1, -1}, {0, 0, 1}}, {{0, 0, -1}, {0, 1, 1}},
  {{0, 0, 1}, {1, 0, 0}}, {{0, 0, 1}, {-1, 0, 0}}, {{0, 0, -1}, {-1, 0, 0}}, {{0, 0, -1}, {1, 0, 0}},
};

// ---------------------------------------------------------------- traits + model table
namespace {
std::vector<BlockTraits> g_traits;
std::vector<std::vector<ShapeBox>> g_models;
std::unordered_map<std::string, int> g_modelIdx;
std::mutex g_initMx;
size_t g_tracedBlocks = 0;
BlockTraits g_none;

int tileOf(const std::string& name) { return name.empty() ? -1 : reg().tile(name); }

void loadModels() {
  if (!g_models.empty()) return;
  auto j = readJson(dataDir() / "shapes.json");
  if (!j || !j->contains("shapes")) { DQ_WARN("data/shapes.json missing: block models draw as cubes"); return; }
  for (auto& [name, arr] : (*j)["shapes"].items()) {
    std::vector<ShapeBox> boxes;
    for (auto& x : arr) {
      ShapeBox b;
      for (int i = 0; i < 6; i++) b.b[i] = x["b"][i].get<float>();
      b.side = tileOf(x.value("side", std::string()));
      b.end = x.contains("end") ? tileOf(x["end"].get<std::string>()) : -1;
      if (b.end < 0) b.end = b.side;
      b.xt = x.contains("x") ? tileOf(x["x"].get<std::string>()) : -1;
      if (b.side < 0) b.side = 0;
      b.tv = x.value("tv", 0.f);
      b.nocol = x.value("nocol", false);
      b.lift = x.value("lift", 0.f);
      boxes.push_back(b);
    }
    g_modelIdx[name] = (int)g_models.size();
    g_models.push_back(std::move(boxes));
  }
}

RsRole rsRole(const std::string& s) {
  static const std::unordered_map<std::string, RsRole> m = {
    {"wire", RsRole::Wire}, {"torch", RsRole::Torch}, {"torch_off", RsRole::TorchOff}, {"block", RsRole::Block}, {"lever", RsRole::Lever},
    {"button", RsRole::Button}, {"plate", RsRole::Plate}, {"repeater", RsRole::Repeater}, {"comparator", RsRole::Comparator},
    {"observer", RsRole::Observer}, {"lamp", RsRole::Lamp}, {"piston", RsRole::Piston}, {"dispenser", RsRole::Dispenser},
    {"dropper", RsRole::Dropper}, {"hopper", RsRole::Hopper}, {"daylight", RsRole::Daylight}, {"target", RsRole::Target},
    {"prail", RsRole::PRail}, {"drail", RsRole::DRail}, {"bulb", RsRole::Bulb}, {"hook", RsRole::Hook}};
  auto it = m.find(s); return it == m.end() ? RsRole::None : it->second;
}
} // namespace

void initBlockTraits() {
  std::lock_guard<std::mutex> lk(g_initMx);
  const Registry& R = reg();
  if (g_tracedBlocks == R.blocks.size() && !g_traits.empty()) return;
  loadModels();
  std::vector<BlockTraits> T(R.blocks.size());
  for (auto& b : R.blocks) {
    BlockTraits& t = T[b.id];
    RsRole r = rsRole(b.str("rs"));
    if (b.key == "redstone_block") r = RsRole::Block;
    else if (b.has("noteBlock")) r = RsRole::Note;
    else if (b.has("tnt")) r = RsRole::Tnt;
    else if (b.has("door")) r = RsRole::Door;
    else if (b.has("trapdoor")) r = RsRole::Trapdoor;
    else if (b.shapeKind == ShapeKind::Gate) r = RsRole::Gate;
    else if (b.has("bellBlock") || b.has("cmdBlock")) r = RsRole::Bell;
    t.rs = r;
    t.cond = b.solid && b.opaque && !b.shapeKind && b.render == Render::Cube && r != RsRole::Block && r != RsRole::Observer && r != RsRole::Piston;
    std::string rail = b.str("rail");
    t.rail = rail.empty() ? 0 : rail == "normal" ? 1 : 2;
    std::string tint = b.str("tint");
    t.tint = tint == "grass" ? 1 : tint == "foliage" ? 2 : 0;
    std::string cloth = b.str("cloth");
    t.cloth = cloth == "flag" ? 1 : cloth == "banner" ? 2 : cloth == "bunting" ? 3 : cloth == "line" ? 4 : cloth == "shopsign" ? 5 : 0;
    t.clothTex = tileOf(b.str("clothTex"));
    std::string shape = b.str("shape");
    t.campfire = shape == "campfire" ? 1 : shape == "soul_campfire" ? 2 : 0;
    t.lantern = shape == "lantern" || shape == "soul_lantern";
    t.bamboo = b.key == "bamboo";
    if (!shape.empty()) {
      auto it = g_modelIdx.find(shape); if (it != g_modelIdx.end()) t.model = it->second;
      it = g_modelIdx.find(shape + "_hang"); if (it != g_modelIdx.end()) t.modelHang = it->second;
    }
    if (t.campfire) { auto it = g_modelIdx.find("campfire_out"); if (it != g_modelIdx.end()) t.modelOut = it->second; }
    t.pickle = b.has("pickle"); t.chorus = b.has("chorus"); t.chorusFlower = b.key == "chorus_flower"; t.tripwire = b.has("tripwire");
    t.door = b.has("door"); t.candle = b.has("candle"); t.cauldron = b.has("cauldron"); t.pot = b.has("pot");
    t.endRod = b.key == "end_rod"; t.facingModel = b.has("facingModel"); t.pile = b.has("pile"); t.backpack = b.has("placedBackpack");
    t.hive = b.has("hive");
    t.axis = b.has("axisBlock");
    t.hasFront = b.props.contains("tex") && b.props["tex"].is_object() && b.props["tex"].contains("front");
    t.f6 = b.props.contains("f6") && b.props["f6"].is_object();
    if (t.f6) {
      const json& f = b.props["f6"]; const char* k[6] = {"frontV", "front", "back", "backOn", "top", "side"};
      for (int i = 0; i < 6; i++) t.f6t[i] = f.contains(k[i]) && f[k[i]].is_number() ? f[k[i]].get<int>() : b.faces[0];
    }
    t.sideAll = b.has("sideAll"); t.selfCull = b.has("selfCull"); t.leaves = b.has("isLeaves");
    t.crop = b.has("crop"); t.dbl = b.has("double"); t.altTile = b.has("altTile"); t.hash = b.has("hash"); t.fire = b.has("fire");
    t.flower = b.has("flower"); t.hangs = b.has("hangs"); t.stackPlant = b.has("stackPlant");
    t.upperTile = b.props.contains("upperTile") && b.props["upperTile"].is_number() ? b.props["upperTile"].get<int>() : -1;
    if (t.stackPlant) {   // weeping/twisting/cave vines: body tiles from tools/port/data_patches.py (or the pack's *_plant)
      const size_t lit = b.key.find("_lit");
      t.bodyTile = tileOf(lit != std::string::npos ? b.key.substr(0, lit) + "_plant_lit" : b.key + "_plant");
    }
    if (b.props.contains("stageTiles")) for (auto& s : b.props["stageTiles"]) if (s.is_number()) t.stageTiles.push_back(s.get<u16>());
    if (b.props.contains("tex") && b.props["tex"].is_object() && b.props["tex"].contains("top")) t.bedTop = tileOf(b.props["tex"]["top"].get<std::string>());
    if (t.bedTop < 0) t.bedTop = tileOf("red_bed_top");
    t.emitsParticles = t.fire || t.campfire || b.render == Render::Torch || R.tPortal[b.id] || t.candle || b.key.find("portal") != std::string::npos;
  }
  g_traits = std::move(T);
  g_tracedBlocks = R.blocks.size();
}
const BlockTraits& traits(BlockId id) { return id < g_traits.size() ? g_traits[id] : g_none; }

const std::vector<ShapeBox>* modelShape(int i) { return i >= 0 && i < (int)g_models.size() ? &g_models[i] : nullptr; }
int modelShapeIndex(const std::string& name) { auto it = g_modelIdx.find(name); return it == g_modelIdx.end() ? -1 : it->second; }

// ---------------------------------------------------------------- box helpers
void rotBox(ShapeBox& b, int f) {
  if (!f) return;
  float x0 = b.b[0], z0 = b.b[2], x1 = b.b[3], z1 = b.b[5], ax, az, bx, bz;
  if (f == 1) { ax = 16 - z0; az = x0; bx = 16 - z1; bz = x1; }
  else if (f == 2) { ax = 16 - x0; az = 16 - z0; bx = 16 - x1; bz = 16 - z1; }
  else { ax = z0; az = 16 - x0; bx = z1; bz = 16 - x1; }
  b.b[0] = std::min(ax, bx); b.b[2] = std::min(az, bz); b.b[3] = std::max(ax, bx); b.b[5] = std::max(az, bz);
}
void flipBox(ShapeBox& b) { float y0 = b.b[1], y1 = b.b[4]; b.b[1] = 16 - y1; b.b[4] = 16 - y0; }

ShapeBox attachBox(const ShapeBox& b, u8 m) {
  int k = m & 7;
  ShapeBox o = b;
  if (k >= 2) { o.b[1] = b.b[2]; o.b[2] = b.b[1]; o.b[4] = b.b[5]; o.b[5] = b.b[4]; rotBox(o, k - 2); return o; }
  if (m & 16) rotBox(o, 1);
  if (k == 1) flipBox(o);
  return o;
}
vec3 attachPoint(vec3 p, u8 m, bool vec) {
  int k = m & 7;
  float x = p.x, y = p.y, z = p.z, c = vec ? 0.f : 16.f;
  if (k >= 2) {
    std::swap(y, z);
    int f = k - 2;
    if (f == 1) { float nx = c - z, nz = x; x = nx; z = nz; }
    else if (f == 2) { x = c - x; z = c - z; }
    else if (f == 3) { float nx = z, nz = c - x; x = nx; z = nz; }
    return vec3(x, y, z);
  }
  if (m & 16) { float nx = c - z, nz = x; x = nx; z = nz; }
  if (k == 1) y = c - y;
  return vec3(x, y, z);
}
ShapeBox box6(const ShapeBox& in, int dir) {
  float x0 = in.b[0], y0 = in.b[1], z0 = in.b[2], x1 = in.b[3], y1 = in.b[4], z1 = in.b[5];
  ShapeBox o = in; float* b = o.b;
  switch (dir) {
    case 1: break;
    case 0: b[1] = 16 - y1; b[4] = 16 - y0; break;
    case 2: b[0] = x0; b[1] = z0; b[2] = 16 - y1; b[3] = x1; b[4] = z1; b[5] = 16 - y0; break;
    case 3: b[0] = x0; b[1] = z0; b[2] = y0; b[3] = x1; b[4] = z1; b[5] = y1; break;
    case 4: b[0] = 16 - y1; b[1] = x0; b[2] = z0; b[3] = 16 - y0; b[4] = x1; b[5] = z1; break;
    default: b[0] = y0; b[1] = x0; b[2] = z0; b[3] = y1; b[4] = x1; b[5] = z1;
  }
  return o;
}
static void faces6(ShapeBox& b, int dir, int front, int back, int side) {
  for (int i = 0; i < 6; i++) b.faces[i] = side;
  b.faces[D6_FACE[dir]] = front; b.faces[D6_FACE[OPP6[dir]]] = back;
  b.hasFaces = true;
}

bool fullSolid(BlockId id) { const Registry& R = reg(); return R.tSolid[id] && R.tOpaque[id] && !R.tShape[id]; }
bool joinsFence(BlockId id) { int s = reg().tShape[id]; return s == ShapeKind::Fence || s == ShapeKind::Gate || fullSolid(id); }
bool joinsWall(BlockId id) { int s = reg().tShape[id]; return s == ShapeKind::Wall || s == ShapeKind::Pane || s == ShapeKind::Gate || fullSolid(id); }
bool joinsPane(BlockId id) { int s = reg().tShape[id]; return s == ShapeKind::Pane || s == ShapeKind::Wall || fullSolid(id); }
bool hangingLantern(BlockId id, u8 meta) { return traits(id).lantern && (meta & 0x7f) == 1; }
bool slabGap(BlockId id, u8 meta, int side) { return reg().tShape[id] == ShapeKind::Slab && (meta & 7) - 2 == side; }

int stairShape(u8 meta, const ShapeNb& nb) {
  int f = meta & 3, top = meta & 4;
  auto isStair = [](BlockId id) { return reg().tShape[id] == ShapeKind::Stairs; };
  int ccw = (f + 3) & 3;
  auto canTake = [&](int dir) { BlockId id = nb.id(DIRX[dir], 0, DIRZ[dir]); if (!isStair(id)) return true; u8 m = nb.meta(DIRX[dir], 0, DIRZ[dir]); return (m & 3) != f || (m & 4) != top; };
  BlockId bid = nb.id(DIRX[f], 0, DIRZ[f]);
  if (isStair(bid)) {
    u8 bm = nb.meta(DIRX[f], 0, DIRZ[f]);
    if ((bm & 4) == top) { int d1 = bm & 3; if ((d1 & 1) != (f & 1) && canTake((d1 + 2) & 3)) return d1 == ccw ? STAIR_OUTER_L : STAIR_OUTER_R; }
  }
  int o = (f + 2) & 3; BlockId fid = nb.id(DIRX[o], 0, DIRZ[o]);
  if (isStair(fid)) {
    u8 fm = nb.meta(DIRX[o], 0, DIRZ[o]);
    if ((fm & 4) == top) { int d2 = fm & 3; if ((d2 & 1) != (f & 1) && canTake(d2)) return d2 == ccw ? STAIR_INNER_L : STAIR_INNER_R; }
  }
  return STAIR_STRAIGHT;
}

static bool rsAttracts(BlockId id, u8 m, int h) {
  switch (traits(id).rs) {
    case RsRole::Wire: case RsRole::Torch: case RsRole::TorchOff: case RsRole::Block: case RsRole::Lever: case RsRole::Button: case RsRole::Plate:
    case RsRole::Comparator: case RsRole::Daylight: case RsRole::Target: case RsRole::DRail: case RsRole::Hook: return true;
    case RsRole::Repeater: return ((m & 3) & 1) == (h & 1);
    case RsRole::Observer: return (m & 7) == H2D6[h];
    default: return false;
  }
}
void wireConnects(const ShapeNb& nb, int out[4]) {
  const Registry& R = reg();
  bool upCond = traits(nb.id(0, 1, 0)).cond;
  for (int h = 0; h < 4; h++) {
    int dx = DIRX[h], dz = DIRZ[h]; BlockId n = nb.id(dx, 0, dz);
    int c = 0;
    if (rsAttracts(n, nb.meta(dx, 0, dz), h)) c = 1;
    else if (!upCond && R.tSolid[n] && traits(nb.id(dx, 1, dz)).rs == RsRole::Wire) c = 2;
    else if (!traits(n).cond && traits(nb.id(dx, -1, dz)).rs == RsRole::Wire) c = 1;
    out[h] = c;
  }
}
int railShape(BlockId id, u8 m) { return traits(id).rail == 1 ? (m & 15) % 10 : (m & 7) % 6; }

// ---------------------------------------------------------------- the box list
namespace {
const float CANDLE_BOXES[4][4][6] = {
  {{7, 0, 7, 9, 6, 9}},
  {{5, 0, 7, 7, 6, 9}, {9, 0, 6, 11, 5, 8}},
  {{7, 0, 9, 9, 6, 11}, {5, 0, 6, 7, 5, 8}, {9, 0, 7, 11, 3, 9}},
  {{5, 0, 5, 7, 6, 7}, {9, 0, 5, 11, 5, 7}, {5, 0, 9, 7, 3, 11}, {9, 0, 8, 11, 4, 10}},
};
const float PICKLE_BOXES[4][4][6] = {
  {{6, 0, 6, 10, 6, 10}},
  {{3, 0, 3, 7, 6, 7}, {9, 0, 9, 13, 4, 13}},
  {{2, 0, 2, 6, 6, 6}, {10, 0, 3, 14, 4, 7}, {6, 0, 9, 10, 6, 13}},
  {{2, 0, 2, 6, 7, 6}, {10, 0, 2, 14, 5, 6}, {2, 0, 10, 6, 5, 14}, {10, 0, 10, 14, 7, 14}},
};
const float STAIR_BOXES[5][2][6] = {
  {{0, 8, 0, 16, 16, 8}}, {{0, 8, 0, 8, 16, 8}}, {{8, 8, 0, 16, 16, 8}},
  {{0, 8, 0, 16, 16, 8}, {0, 8, 8, 8, 16, 16}}, {{0, 8, 0, 16, 16, 8}, {8, 8, 8, 16, 16, 16}},
};
ShapeBox B6(const float* b) { return ShapeBox(b[0], b[1], b[2], b[3], b[4], b[5]); }
ShapeBox BT(float x0, float y0, float z0, float x1, float y1, float z1, int side, int end) { ShapeBox b(x0, y0, z0, x1, y1, z1); b.side = side; b.end = end; return b; }
struct TileNames {   // tile ids the shapes name, looked up once per registry
  int composter_side, composter_bottom, compost, compost_ready, daylight_side, daylight_top, daylight_inv, smooth_stone, rs_post_on, rs_post_off,
      rs_head_on, rs_head_off, bedrock, piston_inner, piston_bottom, piston_side_base, piston_top, piston_top_sticky, piston_head_side, piston_rod,
      hopper_outside, hopper_top, hopper_inside, planks, iron_block, tripwire, bed_pillow, cobblestone, rep[2][2][4], cmp[2][4];
};
const TileNames& tn() {
  static TileNames t; static size_t built = 0;
  if (built != reg().tileIndex.size()) {
    auto T = [](const char* n) { return reg().tile(n); };
    t.composter_side = T("composter_side"); t.composter_bottom = T("composter_bottom"); t.compost = T("compost"); t.compost_ready = T("compost_ready");
    t.daylight_side = T("daylight_detector_side"); t.daylight_top = T("daylight_detector_top"); t.daylight_inv = T("daylight_detector_inverted_top");
    t.smooth_stone = T("smooth_stone"); t.rs_post_on = T("rs_post_on"); t.rs_post_off = T("rs_post_off"); t.rs_head_on = T("rs_head_on"); t.rs_head_off = T("rs_head_off");
    t.bedrock = T("bedrock"); t.piston_inner = T("piston_inner"); t.piston_bottom = T("piston_bottom"); t.piston_side_base = T("piston_side_base");
    t.piston_top = T("piston_top"); t.piston_top_sticky = T("piston_top_sticky"); t.piston_head_side = T("piston_head_side"); t.piston_rod = T("piston_rod");
    t.hopper_outside = T("hopper_outside"); t.hopper_top = T("hopper_top"); t.hopper_inside = T("hopper_inside"); t.planks = T("planks");
    t.iron_block = T("iron_block"); t.tripwire = T("tripwire"); t.bed_pillow = T("bed_pillow"); t.cobblestone = T("cobblestone");
    for (int on = 0; on < 2; on++) for (int f = 0; f < 4; f++) {
      t.rep[0][on][f] = T(strFormat("repeater_top_%s_%d", on ? "on" : "off", f).c_str());
      t.cmp[on][f] = T(strFormat("comparator_top_%s_%d", on ? "on" : "off", f).c_str());
    }
    built = reg().tileIndex.size();
  }
  return t;
}
} // namespace

bool shapeBoxes(BlockId id, u8 meta, const ShapeNb& nb, int purpose, std::vector<ShapeBox>& out) {
  out.clear();
  const Registry& R = reg();
  int s = R.tShape[id];
  int m = meta & 0x7f;
  const BlockDef& def = R.block(id);
  const BlockTraits& T = traits(id);
  auto push = [&](ShapeBox b) { out.push_back(b); };
  auto pushRot = [&](ShapeBox b, int f) { rotBox(b, f); out.push_back(b); };
  switch (s) {
    case ShapeKind::Slab: {
      int k = m & 7;
      if (k == 6) push(ShapeBox(0, 0, 0, 16, 16, 16));
      else if (k == 0) push(ShapeBox(0, 0, 0, 16, 8, 16));
      else if (k == 1) push(ShapeBox(0, 8, 0, 16, 16, 16));
      else pushRot(ShapeBox(0, 0, 0, 16, 16, 8), k - 2);
      return true;
    }
    case ShapeKind::Stairs: {
      int f = m & 3, top = m & 4, sh = stairShape((u8)m, nb);
      push(ShapeBox(0, 0, 0, 16, 8, 16));
      for (int i = 0; i < (sh >= 3 ? 2 : 1); i++) pushRot(B6(STAIR_BOXES[sh][i]), f);
      if (top) for (auto& b : out) flipBox(b);
      return true;
    }
    case ShapeKind::Fence: {
      float H = purpose == ShapePurpose::Collide ? 24 : 16;
      push(ShapeBox(6, 0, 6, 10, H, 10));
      for (int d = 0; d < 4; d++) {
        if (!joinsFence(nb.id(DIRX[d], 0, DIRZ[d]))) continue;
        if (purpose == ShapePurpose::Draw) { pushRot(ShapeBox(7, 12, 0, 9, 15, 6), d); pushRot(ShapeBox(7, 6, 0, 9, 9, 6), d); }
        else pushRot(ShapeBox(6, 0, 0, 10, H, 6), d);
      }
      return true;
    }
    case ShapeKind::Gate: {
      int f = m & 3, open = m & 4;
      if (purpose == ShapePurpose::Collide && open) return true;
      if (purpose == ShapePurpose::Collide) { pushRot(ShapeBox(0, 0, 6, 16, 24, 10), f); return true; }
      std::vector<ShapeBox> l = {ShapeBox(0, 5, 7, 2, 16, 9), ShapeBox(14, 5, 7, 16, 16, 9)};
      if (!open) { l.push_back(ShapeBox(2, 6, 7, 14, 9, 9)); l.push_back(ShapeBox(2, 12, 7, 14, 15, 9)); l.push_back(ShapeBox(6, 9, 7, 10, 12, 9)); }
      else { for (auto& b : {ShapeBox(0, 6, 9, 2, 9, 15), ShapeBox(0, 12, 9, 2, 15, 15), ShapeBox(14, 6, 9, 16, 9, 15), ShapeBox(14, 12, 9, 16, 15, 15), ShapeBox(0, 9, 13, 2, 12, 15), ShapeBox(14, 9, 13, 16, 12, 15)}) l.push_back(b); }
      for (auto& b : l) pushRot(b, f);
      return true;
    }
    case ShapeKind::Wall: {
      bool c[4]; for (int d = 0; d < 4; d++) c[d] = joinsWall(nb.id(DIRX[d], 0, DIRZ[d]));
      bool straight = (c[0] && c[2] && !c[1] && !c[3]) || (c[1] && c[3] && !c[0] && !c[2]);
      bool post = !straight || R.tShape[nb.id(0, 1, 0)] == ShapeKind::Wall;
      if (post) push(ShapeBox(4, 0, 4, 12, purpose == ShapePurpose::Collide ? 24.f : 16.f, 12));
      for (int d = 0; d < 4; d++) if (c[d]) pushRot(ShapeBox(5, 0, 0, 11, purpose == ShapePurpose::Collide ? 24.f : 14.f, 8), d);
      return true;
    }
    case ShapeKind::Pane: {
      push(ShapeBox(7, 0, 7, 9, 16, 9));
      for (int d = 0; d < 4; d++) if (joinsPane(nb.id(DIRX[d], 0, DIRZ[d]))) pushRot(ShapeBox(7, 0, 0, 9, 16, 7), d);
      return true;
    }
    case ShapeKind::Door: {
      int f = m & 3, open = m & 4, right = m & 8;
      if (open) pushRot(right ? ShapeBox(13, 0, 0, 16, 16, 16) : ShapeBox(0, 0, 0, 3, 16, 16), f);
      else pushRot(ShapeBox(0, 0, 13, 16, 16, 16), f);
      return true;
    }
    case ShapeKind::Trapdoor: {
      int f = m & 3, open = m & 4, top = m & 8;
      if (open) pushRot(ShapeBox(0, 0, 0, 16, 16, 3), f);
      else push(top ? ShapeBox(0, 13, 0, 16, 16, 16) : ShapeBox(0, 0, 0, 16, 3, 16));
      return true;
    }
    case ShapeKind::Ladder: pushRot(purpose == ShapePurpose::Draw ? ShapeBox(0, 0, 0, 16, 16, 1) : ShapeBox(0, 0, 0, 16, 16, 3), m & 3); return true;
    case ShapeKind::Carpet: push(ShapeBox(0, 0, 0, 16, 1, 16)); return true;
    case ShapeKind::Layer: push(ShapeBox(0, 0, 0, 16, 2.f * ((m & 7) + 1), 16)); return true;
    case ShapeKind::Cactus: {
      if (purpose != ShapePurpose::Draw) { push(ShapeBox(1, 0, 1, 15, 16, 15)); return true; }
      ShapeBox ends(0, 0, 0, 16, 16, 16), ns(0, 0, 1, 16, 16, 15), ew(1, 0, 0, 15, 16, 16);
      ends.only = (1 << 2) | (1 << 3); ns.only = (1 << 4) | (1 << 5); ew.only = (1 << 0) | (1 << 1);
      push(ends); push(ns); push(ew);
      return true;
    }
    case ShapeKind::Candle: {
      int n = m & 3;
      if (purpose == ShapePurpose::Collide) { push(T.pickle ? ShapeBox(2, 0, 2, 14, 6, 14) : ShapeBox(5, 0, 5, 11, 6, 11)); return true; }
      for (int i = 0; i <= n; i++) push(B6(T.pickle ? PICKLE_BOXES[n][i] : CANDLE_BOXES[n][i]));
      return true;
    }
    case ShapeKind::Cake: push(ShapeBox(1.f + 2 * std::min(6, m & 7), 0, 1, 15, 8, 15)); return true;
    case ShapeKind::Rod: {
      int ax = m & 3;
      if (ax == 1) push(ShapeBox(0, 7, 7, 16, 9, 9));
      else if (ax == 2) push(ShapeBox(7, 7, 0, 9, 9, 16));
      else if (purpose == ShapePurpose::Draw && T.endRod) { push(ShapeBox(7, 1, 7, 9, 16, 9)); push(ShapeBox(6, 0, 6, 10, 1, 10)); }
      else push(ShapeBox(7, 0, 7, 9, 16, 9));
      return true;
    }
    case ShapeKind::Scaffold: {
      if (purpose == ShapePurpose::Collide) { push(ShapeBox(0, 14, 0, 16, 16, 16)); return true; }
      if (purpose == ShapePurpose::Select) { push(ShapeBox(0, 0, 0, 16, 16, 16)); return true; }
      for (auto& b : {ShapeBox(0, 14, 0, 16, 16, 16), ShapeBox(0, 0, 0, 2, 14, 2), ShapeBox(14, 0, 0, 16, 14, 2), ShapeBox(0, 0, 14, 2, 14, 16),
                      ShapeBox(14, 0, 14, 16, 14, 16), ShapeBox(0, 1, 0, 16, 2, 2), ShapeBox(0, 1, 14, 16, 2, 16)}) push(b);
      return true;
    }
    case ShapeKind::Path: push(ShapeBox(0, 0, 0, 16, 15, 16)); return true;
    case ShapeKind::Frame: {
      if (purpose == ShapePurpose::Collide) return false;
      int f = m & 3; ShapeBox bx(2, 2, 0, 14, 14, 1); rotBox(bx, f);
      if (slabGap(nb.id(DIRX[f], 0, DIRZ[f]), nb.meta(DIRX[f], 0, DIRZ[f]), f)) { float ox = DIRX[f] * 8.f, oz = DIRZ[f] * 8.f; bx.b[0] += ox; bx.b[3] += ox; bx.b[2] += oz; bx.b[5] += oz; }
      push(bx);
      return true;
    }
    case ShapeKind::Composter: {
      int lvl = std::min(8, m & 15);
      if (purpose == ShapePurpose::Collide) { push(ShapeBox(0, 0, 0, 16, 16, 16)); return true; }
      const TileNames& t = tn();
      push(BT(0, 0, 0, 16, 2, 16, t.composter_side, t.composter_bottom));
      push(ShapeBox(0, 2, 0, 2, 16, 16)); push(ShapeBox(14, 2, 0, 16, 16, 16)); push(ShapeBox(2, 2, 0, 14, 16, 2)); push(ShapeBox(2, 2, 14, 14, 16, 16));
      if (lvl) push(BT(2, 2, 2, 14, 2 + std::min(13.f, lvl * 1.75f), 14, t.composter_side, lvl >= 8 ? t.compost_ready : t.compost));
      return true;
    }
    case ShapeKind::Cocoa: {
      int st = m & 3, f = (m >> 4) & 3;
      float w = 4.f + st * 2, h = 5.f + st * 2, d = w;
      pushRot(ShapeBox(8 - w / 2, 12 - h, 1, 8 + w / 2, 12, 1 + d), f);
      return true;
    }
    case ShapeKind::Petals: push(ShapeBox(0, 0, 0, 16, 0.6f, 16)); return true;
    case ShapeKind::Wire: if (purpose == ShapePurpose::Collide) return false; if (purpose == ShapePurpose::Select) push(ShapeBox(0, 0, 0, 16, 1, 16)); return true;
    case ShapeKind::Rail: {
      if (purpose == ShapePurpose::Collide) return false;
      int sh = railShape(id, (u8)m);
      if (purpose == ShapePurpose::Select) push(ShapeBox(0, 0, 0, 16, sh >= 2 && sh <= 5 ? 8.f : 2.f, 16));
      return true;
    }
    case ShapeKind::Lever:
      if (purpose == ShapePurpose::Collide) return false;
      if (purpose == ShapePurpose::Select) push(attachBox(ShapeBox(4, 0, 4, 12, 10, 12), (u8)m));
      else push(attachBox(BT(5, 0, 4, 11, 3, 12, tn().cobblestone, tn().cobblestone), (u8)m));
      return true;
    case ShapeKind::Button: if (purpose == ShapePurpose::Collide) return false; push(attachBox(ShapeBox(5, 0, 6, 11, (m & 8) ? 1.f : 2.f, 10), (u8)m)); return true;
    case ShapeKind::Plate: if (purpose == ShapePurpose::Collide) return false; push(ShapeBox(1, 0, 1, 15, (m & 15) ? 0.5f : 1.f, 15)); return true;
    case ShapeKind::Daylight: {
      const TileNames& t = tn(); ShapeBox b(0, 0, 0, 16, 6, 16);
      for (int i = 0; i < 6; i++) b.faces[i] = t.daylight_side;
      b.faces[2] = (m & 16) ? t.daylight_inv : t.daylight_top; b.hasFaces = true;
      push(b); return true;
    }
    case ShapeKind::Repeater: case ShapeKind::Comparator: {
      if (purpose != ShapePurpose::Draw) { push(ShapeBox(0, 0, 0, 16, 2, 16)); return true; }
      const TileNames& t = tn();
      int f = m & 3; bool rep = s == ShapeKind::Repeater;
      bool lit = rep ? (m & 16) : (m & 8);
      push(BT(0, 0, 0, 16, 2, 16, t.smooth_stone, rep ? t.rep[0][lit][f] : t.cmp[lit][f]));
      auto torch = [&](float x0, float z0, float h, bool on) { ShapeBox b = BT(x0, 2, z0, x0 + 2, 2 + h, z0 + 2, on ? t.rs_post_on : t.rs_post_off, on ? t.rs_head_on : t.rs_head_off); if (on) b.glow = 9; return b; };
      if (rep) {
        push(torch(7, 2, 5, lit));
        float zz = 6.f + ((m >> 2) & 3) * 2;
        push((m & 32) ? BT(2, 2, zz, 14, 4, zz + 2, t.bedrock, t.bedrock) : torch(7, zz, 5, lit));
      } else {
        push(torch(7, 2, (m & 4) ? 4.f : 3.f, m & 4));
        push(torch(4, 11, 5, lit)); push(torch(10, 11, 5, lit));
      }
      for (auto& b : out) rotBox(b, f);
      return true;
    }
    case ShapeKind::PistonBase: {
      const TileNames& t = tn(); int dir = std::min(5, m & 7);
      ShapeBox b = box6(ShapeBox(0, 0, 0, 16, 12, 16), dir); faces6(b, dir, t.piston_inner, t.piston_bottom, t.piston_side_base);
      push(b); return true;
    }
    case ShapeKind::PistonHead: {
      const TileNames& t = tn(); int dir = std::min(5, m & 7);
      ShapeBox plate = box6(ShapeBox(0, 12, 0, 16, 16, 16), dir); faces6(plate, dir, (m & 8) ? t.piston_top_sticky : t.piston_top, t.piston_top, t.piston_head_side);
      ShapeBox rod = box6(purpose == ShapePurpose::Draw ? ShapeBox(6, -4, 6, 10, 12, 10) : ShapeBox(6, 0, 6, 10, 12, 10), dir); faces6(rod, dir, t.piston_rod, t.piston_rod, t.piston_rod);
      push(plate); push(rod); return true;
    }
    case ShapeKind::Hopper: {
      const TileNames& t = tn(); int dir = m & 7, o = t.hopper_outside;
      push(BT(0, 10, 0, 16, 16, 2, o, t.hopper_top)); push(BT(0, 10, 14, 16, 16, 16, o, t.hopper_top));
      push(BT(0, 10, 2, 2, 16, 14, o, t.hopper_top)); push(BT(14, 10, 2, 16, 16, 14, o, t.hopper_top));
      push(BT(2, 10, 2, 14, 11, 14, o, t.hopper_inside)); push(BT(4, 4, 4, 12, 10, 12, o, o));
      if (dir >= 2 && dir <= 5) pushRot(BT(6, 4, 0, 10, 8, 4, o, o), D62H[dir]); else push(BT(6, 0, 6, 10, 4, 10, o, o));
      return true;
    }
    case ShapeKind::Hook: {
      int f = m & 3; bool strung = m & 4;
      if (purpose != ShapePurpose::Draw) { pushRot(ShapeBox(5, 0, 0, 11, 10, 8), f); return true; }
      const TileNames& t = tn(); float arm = strung ? 1.5f : 0.f;
      pushRot(BT(6, 0, 0, 10, 10, 2, t.planks, t.planks), f);
      pushRot(BT(7.5f, 5 - arm, 2, 8.5f, 6 - arm, 7, t.iron_block, t.iron_block), f);
      pushRot(BT(6.5f, 4 - arm, 6, 9.5f, 7 - arm, 9, t.iron_block, t.iron_block), f);
      return true;
    }
    case ShapeKind::Tripwire: {
      if (purpose == ShapePurpose::Collide) return false;
      if (purpose == ShapePurpose::Select) { push(ShapeBox(0, 0, 0, 16, 2, 16)); return true; }
      auto along = [&](int d) { BlockId n = nb.id(DIRX[d], 0, DIRZ[d]); return traits(n).tripwire || R.tShape[n] == ShapeKind::Hook; };
      float y = (m & 2) ? 1.f : 2.5f;
      bool xs = along(1) || along(3), zs = along(0) || along(2) || !xs;
      int tw = tn().tripwire;
      if (xs) push(BT(0, y, 7.75f, 16, y + 0.5f, 8.25f, tw, tw));
      if (zs) push(BT(7.75f, y, 0, 8.25f, y + 0.5f, 16, tw, tw));
      return true;
    }
    case ShapeKind::Bed: {
      int f = m & 3, head = m & 4;
      push(ShapeBox(0, 3, 0, 16, 9, 16));
      if (purpose == ShapePurpose::Draw) {
        if (head) { pushRot(ShapeBox(0, 0, 0, 3, 3, 3), f); pushRot(ShapeBox(13, 0, 0, 16, 3, 3), f); }
        else { pushRot(ShapeBox(0, 0, 13, 3, 3, 16), f); pushRot(ShapeBox(13, 0, 13, 16, 3, 16), f); }
        if (head) {
          int pil = tn().bed_pillow, top = T.bedTop;
          pushRot(BT(0.5f, 9, 0.5f, 15.5f, 9.05f, 8.5f, pil, pil), f);
          pushRot(BT(0, 9, 8.5f, 16, 9.6f, 10.5f, top, top), f);
          pushRot(BT(1.5f, 9.05f, 1.2f, 14.5f, 11, 6, pil, pil), f);
        }
      }
      return true;
    }
    case ShapeKind::Chorus: {
      // game/theend.js: a stem reaching out to its neighbours
      auto isC = [](BlockId k) { return traits(k).chorus; };
      bool flower = T.chorusFlower;
      float a = flower ? 2.f : 3.f, b = 16 - a;
      push(ShapeBox(a, a, a, b, b, b));
      BlockId below = nb.id(0, -1, 0);
      if (isC(below) || R.block(below).key == "end_stone") push(ShapeBox(a, 0, a, b, a, b));
      if (!flower && isC(nb.id(0, 1, 0))) push(ShapeBox(a, b, a, b, 16, b));
      if (!flower) {
        if (isC(nb.id(1, 0, 0))) push(ShapeBox(b, a, a, 16, b, b));
        if (isC(nb.id(-1, 0, 0))) push(ShapeBox(0, a, a, a, b, b));
        if (isC(nb.id(0, 0, 1))) push(ShapeBox(a, a, b, b, b, 16));
        if (isC(nb.id(0, 0, -1))) push(ShapeBox(a, a, 0, b, b, a));
      }
      return true;
    }
    default: break;
  }
  // static models and small things
  if (T.model >= 0) {
    int f = T.facingModel ? m & 3 : 0;
    const std::vector<ShapeBox>* boxes = modelShape(hangingLantern(id, meta) && T.modelHang >= 0 ? T.modelHang : T.model);
    if (purpose == ShapePurpose::Collide) {
      if (!def.solid) return false;
      for (auto& x : *boxes) if (!x.nocol) pushRot(x, f);
      return true;
    }
    for (auto& x : *boxes) pushRot(x, f);
    return true;
  }
  if (purpose == ShapePurpose::Select) {
    if (def.render == Render::Cross) { push(ShapeBox(2, 0, 2, 14, 13, 14)); return true; }
    if (def.render == Render::Torch) {
      if (m == 0) push(ShapeBox(6, 0, 6, 10, 10, 10));
      else { static const int rf[4] = {3, 1, 0, 2}; pushRot(ShapeBox(5, 3, 0, 11, 13, 6), m >= 1 && m <= 4 ? rf[m - 1] : 0); }
      return true;
    }
    if (T.pile) { push(ShapeBox(1, 0, 1, 15, 7, 15)); return true; }
    if (T.backpack) { push(ShapeBox(3, 0, 3, 13, 12, 13)); return true; }
  }
  return false;
}

// ---- chisel carving (Cubyz-style): a full block minus any of its 8 corners ----
// The high nibble of meta carries which of the 8 corner octants are carved away.
// Bit b = (hx<<2)|(hy<<1)|hz with hx/hz picking the +half along x/z, hy the upper half.
// Carve octant mask lives in meta bits 0..6 (Cubyz-style byte: bit (hx<<2)|(hy<<1)|hz per corner).
// Only full-solid NON-shape blocks ever carry it, and they never read those low bits as facing/shape
// states (all such readers are trait-gated), so the whole low 7 bits are free. WLOG stays bit 7.
static inline u8 carveNib(const ShapeNb& nb, int dx, int dy, int dz) {
  BlockId n = nb.id(dx, dy, dz);
  return fullSolid(n) ? nb.carve(dx, dy, dz) : 0;   // carve nibble comes from the dedicated carve channel now
}
// Face bits in ShapeBox.only follow FACES order: +x -x +y -y +z -z.
bool carveBoxes(std::vector<ShapeBox>& out, u8 carve, const ShapeNb& nb) {   // carve = octant mask from the dedicated carve byte
  out.clear();
  // own octant present unless its bit is carved
  auto own = [&](int x, int y, int z) { return !((carve >> ((x << 2) | (y << 1) | z)) & 1); };
  for (int m = 0; m < 8; m++) {
    // encode: bit = (hx<<2)|(hy<<1)|hz (Cubyz subBlockMask / build.cpp). Decode MUST match:
    int x = (m >> 2) & 1, y = (m >> 1) & 1, z = m & 1;
    if (!own(x, y, z)) continue;
    // Cubyz: every PRESENT octant always stands (its collision box and geometry exist);
    // only its faces get culled when the sub-block across each face plane is present.
    u8 only = 0;
    // A half-face is covered ONLY by a fullSolid neighbor whose touching corner is present:
    // any other neighbor (open space, carved-away touching corner, or a non-full-solid
    // shape) leaves our notch exposed. emitBox must not cull those faces against opaque
    // neighbors, so carveBoxes marks them and the carve path passes their mask to emitBox.
    // half-face toward a PRESENT sibling octant is interior (same block): don't draw it
    auto sib = [&](int sx, int sy, int sz) {
      int ox = x + sx, oy = y + sy, oz = z + sz;
      if (ox < 0 || ox > 1 || oy < 0 || oy > 1 || oz < 0 || oz > 1) return false;
      return !((carve >> ((ox << 2) | (oy << 1) | oz)) & 1);
    };
    auto covered = [&](int dx, int dy, int dz) -> bool {
      if (sib(dx, dy, dz)) return true;   // interior shared face
      if (!fullSolid(nb.id(dx, dy, dz))) return false;
      int hx = dx ? (dx > 0 ? 0 : 1) : x, hy = dy ? (dy > 0 ? 0 : 1) : y, hz = dz ? (dz > 0 ? 0 : 1) : z;
      return !((carveNib(nb, dx, dy, dz) >> ((hx << 2) | (hy << 1) | hz)) & 1);
    };
    if (!covered(1, 0, 0)) only |= 1;
    if (!covered(-1, 0, 0)) only |= 2;
    if (!covered(0, 1, 0)) only |= 4;
    if (!covered(0, -1, 0)) only |= 8;
    if (!covered(0, 0, 1)) only |= 16;
    if (!covered(0, 0, -1)) only |= 32;
    ShapeBox b(x * 8.f, y * 8.f, z * 8.f, x * 8 + 8.f, y * 8 + 8.f, z * 8 + 8.f);
    b.only = only;   // interior faces sit against our own present octants and are culled by emitBox
    out.push_back(b);
  }
  return true;
}

int sinkOffset(BlockId id, const ShapeNb& nb) {
  const Registry& R = reg();
  if (!R.tSink[id]) return 0;
  BlockId b = nb.id(0, -1, 0);
  return R.tShape[b] == ShapeKind::Slab && (nb.meta(0, -1, 0) & 7) == 0 ? -8 : 0;
}
float torchSetBack(u8 m, const ShapeNb& nb) {
  if (m < 1 || m > 4) return 0;
  int wx = m == 1 ? 1 : m == 2 ? -1 : 0, wz = m == 3 ? 1 : m == 4 ? -1 : 0;
  static const int side[4] = {3, 1, 0, 2};
  return slabGap(nb.id(-wx, 0, -wz), nb.meta(-wx, 0, -wz), side[m - 1]) ? 0.5f : 0.f;
}

bool blockShapeBoxes(const World& w, int x, int y, int z, int purpose, std::vector<BoxF>& out) {
  out.clear();
  BlockId id = w.getBlock(x, y, z);
  u8 meta = w.getMeta(x, y, z);
  WorldShapeNb nb(w, x, y, z);
  static thread_local std::vector<ShapeBox> boxes;
  if (!shapeBoxes(id, meta, nb, purpose, boxes)) return false;
  float sink = (float)sinkOffset(id, nb);
  vec3 off(0);
  if (reg().tRender[id] == (u8)Render::Torch) {
    int m = meta & 7; float back = torchSetBack((u8)m, nb);
    if (back > 0) off = vec3(m == 1 ? -8 : m == 2 ? 8 : 0, 0, m == 3 ? -8 : m == 4 ? 8 : 0);
  }
  vec3 base(x, y, z);
  for (auto& b : boxes) out.push_back({base + (vec3(b.b[0], b.b[1] + sink, b.b[2]) + off) / 16.f, base + (vec3(b.b[3], b.b[4] + sink, b.b[5]) + off) / 16.f});
  return true;
}

// ---------------------------------------------------------------- cloth (game/townlife.js clothBoxes)
void clothBoxes(BlockId id, u8 meta, std::vector<ShapeBox>& out) {
  out.clear();
  const BlockTraits& T = traits(id);
  auto tile = [](const char* n) { int t = reg().tile(n); return t < 0 ? 0 : t; };
  auto woolTex = [&](const std::string& key) {
    BlockId b = reg().blockId(key);
    if (!b) return tile("wool");
    const BlockDef& d = reg().block(b);
    return (int)d.faces[0];
  };
  int t = T.clothTex >= 0 ? T.clothTex : tile("wool");
  auto K = [](ShapeBox b, int side, int end, u8 kind, float lift = 0) { b.side = side; b.end = end; b.kind = kind; b.lift = lift; return b; };
  if (T.cloth == 1) {
    for (int i = 0; i < 8; i++) out.push_back(K(ShapeBox(7.6f, 5, 14.f - i * 2, 8.4f, 15, 16.f - i * 2), t, t, 1));
    out.push_back(K(ShapeBox(7.5f, 14.5f, 12, 8.5f, 15.2f, 16), t, t, 1));
  } else if (T.cloth == 2) {
    bool low = meta & 4;
    if (!low) out.push_back(K(ShapeBox(0, 14.5f, 7, 16, 16, 9), tile("spruce_log_side"), tile("spruce_log_top"), 0));
    float top = low ? 16.f : 14.5f, bot = low ? 3.f : 0.f; int n = 6; float h = (top - bot) / n;
    for (int i = 0; i < n; i++) out.push_back(K(ShapeBox(2, top - (i + 1) * h, 7.6f, 14, top - i * h, 8.4f), t, t, 2));
    if (low) { out.push_back(K(ShapeBox(3, 1.5f, 7.6f, 13, 3, 8.4f), t, t, 2)); out.push_back(K(ShapeBox(5, 0, 7.6f, 11, 1.5f, 8.4f), t, t, 2)); }
  } else if (T.cloth == 3 || T.cloth == 4) {
    float sag = -((meta >> 2) & 15) * 0.5f;
    int rope = tile("rope");
    out.push_back(K(ShapeBox(0, 14, 7.8f, 16, 14.5f, 8.2f), rope, rope, 4, sag));
    if (T.cloth == 3) {
      static const char* PAL[8] = {"red_wool", "yellow_wool", "blue_wool", "wool", "green_wool", "orange_wool", "light_blue_wool", "magenta_wool"};
      for (int j = 0; j < 2; j++) {
        float x0 = 1.f + j * 8; int tex = woolTex(PAL[(j + ((meta >> 6) & 3) * 2) % 8]);
        out.push_back(K(ShapeBox(x0, 11.5f, 7.85f, x0 + 6, 14, 8.15f), tex, tex, 3, sag));
        out.push_back(K(ShapeBox(x0 + 1, 9.5f, 7.85f, x0 + 5, 11.5f, 8.15f), tex, tex, 3, sag));
        out.push_back(K(ShapeBox(x0 + 2, 7.5f, 7.85f, x0 + 4, 9.5f, 8.15f), tex, tex, 3, sag));
      }
    } else {
      static const char* A[4] = {"wool", "light_blue_wool", "yellow_wool", "pink_wool"};
      static const char* Bc[4] = {"light_blue_wool", "wool", "brown_wool", "wool"};
      int v = (meta >> 6) & 3, ta = woolTex(A[v]), tb = woolTex(Bc[v]);
      out.push_back(K(ShapeBox(2, 7, 7.85f, 8, 14, 8.15f), ta, ta, 3, sag));
      out.push_back(K(ShapeBox(1, 11.5f, 7.85f, 2, 14, 8.15f), ta, ta, 3, sag));
      out.push_back(K(ShapeBox(8, 11.5f, 7.85f, 9, 14, 8.15f), ta, ta, 3, sag));
      out.push_back(K(ShapeBox(10, 4, 7.85f, 15, 14, 8.15f), tb, tb, 3, sag));
    }
  } else if (T.cloth == 5) {
    int log = tile("spruce_log_side"), chain = tile("chain");
    out.push_back(K(ShapeBox(7.5f, 14.5f, 0, 8.5f, 16, 14), log, log, 0));
    out.push_back(K(ShapeBox(7.6f, 15.5f, 0, 8.4f, 16.5f, 2), log, log, 0));
    out.push_back(K(ShapeBox(7.75f, 11.5f, 3, 8.25f, 14.5f, 3.5f), chain, chain, 5));
    out.push_back(K(ShapeBox(7.75f, 11.5f, 12, 8.25f, 14.5f, 12.5f), chain, chain, 5));
    out.push_back(K(ShapeBox(7.4f, 3, 2, 8.6f, 11.5f, 14), tile("spruce_planks"), tile("spruce_planks"), 5));
    out.push_back(K(ShapeBox(7.2f, 4, 3.5f, 8.8f, 10.5f, 12.5f), tile("dark_oak_planks"), tile("dark_oak_planks"), 5));
  }
}

} // namespace dq
