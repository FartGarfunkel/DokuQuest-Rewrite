// world/shapes.cpp — port of world/shapes.js shapeBoxes/worldBoxes (+ the static SHAPES models).
#include "world/shapes.h"
#include <mutex>

namespace dq {

// ---- static models (data/shapes.json) ---------------------------------------------------------
static std::unordered_map<std::string, std::vector<ModelBox>>& modelMap() {
  static std::unordered_map<std::string, std::vector<ModelBox>> m;
  static std::once_flag once;
  std::call_once(once, [] {
    auto j = readJson(dataDir() / "shapes.json");
    if (!j || !j->is_object()) { DQ_WARN("shapes: data/shapes.json missing"); return; }
    for (auto& [k, arr] : j->items()) {
      std::vector<ModelBox> v;
      for (auto& e : arr) {
        if (!e.contains("b") || !e["b"].is_array() || e["b"].size() < 6) continue;
        ModelBox mb; auto& b = e["b"];
        mb.b = {b[0].get<float>(), b[1].get<float>(), b[2].get<float>(), b[3].get<float>(), b[4].get<float>(), b[5].get<float>()};
        mb.nocol = e.value("nocol", false);
        if (e.contains("side") && e["side"].is_string()) mb.side = e["side"];
        if (e.contains("end") && e["end"].is_string()) mb.end = e["end"];
        v.push_back(std::move(mb));
      }
      m[k] = std::move(v);
    }
  });
  return m;
}
const std::vector<ModelBox>* shapeModel(const std::string& name) {
  auto& m = modelMap(); auto it = m.find(name);
  return it == m.end() ? nullptr : &it->second;
}

// per-block cache: the block's static model (or null)
static const std::vector<ModelBox>* modelOf(BlockId id) {
  static std::vector<const std::vector<ModelBox>*> cache;
  static std::vector<u8> known;
  const Registry& R = reg();
  if (known.size() != R.blocks.size()) { cache.assign(R.blocks.size(), nullptr); known.assign(R.blocks.size(), 0); }
  if (id >= known.size()) return nullptr;
  if (!known[id]) { known[id] = 1; std::string s = R.block(id).str("shape"); cache[id] = s.empty() ? nullptr : shapeModel(s); }
  return cache[id];
}
bool hasShapeBoxes(BlockId id) { return reg().tShape[id] || modelOf(id); }

// ---- box helpers ---------------------------------------------------------------------------
PixBox rotBox(PixBox b, int f) {
  if (!f) return b;
  float ax, az, bx, bz;
  if (f == 1) { ax = 16 - b.z0; az = b.x0; bx = 16 - b.z1; bz = b.x1; }
  else if (f == 2) { ax = 16 - b.x0; az = 16 - b.z0; bx = 16 - b.x1; bz = 16 - b.z1; }
  else { ax = b.z0; az = 16 - b.x0; bx = b.z1; bz = 16 - b.x1; }
  return {std::min(ax, bx), b.y0, std::min(az, bz), std::max(ax, bx), b.y1, std::max(az, bz)};
}
PixBox flipBox(PixBox b) { PixBox o = b; o.y0 = 16 - b.y1; o.y1 = 16 - b.y0; return o; }
static PixBox flipY(PixBox b) { return flipBox(b); }
// levers/buttons: boxes are given lying on the floor; m&7: 0 floor, 1 ceiling, 2..5 wall N/E/S/W; m&16 turned
static PixBox attachBox(PixBox b, u8 m) {
  int k = m & 7;
  if (k >= 2) { PixBox o{b.x0, b.z0, b.y0, b.x1, b.z1, b.y1}; return rotBox(o, k - 2); }
  PixBox o = b;
  if (m & 16) o = rotBox(o, 1);
  return k == 1 ? flipBox(o) : o;
}
static PixBox box6(PixBox b, int dir) {
  switch (dir) {
    case 1: return b;
    case 0: return {b.x0, 16 - b.y1, b.z0, b.x1, 16 - b.y0, b.z1};
    case 2: return {b.x0, b.z0, 16 - b.y1, b.x1, b.z1, 16 - b.y0};
    case 3: return {b.x0, b.z0, b.y0, b.x1, b.z1, b.y1};
    case 4: return {16 - b.y1, b.x0, b.z0, 16 - b.y0, b.x1, b.z1};
    default: return {b.y0, b.x0, b.z0, b.y1, b.x1, b.z1};
  }
}

static bool fullSolid(BlockId id) { const Registry& R = reg(); return R.tSolid[id] && R.tOpaque[id] && !R.tShape[id]; }
static bool joinsFence(BlockId id) { int s = reg().tShape[id]; return s == ShapeKind::Fence || s == ShapeKind::Gate || fullSolid(id); }
static bool joinsWall(BlockId id) { int s = reg().tShape[id]; return s == ShapeKind::Wall || s == ShapeKind::Pane || s == ShapeKind::Gate || fullSolid(id); }
static bool joinsPane(BlockId id) { int s = reg().tShape[id]; return s == ShapeKind::Pane || s == ShapeKind::Wall || fullSolid(id); }

int stairShape(u8 meta, const ShapeNb& nb) {
  int f = meta & 3, top = meta & 4;
  auto isStair = [](BlockId id) { return reg().tShape[id] == ShapeKind::Stairs; };
  int ccw = (f + 3) & 3;
  auto canTake = [&](int dir) {
    BlockId id = nb.id(DIRX[dir], 0, DIRZ[dir]); if (!isStair(id)) return true;
    u8 m = nb.meta(DIRX[dir], 0, DIRZ[dir]); return (m & 3) != f || (m & 4) != top;
  };
  BlockId bid = nb.id(DIRX[f], 0, DIRZ[f]);
  if (isStair(bid)) {
    u8 bm = nb.meta(DIRX[f], 0, DIRZ[f]);
    if ((bm & 4) == top) { int d1 = bm & 3; if ((d1 & 1) != (f & 1) && canTake((d1 + 2) & 3)) return d1 == ccw ? 1 : 2; }
  }
  int o = (f + 2) & 3; BlockId fid = nb.id(DIRX[o], 0, DIRZ[o]);
  if (isStair(fid)) {
    u8 fm = nb.meta(DIRX[o], 0, DIRZ[o]);
    if ((fm & 4) == top) { int d2 = fm & 3; if ((d2 & 1) != (f & 1) && canTake(d2)) return d2 == ccw ? 3 : 4; }
  }
  return 0;
}

static const std::vector<PixBox> CANDLE_BOXES[4] = {
  {{7, 0, 7, 9, 6, 9}},
  {{5, 0, 7, 7, 6, 9}, {9, 0, 6, 11, 5, 8}},
  {{7, 0, 9, 9, 6, 11}, {5, 0, 6, 7, 5, 8}, {9, 0, 7, 11, 3, 9}},
  {{5, 0, 5, 7, 6, 7}, {9, 0, 5, 11, 5, 7}, {5, 0, 9, 7, 3, 11}, {9, 0, 8, 11, 4, 10}},
};
static const std::vector<PixBox> PICKLE_BOXES[4] = {
  {{6, 0, 6, 10, 6, 10}},
  {{3, 0, 3, 7, 6, 7}, {9, 0, 9, 13, 4, 13}},
  {{2, 0, 2, 6, 6, 6}, {10, 0, 3, 14, 4, 7}, {6, 0, 9, 10, 6, 13}},
  {{2, 0, 2, 6, 7, 6}, {10, 0, 2, 14, 5, 6}, {2, 0, 10, 6, 5, 14}, {10, 0, 10, 14, 7, 14}},
};
static const std::vector<PixBox> STAIR_BOXES[5] = {
  {{0, 8, 0, 16, 16, 8}},
  {{0, 8, 0, 8, 16, 8}},
  {{8, 8, 0, 16, 16, 8}},
  {{0, 8, 0, 16, 16, 8}, {0, 8, 8, 8, 16, 16}},
  {{0, 8, 0, 16, 16, 8}, {8, 8, 8, 16, 16, 16}},
};
static bool slabGap(BlockId id, u8 meta, int side) { return reg().tShape[id] == ShapeKind::Slab && (meta & 7) - 2 == side; }
static bool hangingLantern(const BlockDef& d, u8 meta) { std::string sh = d.str("shape"); return (sh == "lantern" || sh == "soul_lantern") && (meta & 0x7f) == 1; }

bool shapeBoxes(BlockId id, u8 meta, const ShapeNb& nb, int purpose, std::vector<PixBox>& out) {
  out.clear();
  const Registry& R = reg();
  int s = R.tShape[id];
  u8 m = meta & 0x7f;
  const BlockDef& def = R.block(id);
  switch (s) {
    case ShapeKind::Slab: {
      int k = m & 7;
      if (k == 6) out.push_back({0, 0, 0, 16, 16, 16});
      else if (k == 0) out.push_back({0, 0, 0, 16, 8, 16});
      else if (k == 1) out.push_back({0, 8, 0, 16, 16, 16});
      else out.push_back(rotBox({0, 0, 0, 16, 16, 8}, k - 2));
      return true;
    }
    case ShapeKind::Stairs: {
      int f = m & 3, top = m & 4, sh = stairShape(m, nb);
      out.push_back({0, 0, 0, 16, 8, 16});
      for (auto& b : STAIR_BOXES[sh]) out.push_back(rotBox(b, f));
      if (top) for (auto& b : out) b = flipY(b);
      return true;
    }
    case ShapeKind::Fence: {
      float H = purpose == SHAPE_COLLIDE ? 24 : 16;
      out.push_back({6, 0, 6, 10, H, 10});
      for (int d = 0; d < 4; d++) {
        if (!joinsFence(nb.id(DIRX[d], 0, DIRZ[d]))) continue;
        if (purpose == SHAPE_DRAW) { out.push_back(rotBox({7, 12, 0, 9, 15, 6}, d)); out.push_back(rotBox({7, 6, 0, 9, 9, 6}, d)); }
        else out.push_back(rotBox({6, 0, 0, 10, H, 6}, d));
      }
      return true;
    }
    case ShapeKind::Gate: {
      int f = m & 3; bool open = m & 4;
      if (purpose == SHAPE_COLLIDE && open) return true;
      if (purpose == SHAPE_COLLIDE) { out.push_back(rotBox({0, 0, 6, 16, 24, 10}, f)); return true; }
      std::vector<PixBox> v = {{0, 5, 7, 2, 16, 9}, {14, 5, 7, 16, 16, 9}};
      if (!open) { v.push_back({2, 6, 7, 14, 9, 9}); v.push_back({2, 12, 7, 14, 15, 9}); v.push_back({6, 9, 7, 10, 12, 9}); }
      else for (PixBox b : {PixBox{0, 6, 9, 2, 9, 15}, {0, 12, 9, 2, 15, 15}, {14, 6, 9, 16, 9, 15}, {14, 12, 9, 16, 15, 15}, {0, 9, 13, 2, 12, 15}, {14, 9, 13, 16, 12, 15}}) v.push_back(b);
      for (auto& b : v) out.push_back(rotBox(b, f));
      return true;
    }
    case ShapeKind::Wall: {
      bool c[4]; for (int d = 0; d < 4; d++) c[d] = joinsWall(nb.id(DIRX[d], 0, DIRZ[d]));
      bool straight = (c[0] && c[2] && !c[1] && !c[3]) || (c[1] && c[3] && !c[0] && !c[2]);
      bool post = !straight || R.tShape[nb.id(0, 1, 0)] == ShapeKind::Wall;
      if (post) out.push_back({4, 0, 4, 12, purpose == SHAPE_COLLIDE ? 24.f : 16.f, 12});
      for (int d = 0; d < 4; d++) if (c[d]) out.push_back(rotBox({5, 0, 0, 11, purpose == SHAPE_COLLIDE ? 24.f : 14.f, 8}, d));
      return true;
    }
    case ShapeKind::Pane: {
      out.push_back({7, 0, 7, 9, 16, 9});
      for (int d = 0; d < 4; d++) if (joinsPane(nb.id(DIRX[d], 0, DIRZ[d]))) out.push_back(rotBox({7, 0, 0, 9, 16, 7}, d));
      return true;
    }
    case ShapeKind::Door: {
      int f = m & 3; bool open = m & 4, right = m & 8;
      PixBox b = rotBox({0, 0, 13, 16, 16, 16}, f);
      if (open) b = rotBox(right ? PixBox{13, 0, 0, 16, 16, 16} : PixBox{0, 0, 0, 3, 16, 16}, f);
      out.push_back(b); return true;
    }
    case ShapeKind::Trapdoor: {
      int f = m & 3; bool open = m & 4, top = m & 8;
      if (open) out.push_back(rotBox({0, 0, 0, 16, 16, 3}, f));
      else out.push_back(top ? PixBox{0, 13, 0, 16, 16, 16} : PixBox{0, 0, 0, 16, 3, 16});
      return true;
    }
    case ShapeKind::Ladder: out.push_back(rotBox(purpose == SHAPE_DRAW ? PixBox{0, 0, 0, 16, 16, 1} : PixBox{0, 0, 0, 16, 16, 3}, m & 3)); return true;
    case ShapeKind::Carpet: out.push_back({0, 0, 0, 16, 1, 16}); return true;
    case ShapeKind::Layer: out.push_back({0, 0, 0, 16, 2.f * ((m & 7) + 1), 16}); return true;
    case ShapeKind::Cactus:
      if (purpose != SHAPE_DRAW) { out.push_back({1, 0, 1, 15, 16, 15}); return true; }
      out.push_back({0, 0, 0, 16, 16, 16}); out.push_back({0, 0, 1, 16, 16, 15}); out.push_back({1, 0, 0, 15, 16, 16});
      return true;
    case ShapeKind::Candle: {
      bool pickle = def.has("pickle");
      if (purpose == SHAPE_COLLIDE) { out.push_back(pickle ? PixBox{2, 0, 2, 14, 6, 14} : PixBox{5, 0, 5, 11, 6, 11}); return true; }
      out = (pickle ? PICKLE_BOXES : CANDLE_BOXES)[m & 3];
      return true;
    }
    case ShapeKind::Cake: out.push_back({1.f + 2 * std::min(6, m & 7), 0, 1, 15, 8, 15}); return true;
    case ShapeKind::Rod: {
      int ax = m & 3;
      if (ax == 1) out.push_back({0, 7, 7, 16, 9, 9});
      else if (ax == 2) out.push_back({7, 7, 0, 9, 9, 16});
      else if (purpose == SHAPE_DRAW && def.key == "end_rod") { out.push_back({7, 1, 7, 9, 16, 9}); out.push_back({6, 0, 6, 10, 1, 10}); }
      else out.push_back({7, 0, 7, 9, 16, 9});
      return true;
    }
    case ShapeKind::Scaffold:
      if (purpose == SHAPE_COLLIDE) { out.push_back({0, 14, 0, 16, 16, 16}); return true; }
      if (purpose == SHAPE_SELECT) { out.push_back({0, 0, 0, 16, 16, 16}); return true; }
      out = {{0, 14, 0, 16, 16, 16}, {0, 0, 0, 2, 14, 2}, {14, 0, 0, 16, 14, 2}, {0, 0, 14, 2, 14, 16}, {14, 0, 14, 16, 14, 16}, {0, 1, 0, 16, 2, 2}, {0, 1, 14, 16, 2, 16}};
      return true;
    case ShapeKind::Path: out.push_back({0, 0, 0, 16, 15, 16}); return true;
    case ShapeKind::Frame: {
      if (purpose == SHAPE_COLLIDE) return false;
      int f = m & 3; PixBox bx = rotBox({2, 2, 0, 14, 14, 1}, f);
      if (slabGap(nb.id(DIRX[f], 0, DIRZ[f]), nb.meta(DIRX[f], 0, DIRZ[f]), f)) { float ox = DIRX[f] * 8.f, oz = DIRZ[f] * 8.f; bx.x0 += ox; bx.x1 += ox; bx.z0 += oz; bx.z1 += oz; }
      out.push_back(bx); return true;
    }
    case ShapeKind::Composter: {
      int lvl = std::min(8, m & 15);
      if (purpose == SHAPE_COLLIDE) { out.push_back({0, 0, 0, 16, 16, 16}); return true; }
      out = {{0, 0, 0, 16, 2, 16}, {0, 2, 0, 2, 16, 16}, {14, 2, 0, 16, 16, 16}, {2, 2, 0, 14, 16, 2}, {2, 2, 14, 14, 16, 16}};
      if (lvl) out.push_back({2, 2, 2, 14, 2 + std::min(13.f, lvl * 1.75f), 14});
      return true;
    }
    case ShapeKind::Cocoa: {
      int st = m & 3, f = (m >> 4) & 3;
      float w = 4 + st * 2.f, h = 5 + st * 2.f, d = w;
      out.push_back(rotBox({8 - w / 2, 12 - h, 1, 8 + w / 2, 12, 1 + d}, f)); return true;
    }
    case ShapeKind::Petals: out.push_back({0, 0, 0, 16, 0.6f, 16}); return true;
    case ShapeKind::Wire:
      if (purpose == SHAPE_COLLIDE) return false;
      if (purpose == SHAPE_SELECT) out.push_back({0, 0, 0, 16, 1, 16});
      return true;
    case ShapeKind::Rail: {
      if (purpose == SHAPE_COLLIDE) return false;
      int sh = def.str("rail") == "normal" ? (m & 15) % 10 : (m & 7) % 6;
      if (purpose == SHAPE_SELECT) out.push_back({0, 0, 0, 16, sh >= 2 && sh <= 5 ? 8.f : 2.f, 16});
      return true;
    }
    case ShapeKind::Lever:
      if (purpose == SHAPE_COLLIDE) return false;
      out.push_back(purpose == SHAPE_SELECT ? attachBox({4, 0, 4, 12, 10, 12}, m) : attachBox({5, 0, 4, 11, 3, 12}, m));
      return true;
    case ShapeKind::Button: if (purpose == SHAPE_COLLIDE) return false; out.push_back(attachBox({5, 0, 6, 11, (m & 8) ? 1.f : 2.f, 10}, m)); return true;
    case ShapeKind::Plate: if (purpose == SHAPE_COLLIDE) return false; out.push_back({1, 0, 1, 15, (m & 15) ? 0.5f : 1.f, 15}); return true;
    case ShapeKind::Daylight: out.push_back({0, 0, 0, 16, 6, 16}); return true;
    case ShapeKind::Repeater: case ShapeKind::Comparator: {
      if (purpose != SHAPE_DRAW) { out.push_back({0, 0, 0, 16, 2, 16}); return true; }
      int f = m & 3; bool rep = s == ShapeKind::Repeater;
      std::vector<PixBox> v = {{0, 0, 0, 16, 2, 16}};
      auto torch = [](float x0, float z0, float h) { return PixBox{x0, 2, z0, x0 + 2, 2 + h, z0 + 2}; };
      if (rep) { v.push_back(torch(7, 2, 5)); float zz = 6 + ((m >> 2) & 3) * 2.f; v.push_back((m & 32) ? PixBox{2, 2, zz, 14, 4, zz + 2} : torch(7, zz, 5)); }
      else { v.push_back(torch(7, 2, (m & 4) ? 4 : 3)); v.push_back(torch(4, 11, 5)); v.push_back(torch(10, 11, 5)); }
      for (auto& b : v) out.push_back(rotBox(b, f));
      return true;
    }
    case ShapeKind::PistonBase: out.push_back(box6({0, 0, 0, 16, 12, 16}, m & 7)); return true;
    case ShapeKind::PistonHead: out.push_back(box6({0, 12, 0, 16, 16, 16}, m & 7)); out.push_back(box6(purpose == SHAPE_DRAW ? PixBox{6, -4, 6, 10, 12, 10} : PixBox{6, 0, 6, 10, 12, 10}, m & 7)); return true;
    case ShapeKind::Hopper: {
      int dir = m & 7;
      out = {{0, 10, 0, 16, 16, 2}, {0, 10, 14, 16, 16, 16}, {0, 10, 2, 2, 16, 14}, {14, 10, 2, 16, 16, 14}, {2, 10, 2, 14, 11, 14}, {4, 4, 4, 12, 10, 12}};
      static const int D62H[6] = {-1, -1, 0, 2, 3, 1};
      out.push_back(dir >= 2 ? rotBox({6, 4, 0, 10, 8, 4}, D62H[dir]) : PixBox{6, 0, 6, 10, 4, 10});
      return true;
    }
    case ShapeKind::Hook: {
      int f = m & 3; bool strung = m & 4;
      if (purpose != SHAPE_DRAW) { out.push_back(rotBox({5, 0, 0, 11, 10, 8}, f)); return true; }
      float arm = strung ? 1.5f : 0;
      out.push_back(rotBox({6, 0, 0, 10, 10, 2}, f)); out.push_back(rotBox({7.5f, 5 - arm, 2, 8.5f, 6 - arm, 7}, f)); out.push_back(rotBox({6.5f, 4 - arm, 6, 9.5f, 7 - arm, 9}, f));
      return true;
    }
    case ShapeKind::Tripwire: {
      if (purpose == SHAPE_COLLIDE) return false;
      if (purpose == SHAPE_SELECT) { out.push_back({0, 0, 0, 16, 2, 16}); return true; }
      auto along = [&](int d) { BlockId n = nb.id(DIRX[d], 0, DIRZ[d]); return R.block(n).has("tripwire") || R.tShape[n] == ShapeKind::Hook; };
      float y = (m & 2) ? 1 : 2.5f;
      bool xs = along(1) || along(3), zs = along(0) || along(2) || !xs;
      if (xs) out.push_back({0, y, 7.75f, 16, y + 0.5f, 8.25f});
      if (zs) out.push_back({7.75f, y, 0, 8.25f, y + 0.5f, 16});
      return true;
    }
    case ShapeKind::Bed: {
      int f = m & 3; bool head = m & 4;
      out.push_back({0, 3, 0, 16, 9, 16});
      if (purpose == SHAPE_DRAW) {
        if (head) { out.push_back(rotBox({0, 0, 0, 3, 3, 3}, f)); out.push_back(rotBox({13, 0, 0, 16, 3, 3}, f)); }
        else { out.push_back(rotBox({0, 0, 13, 3, 3, 16}, f)); out.push_back(rotBox({13, 0, 13, 16, 3, 16}, f)); }
        if (head) { out.push_back(rotBox({0.5f, 9, 0.5f, 15.5f, 9.05f, 8.5f}, f)); out.push_back(rotBox({0, 9, 8.5f, 16, 9.6f, 10.5f}, f)); out.push_back(rotBox({1.5f, 9.05f, 1.2f, 14.5f, 11, 6}, f)); }
      }
      return true;
    }
    default: break;
  }
  // static models and small things
  if (auto* model = modelOf(id)) {
    int f = def.has("facingModel") ? (m & 3) : 0;
    const std::vector<ModelBox>* boxes = model;
    if (hangingLantern(def, meta)) if (auto* h = shapeModel(def.str("shape") + "_hang")) boxes = h;
    if (purpose == SHAPE_DRAW && (def.str("shape") == "campfire" || def.str("shape") == "soul_campfire") && (meta & 8)) if (auto* o = shapeModel("campfire_out")) boxes = o;
    if (purpose == SHAPE_COLLIDE) {
      if (!def.solid) return false;
      for (auto& x : *boxes) if (!x.nocol) out.push_back(rotBox(x.b, f));
      return true;
    }
    for (auto& x : *boxes) out.push_back(rotBox(x.b, f));
    return true;
  }
  if (purpose == SHAPE_SELECT) {
    if (def.render == Render::Cross) { out.push_back({2, 0, 2, 14, 13, 14}); return true; }
    if (def.render == Render::Model && def.key == "bamboo") { out.push_back({5, 0, 5, 11, 16, 11}); return true; }
    if (def.render == Render::Torch) {
      if (m == 0) out.push_back({6, 0, 6, 10, 10, 10});
      else { static const int rf[4] = {3, 1, 0, 2}; out.push_back(rotBox({5, 3, 0, 11, 13, 6}, m >= 1 && m <= 4 ? rf[m - 1] : 0)); }
      return true;
    }
    if (def.has("pile")) { out.push_back({1, 0, 1, 15, 7, 15}); return true; }
    if (def.has("placedBackpack")) { out.push_back({3, 0, 3, 13, 12, 13}); return true; }
  }
  return false;   // full cube
}

bool worldBoxes(const World& w, int x, int y, int z, BlockId id, int purpose, std::vector<PixBox>& out) {
  u8 meta = w.getMeta(x, y, z);
  ShapeNb nb{&w, x, y, z};
  if (!shapeBoxes(id, meta, nb, purpose, out)) return false;
  const Registry& R = reg();
  // things set on top of a bottom half slab sink down to sit on it
  if (R.tSink[id]) {
    BlockId b = nb.id(0, -1, 0);
    if (R.tShape[b] == ShapeKind::Slab && (nb.meta(0, -1, 0) & 7) == 0) for (auto& bx : out) { bx.y0 -= 8; bx.y1 -= 8; }
  }
  // a wall torch on the open face of a vertical slab sits back against it
  if (R.tRender[id] == (u8)Render::Torch) {
    int m = meta & 7;
    if (m >= 1 && m <= 4) {
      int wx = m == 1 ? 1 : m == 2 ? -1 : 0, wz = m == 3 ? 1 : m == 4 ? -1 : 0;
      static const int side[4] = {3, 1, 0, 2};
      if (slabGap(nb.id(-wx, 0, -wz), nb.meta(-wx, 0, -wz), side[m - 1])) {
        float ox = m == 1 ? -8.f : m == 2 ? 8.f : 0, oz = m == 3 ? -8.f : m == 4 ? 8.f : 0;
        for (auto& bx : out) { bx.x0 += ox; bx.x1 += ox; bx.z0 += oz; bx.z1 += oz; }
      }
    }
  }
  return true;
}

float liquidHeight(BlockId id, u8 meta, BlockId above) {
  if (above == id) return 1;
  if (reg().tPortal[id]) return 1;
  int lvl = meta & 7;
  if (meta & 8) return 0.9f;
  return std::max(0.12f, 0.875f - lvl * 0.105f);
}
bool waterAt(const World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  if (reg().tLiquid[id] == 1) return true;
  return reg().tWlog[id] && (w.getMeta(x, y, z) & WLOG);
}

} // namespace dq
