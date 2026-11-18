// world/attach.cpp — see attach.h.
#include "world/attach.h"
#include "sim/blockinfo.h"

namespace dq {

bool attachable(BlockId id) {
  if (!id) return false;
  const Registry& R = reg();
  if (R.tRender[id] == (u8)Render::Torch) return true;
  const int k = R.tShape[id];
  if (k == ShapeKind::Trapdoor || k == ShapeKind::Ladder || k == ShapeKind::Lever || k == ShapeKind::Button || k == ShapeKind::Plate || k == ShapeKind::Frame) return true;
  if (bhas(id, BF::Sign) || traits(id).lantern) return true;
  // candles standing on the floor (not sea pickles: a plant of the sea bed) and hanging chains
  if (k == ShapeKind::Candle && bhas(id, BF::Candle)) return true;
  if (R.block(id).str("shape") == "chain") return true;
  return false;
}

bool attachSupportDir(BlockId id, u8 meta, ivec3& d) {
  const Registry& R = reg();
  const int k = R.tShape[id];
  if (R.tRender[id] == (u8)Render::Torch) {
    const int t = meta & 7;
    d = t == 1 ? ivec3(-1, 0, 0) : t == 2 ? ivec3(1, 0, 0) : t == 3 ? ivec3(0, 0, -1) : t == 4 ? ivec3(0, 0, 1) : ivec3(0, -1, 0);
    return true;
  }
  if (k == ShapeKind::Ladder || k == ShapeKind::Frame || k == ShapeKind::Hook || (bhas(id, BF::Sign) && (meta & 16))) { const int f = meta & 3; d = ivec3(DIRX[f], 0, DIRZ[f]); return true; }
  if (k == ShapeKind::Lever || k == ShapeKind::Button) {
    const int m = meta & 7, s = m == 0 ? 0 : m == 1 ? 1 : H2D6[(m - 2) & 3];
    d = ivec3(D6X[s], D6Y[s], D6Z[s]);
    return true;
  }
  if (k == ShapeKind::Trapdoor) return false;   // (a trapdoor holds itself up, as the block does)
  if (R.block(id).str("shape") == "chain") return false;   // (nor does a chain: the block hangs on nothing either)
  if (traits(id).lantern && hangingLantern(id, meta)) { d = ivec3(0, 1, 0); return true; }
  d = ivec3(0, -1, 0);   // standing: a sign on its post, a plate, a lantern, candles
  return true;
}
bool attachSupported(const World& w, int x, int y, int z, BlockId id, u8 meta) {
  ivec3 d;
  if (!attachSupportDir(id, meta, d)) return true;
  return reg().tSolid[w.getBlock(x + d.x, y + d.y, z + d.z)];
}

namespace {
// the cell's neighbours as they are, the part itself in its own cell
struct PartNb : ShapeNb {
  const World& w; int x, y, z; BlockId pid; u8 pm;
  PartNb(const World& w, int x, int y, int z, BlockId id, u8 m) : w(w), x(x), y(y), z(z), pid(id), pm(m) {}
  BlockId id(int dx, int dy, int dz) const override { return !dx && !dy && !dz ? pid : w.getBlock(x + dx, y + dy, z + dz); }
  u8 meta(int dx, int dy, int dz) const override { return !dx && !dy && !dz ? pm : w.getMeta(x + dx, y + dy, z + dz); }
  u8 carve(int dx, int dy, int dz) const override { return !dx && !dy && !dz ? 0 : w.getCarve(x + dx, y + dy, z + dz); }
};

// signs are drawn by their own layer (render/sign_layer.cpp): their boards and posts as boxes
void signBoxes(u8 meta, std::vector<ShapeBox>& out) {
  if (meta & 16) { ShapeBox b(0, 4.5f, 0, 16, 12.5f, 1.6f); rotBox(b, meta & 3); out.push_back(b); return; }
  const float ang = (float)(meta & 15) / 16.f * TAU, c = std::cos(ang), s = std::sin(ang);
  float lo[2] = {16, 16}, hi[2] = {0, 0};
  for (float bx : {0.f, 16.f}) for (float bz : {7.f, 8.6f}) {
    const float dx = bx - 8, dz = bz - 8, rx = 8 + dx * c + dz * s, rz = 8 - dx * s + dz * c;
    lo[0] = std::min(lo[0], rx); hi[0] = std::max(hi[0], rx); lo[1] = std::min(lo[1], rz); hi[1] = std::max(hi[1], rz);
  }
  out.push_back(ShapeBox(std::max(0.f, lo[0]), 7, std::max(0.f, lo[1]), std::min(16.f, hi[0]), 15, std::min(16.f, hi[1])));
  out.push_back(ShapeBox(7.2f, 0, 7.2f, 8.8f, 7, 8.8f));
}

// A chest drawn from its hands-on model (assets/local_models.h) leaves part of its cell open: the shut model's box,
// turned the way the mesher turns it. Read here from the same files (the server has no tiles but the same data).
struct ChestRoom { bool ok = false; float lo[3], hi[3]; int turn[4] = {0, 0, 0, 0}; };
const ChestRoom* chestRoom(BlockId id) {
  static const std::vector<ChestRoom> rooms = [] {
    std::vector<ChestRoom> v(reg().blocks.size());
    const fs::path dir = dataDir() / "local_models" / "hands";
    const auto man = readJson(dir / "stations.json");
    const auto mj = readJson(dir / "models" / "chest_full.json");
    if (!man || !man->is_object() || !mj || !mj->contains("elements")) return v;
    ChestRoom base; base.ok = true;
    for (int i = 0; i < 3; i++) { base.lo[i] = 16; base.hi[i] = 0; }
    for (auto& e : (*mj)["elements"]) for (int i = 0; i < 3; i++) {
      base.lo[i] = std::min(base.lo[i], std::min(e["from"][i].get<float>(), e["to"][i].get<float>()));
      base.hi[i] = std::max(base.hi[i], std::max(e["from"][i].get<float>(), e["to"][i].get<float>()));
    }
    for (auto& [key, st] : man->items()) {
      if (!st.is_object() || st.value("state", std::string()) != "chest") continue;
      const BlockId b = reg().blockId(key);
      if (!b || (size_t)b >= v.size()) continue;
      const BlockDef& bd = reg().block(b);
      if (!(bd.props.contains("tex") && bd.props["tex"].is_object() && bd.props["tex"].contains("front"))) continue;   // (turned by its front, meta & 3)
      ChestRoom r = base;
      if (st.contains("y") && st["y"].is_array()) for (int k = 0; k < 4 && k < (int)st["y"].size(); k++) r.turn[k] = ((st["y"][k].get<int>() / 90) % 4 + 4) % 4;
      v[b] = r;
    }
    return v;
  }();
  return (size_t)id < rooms.size() && rooms[id].ok ? &rooms[id] : nullptr;
}
} // namespace

bool partBoxes(const World& w, int x, int y, int z, BlockId id, u8 meta, int purpose, std::vector<PixBox>& out) {
  out.clear();
  static thread_local std::vector<ShapeBox> boxes;
  boxes.clear();
  PartNb nb(w, x, y, z, id, meta);
  if (bhas(id, BF::Sign) && !reg().tShape[id]) signBoxes(meta, boxes);
  else if (!shapeBoxes(id, meta, nb, purpose, boxes)) return false;
  float ox = 0, oz = 0, oy = (float)sinkOffset(id, nb);
  if (reg().tRender[id] == (u8)Render::Torch) {
    const int m = meta & 7;
    if (torchSetBack((u8)m, nb) > 0) { ox = m == 1 ? -8.f : m == 2 ? 8.f : 0; oz = m == 3 ? -8.f : m == 4 ? 8.f : 0; }
  }
  for (auto& b : boxes) {
    if (purpose == ShapePurpose::Collide && b.nocol) continue;
    out.push_back({b.b[0] + ox, b.b[1] + oy, b.b[2] + oz, b.b[3] + ox, b.b[4] + oy, b.b[5] + oz});
  }
  return true;
}

bool partRoom(const World& w, int x, int y, int z, BlockId id, u8 meta, bool main, std::vector<PixBox>& out) {
  out.clear();
  const Registry& R = reg();
  if (!id || R.tLiquid[id]) return true;   // air and water take no room
  if (main) {
    if (const ChestRoom* cr = chestRoom(id)) {
      const int q = cr->turn[meta & 3];
      float lo[3] = {cr->lo[0], cr->lo[1], cr->lo[2]}, hi[3] = {cr->hi[0], cr->hi[1], cr->hi[2]};
      for (int k = 0; k < q; k++) {   // (render/mesher.cpp emitPackModel: x' = 8 - (z - 8), z' = 8 + (x - 8))
        const float nx0 = 16 - hi[2], nx1 = 16 - lo[2], nz0 = lo[0], nz1 = hi[0];
        lo[0] = nx0; hi[0] = nx1; lo[2] = nz0; hi[2] = nz1;
      }
      out.push_back({lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]});
      return true;
    }
    if (fullSolid(id) && w.getCarve(x, y, z)) return worldBoxes(w, x, y, z, id, ShapePurpose::Collide, out);
  }
  const Render r = (Render)R.tRender[id];
  const bool cube = r == Render::Cube || r == Render::Cutout;
  static thread_local std::vector<PixBox> tmp;
  bool any = false;
  for (int purpose : {(int)ShapePurpose::Collide, (int)ShapePurpose::Draw}) {
    if (purpose == ShapePurpose::Collide && !R.tSolid[id]) continue;
    if (purpose == ShapePurpose::Collide && R.tShape[id] == ShapeKind::Candle) continue;   // (a loose square round the candles: the drawn candles are what take room)
    if (partBoxes(w, x, y, z, id, meta, purpose, tmp)) { out.insert(out.end(), tmp.begin(), tmp.end()); any = true; }
    else if (cube) return false;
  }
  if (!any || out.empty()) {   // drawn on its own (a torch): its select box
    if (partBoxes(w, x, y, z, id, meta, ShapePurpose::Select, tmp)) { out.insert(out.end(), tmp.begin(), tmp.end()); return true; }
    return false;
  }
  return true;
}

static bool overlapPix(const PixBox& a, const PixBox& b) {
  const float e = 0.01f;   // touching is fine
  return a.x0 < b.x1 - e && b.x0 < a.x1 - e && a.y0 < b.y1 - e && b.y0 < a.y1 - e && a.z0 < b.z1 - e && b.z0 < a.z1 - e;
}
static bool roomsOverlap(bool fullA, const std::vector<PixBox>& a, bool fullB, const std::vector<PixBox>& b) {
  if (fullA && fullB) return true;
  if (fullA) return !b.empty();
  if (fullB) return !a.empty();
  for (auto& p : a) for (auto& q : b) if (overlapPix(p, q)) return true;
  return false;
}

bool attachRoomFree(const World& w, int x, int y, int z, BlockId id, u8 meta, int skipSlot, bool skipMain) {
  if (reg().tShape[id] == ShapeKind::Candle) {   // one group of candles to a cell (another joins it: sim/attach_play.cpp), never two side by side
    if (!skipMain && reg().tShape[w.getBlock(x, y, z)] == ShapeKind::Candle) return false;
    if (const auto* parts = w.attachments(x, y, z))
      for (int i = 0; i < (int)parts->size(); i++) if (i != skipSlot && reg().tShape[(*parts)[i].id] == ShapeKind::Candle) return false;
  }
  std::vector<PixBox> mine, other;
  const bool fullMine = !partRoom(w, x, y, z, id, meta, false, mine);
  if (!skipMain) {
    const BlockId m = w.getBlock(x, y, z);
    const bool full = !partRoom(w, x, y, z, m, w.getMeta(x, y, z), true, other);
    if (m && roomsOverlap(fullMine, mine, full, other)) return false;
  }
  if (const auto* parts = w.attachments(x, y, z))
    for (int i = 0; i < (int)parts->size(); i++) {
      if (i == skipSlot) continue;
      const bool full = !partRoom(w, x, y, z, (*parts)[i].id, (*parts)[i].meta, false, other);
      if (roomsOverlap(fullMine, mine, full, other)) return false;
    }
  return true;
}

bool attachRoomFor(const World& w, int x, int y, int z) {
  const auto* parts = w.attachments(x, y, z);
  if (!parts) return true;
  std::vector<PixBox> mainRoom, other;
  const BlockId m = w.getBlock(x, y, z);
  const bool full = !partRoom(w, x, y, z, m, w.getMeta(x, y, z), true, mainRoom);
  for (const Attachment& a : *parts) {
    const bool fa = !partRoom(w, x, y, z, a.id, a.meta, false, other);
    if (m && roomsOverlap(full, mainRoom, fa, other)) return false;
  }
  return true;
}

bool partWorldBoxes(const World& w, int x, int y, int z, int slot, int purpose, std::vector<BoxF>& out) {
  out.clear();
  const vec3 base(x, y, z);
  if (slot < 0) {
    if (!w.hasAttachments(x, y, z)) return blockShapeBoxes(w, x, y, z, purpose, out);
    std::vector<PixBox> pb;   // a cell with attachments: the main block's own room (a chest's model, not the whole cell)
    if (!partRoom(w, x, y, z, w.getBlock(x, y, z), w.getMeta(x, y, z), true, pb)) return blockShapeBoxes(w, x, y, z, purpose, out);
    if (!blockShapeBoxes(w, x, y, z, purpose, out) || !reg().tShape[w.getBlock(x, y, z)]) {
      out.clear();
      for (auto& b : pb) out.push_back({base + vec3(b.x0, b.y0, b.z0) / 16.f, base + vec3(b.x1, b.y1, b.z1) / 16.f});
    }
    return true;
  }
  const auto* parts = w.attachments(x, y, z);
  if (!parts || slot >= (int)parts->size()) return false;
  std::vector<PixBox> pb;
  const Attachment& a = (*parts)[slot];
  if (!partBoxes(w, x, y, z, a.id, a.meta, purpose, pb) || pb.empty()) {
    if (!partRoom(w, x, y, z, a.id, a.meta, false, pb)) { out.push_back({base, base + vec3(1)}); return true; }
  }
  for (auto& b : pb) out.push_back({base + vec3(b.x0, b.y0, b.z0) / 16.f, base + vec3(b.x1, b.y1, b.z1) / 16.f});
  return true;
}

static double rayBox(const glm::dvec3& o, const glm::dvec3& d, const glm::dvec3& mn, const glm::dvec3& mx, ivec3& nOut) {
  double tmin = -INFINITY, tmax = INFINITY; ivec3 n(0);
  for (int a = 0; a < 3; a++) {
    if (std::fabs(d[a]) < 1e-9) { if (o[a] < mn[a] || o[a] > mx[a]) return -1; continue; }
    double t1 = (mn[a] - o[a]) / d[a], t2 = (mx[a] - o[a]) / d[a];
    int s = -1;
    if (t1 > t2) { std::swap(t1, t2); s = 1; }
    if (t1 > tmin) { tmin = t1; n = ivec3(0); n[a] = s; }
    tmax = std::min(tmax, t2);
    if (tmin > tmax) return -1;
  }
  if (tmax < 0) return -1;
  nOut = n;
  return std::max(0.0, tmin);
}

bool attachRayCell(const World& w, glm::dvec3 o, glm::dvec3 d, int x, int y, int z, double maxDist, int& part, double& t, ivec3& n) {
  const auto* parts = w.attachments(x, y, z);
  if (!parts) return false;
  double best = INFINITY; bool any = false;
  std::vector<BoxF> boxes;
  for (int slot = -1; slot < (int)parts->size(); slot++) {
    if (slot < 0) { const BlockId m = w.getBlock(x, y, z); if (!m || reg().tLiquid[m]) continue; }
    if (!partWorldBoxes(w, x, y, z, slot, ShapePurpose::Select, boxes)) continue;
    for (auto& b : boxes) {
      ivec3 nn; const double tt = rayBox(o, d, glm::dvec3(b.lo), glm::dvec3(b.hi), nn);
      if (tt >= 0 && tt < best && tt <= maxDist) { best = tt; part = slot; n = nn; any = true; }
    }
  }
  if (any) t = best;
  return any;
}

json attachmentsToJson(const std::vector<Attachment>* parts) {
  json l = json::array();
  if (parts) for (const Attachment& a : *parts) { json e = json::array({reg().block(a.id).key, a.meta}); if (!a.be.is_null()) e.push_back(a.be); l.push_back(e); }
  return l;
}
std::vector<Attachment> attachmentsFromJson(const json& j) {
  std::vector<Attachment> v;
  if (!j.is_array()) return v;
  for (auto& e : j) {
    if (!e.is_array() || e.size() < 2 || !e[0].is_string() || !e[1].is_number()) continue;
    Attachment a; a.id = reg().blockId(e[0].get<std::string>()); a.meta = (u8)e[1].get<int>();
    if (e.size() > 2) a.be = e[2];
    if (a.id && (int)v.size() < MAX_ATTACH) v.push_back(std::move(a));
  }
  return v;
}

} // namespace dq
