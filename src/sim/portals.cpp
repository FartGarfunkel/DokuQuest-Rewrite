// sim/portals.cpp — frames, lighting, links, windows and walking through (see portals.h).
#include "sim/portals.h"
#include "sim/seamless.h"
#include <algorithm>
#include <set>

namespace dq {

namespace {
inline i64 packP(ivec3 p) { return ((i64)(p.x & 0x3FFFFF) << 42) | ((i64)(p.y & 0xFFFFF) << 22) | (i64)(p.z & 0x3FFFFF); }
const u8* fireTable() {
  static std::vector<u8> t;
  if (t.size() != reg().blocks.size()) { t.assign(reg().blocks.size(), 0); for (size_t i = 0; i < t.size(); i++) t[i] = reg().blocks[i].has("fire"); }
  return t.data();
}
struct KindDef { BlockId frame, portal; };
KindDef kindDef(PortalKind k) {
  const Registry& R = reg();
  if (k == PortalKind::Sky) return {R.blockId("glowstone"), R.blockId("sky_portal")};
  return {R.blockId("obsidian"), R.blockId("ember_portal")};
}
float yawOf(vec3 n) { return std::atan2(-n.x, -n.z); }
int scaleOf(const Level& lv, int dim) {
  if (dim != DIM_NETHER) return 1;
  int s = NETHER_SCALE;
  if (lv.info.rules.is_object() && lv.info.rules.contains("portalScale") && lv.info.rules["portalScale"].is_number()) s = std::max(1, lv.info.rules["portalScale"].get<int>());
  return s;
}
const ivec3 STEP[3][4] = {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}}, {{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}, {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
} // namespace

// ---------------------------------------------------------------------------------------------------
// the pair transform
glm::dvec3 PortalXform::point(glm::dvec3 p) const {
  double dx = p.x - c0.x, dz = p.z - c0.z, cs = std::cos((double)yaw), sn = std::sin((double)yaw);
  return {c1.x + dx * cs + dz * sn, y0b + (p.y - y0a), c1.z + dz * cs - dx * sn};
}
vec3 PortalXform::dir(vec3 v) const {
  float cs = std::cos(yaw), sn = std::sin(yaw);
  return {v.x * cs + v.z * sn, v.y, v.z * cs - v.x * sn};
}
PortalXform portalXform(const PortalWindow& w, const PortalDest& d, glm::dvec3 viewer) {
  PortalXform x;
  x.fromRealm = w.realm; x.toRealm = d.realm; x.c0 = w.c; x.c1 = d.c; x.y0a = w.y0; x.y0b = d.y0;
  if (w.axis == 2) { x.flat = true; return x; }
  vec3 wn = w.n;
  // a tear has no back: from either side you look into the same place
  if (w.kind == PortalKind::Rift && wn.x * (viewer.x - w.c.x) + wn.z * (viewer.z - w.c.z) > 0) wn = -wn;
  x.yaw = yawOf(d.n) - yawOf(wn);
  return x;
}

// ---------------------------------------------------------------------------------------------------
// frames (game/portals.js)
bool findPortalOpening(const World& w, ivec3 s, BlockId frame, int axis, bool sky, std::vector<ivec3>& cells) {
  const Registry& R = reg();
  const u8* fire = fireTable();
  cells.clear();
  std::unordered_set<i64> seen;
  std::vector<ivec3> q{s};
  int y0 = s.y, y1 = s.y, h0 = axis ? s.z : s.x, h1 = h0;
  while (!q.empty()) {
    ivec3 p = q.back(); q.pop_back();
    if (!seen.insert(packP(p)).second) continue;
    BlockId id = w.getBlock(p.x, p.y, p.z);
    if (id == frame) continue;
    if (sky) {
      // (portallinks.js) water standing in the frame becomes part of the portal
      if (id != AIR && !R.tReplaceable[id] && R.tLiquid[id] != 1 && !fire[id]) return false;
    } else {
      if (id != AIR && !R.tReplaceable[id] && !fire[id]) return false;
      if (R.tLiquid[id] && (w.getMeta(p.x, p.y, p.z) & 15) == 0) return false;   // a pool in the frame
    }
    cells.push_back(p);
    int h = axis ? p.z : p.x;
    y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); h0 = std::min(h0, h); h1 = std::max(h1, h);
    if (y1 - y0 >= PORTAL_MAX || h1 - h0 >= PORTAL_MAX || (int)cells.size() > PORTAL_MAX * PORTAL_MAX) return false;
    q.push_back(p + ivec3(0, 1, 0)); q.push_back(p - ivec3(0, 1, 0));
    if (axis) { q.push_back(p + ivec3(0, 0, 1)); q.push_back(p - ivec3(0, 0, 1)); }
    else { q.push_back(p + ivec3(1, 0, 0)); q.push_back(p - ivec3(1, 0, 0)); }
  }
  return !(y1 - y0 < 2 || h1 - h0 < 1);
}

bool tryIgniteAt(World& w, ivec3 p, PortalKind kind) {
  KindDef K = kindDef(kind);
  if (!K.frame || !K.portal || w.getBlock(p.x, p.y, p.z) == K.frame) return false;
  std::vector<ivec3> cells;
  for (int axis = 0; axis < 2; axis++) {
    if (!findPortalOpening(w, p, K.frame, axis, kind == PortalKind::Sky, cells)) continue;
    for (auto& c : cells) w.setBlock(c.x, c.y, c.z, K.portal, (u8)axis, SetFlags::Urgent);
    return true;
  }
  return false;
}

bool tryIgnitePortal(World& w, ivec3 hit, ivec3 normal, PortalKind kind) {
  if (tryIgniteAt(w, hit + normal, kind)) return true;
  // the Skylands, forgivingly: clicked the frame from the front, or the floor in front — every side
  if (kind != PortalKind::Sky || w.getBlock(hit.x, hit.y, hit.z) != kindDef(kind).frame) return false;
  static const ivec3 D[6] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
  for (auto& d : D) if (tryIgniteAt(w, hit + d, kind)) return true;
  return false;
}

void collapsePortalNear(World& w, ivec3 p) {
  const Registry& R = reg();
  std::vector<ivec3> q;
  static const ivec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (auto& d : N) q.push_back(p + d);
  std::unordered_set<i64> seen;
  int n = 0;
  while (!q.empty() && n < PORTAL_MAX * PORTAL_MAX + 50) {
    ivec3 a = q.back(); q.pop_back();
    if (!seen.insert(packP(a)).second) continue;
    if (!R.tPortal[w.getBlock(a.x, a.y, a.z)]) continue;
    w.setBlock(a.x, a.y, a.z, AIR, 0, SetFlags::Urgent);
    n++;
    for (auto& d : N) q.push_back(a + d);
  }
}

// (theend.js) is (p) part of a complete ring of twelve eyes? then fill its middle with the portal
bool endPortalCheck(World& w, ivec3 p) {
  const Registry& R = reg();
  BlockId eyeId = R.blockId("end_portal_frame_eye"), portal = R.blockId("end_portal");
  if (!eyeId || !portal) return false;
  auto eye = [&](int a, int b) { return w.getBlock(a, p.y, b) == eyeId; };
  for (int cz = p.z - 2; cz <= p.z + 2; cz++) for (int cx = p.x - 2; cx <= p.x + 2; cx++) {
    std::vector<ivec2> ring;
    for (int i = -1; i <= 1; i++) { ring.push_back({cx + i, cz - 2}); ring.push_back({cx + i, cz + 2}); ring.push_back({cx - 2, cz + i}); ring.push_back({cx + 2, cz + i}); }
    if (std::none_of(ring.begin(), ring.end(), [&](ivec2 r) { return r.x == p.x && r.y == p.z; })) continue;
    if (!std::all_of(ring.begin(), ring.end(), [&](ivec2 r) { return eye(r.x, r.y); })) continue;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) w.setBlock(cx + dx, p.y, cz + dz, portal, 0, SetFlags::Urgent);
    return true;
  }
  return false;
}

bool portalAnchor(const World& w, vec3 p, ivec3& out) {
  const Registry& R = reg();
  bool found = false; ivec3 start{0};
  for (int dy = 0; dy <= 2 && !found; dy++) for (int dz = -1; dz <= 1 && !found; dz++) for (int dx = -1; dx <= 1 && !found; dx++) {
    ivec3 b(ifloor(p.x) + dx, ifloor(p.y) + dy, ifloor(p.z) + dz);
    if (R.tPortal[w.getBlock(b.x, b.y, b.z)]) { start = b; found = true; }
  }
  if (!found) return false;
  BlockId id = w.getBlock(start.x, start.y, start.z);
  std::unordered_set<i64> seen{packP(start)};
  std::vector<ivec3> q{start};
  ivec3 best = start;
  static const ivec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  while (!q.empty() && seen.size() < 500) {
    ivec3 c = q.back(); q.pop_back();
    if (c.y < best.y || (c.y == best.y && (c.x < best.x || (c.x == best.x && c.z < best.z)))) best = c;
    for (auto& d : N) { ivec3 n = c + d; if (w.getBlock(n.x, n.y, n.z) == id && seen.insert(packP(n)).second) q.push_back(n); }
  }
  out = best;
  return true;
}

vec3 portalStand(const World& w, ivec3 a) {
  int axis = w.getMeta(a.x, a.y, a.z) & 1;
  return vec3(a.x + (axis ? 1.5f : 0.5f), (float)a.y, a.z + (axis ? 0.5f : 1.5f));
}

int portalTargetDim(int fromDim, PortalKind k) {
  int far = k == PortalKind::Sky ? DIM_AETHER : DIM_NETHER;
  return fromDim == DIM_OVERWORLD ? far : DIM_OVERWORLD;
}

vec3 arrivePortal(World& w, int dim, double x, double z, PortalKind kind, PortalSystem* ps) {
  const Registry& R = reg();
  KindDef K = kindDef(kind);
  // 1. an existing portal close by? (stand beside it)
  ivec3 c;
  if (ps && ps->nearestPortalCell(w.realm, K.portal, dim, x, z, 24, c)) {
    int axis = w.getMeta(c.x, c.y, c.z) & 1;
    return vec3(c.x + (axis ? 1.5f : 0.5f), (float)c.y, c.z + (axis ? 0.5f : 1.5f));
  }
  // 2. build one, on safe ground (the original's height ranges, doubled for the 256-tall band)
  int bx = ifloor(x), bz = ifloor(z), base = dimBase(dim), by = -1;
  auto solid = [&](int yy) { return R.tSolid[w.getBlock(bx, yy, bz)] != 0; };
  if (dim == DIM_NETHER) {
    for (int y = base + 24; y < base + 200 && by < 0; y++) {
      bool ok = true;
      for (int dx = -1; dx <= 3 && ok; dx++) for (int dy = 0; dy < 5 && ok; dy++) if (w.getBlock(bx + dx, y + dy, bz) != AIR) ok = false;
      if (ok && solid(y - 1)) by = y;
    }
    if (by < 0) by = base + 90;
  } else if (dim == DIM_AETHER) {
    for (int y = base + 220; y > base + 40; y--) if (solid(y)) { by = y + 1; break; }
    if (by < 0) by = base + 160;
  } else if (dim == DIM_END) {
    for (int y = base + 240; y > base + 4; y--) if (solid(y)) { by = y + 1; break; }
    if (by < 0) by = base + END_PLAT_LY + 1;
  } else {
    for (int y = base + 250; y > base + 2; y--) { BlockId id = w.getBlock(bx, y, bz); if (R.tSolid[id] || R.tLiquid[id]) { by = y + 1; break; } }
    if (by < 0) by = base + SEA_LEVEL + 22;
  }
  by = clampv(by, base + 2, base + DIM_H - 8);
  BlockId floor = R.blockId(dim == DIM_AETHER ? "skystone" : (dim == DIM_NETHER || dim == DIM_END) ? "obsidian" : "cobblestone");
  if (ps) ps->guard++;
  // clear space + platform
  for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 4; dx++) {
    w.setBlock(bx + dx, by - 1, bz + dz, floor, 0);
    for (int dy = 0; dy < 6; dy++) w.setBlock(bx + dx, by + dy, bz + dz, AIR, 0);
  }
  // frame 4 wide x 5 tall in the X plane, opening 2 x 3, its foot set into the floor (walk straight in)
  for (int dx = 0; dx < 4; dx++) for (int dy = 0; dy < 5; dy++) {
    bool edge = dx == 0 || dx == 3 || dy == 0 || dy == 4;
    w.setBlock(bx + dx, by - 1 + dy, bz, edge ? K.frame : K.portal, 0, SetFlags::Urgent);
  }
  if (ps) ps->guard--;
  return vec3(bx + 1.5f, (float)by, bz + 1.6f);
}

// ---------------------------------------------------------------------------------------------------
// the per-World hook: keeps the portal-cell index and reacts to block changes
namespace {
class PortalWorldHook : public WorldSystem {
public:
  explicit PortalWorldHook(std::weak_ptr<PortalSystem> p) : ps(std::move(p)) {}
  const char* name() const override { return "portals"; }
  void onBlockChanged(World& w, int x, int y, int z, BlockId o, BlockId n) override { if (auto p = ps.lock()) p->onBlockChanged(w, x, y, z, o, n); }
  void onChunkLoaded(World& w, Chunk& c) override { if (auto p = ps.lock()) p->onChunkLoaded(w, c); }
  void onChunkUnloading(World& w, Chunk& c) override { if (auto p = ps.lock()) p->onChunkUnloading(w, c); }
  std::weak_ptr<PortalSystem> ps;
};
} // namespace

PortalSystem::PortalSystem(Level& l) : lv(l) {
  const Registry& R = reg();
  bEmber = R.blockId("ember_portal"); bSky = R.blockId("sky_portal"); bEnd = R.blockId("end_portal");
  bObsidian = R.blockId("obsidian"); bGlow = R.blockId("glowstone");
  bFrame = R.blockId("end_portal_frame"); bFrameEye = R.blockId("end_portal_frame_eye");
  for (BlockId b : {bEmber, bSky, bEnd}) if (b) isCell[b] = 1;
  hurt = [](Entity& e, float amount, const char*) { e.hp = std::max(0.f, e.hp - amount); e.hurtTime = 0.5f; };
  if (lv.events) itemUseHandler = lv.events->on("itemUse", [this](const json& a) { return onItemUse(a); });
}
PortalSystem::~PortalSystem() { if (lv.events && itemUseHandler) lv.events->off(itemUseHandler); }

PortalSystem* portalSystem(Level& lv) { return lv.system<PortalSystem>(); }

void PortalSystem::attach(World& w) {
  if (!attached.insert(w.realm).second) return;
  w.addSystem(std::make_shared<PortalWorldHook>(weak_from_this()));
  for (auto& [k, c] : w.chunks()) onChunkLoaded(w, *c);
}

void PortalSystem::onChunkLoaded(World& w, Chunk& c) {
  epoch_++;
  std::vector<Cell> v;
  for (int sy = 0; sy < SECS; sy++) {
    const Section* s = c.section(sy);
    if (!s) continue;
    for (int i = 0; i < 4096; i++) {
      BlockId id = s->blocks[i];
      if (!isCell[id]) continue;
      ivec3 p(c.worldX() + (i & 15), c.baseY() + sy * 16 + (i >> 8), c.worldZ() + ((i >> 4) & 15));
      v.push_back({p, id, (u8)(id == bEnd ? 2 : (s->meta[i] & 1))});
    }
  }
  if (v.empty()) return;
  auto& r = idx[w.realm];
  r.cells[c.key()] = std::move(v);
  r.version++;
}
void PortalSystem::onChunkUnloading(World& w, Chunk& c) {
  epoch_++;
  auto it = idx.find(w.realm);
  if (it != idx.end() && it->second.cells.erase(c.key())) it->second.version++;
}
void PortalSystem::addCell(int realm, const Cell& c) {
  auto& r = idx[realm];
  r.cells[ChunkKey{dimOfY(c.p.y), c.p.x >> 4, c.p.z >> 4}].push_back(c);
  r.version++;
}
void PortalSystem::removeCell(int realm, ivec3 p) {
  auto& r = idx[realm];
  auto it = r.cells.find(ChunkKey{dimOfY(p.y), p.x >> 4, p.z >> 4});
  if (it == r.cells.end()) return;
  auto& v = it->second;
  v.erase(std::remove_if(v.begin(), v.end(), [&](const Cell& c) { return c.p == p; }), v.end());
  if (v.empty()) r.cells.erase(it);
  r.version++;
}

void PortalSystem::onBlockChanged(World& w, int x, int y, int z, BlockId o, BlockId n) {
  ivec3 p(x, y, z);
  if (isCell[o] || isCell[n]) {
    removeCell(w.realm, p);
    if (isCell[n]) addCell(w.realm, {p, n, (u8)(n == bEnd ? 2 : (w.getMeta(x, y, z) & 1))});
    epoch_++;
  }
  if (guard) return;
  guard++;
  const Registry& R = reg();
  // fire set inside an obsidian frame lights it (however the fire got there)
  if (fireTable()[n] && tryIgniteAt(w, p, PortalKind::Ember)) {
    vec3 c = vec3(p) + vec3(0.5f);
    lv.fx->sound("ignite", c); lv.fx->sound("portal", c, 32);
    for (auto& e : lv.entities()) if (e->isPlayer() && !e->removed && e->realm == w.realm && glm::distance(e->pos, c) < 16) lv.fx->message(e->id, "The portal roars open", "boss");
    if (lv.events) lv.events->emit("portalLit", json::array({x, y, z, "ember", w.realm}));
  }
  // removing a frame block (or a portal block) collapses the whole portal
  if (o == bObsidian || o == bGlow || R.tPortal[o]) collapsePortalNear(w, p);
  // a portal frame without its eye doesn't hold an End portal open
  if (o == bFrameEye && n != bFrameEye && bEnd)
    for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (w.getBlock(x + dx, y, z + dz) == bEnd) w.setBlock(x + dx, y, z + dz, AIR, 0, SetFlags::Urgent);
  guard--;
}

bool PortalSystem::nearestPortalCell(int realm, BlockId id, int dim, double x, double z, int r, ivec3& out) {
  auto it = idx.find(realm);
  if (it == idx.end()) return false;
  int bx = ifloor(x), bz = ifloor(z), best = 1 << 30;
  bool found = false;
  for (int cz = (bz - r) >> 4; cz <= (bz + r) >> 4; cz++) for (int cx = (bx - r) >> 4; cx <= (bx + r) >> 4; cx++) {
    auto ci = it->second.cells.find(ChunkKey{dim, cx, cz});
    if (ci == it->second.cells.end()) continue;
    for (auto& c : ci->second) {
      if (c.id != id) continue;
      int d = std::max(std::abs(c.p.x - bx), std::abs(c.p.z - bz));
      if (d > r) continue;
      int score = d * 2048 + (c.p.y - dimBase(dim));
      if (score < best) { best = score; out = c.p; found = true; }
    }
  }
  // the bottom of that column of portal blocks
  if (found) { World* W = lv.worldIfOpen(realm); while (W && W->getBlock(out.x, out.y - 1, out.z) == id) out.y--; }
  return found;
}

// ---------------------------------------------------------------------------------------------------
// windows
void PortalSystem::rebuild(int realm, RealmIdx& r) {
  r.wins.clear();
  std::unordered_map<i64, const Cell*> all;
  for (auto& [k, v] : r.cells) for (auto& c : v) all[packP(c.p)] = &c;
  std::unordered_set<i64> seen;
  for (auto& [key, c0] : all) {
    if (seen.count(key)) continue;
    PortalWindow w;
    w.realm = realm; w.id = c0->id; w.axis = c0->axis;
    std::vector<const Cell*> q{c0};
    seen.insert(key);
    while (!q.empty() && w.cells.size() < 450) {
      const Cell* a = q.back(); q.pop_back();
      w.cells.push_back(a->p);
      for (auto& d : STEP[w.axis]) {
        i64 k2 = packP(a->p + d);
        auto f = all.find(k2);
        if (f != all.end() && !seen.count(k2) && f->second->id == w.id) { seen.insert(k2); q.push_back(f->second); }
      }
    }
    ivec3 an = w.cells[0], lo = an, hi = an;
    for (auto& p : w.cells) {
      if (p.y < an.y || (p.y == an.y && (p.x < an.x || (p.x == an.x && p.z < an.z)))) an = p;
      lo = glm::min(lo, p); hi = glm::max(hi, p);
    }
    w.anchor = an; w.lo = lo; w.hi = hi;
    w.key = strFormat("%d:%d,%d,%d", realm, an.x, an.y, an.z);
    w.c = glm::dvec3((lo.x + hi.x + 1) * 0.5, (lo.y + hi.y + 1) * 0.5, (lo.z + hi.z + 1) * 0.5);
    w.y0 = lo.y;
    if (w.axis == 2) {
      // lying flat: a hole in the floor, looked down into (theend.js)
      w.kind = PortalKind::End;
      double top = lo.y + 0.75;
      w.c.y = top; w.y0 = top; w.n = vec3(0, 1, 0); w.along = vec3(1, 0, 0);
      bool exit = dimOfY(an.y) == DIM_END;
      w.rim = exit ? vec3(0.55f, 0.85f, 1.0f) : vec3(0.25f, 0.95f, 0.75f);
    } else {
      w.kind = w.id == bSky ? PortalKind::Sky : PortalKind::Ember;
      w.n = w.axis ? vec3(1, 0, 0) : vec3(0, 0, 1);
      w.along = w.axis ? vec3(0, 0, 1) : vec3(1, 0, 0);
      w.rim = w.kind == PortalKind::Sky ? vec3(0.55f, 0.85f, 1.0f) : vec3(1.0f, 0.45f, 0.12f);
    }
    r.wins.push_back(std::move(w));
  }
  for (auto& [id, v] : virtuals) {
    if (v.realm != realm) continue;
    PortalWindow w;
    w.kind = PortalKind::Rift; w.realm = realm; w.axis = 3;
    w.n = vec3(std::sin(v.yaw), 0, std::cos(v.yaw)); w.along = vec3(std::cos(v.yaw), 0, -std::sin(v.yaw));
    w.w = v.w; w.h = v.h; w.walk = v.walk; w.torn = v.torn; w.rim = v.rim;
    w.c = v.bottom + glm::dvec3(0, v.h * 0.5, 0); w.y0 = v.bottom.y;
    w.lo = ivec3(glm::floor(w.c - glm::dvec3(v.w * 0.5 + 0.5, v.h * 0.5, v.w * 0.5 + 0.5)));
    w.hi = ivec3(glm::floor(w.c + glm::dvec3(v.w * 0.5 + 0.5, v.h * 0.5, v.w * 0.5 + 0.5)));
    w.anchor = ivec3(glm::floor(v.bottom));
    w.key = "rift:" + id;
    r.wins.push_back(std::move(w));
  }
  r.builtVersion = r.version;
}

const std::vector<PortalWindow>& PortalSystem::windows(int realm) {
  auto& r = idx[realm];
  if (r.builtVersion != r.version) rebuild(realm, r);
  return r.wins;
}
const PortalWindow* PortalSystem::windowByKey(int realm, const std::string& key) {
  for (auto& w : windows(realm)) if (w.key == key) return &w;
  return nullptr;
}

void PortalSystem::setVirtual(const std::string& id, const VirtualPortal& v) {
  virtuals[id] = v;
  idx[v.realm].version++;
  for (auto& [r, x] : idx) x.version++;
  epoch_++;
}
void PortalSystem::removeVirtual(const std::string& id) {
  if (!virtuals.erase(id)) return;
  for (auto& [r, x] : idx) x.version++;
  epoch_++;
}

json& PortalSystem::links() {
  json& d = lv.info.data;
  if (!d.is_object()) d = json::object();
  if (!d.contains("portalLinks") || !d["portalLinks"].is_object()) d["portalLinks"] = json::object();
  return d["portalLinks"];
}
void PortalSystem::link(int ra, ivec3 a, vec3 pa, int rb, ivec3 b, vec3 pb) {
  json& L = links();
  L[strFormat("%d:%d,%d,%d", ra, a.x, a.y, a.z)] = {{"realm", rb}, {"anchor", {b.x, b.y, b.z}}, {"pos", {pb.x, pb.y, pb.z}}};
  L[strFormat("%d:%d,%d,%d", rb, b.x, b.y, b.z)] = {{"realm", ra}, {"anchor", {a.x, a.y, a.z}}, {"pos", {pa.x, pa.y, pa.z}}};
  epoch_++;
}

void PortalSystem::ensureEndPlatform(World& W) {
  ivec3 P = endPlatform();
  if (!W.isLoaded(P.x - 2, P.y, P.z - 2) || !W.isLoaded(P.x + 2, P.y, P.z + 2) || W.getBlock(P.x, P.y, P.z) == bObsidian) return;
  guard++;
  for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
    W.setBlock(P.x + dx, P.y, P.z + dz, bObsidian, 0);
    for (int dy = 1; dy <= 3; dy++) W.setBlock(P.x + dx, P.y + dy, P.z + dz, AIR, 0);
  }
  guard--;
}

bool PortalSystem::flatDest(const PortalWindow& w, PortalDest& d) {
  int dim = dimOfY(w.anchor.y);
  d.n = vec3(0, 1, 0);
  if (w.realm == REALM_MAIN && dim == DIM_OVERWORLD) {
    // a stronghold's portal: high over the obsidian platform, looking down on it
    ivec3 P = endPlatform();
    d.realm = REALM_MAIN; d.c = glm::dvec3(P.x + 0.5, P.y + 22, P.z + 0.5); d.y0 = P.y + 22;
    World& W = lv.world(REALM_MAIN);
    d.ready = W.isLoaded(P.x, P.y, P.z);
    if (d.ready) ensureEndPlatform(W);
    return true;
  }
  if (w.realm == REALM_MAIN && dim == DIM_END && std::hypot(w.c.x, w.c.z) < 8) {
    // the exit portal: high over the world's spawn
    vec3 s = lv.info.spawn;
    json& E = lv.info.data["end"];
    if (!E.is_object()) E = json::object();
    if (!E.contains("exitY")) E["exitY"] = s.y > 0 ? (double)s.y : (double)(dimBase(DIM_OVERWORLD) + SEA_LEVEL + 8);
    double sy = E["exitY"].get<double>() + 24;
    d.realm = REALM_MAIN; d.c = glm::dvec3(std::floor(s.x) + 0.5, sy, std::floor(s.z) + 0.5); d.y0 = sy;
    d.ready = lv.world(REALM_MAIN).isLoaded(ifloor(d.c.x), ifloor(sy), ifloor(d.c.z));
    return true;
  }
  return false;
}

PortalDest PortalSystem::dest(const PortalWindow& w) {
  auto ci = dests.find(w.key);
  if (ci != dests.end() && ci->second.epoch == epoch_) return ci->second.d;
  PortalDest d;
  if (w.kind == PortalKind::Rift) {
    auto it = virtuals.find(w.key.substr(5));
    if (it != virtuals.end()) {
      const VirtualPortal& v = it->second;
      d.realm = v.destRealm; d.c = v.destBottom + glm::dvec3(0, v.h * 0.5, 0); d.y0 = v.destBottom.y;
      d.n = vec3(std::sin(v.destYaw), 0, std::cos(v.destYaw)); d.linked = true;
      World* W = lv.worldIfOpen(v.destRealm);
      d.ready = W && W->isLoaded(ifloor(d.c.x), ifloor(d.y0), ifloor(d.c.z));
    }
  } else if (w.kind == PortalKind::End) {
    d.linked = flatDest(w, d);
  } else {
    json& L = links();
    auto it = L.find(w.key);
    if (it != L.end() && it->is_object()) {
      const json& j = *it;
      int r = j.value("realm", 0);
      ivec3 a(j["anchor"][0].get<int>(), j["anchor"][1].get<int>(), j["anchor"][2].get<int>());
      d.realm = r; d.linked = true;
      d.c = glm::dvec3(a) + glm::dvec3(0.5, 1.5, 0.5); d.y0 = a.y;
      World* W = lv.worldIfOpen(r);
      if (W && W->isLoaded(a.x, a.y, a.z)) {
        BlockId id = W->getBlock(a.x, a.y, a.z);
        if (!reg().tPortal[id]) {
          // the far portal is gone: forget the link (both ways) and find or build another
          std::string back = strFormat("%d:%d,%d,%d", r, a.x, a.y, a.z);
          auto b = L.find(back);
          if (b != L.end() && b->is_object() && b->value("realm", -1) == w.realm) L.erase(b);
          L.erase(w.key);
          failed.erase(w.key);
          epoch_++;
          return PortalDest{};
        }
        int axis = W->getMeta(a.x, a.y, a.z) & 1;
        std::vector<ivec3> q{a};
        std::unordered_set<i64> seen{packP(a)};
        ivec3 lo = a, hi = a;
        int n = 0;
        while (!q.empty() && n < 450) {
          ivec3 c = q.back(); q.pop_back(); n++;
          lo = glm::min(lo, c); hi = glm::max(hi, c);
          for (auto& s : STEP[axis]) { ivec3 b = c + s; if (W->getBlock(b.x, b.y, b.z) == id && seen.insert(packP(b)).second) q.push_back(b); }
        }
        d.c = glm::dvec3((lo.x + hi.x + 1) * 0.5, (lo.y + hi.y + 1) * 0.5, (lo.z + hi.z + 1) * 0.5);
        d.y0 = lo.y; d.n = axis ? vec3(1, 0, 0) : vec3(0, 0, 1);
        d.ready = true;
      }
    }
  }
  dests[w.key] = {epoch_, d};
  return d;
}

void PortalSystem::farAnchors(int viewerRealm, vec3 pos, int rd, std::map<int, std::vector<StreamAnchor>>& out) {
  float far = std::max(48.f, rd * 16.f);
  for (auto& w : windows(viewerRealm)) {
    float dist = glm::distance(vec3(w.c), pos);
    if (dist > far) continue;
    PortalDest d = dest(w);
    if (!d.linked) continue;
    int R = dist < 12 ? std::min(rd, 5) : 3;
    out[d.realm].push_back({vec3(d.c), R, false});
  }
  for (auto& [k, p] : pending) out[p.realm].push_back({vec3((float)p.x, (float)(dimBase(p.dim) + 128), (float)p.z), 3, false});
}

// A portal nobody has been through yet: the far world is loaded round where it would come out, the
// portal there found or built (just as going through would), and the two linked, both ways.
void PortalSystem::prelink(const PortalWindow& w, const Pending& p) {
  World& W = lv.world(p.realm);
  int cx = ifloor(p.x) >> 4, cz = ifloor(p.z) >> 4;
  for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) if (!W.chunk(p.dim, cx + dx, cz + dz)) return;   // not there yet
  vec3 pos = arrivePortal(W, p.dim, p.x, p.z, p.kind, this);
  ivec3 there;
  if (!portalAnchor(W, pos, there) && !portalAnchor(W, pos - vec3(1, 0, 1), there)) { failed.insert(w.key); return; }
  World& Here = lv.world(w.realm);
  link(w.realm, w.anchor, portalStand(Here, w.anchor), p.realm, there, pos);
  DQ_LOG("portal %s linked to %d:%d,%d,%d", w.key.c_str(), p.realm, there.x, there.y, there.z);
}

// ---------------------------------------------------------------------------------------------------
// walking through
namespace {
bool insideWindow(const PortalWindow& w, glm::dvec3 p) {
  if (w.axis == 2) return p.x > w.lo.x && p.x < w.hi.x + 1 && p.z > w.lo.z && p.z < w.hi.z + 1;
  if (w.axis == 3) {
    double lat = w.along.x * (p.x - w.c.x) + w.along.z * (p.z - w.c.z);
    return std::abs(lat) < w.w * 0.5 && p.y > w.y0 - 0.5 && p.y < w.y0 + w.h;
  }
  double a = w.axis ? p.z : p.x, lo = w.axis ? w.lo.z : w.lo.x, hi = (w.axis ? w.hi.z : w.hi.x) + 1;
  return p.y + 0.3 >= w.lo.y && p.y < w.hi.y + 1 && a > lo - 0.1 && a < hi + 0.1;
}
} // namespace

void PortalSystem::swap(Entity& e, const PortalWindow& w, const PortalDest& d, bool mirror) {
  PortalXform x = portalXform(w, d, glm::dvec3(e.pos));
  // the creature you ride, and whoever rides with you, come along
  std::vector<Entity*> group{&e};
  if (e.riding) if (Entity* m = lv.entity(e.riding)) group.push_back(m);
  for (auto& o : lv.entities()) {
    if (o->removed || std::find(group.begin(), group.end(), o.get()) != group.end()) continue;
    for (Entity* g : std::vector<Entity*>(group)) if (o->riding == g->id) { group.push_back(o.get()); break; }
  }
  int fromDim = dimOfY(ifloor(e.pos.y));
  vec3 dn = d.n;
  for (Entity* g : group) {
    glm::dvec3 np = x.point(glm::dvec3(g->pos));
    if (w.kind == PortalKind::Rift || mirror) {
      // come out on the far side's open face, as far past it as you were from this one (at least a little)
      double s = dn.x * (np.x - d.c.x) + dn.y * (np.y - d.c.y) + dn.z * (np.z - d.c.z);
      vec3 out = dn;
      if (mirror) {
        // standing still in it: out on the side you were facing
        vec3 f = x.dir(vec3(-std::sin(g->yaw), 0, -std::cos(g->yaw)));
        if (glm::dot(f, dn) < 0) out = -dn;
        s = glm::dot(glm::dvec3(out), np - d.c);
      }
      double want = std::max(mirror ? 0.4 : 0.08, std::abs(s));
      if (s < want) np += glm::dvec3(out) * (want - s);
    }
    vec3 before = g->pos;
    g->pos = vec3(np);
    g->prevPos = vec3(x.point(glm::dvec3(g->prevPos))) + (g->pos - vec3(x.point(glm::dvec3(before))));
    g->vel = x.dir(g->vel);
    g->yaw += x.yaw; g->bodyYaw += x.yaw; g->prevYaw += x.yaw;
    g->realm = d.realm;
    g->fallDist = 0;
    Track& t = tracks[g->id];
    t.init = true; t.pos = g->pos; t.realm = g->realm; t.cool = mirror ? 3.f : 0.35f; t.portalTime = 0;
    if (lv.events) lv.events->emit("portalEnter", json::array({g->id, w.realm, d.realm, g->pos.x, g->pos.y, g->pos.z, w.key}));
  }
  if (SeamlessSystem* ss = lv.system<SeamlessSystem>())
    for (Entity* g : group) ss->arrived(*g, w.kind, fromDim, dimOfY(ifloor(g->pos.y)));
}

bool PortalSystem::track(Entity& e, double dt, bool fromClient) {
  if (fromClient) clientTracked.insert(e.id);
  Track& t = tracks[e.id];
  t.cool = std::max(0.f, t.cool - (float)dt);
  if (!t.init || t.realm != e.realm || glm::distance(t.pos, e.pos) > 12) { t.init = true; t.pos = e.pos; t.realm = e.realm; return false; }
  vec3 from = t.pos, to = e.pos;
  t.pos = to;
  if (t.cool <= 0 && from != to) {
    for (const PortalWindow& w : windows(e.realm)) {
      if (!w.walk) continue;
      float reach = glm::length(vec3(w.hi - w.lo) + vec3(1)) * 0.5f + 3.f;
      if (glm::distance(vec3(w.c), to) > reach && glm::distance(vec3(w.c), from) > reach) continue;
      glm::dvec3 n(w.n), a(from), b(to);
      double s0 = glm::dot(n, a - w.c), s1 = glm::dot(n, b - w.c);
      if (!((s0 < 0 && s1 >= 0) || (s0 > 0 && s1 <= 0))) continue;
      glm::dvec3 p = a + (b - a) * (s0 / (s0 - s1));
      if (!insideWindow(w, p)) continue;
      PortalDest d = dest(w);
      if (!d.ready) continue;
      PortalWindow wc = w;   // swap() may rebuild the window list
      swap(e, wc, d, false);
      return true;
    }
  }
  // standing in a portal (local.js): walking, a moment; standing still, a moment longer
  if (e.isPlayer()) {
    const PortalWindow* in = nullptr;
    ivec3 feet(ifloor(e.pos.x), ifloor(e.pos.y + 0.2f), ifloor(e.pos.z)), body = feet + ivec3(0, 1, 0);
    for (const PortalWindow& w : windows(e.realm)) {
      if (w.axis > 1 || !w.walk) continue;
      if (feet.x < w.lo.x - 1 || feet.x > w.hi.x + 1 || feet.z < w.lo.z - 1 || feet.z > w.hi.z + 1) continue;
      if (std::find(w.cells.begin(), w.cells.end(), feet) != w.cells.end() || std::find(w.cells.begin(), w.cells.end(), body) != w.cells.end()) { in = &w; break; }
    }
    if (in && t.cool <= 0) {
      t.portalTime += (float)dt;
      float sp = std::hypot(e.vel.x, e.vel.z);
      if (t.portalTime > (sp > 1.2f ? 0.25f : 1.2f)) {
        PortalDest d = dest(*in);
        if (d.ready) { PortalWindow wc = *in; swap(e, wc, d, true); return true; }
      }
    } else t.portalTime = std::max(0.f, t.portalTime - (float)dt * 2);
  }
  return false;
}

void PortalSystem::tick(Level& l, double dt) {
  for (auto& [r, w] : l.allRealms()) attach(*w);
  // portals nobody has been through yet, near a player: find (or build) their far end now
  std::set<std::string> want;
  for (auto& e : l.entities()) {
    if (e->removed || !e->isPlayer()) continue;
    std::vector<PortalWindow> wins = windows(e->realm);
    for (auto& w : wins) {
      if (w.kind != PortalKind::Ember && w.kind != PortalKind::Sky) continue;
      if (glm::distance(vec3(w.c), e->pos) > 28 || failed.count(w.key) || want.count(w.key)) continue;
      if (dest(w).linked) continue;
      int src = w.realm == REALM_MAIN ? dimOfY(w.anchor.y) : DIM_OVERWORLD;
      Pending p;
      p.realm = REALM_MAIN; p.dim = portalTargetDim(src, w.kind); p.kind = w.kind;
      double sc = (double)scaleOf(l, src) / scaleOf(l, p.dim);
      p.x = w.c.x * sc; p.z = w.c.z * sc;
      pending[w.key] = p;
      want.insert(w.key);
      prelink(w, p);
    }
  }
  for (auto it = pending.begin(); it != pending.end();) it = want.count(it->first) ? std::next(it) : pending.erase(it);
  // crossings for everything that moves
  for (size_t i = 0; i < l.entities().size(); i++) { Entity* e = l.entities()[i].get(); if (!e->removed && !clientTracked.count(e->id)) track(*e, dt); }
  if (tracks.size() > l.entities().size() + 64) {
    for (auto it = tracks.begin(); it != tracks.end();) it = l.entity(it->first) ? std::next(it) : tracks.erase(it);
  }
}

// ---------------------------------------------------------------------------------------------------
// lighting by using an item (EventBus "itemUse")
Player* PortalSystem::playerNamed(const std::string& n) {
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer() && static_cast<Player*>(e.get())->name == n) return static_cast<Player*>(e.get());
  return nullptr;
}

bool PortalSystem::onItemUse(const json& a) {
  if (!a.is_array() || a.size() < 6 || !a[1].is_string()) return true;
  std::string name = a[0].is_string() ? a[0].get<std::string>() : "", key = a[1].get<std::string>();
  const ItemDef* it = reg().item(key);
  if (!it) return true;
  int face = a[5].is_number() ? a[5].get<int>() : -1;
  ivec3 hit(a[2].is_number() ? a[2].get<int>() : 0, a[3].is_number() ? a[3].get<int>() : 0, a[4].is_number() ? a[4].get<int>() : 0);
  Player* pl = playerNamed(name);
  World& W = lv.world(pl ? pl->realm : REALM_MAIN);
  u32 pid = pl ? pl->id : 0;
  bool creative = pl && pl->creative();
  ItemStack* held = pl && pl->held().id == key ? &pl->held() : nullptr;
  vec3 at = vec3(hit) + vec3(0.5f);
  std::string ig = it->str("igniter");
  if (!ig.empty()) {
    if (face >= 0) {
      ivec3 t = hit + faceNormal(face);
      guard++;
      bool lit = tryIgniteAt(W, t, PortalKind::Ember);
      guard--;
      if (lit) {
        lv.fx->sound("ignite", vec3(t) + vec3(0.5f)); lv.fx->soundUi(pid, "portal");
        lv.fx->message(pid, "The portal roars open", "boss");
        if (lv.events) lv.events->emit("portalLit", json::array({t.x, t.y, t.z, "ember", W.realm}));
        if (held && ig == "flint" && !creative) {
          held->d++;
          if (it->hasTool && it->tool.durability > 0 && held->d >= it->tool.durability) { lv.fx->message(pid, "Your " + it->name + " broke", "warn"); held->clear(); }
        }
        return false;
      }
    }
    if (ig == "ember") { lv.fx->message(pid, "Use the igniter on the inside of an obsidian frame (at least 2 wide, 3 tall)", ""); return false; }
    return true;   // flint and steel: the default (a fire) goes ahead
  }
  if (face >= 0 && it->str("bucket") == "water") {
    guard++;
    bool lit = tryIgnitePortal(W, hit, faceNormal(face), PortalKind::Sky);
    guard--;
    if (!lit) return true;
    if (held) *held = ItemStack("bucket", 1);
    lv.fx->soundUi(pid, "portal");
    lv.fx->message(pid, "Light pours through \xE2\x80\x94 the Skylands await", "rare");
    if (lv.events) lv.events->emit("portalLit", json::array({hit.x, hit.y, hit.z, "sky", W.realm}));
    return false;
  }
  if (face >= 0 && it->has("enderEye") && bFrame) {
    BlockId b = W.getBlock(hit.x, hit.y, hit.z);
    if (b == bFrameEye) return false;
    if (b != bFrame) return true;
    W.setBlock(hit.x, hit.y, hit.z, bFrameEye, W.getMeta(hit.x, hit.y, hit.z), SetFlags::Urgent);
    if (held && !creative && --held->n <= 0) held->clear();
    lv.fx->sound("ender_eye", at + vec3(0, 0.5f, 0));
    lv.fx->particles("end_mote", at + vec3(0, 0.5f, 0), 8);
    if (endPortalCheck(W, hit)) {
      lv.fx->sound("end_portal_open", at, 80);
      lv.fx->particles("end_mote", at + vec3(0, 0.3f, 0), 40, json{{"spread", 3}});
      lv.fx->message(pid, "The portal opens", "epic");
      if (lv.events) lv.events->emit("portalLit", json::array({hit.x, hit.y, hit.z, "end", W.realm}));
    }
    return false;
  }
  return true;
}

void registerPortals() {
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    auto ps = std::make_shared<PortalSystem>(lv);
    ps->attach(lv.world(REALM_MAIN));
    return ps;
  });
  registerSeamless();
}

} // namespace dq
