// gen/structures_townbld.cpp — the buildings of villages, towns and cities: villages.js vStamp (a building's local
// frame: x across the front, z from the front wall back, y up from the floor; the door in the middle of the front
// wall, the street in front) with towns.js's furnishing helpers, vHouse (half-timbered houses: stone ground floor,
// plastered storeys jettied out over the street, steep tiled or thatched roofs) and every TOWN_VT building.
#include "gen/structures_town.h"
#include <optional>

namespace dq {

static const int* DXL = DIRX;
static const int* DZL = DIRZ;

const VStyle& vstyle(int style) {
  static const std::array<VStyle, 5> S = [] {
    std::array<VStyle, 5> r;
    const char* names[5] = {"plains", "savanna", "taiga", "snowy", "desert"};
    auto str = [](const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
    for (int i = 0; i < 5; i++) {
      const json& j = SD()["VSTYLE"][names[i]];
      VStyle& s = r[i]; s.key = names[i];
      s.planks = str(j, "planks"); s.log = str(j, "log"); s.beam = str(j, "beam"); s.base = str(j, "base"); s.floor = str(j, "floor");
      s.stairs = str(j, "stairs"); s.slab = str(j, "slab"); s.fence = str(j, "fence"); s.gate = str(j, "gate"); s.door = str(j, "door");
      s.trapdoor = str(j, "trapdoor"); s.wood = str(j, "wood"); s.stone = str(j, "stone"); s.stoneSlab = str(j, "stoneSlab");
      s.stoneStairs = str(j, "stoneStairs"); s.lampPost = str(j, "lampPost"); s.roof = str(j, "roof"); s.rim = str(j, "rim");
      s.plaster = str(j, "plaster"); s.frame = str(j, "frame"); s.beamH = str(j, "beamH"); s.base2 = str(j, "base2"); s.base3 = str(j, "base3");
      s.leaves = str(j, "leaves"); s.fwood = str(j, "fwood");
      s.path = strList(j["path"]); s.bed = strList(j["bed"]); s.awning = strList(j["awning"]); s.awning2 = strList(j["awning2"]);
      s.timber = strList(j["timber"]); s.banner = strList(j["banner"]);
      for (auto& rr : j["roofs"]) s.roofs.push_back({rr[0].get<std::string>(), rr[1].get<std::string>()});
      s.thatch = {j["thatch"][0].get<std::string>(), j["thatch"][1].get<std::string>()};
      s.flat = j.contains("flat") && j["flat"].is_boolean() && j["flat"].get<bool>();
    }
    return r;
  }();
  return S[style];
}

const std::vector<std::string>& townDecor(const std::string& kind) {
  static const std::unordered_map<std::string, std::vector<std::string>> M = [] {
    std::unordered_map<std::string, std::vector<std::string>> m;
    for (auto& [k, v] : SD()["TOWN_DECOR"].items()) m[k] = strList(v);
    return m;
  }();
  static const std::vector<std::string> none;
  auto it = M.find(kind); return it == M.end() ? none : it->second;
}

bool softBlock(BlockId id) {
  static const std::vector<u8> T = [] {
    std::vector<u8> t(reg().blocks.size() + 1, 0);
    for (const auto& b : reg().blocks) {
      const bool s = b.id == AIR || !reg().tSolid[b.id] || reg().tLiquid[b.id] || b.render == Render::Cross ||
                     b.key.find("leaves") != std::string::npos || b.key.find("log") != std::string::npos || b.key.find("snow_layer") != std::string::npos;
      t[b.id] = s;
    }
    return t;
  }();
  return id == AIR || (id != NOBLOCK && id < T.size() && T[id]);
}

int townTemplateSize(const std::string& type, int& W, int& D, int& H) {
  const json& t = SD()["TOWN_VT"];
  if (!t.contains(type)) return 0;
  W = t[type][0]; D = t[type][1]; H = t[type][2];
  return 1;
}

// ---- villages.js vStamp + the towns.js helpers ----
class VS {
public:
  VS(SP& P, const Town& v, const Bld& b) : P(P), v(v), st(vstyle(v.style)), b(b), r(b.r), rnd(b.seed), fw(st.fwood.empty() ? "oak" : st.fwood) {}
  SP& P; const Town& v; const VStyle& st; const Bld& b; const int r; Mulberry32 rnd; const std::string fw; int salt = 0;
  static std::string VB(const std::string& k, const std::string& alt) { return hasBlock(k) ? k : alt; }
  int set(int lx, int ly, int lz, std::string_view key, int meta = 0) { auto [a, c] = vRot(r, lx, lz); return P.put(b.ox + a, b.y + ly, b.oz + c, key, (u8)meta); }
  BlockId get(int lx, int ly, int lz) const { auto [a, c] = vRot(r, lx, lz); return P.get(b.ox + a, b.y + ly, b.oz + c); }
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, std::string_view key, int meta = 0) { for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) set(x, y, z, key, meta); }
  int d(int f) const { return (f + r) & 3; }
  void stairs(int lx, int ly, int lz, std::string_view key, int f, bool top = false) { set(lx, ly, lz, key, d(f) | (top ? 4 : 0)); }
  void face(int lx, int ly, int lz, std::string_view key, int f) { set(lx, ly, lz, key, (d(f) + 2) & 3); }
  void log(int lx, int ly, int lz, std::string_view key, int axis) { set(lx, ly, lz, key, axis ? ((axis == 1) == !(r & 1) ? 1 : 2) : 0); }
  void door(int lx, int lz, std::string_view key, int f = 2, bool right = false) { const int m = d(f) | (right ? 8 : 0); set(lx, 1, lz, key, m); set(lx, 2, lz, key, m | 16); }
  void bed(int lx, int lz, int f, std::string_view key) { const int m = d(f); set(lx, 1, lz, key, m); set(lx + DXL[f], 1, lz + DZL[f], key, m | 4); }
  void torch(int lx, int ly, int lz, int wall) { static const int T[4] = {3, 2, 4, 1}; set(lx, ly, lz, "torch", T[d(wall)]); }
  void chest(int lx, int ly, int lz, int f, const std::string& items) { int i = set(lx, ly, lz, "chest", (d(f) + 2) & 3); if (i >= 0) P.setEntity(i, json{{"type", "chest"}, {"items", vLoot(items, rnd)}}); }
  void shutter(int lx, int ly, int lz, int toWall) { if (!st.trapdoor.empty()) set(lx, ly, lz, st.trapdoor, d(toWall) | 4); }
  void flower(int lx, int lz) {
    static const std::vector<std::string> F = strList(SD()["VFLOWERS"]);
    set(lx, 0, lz, "grass"); const int k = jsFloor(rnd() * F.size()); set(lx, 1, lz, F[k]);
  }
  void floor(int W, int D, const std::string& key = "") { fill(0, 0, 0, W - 1, 0, D - 1, key.empty() ? st.floor : key); }
  // a fireplace in the back wall, the chimney stack behind it climbing past the roof
  void chimney(int lx, int D, int top, int W) {
    const int z = D - 1;
    for (int ly = 0; ly <= top + 2; ly++) set(lx, ly, D, st.stone);
    set(lx, 0, z, st.stone); set(lx, 1, z, "campfire"); set(lx, 2, z, st.stone);
    for (int s : {-1, 1}) if (lx + s > 0 && lx + s < (W ? W : 99) - 1) { set(lx + s, 1, z, st.stone); set(lx + s, 0, z, st.stone); }
  }
  void frontPath(int lx) { const std::string& p = st.path[0]; set(lx, 0, -1, p); set(lx, 1, -1, "air"); set(lx, 2, -1, "air"); set(lx, 0, -2, p); set(lx, 1, -2, "air"); }
  // the porch: a little canopy over the door with lanterns hanging under it
  void porch(int dx) {
    if (!st.flat) { for (int k : {-1, 0, 1}) set(dx + k, 3, -1, st.slab, 1); set(dx - 1, 2, -1, "lantern", 1); set(dx + 1, 2, -1, "lantern", 1); }
    else { torch(dx - 1, 2, -1, 2); torch(dx + 1, 2, -1, 2); }
    frontPath(dx);
  }
  // ---- towns.js ----
  std::string pick(const char* kind) { const auto& l = townDecor(kind); const int k = salt++; return l.empty() ? std::string() : l[hash32((i32)b.seed, k, 77, 3) % l.size()]; }
  static json stack(const std::string& id) { return !id.empty() && hasItem(id) ? json{{"id", id}, {"n", 1}} : json(nullptr); }
  void pile(int lx, int ly, int lz, const std::vector<std::string>& ids) {
    json items = json::array(); bool any = false;
    for (int k = 0; k < 4; k++) { json s = stack(k < (int)ids.size() ? ids[k] : ""); any |= !s.is_null(); items.push_back(s); }
    if (!any) return;
    const int i = set(lx, ly, lz, "item_pile");
    if (i < 0) return;
    json rot = json::array(); for (int k = 0; k < 4; k++) rot.push_back(((int)(hash32((i32)b.seed, lx, lz, k) & 15) - 8) * JPI / 16);
    P.setEntity(i, json{{"type", "pile"}, {"items", items}, {"rot", rot}});
  }
  void shelf(int lx, int ly, int lz, int f, const std::vector<std::string>& ids) {
    const int i = set(lx, ly, lz, VB(fw + "_shelf", "oak_shelf"), (d(f) + 2) & 3);
    json items = json::array(); for (int k = 0; k < 3; k++) items.push_back(stack(k < (int)ids.size() ? ids[k] : ""));
    if (i >= 0) P.setEntity(i, json{{"type", "shelf"}, {"items", items}});
  }
  void standShelf(int lx, int lz, int f, const std::vector<std::string>& ids, int ly = 1) {
    const int i = set(lx, ly, lz, VB(fw + "_standing_shelf", "oak_standing_shelf"), (d(f) + 2) & 3);
    json items = json::array(); for (int k = 0; k < 6; k++) items.push_back(stack(k < (int)ids.size() ? ids[k] : ""));
    if (i >= 0) P.setEntity(i, json{{"type", "shelf"}, {"items", items}});
  }
  void bookcase(int lx, int ly, int lz, int f, int n = 6) {
    const int i = set(lx, ly, lz, VB(fw + "_bookcase", "bookshelf"), (d(f) + 2) & 3);
    if (i < 0 || !reg().block(P.w.buf.blocks[i]).has("bookcase")) return;   // (out of this chunk: no books picked)
    json items = json::array(); for (int k = 0; k < 6; k++) items.push_back(k < n ? stack(pick("books")) : json(nullptr));
    P.setEntity(i, json{{"type", "bookcase"}, {"items", items}});
  }
  void hang(int lx, int ly, int lz, int f, const std::string& id, double rot = 0) {
    if (id.empty()) return;
    const int i = set(lx, ly, lz, "item_frame", (d(f) + 2) & 3);
    if (i >= 0) P.setEntity(i, json{{"type", "frame"}, {"item", {{"id", id}, {"n", 1}}}, {"rot", rot}});
  }
  void book(int lx, int ly, int lz, int f) { const int i = set(lx, ly, lz, "lectern", (d(f) + 2) & 3); if (i >= 0) P.setEntity(i, json{{"type", "lectern"}, {"book", true}}); }
  void table(int lx, int lz, int ly = 1) { set(lx, ly, lz, VB(fw + "_table", st.slab)); }
  void chair(int lx, int lz, int f, int ly = 1) { face(lx, ly, lz, VB(fw + "_chair", st.stairs), f); }
  void bedAt(int lx, int ly, int lz, int f, std::string_view key) { const int m = d(f); set(lx, ly, lz, key, m); set(lx + DXL[f], ly, lz + DZL[f], key, m | 4); }
  void rug(int x0, int z0, int x1, int z1, const std::string& key, int ly = 1) { if (!hasBlock(key)) return; for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) if (get(x, ly, z) == AIR) set(x, ly, z, key); }
  void plant(int lx, int ly, int lz) { static const char* K[3] = {"flowering_azalea", "azalea", "flower_pot"}; set(lx, ly, lz, VB(K[hash32((i32)b.seed, lx, lz, 9) % 3], "flower_pot")); }
  void armourStand(int lx, int lz) {
    set(lx, 1, lz, VB("polished_andesite_slab", st.stoneSlab)); set(lx, 2, lz, VB(fw + "_fence", st.fence));
    std::string a = pick("chest"), h = pick("helm"); pile(lx, 3, lz, {a, h, "", ""});
  }
  // a gable roof, ridge front to back over z0..z1, eaves all round; the gables filled, a beam up the middle
  int roofG(int W, int z0, int z1, int ly0, const std::string& rs, const std::string& rsl, const std::string& fillK, const std::string& frame) {
    int top = ly0;
    for (int k = 0;; k++) {
      const int L = k - 1, R = W - k, ly = ly0 + k;
      if (L > R) break;
      top = ly;
      for (int lz = z0 - 1; lz <= z1 + 1; lz++) { if (L == R) set(L, ly, lz, rsl, 0); else { stairs(L, ly, lz, rs, 1); stairs(R, ly, lz, rs, 3); } }
      if (k >= 1) for (int lx = L + 1; lx < R; lx++) for (int lz : {z0, z1}) { if (!frame.empty() && lx == (W - 1) / 2) log(lx, ly, lz, frame, 0); else set(lx, ly, lz, fillK); }
      if (L == R) break;
    }
    if (W >= 5) for (int lz : {z0, z1}) set((W - 1) / 2 + (frame.empty() ? 0 : 1), ly0 + 2, lz, "glass_pane");
    return top;
  }
  // a pyramid roof (towers, the windmill): square rings of stairs shrinking to a point
  int hipRoof(int cx, int cz, int r0, int ly0, const std::string& rs, const std::string& rsl, const std::string& core) {
    int ly = ly0;
    for (int rr = r0; rr >= 0; rr--, ly++) {
      if (rr == 0) { set(cx, ly, cz, rsl, 0); break; }
      for (int dz = -rr; dz <= rr; dz++) for (int dx = -rr; dx <= rr; dx++) {
        const int ax = std::abs(dx), az = std::abs(dz);
        if (ax != rr && az != rr) { if (!core.empty()) set(cx + dx, ly, cz + dz, core); continue; }
        if (ax == rr && az == rr) { std::string full = rs; size_t p = full.find("_stairs"); if (p != std::string::npos) full.erase(p, 7); set(cx + dx, ly, cz + dz, !core.empty() ? core : hasBlock(full) ? full : rsl); }
        else if (ax == rr) stairs(cx + dx, ly, cz + dz, rs, dx < 0 ? 1 : 3);
        else stairs(cx + dx, ly, cz + dz, rs, dz < 0 ? 2 : 0);
      }
    }
    return ly;
  }
  void win(int lx, int ly, int lz, int out, const std::string& glass = "") {
    set(lx, ly, lz, glass.empty() ? "glass_pane" : glass);
    if (st.flat) return;
    const int ox = DXL[out], oz = DZL[out], tx = oz ? 1 : 0, tz = ox ? 1 : 0;
    for (int s : {-1, 1}) if (get(lx + ox + tx * s, ly, lz + oz + tz * s) == AIR) shutter(lx + ox + tx * s, ly, lz + oz + tz * s, (out + 2) & 3);
  }
};

// ---- vHouse ----
struct HouseOpt {
  int W = 0, D = 0, floors = 1;
  bool jetty = false, thatch = false, stoneGround = false, allStone = false, attic = false, noFooting = false, noWinWest = false, noWinEast = false;
  std::optional<int> chimney;
  std::optional<std::array<int, 2>> ladder;
  std::string frame, beam, fill, stone, corner, floorKey, glass;
  std::optional<std::array<std::string, 2>> roof;
  std::optional<std::vector<int>> winFront, winSide, winBack;
};
static int vHouse(VS& S, const HouseOpt& o) {
  const VStyle& st = S.st; const Bld& b = S.b;
  const int W = o.W, D = o.D, n = o.floors ? o.floors : 1; const bool J = o.jetty && n > 1 && !st.flat;
  auto or2 = [](const std::string& a, const std::string& c) { return !a.empty() ? a : c; };
  const std::string frame = or2(o.frame, or2(st.frame, st.log));
  const std::string beam = or2(o.beam, or2(st.beamH, or2(st.beam, st.log)));
  const std::string fill = or2(o.fill, or2(st.plaster, st.planks));
  const std::string stone = or2(o.stone, or2(st.base2, st.stone));
  const std::string corner = or2(o.corner, or2(st.base3, stone));
  const std::vector<std::string>* timber = o.fill.empty() && !st.timber.empty() ? &st.timber : nullptr;
  const std::array<std::string, 2> roof = o.roof ? *o.roof : o.thatch ? st.thatch : st.roofs[b.roofI % st.roofs.size()];
  auto zfOf = [&](int f) { return f > 0 && J ? -1 : 0; };
  S.floor(W, D, or2(o.floorKey, st.floor));
  int yb = 0;
  for (int f = 0; f < n; f++) {
    const int zf = zfOf(f); const bool allStone = (f == 0 && o.stoneGround) || o.allStone;
    for (int ly = yb + 1; ly <= yb + 3; ly++) for (int lz = zf; lz < D; lz++) for (int lx = 0; lx < W; lx++) {
      const bool ex = lx == 0 || lx == W - 1, ez = lz == zf || lz == D - 1;
      if (!ex && !ez) { S.set(lx, ly, lz, "air"); continue; }
      if (allStone) {
        if (ex && ez && !o.allStone && !st.flat) S.log(lx, ly, lz, frame, 0);
        else S.set(lx, ly, lz, ex && ez ? corner : (o.allStone ? stone : or2(o.stone, or2(st.base3, stone))));
        continue;
      }
      const bool post = (ex && ez) || (ez && lx % 3 == 0) || (ex && (lz - zf) % 3 == 0);
      if (post) { if (st.flat) S.set(lx, ly, lz, corner); else S.log(lx, ly, lz, frame, 0); continue; }
      if (f == 0 && ly == 1 && !o.noFooting) { S.set(lx, ly, lz, stone); continue; }
      const std::string* key = &fill;
      if (timber && ly == yb + 2 && ((lx * 7 + lz * 3 + f * 5 + (int)(b.seed & 7)) % 5 == 0)) key = &(*timber)[(lx + lz + f) % timber->size()];
      S.set(lx, ly, lz, *key);
    }
    yb += 4;
    const bool last = f == n - 1; const int z0 = std::min(zf, last ? zf : zfOf(f + 1));
    for (int lz = z0; lz < D; lz++) for (int lx = 0; lx < W; lx++) {
      const bool ex = lx == 0 || lx == W - 1, ez = lz == z0 || lz == D - 1;
      if (ex || ez) { if (st.flat || o.allStone) S.set(lx, yb, lz, corner); else S.log(lx, yb, lz, beam, ez ? 1 : 2); }
      else S.set(lx, yb, lz, !last || o.attic ? std::string_view(st.floor) : std::string_view("air"));
    }
  }
  // a ladder up through the floors, against the back wall
  if (n > 1 || o.attic) {
    const int lx = o.ladder ? (*o.ladder)[0] : W - 2, lz = o.ladder ? (*o.ladder)[1] : D - 2, topY = o.attic ? 4 * n : 4 * (n - 1);
    for (int ly = 1; ly <= topY; ly++) S.set(lx, ly, lz, "ladder", S.d(2));
  }
  // windows (the ground floor's front has the door in the middle)
  for (int f = 0; f < n; f++) {
    const int zf = zfOf(f), ly = f * 4 + 2;
    const std::vector<int> fronts = o.winFront ? *o.winFront : W >= 9 ? std::vector<int>{1, (W - 1) / 2 - 2, (W - 1) / 2 + 2, W - 2} : std::vector<int>{1, W - 2};
    for (int lx : fronts) S.win(lx, ly, zf, 0, o.glass);
    if (f > 0 && W >= 7) S.win((W - 1) / 2, ly, zf, 0, o.glass);
    for (int lz : o.winSide ? *o.winSide : std::vector<int>{2, D - 3}) {
      const int z = std::max(zf + 1, std::min(D - 2, lz));
      if (!(o.noWinWest && f == 0)) S.win(0, ly, z, 3, o.glass);
      if (!(o.noWinEast && f == 0)) S.win(W - 1, ly, z, 1, o.glass);
    }
    for (int lx : o.winBack ? *o.winBack : std::vector<int>{1, W - 2}) if (!o.chimney || std::abs(lx - *o.chimney) > 1) S.win(lx, ly, D - 1, 2, o.glass);
  }
  // the roof
  const int zr0 = zfOf(n - 1);
  int top;
  if (st.flat) {
    S.fill(0, yb, zr0, W - 1, yb, D - 1, st.roof);
    for (int lz = zr0; lz < D; lz++) for (int lx = 0; lx < W; lx++) if (lx == 0 || lz == zr0 || lx == W - 1 || lz == D - 1) S.set(lx, yb + 1, lz, st.rim, 0);
    top = yb + 1;
  } else top = S.roofG(W, zr0, D - 1, yb, roof[0], roof[1], fill, o.allStone ? std::string() : frame);
  if (o.chimney && !st.flat) S.chimney(*o.chimney, D, top, W);
  return top;
}

// ---- the plans: beds, workplaces, seats ----
static VSpot vSpot(const Bld& b, int lx, int lz, int f) { auto w = vWorld(b, lx, 1, lz); return {w[0], w[1], w[2], (f + b.r) & 3}; }
static VSeat vSeat(const Bld& b, int lx, int lz, int f, int ax, int az, const char* held) {
  auto w = vWorld(b, lx, 1, lz), a = vWorld(b, ax, 1, az);
  VSeat s; s.x = w[0]; s.y = w[1]; s.z = w[2]; s.f = (f + b.r) & 3; s.walk = true; s.ax = a[0]; s.az = a[2]; s.held = held ? held : ""; return s;
}
void townBuildingPlan(Bld& b, Mulberry32& rnd, const VStyle& st) {
  static const std::vector<std::string> WJ = strList(SD()["WORKSHOP_JOBS"]);
  const std::string& t = b.type;
  auto work = [&](int lx, int lz, int f) { b.work = vSpot(b, lx, lz, f); b.hasWork = true; };
  if (t == "house_s") { b.beds.push_back(vSpot(b, 5, 3, 2)); b.bedCol = jsFloor(rnd() * 4); }
  else if (t == "workshop") {
    b.beds.push_back(vSpot(b, 5, 3, 2)); b.job = WJ[jsFloor(rnd() * WJ.size())];
    const json& js = SD()["JOB_STATION"]; b.station = js.contains(b.job) ? js[b.job].get<std::string>() : "";
    work(2, 2, 3); b.bedCol = jsFloor(rnd() * 4);
  }
  else if (t == "house_m" || t == "house_l") { b.beds.push_back(vSpot(b, 5, 2, 2)); b.beds.push_back(vSpot(b, 5, 5, 2)); b.bedCol = jsFloor(rnd() * 4); }
  else if (t == "hall") {
    b.beds.push_back(vSpot(b, 1, 8, 2)); b.beds.push_back(vSpot(b, 3, 8, 2)); work(8, 6, 3); b.elderAt = vSpot(b, 5, 4, 0); b.hasElder = true; b.bedCol = jsFloor(rnd() * 4);
    const char* FD[6] = {"ale_mug", "ale_mug", "bread", "vegetable_stew", "cooked_chicken", "ale_mug"};
    b.seats = {vSeat(b, 1, 2, 1, 1, 1, FD[0]), vSeat(b, 3, 2, 3, 3, 1, FD[1]), vSeat(b, 1, 5, 1, 1, 4, FD[2]), vSeat(b, 3, 5, 3, 3, 4, FD[3]), vSeat(b, 4, 7, 1, 4, 6, FD[4]), vSeat(b, 6, 7, 3, 6, 6, FD[5])};
  }
  else if (t == "tavern") {
    work(4, 5, 0); b.beds.push_back(vSpot(b, 7, 6, 2)); b.bedCol = jsFloor(rnd() * 4);
    b.seats = {vSeat(b, 1, 2, 1, 1, 1, "ale_mug"), vSeat(b, 3, 2, 3, 3, 1, "ale_mug"), vSeat(b, 5, 2, 1, 5, 1, "bread"), vSeat(b, 7, 2, 3, 7, 1, "ale_mug")};
  }
  else if (t == "store") { work(3, 4, 0); b.beds.push_back(vSpot(b, 5, 5, 2)); b.bedCol = jsFloor(rnd() * 4); }
  else if (t == "library") { b.job = "librarian"; work(4, 5, 2); }
  else if (t == "smithy") { static const char* J[3] = {"armorer", "weaponsmith", "toolsmith"}; b.job = J[jsFloor(rnd() * 3)]; work(4, 3, 0); }
  else if (t == "chapel") work(4, 10, 0);
  else if (t == "council") work(5, 9, 0);
  else if (t == "tower" || t == "sage") work(3, 3, 0);
  else if (t == "farm") {
    static const char* C[4] = {"wheat_crop", "carrot_crop", "potato_crop", "beetroot_crop"};
    b.job = "farmer"; work(4, -1, 2); b.crop = C[jsFloor(rnd() * 4)]; b.crop2 = rnd() < 0.5 ? "wheat_crop" : b.crop;
  }
  else if (t == "pen") { static const char* A[4] = {"sheep", "cow", "pig", "chicken"}; b.animals = A[jsFloor(rnd() * 4)]; auto w = vWorld(b, 4, 1, 2); b.mid = {w[0], w[1], w[2], 0}; b.hasMid = true; }
  (void)st;
}

// ---- the buildings ----
static const char* CARPETS[4] = {"red_carpet", "brown_carpet", "green_carpet", "blue_carpet"};

static void bHouseS(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  HouseOpt o; o.W = 7; o.D = 6; o.floors = 1; o.thatch = b.tier == TIER_VILLAGE; o.chimney = 3; o.winBack = std::vector<int>{1, 5};
  vHouse(S, o);
  S.door(3, 0, st.door);
  S.bed(5, 3, 2, st.bed[b.bedCol % st.bed.size()]);
  S.set(1, 1, 1, "crafting_table");
  if (!b.station.empty()) S.face(1, 1, 2, b.station, 1);
  S.chest(1, 1, 4, 1, "house");
  S.table(5, 1); S.chair(4, 1, 1); S.pile(5, 2, 1, {S.pick("food"), "", S.pick("food"), ""});
  S.shelf(1, 2, 3, 1, {S.pick("goods"), "bowl", S.pick("food")});
  S.rug(2, 2, 4, 2, CARPETS[b.bedCol % 4]);
  S.torch(2, 3, 4, 2); S.torch(4, 3, 1, 0);
  S.porch(3); S.flower(0, -1); S.flower(6, -1);
}
static void bWorkshop(VS& S) {
  bHouseS(S);
  const VStyle& st = S.st;
  // the trade spills outside: barrels and a crate by the wall, a lantern on a post
  S.set(7, 1, 1, "barrel"); S.set(7, 1, 2, "barrel"); S.set(7, 2, 1, "barrel"); S.pile(7, 1, 3, {S.pick("tools"), S.pick("goods"), "", ""});
  S.set(-1, 1, 1, st.fence); S.set(-1, 2, 1, "lantern");
}
static void bHouseM(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b; const bool village = b.tier == TIER_VILLAGE;
  HouseOpt o; o.W = 7; o.D = 8; o.floors = 2; o.jetty = !village; o.stoneGround = !village; o.chimney = 3; o.ladder = std::array<int, 2>{1, 6}; o.winBack = std::vector<int>{1, 5};
  vHouse(S, o);
  S.door(3, 0, st.door);
  S.bed(5, 2, 2, st.bed[b.bedCol % st.bed.size()]); S.bed(5, 5, 2, st.bed[(b.bedCol + 1) % st.bed.size()]);
  S.set(1, 1, 1, "crafting_table"); S.chest(1, 1, 2, 1, "house");
  S.table(3, 4); S.chair(2, 4, 1); S.chair(4, 4, 3); S.pile(3, 2, 4, {S.pick("food"), S.pick("drink"), S.pick("food"), ""});
  S.standShelf(1, 4, 1, {S.pick("goods"), "bowl", S.pick("food"), S.pick("goods"), "", S.pick("drink")});
  S.torch(2, 3, 6, 2); S.torch(4, 3, 1, 0);
  // upstairs: a bedroom and a little study
  S.bedAt(5, 5, 1, 2, st.bed[(b.bedCol + 2) % st.bed.size()]); S.chest(5, 5, 5, 3, "house");
  S.bookcase(3, 5, 6, 0, 4); S.table(2, 2, 5); S.pile(2, 6, 2, {S.pick("desk"), "", "candle", ""}); S.chair(2, 3, 0, 5);
  S.rug(2, 0, 4, 4, CARPETS[(b.bedCol + 1) % 4], 5);
  S.torch(1, 7, 2, 3); S.torch(5, 7, 4, 1);
  S.porch(3); S.flower(0, -1); S.flower(6, -1);
}
static void bHouseL(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  HouseOpt o; o.W = 7; o.D = 8; o.floors = 3; o.jetty = true; o.stoneGround = true; o.chimney = 3; o.ladder = std::array<int, 2>{1, 6}; o.winBack = std::vector<int>{1, 5};
  vHouse(S, o);
  S.door(3, 0, st.door);
  S.bed(5, 2, 2, st.bed[b.bedCol % st.bed.size()]); S.bed(5, 5, 2, st.bed[(b.bedCol + 1) % st.bed.size()]);
  S.set(1, 1, 1, "crafting_table"); S.chest(1, 1, 2, 1, "house");
  S.table(3, 4); S.chair(2, 4, 1); S.chair(4, 4, 3); S.pile(3, 2, 4, {S.pick("food"), S.pick("drink"), "", S.pick("food")});
  S.torch(2, 3, 6, 2);
  S.bedAt(5, 5, 1, 2, st.bed[(b.bedCol + 2) % st.bed.size()]); S.bookcase(3, 5, 6, 0, 5); S.standShelf(5, 4, 3, {S.pick("goods"), S.pick("goods"), "", S.pick("drink"), "bowl", ""}, 5);
  S.table(2, 2, 5); S.chair(2, 3, 0, 5); S.pile(2, 6, 2, {S.pick("desk"), "candle", "", ""}); S.torch(1, 7, 2, 3);
  S.set(5, 9, 5, "barrel"); S.set(5, 9, 4, "barrel"); S.set(5, 10, 5, "barrel"); S.chest(3, 9, 6, 0, "house"); S.set(2, 9, 1, "hay_bale"); S.torch(1, 11, 2, 3);
  S.porch(3); S.flower(0, -1); S.flower(6, -1);
}
static void bHall(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  HouseOpt o; o.W = 11; o.D = 11; o.floors = 2; o.jetty = true; o.stoneGround = true; o.chimney = 5; o.ladder = std::array<int, 2>{9, 1};
  o.winBack = std::vector<int>{1, 9}; o.winSide = std::vector<int>{2, 5};
  vHouse(S, o);
  S.set(9, 1, 1, "air"); for (int ly = 1; ly <= 4; ly++) S.set(9, ly, 1, "ladder", S.d(1));
  S.door(5, 0, st.door);
  // the bar down the east side, the kegs behind it, bottles on the shelves above
  const std::string slab = VS::VB(st.fwood + "_slab", st.slab);
  for (int z = 4; z <= 8; z++) if (z != 6) S.set(7, 1, z, slab, 1);
  S.face(7, 1, 6, !st.trapdoor.empty() ? st.trapdoor : slab, 3);
  for (int z = 4; z <= 8; z++) { S.set(9, 1, z, "barrel"); if (z % 2 == 0) S.set(9, 2, z, "barrel"); }
  S.shelf(9, 3, 5, 3, {S.pick("drink"), S.pick("drink"), S.pick("drink")}); S.shelf(9, 3, 7, 3, {"honey_bottle", S.pick("drink"), "glass_bottle"});
  S.pile(7, 2, 4, {"ale_mug", "ale_mug", "", "ale_mug"}); S.pile(7, 2, 8, {"bowl", "", "ale_mug", "bread"}); S.pile(7, 2, 5, {"", "ale_mug", "", ""});
  // three tables laid for supper
  for (auto [x, z] : {std::pair{2, 2}, {2, 5}, {5, 7}}) { S.table(x, z); S.pile(x, 2, z, {S.pick("food"), S.pick("drink"), S.pick("food"), S.pick("drink")}); }
  S.chair(1, 2, 1); S.chair(3, 2, 3); S.chair(1, 5, 1); S.chair(3, 5, 3); S.chair(4, 7, 1); S.chair(6, 7, 3);
  // the kitchen corner by the hearth, and the innkeepers' beds
  S.set(4, 1, 9, "cauldron"); S.face(6, 1, 9, "smoker", 0); S.pile(6, 2, 9, {"cooked_beef", "bread", "", ""});
  S.bed(1, 8, 2, st.bed[b.bedCol % st.bed.size()]); S.bed(3, 8, 2, st.bed[(b.bedCol + 2) % st.bed.size()]);
  S.chest(1, 1, 7, 1, "tavern"); S.shelf(1, 3, 3, 1, {"bread", "apple", "bowl"});
  S.rug(4, 1, 6, 5, "red_carpet");
  for (auto [x, z, wl] : {std::tuple{1, 4, 3}, {5, 9, 2}, {6, 1, 0}, {4, 1, 0}}) S.torch(x, 3, z, wl);
  S.set(5, 3, 3, "lantern", 1); S.set(2, 3, 7, "lantern", 1);
  // upstairs: rooms for travellers
  for (auto [x, z] : {std::pair{1, 0}, {1, 4}, {9, 4}, {9, 7}}) S.bedAt(x, 5, z, 2, st.bed[(b.bedCol + x + z) % st.bed.size()]);
  for (auto [x, z] : {std::pair{3, 1}, {3, 5}, {7, 5}}) { S.table(x, z, 5); S.set(x, 6, z, "candle"); }
  S.chest(1, 5, 8, 1, "tavern"); S.chest(5, 5, 9, 0, "house"); S.rug(4, 0, 6, 8, "brown_carpet", 5);
  for (auto [x, z, wl] : {std::tuple{1, 2, 3}, {9, 2, 1}, {5, 9, 2}}) S.torch(x, 7, z, wl);
  S.porch(5); S.flower(1, -1); S.flower(9, -1); S.set(-1, 1, 3, "barrel"); S.set(-1, 1, 4, "barrel"); S.set(11, 1, 3, "hay_bale");
}
static void bTavern(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  const std::string dk = VS::VB("dark_oak_planks", st.planks), dl = VS::VB("dark_oak_log", st.log);
  HouseOpt o; o.W = 9; o.D = 9; o.floors = 2; o.fill = dk; o.frame = dl; o.beam = VS::VB("stripped_dark_oak_log", dl);
  o.roof = std::array<std::string, 2>{VS::VB("dark_roof_tile_stairs", st.stairs), VS::VB("dark_roof_tile_slab", st.slab)};
  o.ladder = std::array<int, 2>{1, 7}; o.winFront = std::vector<int>{2, 6}; o.winSide = std::vector<int>{3}; o.winBack = std::vector<int>{}; o.floorKey = VS::VB("spruce_planks", st.planks);
  vHouse(S, o);
  S.door(4, 0, VS::VB("dark_oak_door", st.door));
  const std::string slab = VS::VB("dark_oak_slab", st.slab);
  for (int x = 2; x <= 6; x++) if (x != 4) S.set(x, 1, 4, slab, 1);
  S.face(4, 1, 4, VS::VB("dark_oak_trapdoor", !st.trapdoor.empty() ? st.trapdoor : slab), 0);
  S.pile(2, 2, 4, {"ale_mug", "", "ale_mug", ""}); S.pile(5, 2, 4, {"", "ale_mug", "", "glass_bottle"});
  S.set(2, 1, 6, "barrel"); S.set(3, 1, 6, "barrel"); S.set(2, 2, 6, "barrel"); S.set(2, 1, 7, "barrel"); S.set(3, 1, 7, "barrel");
  S.shelf(3, 3, 7, 0, {S.pick("drink"), "glass_bottle", S.pick("drink")}); S.shelf(5, 3, 7, 0, {"honey_bottle", S.pick("drink"), ""});
  S.set(5, 0, 6, VS::VB("dark_oak_trapdoor", !st.trapdoor.empty() ? st.trapdoor : st.planks), 0);
  S.bed(7, 6, 2, st.bed[b.bedCol % st.bed.size()]);
  for (auto [x, z] : {std::pair{2, 2}, {6, 2}}) { S.table(x, z); S.pile(x, 2, z, {S.pick("drink"), "ale_mug", "", S.pick("food")}); }
  S.chair(1, 2, 1); S.chair(3, 2, 3); S.chair(5, 2, 1); S.chair(7, 2, 3);
  S.set(4, 3, 2, "lantern", 1); S.torch(1, 3, 4, 3); S.torch(7, 3, 4, 1);
  S.chest(1, 1, 5, 1, "tavern");
  S.bedAt(6, 5, 2, 2, st.bed[(b.bedCol + 1) % st.bed.size()]); S.table(3, 3, 5); S.set(3, 6, 3, "candle"); S.chest(6, 5, 6, 3, "tavern"); S.torch(4, 7, 7, 2);
  S.frontPath(4); S.set(3, 2, -1, "lantern", 1); S.set(5, 2, -1, "lantern", 1);
}
static void bStore(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  HouseOpt o; o.W = 7; o.D = 8; o.floors = 2; o.jetty = b.tier != TIER_VILLAGE; o.stoneGround = true; o.chimney = 3; o.ladder = std::array<int, 2>{4, 6}; o.winBack = std::vector<int>{1, 5};
  vHouse(S, o);
  S.door(3, 0, st.door);
  const std::string slab = VS::VB(st.fwood + "_slab", st.slab);
  for (int x : {1, 2, 4, 5}) S.set(x, 1, 3, slab, 1);
  S.face(3, 1, 3, !st.trapdoor.empty() ? st.trapdoor : slab, 0);
  const char *goods = "goods", *more = "goods";   // (the original's apothecary decor is never chosen)
  S.standShelf(1, 1, 1, {S.pick(goods), S.pick(goods), S.pick(more), S.pick(goods), S.pick(more), S.pick(goods)});
  S.standShelf(5, 1, 3, {S.pick(more), S.pick(goods), S.pick(goods), S.pick(goods), S.pick(goods), S.pick(more)});
  S.standShelf(1, 5, 1, {S.pick(goods), S.pick(goods), S.pick(goods), S.pick(more), S.pick(goods), S.pick(goods)});
  S.standShelf(1, 6, 1, {S.pick(more), S.pick(more), S.pick(goods), S.pick(goods), S.pick(more), S.pick(goods)});
  S.set(2, 1, 6, "barrel"); S.bed(5, 5, 2, st.bed[b.bedCol % st.bed.size()]);
  S.pile(1, 2, 3, {S.pick("goods"), "gold_coin", "", S.pick("goods")}); S.pile(5, 2, 3, {S.pick("goods"), "", S.pick("goods"), ""});
  S.set(-1, 1, 1, "barrel"); S.set(-1, 1, 2, "hay_bale"); S.set(7, 1, 1, "barrel"); S.pile(7, 1, 2, {S.pick("goods"), S.pick("goods"), "", ""});
  S.torch(1, 3, 2, 3); S.torch(5, 3, 2, 1); S.set(3, 3, 1, "lantern", 1);
  // the awning over the shop window
  const std::vector<std::string> aw = !st.awning.empty() ? st.awning : std::vector<std::string>{"wool"};
  if (!st.flat) for (int x = 0; x <= 6; x++) S.set(x, 3, -1, aw[x % aw.size()]);
  S.frontPath(3);
  // upstairs
  S.bedAt(1, 5, 1, 2, st.bed[(b.bedCol + 1) % st.bed.size()]); S.table(4, 2, 5); S.chair(5, 2, 3, 5); S.pile(4, 6, 2, {S.pick("food"), "candle", "", ""});
  S.chest(5, 5, 5, 3, "house"); S.shelf(1, 6, 4, 1, {S.pick(goods), S.pick(more), ""}); S.torch(3, 7, 6, 2);
}
static void bLibrary(VS& S) {
  const VStyle& st = S.st;
  const std::string sb = VS::VB("stone_bricks", st.stone);
  HouseOpt o; o.W = 9; o.D = 9; o.floors = 2; o.allStone = true; o.stone = sb; o.fill = sb; o.corner = VS::VB("chiseled_stone_bricks", sb); o.ladder = std::array<int, 2>{7, 7};
  o.winFront = std::vector<int>{2, 6}; o.winSide = std::vector<int>{4}; o.winBack = std::vector<int>{2, 6}; o.glass = "glass_pane";
  vHouse(S, o);
  S.door(4, 0, st.door);
  for (int z : {2, 3, 5, 6}) { S.bookcase(1, 1, z, 1); S.bookcase(1, 2, z, 1, 5); }
  for (int z = 2; z <= 6; z++) { S.bookcase(7, 1, z, 3); S.bookcase(7, 2, z, 3, 4); }
  S.book(4, 1, 6, 0);
  S.table(3, 2); S.table(5, 2); S.chair(3, 3, 0); S.chair(5, 3, 0);
  S.pile(3, 2, 2, {S.pick("desk"), S.pick("desk"), "", "book"}); S.pile(5, 2, 2, {"candle", S.pick("desk"), S.pick("desk"), ""});
  S.rug(4, 1, 4, 4, "blue_carpet");
  S.chest(1, 1, 7, 1, "library");
  for (auto [x, z, wl] : {std::tuple{2, 1, 0}, {6, 1, 0}, {4, 7, 2}}) S.torch(x, 3, z, wl);
  // upstairs: the reading room
  for (int z : {1, 3, 5}) { S.bookcase(1, 5, z, 1); S.bookcase(7, 5, z, 3); }
  S.table(4, 4, 5); S.pile(4, 6, 4, {S.pick("desk"), "candle", S.pick("desk"), ""}); S.chair(4, 5, 0, 5); S.book(4, 5, 1, 2);
  S.torch(4, 7, 7, 2); S.torch(4, 7, 1, 0);
  S.frontPath(4); S.flower(1, -1); S.flower(7, -1);
}
static void bSmithy(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  const std::string stone = !st.base3.empty() ? st.base3 : st.stone, rough = !st.base2.empty() ? st.base2 : st.stone;
  const std::string frameK = !st.frame.empty() ? st.frame : st.log, beamK = !st.beamH.empty() ? st.beamH : st.log;
  S.floor(9, 8, rough);
  for (int ly = 1; ly <= 3; ly++) {
    for (int lz = 1; lz < 8; lz++) { S.set(0, ly, lz, lz == 7 ? stone : rough); S.set(8, ly, lz, lz == 7 ? stone : rough); }
    for (int lx = 1; lx < 8; lx++) S.set(lx, ly, 7, rough);
    S.log(0, ly, 0, frameK, 0); S.log(8, ly, 0, frameK, 0);
    for (int lx = 1; lx < 8; lx++) for (int lz = 0; lz < 7; lz++) S.set(lx, ly, lz, "air");
  }
  for (int lz = 0; lz <= 7; lz++) for (int lx = 0; lx <= 8; lx++) if (lx == 0 || lx == 8 || lz == 0 || lz == 7) S.log(lx, 4, lz, beamK, lz == 0 || lz == 7 ? 1 : 2);
  const auto& roof = st.roofs[b.roofI % st.roofs.size()];
  int top;
  if (st.flat) { S.fill(0, 4, 0, 8, 4, 7, st.roof); top = 5; } else top = S.roofG(9, 0, 7, 4, roof[0], roof[1], rough, "");
  S.chimney(2, 8, top, 9);
  // the forge: furnaces, a lava trough under a grate, the anvil, the quenching barrel
  S.face(3, 1, 6, "furnace", 0); S.face(4, 1, 6, "furnace", 0); S.face(5, 1, 6, VS::VB("blast_furnace", "furnace"), 0);
  S.set(6, 0, 6, "lava"); S.set(6, 1, 6, "iron_bars");
  const json& js = SD()["JOB_STATION"];
  S.set(4, 1, 2, "anvil"); S.face(7, 1, 3, js.contains(b.job) ? js[b.job].get<std::string>() : std::string("smithing_table"), 3); S.set(7, 1, 5, "barrel"); S.set(6, 1, 4, "cauldron"); S.set(2, 1, 4, VS::VB("grindstone", "stonecutter"));
  S.chest(1, 1, 1, 1, "smithy");
  // weapons on the walls, shields and helms over the forge
  for (int z : {3, 4, 5}) S.hang(1, 2, z, 1, S.pick("weapons"), 2);
  for (int z : {3, 4, 5, 6}) S.hang(1, 3, z, 1, S.pick("weapons"), 2);
  for (int x : {3, 4, 5}) S.hang(x, 3, 6, 0, x == 4 ? S.pick("shields") : S.pick("helm"), 0);
  for (int z : {4, 5}) S.hang(7, 2, z, 3, S.pick("weapons"), 2);
  // armour on stands, a table of blades
  S.armourStand(7, 1); S.armourStand(7, 2);
  S.table(5, 2); S.pile(5, 2, 2, {S.pick("weapons"), S.pick("weapons"), S.pick("weapons"), S.pick("tools")});
  S.torch(1, 3, 2, 3); S.torch(5, 3, 6, 2);
  S.set(-1, 1, 2, "hay_bale"); S.set(9, 1, 1, st.log, 1); S.set(9, 1, 2, st.log, 1); S.set(9, 2, 1, st.log, 1);
  S.frontPath(4);
}
static void bChapel(VS& S) {
  const VStyle& st = S.st;
  const std::string sb = VS::VB("stone_bricks", st.stone), ch = VS::VB("chiseled_stone_bricks", sb), mo = VS::VB("mossy_stone_bricks", sb);
  S.floor(9, 13, sb);
  for (int ly = 1; ly <= 6; ly++) for (int lz = 0; lz < 13; lz++) for (int lx = 0; lx < 9; lx++) {
    const bool ex = lx == 0 || lx == 8, ez = lz == 0 || lz == 12;
    if (!ex && !ez) { S.set(lx, ly, lz, "air"); continue; }
    S.set(lx, ly, lz, ex && ez ? ch : ((lx * 3 + lz + ly) % 9 == 0 ? mo : sb));
  }
  const std::array<std::string, 2>* roof = &st.roofs[0];
  for (const auto& rr : st.roofs) if (rr[0].find("slate") != std::string::npos || rr[0].find("dark_roof") != std::string::npos) { roof = &rr; break; }
  if (st.flat) S.fill(0, 7, 0, 8, 7, 12, st.roof); else S.roofG(9, 0, 12, 7, (*roof)[0], (*roof)[1], sb, "");
  // stained glass down both sides and a rose window over the altar
  std::vector<std::string> glass;
  for (const char* k : {"yellow_stained_glass_pane", "blue_stained_glass_pane", "red_stained_glass_pane", "purple_stained_glass_pane"}) if (hasBlock(k)) glass.push_back(k);
  auto gl = [&](int i) { return glass.empty() ? std::string("glass_pane") : glass[i % glass.size()]; };
  for (int z : {4, 7, 10}) for (int ly = 2; ly <= 4; ly++) { S.set(0, ly, z, gl(z + ly)); S.set(8, ly, z, gl(z + ly + 1)); }
  for (auto [x, y] : {std::pair{4, 4}, {3, 5}, {5, 5}, {4, 6}, {4, 5}}) S.set(x, y, 12, gl(x + y));
  // the bell tower, standing on the front wall
  for (int ly = 1; ly <= 6; ly++) { S.set(3, ly, 1, sb); S.set(5, ly, 1, sb); }
  for (int ly = 7; ly <= 15; ly++) for (int lz = 0; lz <= 2; lz++) for (int lx = 3; lx <= 5; lx++) {
    if (lx == 4 && lz == 1) { S.set(lx, ly, lz, "air"); continue; }
    const bool open = ly >= 13 && ly <= 14 && (lx == 4 || lz == 1);
    S.set(lx, ly, lz, open ? std::string("air") : sb);
  }
  S.fill(3, 7, 0, 5, 7, 2, sb); S.set(4, 7, 1, sb); S.set(4, 12, 1, sb); S.set(4, 13, 1, "bell");
  S.hipRoof(4, 1, 2, 16, (*roof)[0], (*roof)[1], sb);
  S.set(4, 19, 1, VS::VB("end_rod", st.fence)); S.set(4, 20, 1, "lantern");
  S.door(4, 0, st.door);
  // pews facing the altar, candles, the altar itself
  const std::string pew = VS::VB(st.fwood + "_bench", st.stairs);
  for (int z : {4, 6, 8}) for (int x : {1, 2, 3, 5, 6, 7}) S.face(x, 1, z, pew, 2);
  S.set(4, 1, 11, ch); S.set(4, 2, 11, "candle"); S.set(3, 1, 11, "candle"); S.set(5, 1, 11, "candle");
  S.pile(4, 3, 11, {"", "book", "", ""});
  S.rug(4, 2, 4, 10, "red_carpet");
  // banners either side of the altar
  const auto& ban = st.banner;
  for (int ly = 2; ly <= 5; ly++) { S.set(2, ly, 11, ban[0]); S.set(6, ly, 11, ban[ban.size() - 1]); }
  for (auto [x, z, wl] : {std::tuple{1, 3, 3}, {7, 3, 1}, {1, 9, 3}, {7, 9, 1}}) S.torch(x, 4, z, wl);
  S.torch(4, 3, 11, 2);
  S.plant(1, 1, 11); S.plant(7, 1, 11);
  S.porch(4);
}
static void bCouncil(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  const std::string sb = VS::VB("stone_bricks", st.stone);
  HouseOpt o; o.W = 11; o.D = 11; o.floors = 2; o.stoneGround = true; o.ladder = std::array<int, 2>{9, 9};
  o.winSide = std::vector<int>{3, 7}; o.winBack = std::vector<int>{2, 8}; o.winFront = std::vector<int>{2, 8};
  vHouse(S, o);
  S.door(5, 0, st.door);
  for (int z = 3; z <= 7; z++) { S.table(5, z); S.chair(4, z, 1); S.chair(6, z, 3); }
  S.chair(5, 8, 0);
  for (int z : {3, 5, 7}) S.pile(5, 2, z, {S.pick("desk"), S.pick("desk"), "", S.pick("desk")});
  S.pile(5, 2, 4, {"candle", "", "", "book"}); S.pile(5, 2, 6, {"", "paper", "candle", ""});
  S.rug(2, 1, 2, 9, "red_carpet"); S.rug(8, 1, 8, 9, "red_carpet");
  for (int x : {1, 3, 7}) S.bookcase(x, 1, 9, 0);
  const auto& ban = st.banner;
  for (int ly = 2; ly <= 3; ly++) for (int z : {2, 5, 8}) { S.set(1, ly, z, ban[0]); S.set(9, ly, z, ban[ban.size() - 1]); }
  S.hang(5, 3, 9, 0, hasItem("empty_map") ? "empty_map" : "paper");
  for (auto [x, z, wl] : {std::tuple{1, 1, 3}, {9, 1, 1}, {5, 9, 2}}) S.torch(x, 3, z, wl);
  S.set(5, 3, 2, "lantern", 1); S.set(5, 3, 6, "lantern", 1);
  // the archive upstairs
  for (int x : {1, 3, 7}) { S.bookcase(x, 5, 9, 0); S.bookcase(x, 6, 9, 0, 4); }
  S.chest(1, 5, 1, 1, "library"); S.chest(9, 5, 1, 3, "library"); S.table(5, 4, 5); S.pile(5, 6, 4, {S.pick("desk"), S.pick("desk"), "candle", ""}); S.torch(5, 7, 1, 0);
  // the clock tower over the front gable
  for (int ly = 9; ly <= 16; ly++) for (int lz = 0; lz <= 2; lz++) for (int lx = 4; lx <= 6; lx++) if (lx != 5 || lz != 1) S.set(lx, ly, lz, sb);
  const std::string q = VS::VB("quartz_block", sb);
  S.set(5, 14, 0, VS::VB("chiseled_quartz_block", "glowstone")); S.set(4, 14, 0, q); S.set(6, 14, 0, q); S.set(5, 15, 0, q); S.set(5, 13, 0, q);
  S.set(5, 12, 1, "bell");
  const auto& roof = st.roofs[b.roofI % st.roofs.size()];
  if (!st.flat) S.hipRoof(5, 1, 2, 17, roof[0], roof[1], sb);
  S.porch(5); S.flower(1, -1); S.flower(9, -1);
}
static int townWind(const Town& v) { return v.wind; }
static std::string clothKey(const std::string& wool, const char* kind) {
  std::string c = wool.empty() || wool == "wool" ? "white" : wool;
  if (c.size() > 5 && c.compare(c.size() - 5, 5, "_wool") == 0) c.erase(c.size() - 5);
  const std::string k = c + "_" + kind;
  return hasBlock(k) ? k : std::string("white_") + kind;
}
static void bTower(VS& S) {
  const VStyle& st = S.st;
  const std::string sb = VS::VB("stone_bricks", st.stone), cr = VS::VB("cracked_stone_bricks", sb), slit = VS::VB("iron_bars", "glass_pane");
  S.floor(7, 7, !st.base2.empty() ? st.base2 : st.stone);
  for (int ly = 1; ly <= 14; ly++) for (int lz = 0; lz < 7; lz++) for (int lx = 0; lx < 7; lx++) {
    const bool edge = lx == 0 || lx == 6 || lz == 0 || lz == 6;
    if (!edge) { S.set(lx, ly, lz, ly == 5 || ly == 10 ? st.planks : std::string("air")); continue; }
    const bool isSlit = (ly == 7 || ly == 12) && (lx == 3 || lz == 3);
    S.set(lx, ly, lz, isSlit ? slit : ((lx + ly + lz) % 7 == 0 ? cr : sb));
  }
  S.door(3, 0, st.door);
  S.fill(-1, 15, -1, 7, 15, 7, sb);
  for (int lz = -1; lz <= 7; lz++) for (int lx = -1; lx <= 7; lx++) if (lx == -1 || lx == 7 || lz == -1 || lz == 7) { S.set(lx, 16, lz, (lx + lz) % 2 == 0 ? sb : std::string("air")); S.set(lx, 14, lz, VS::VB("stone_brick_stairs", sb), 0); }
  for (int ly = 1; ly <= 15; ly++) S.set(3, ly, 5, "ladder", S.d(2));
  S.set(3, 16, 5, "air");
  // the guardroom
  for (int z : {1, 2, 3}) S.hang(5, 2, z, 3, z == 2 ? S.pick("shields") : S.pick("weapons"), 2);
  S.set(5, 1, 5, "barrel"); S.set(5, 2, 5, "barrel"); S.chest(1, 1, 5, 1, "smithy"); S.set(5, 1, 4, "crafting_table");
  S.table(1, 4); { std::string wp = S.pick("weapons"); S.pile(1, 2, 4, {"paper", wp, "", "ale_mug"}); }
  S.torch(1, 3, 3, 3); S.torch(5, 3, 3, 1); S.torch(3, 3, 1, 0);
  // bunks upstairs, the armoury above that
  S.bedAt(1, 6, 1, 2, st.bed[0]); S.bedAt(5, 6, 1, 2, st.bed[0]); S.chest(1, 6, 5, 1, "smithy"); S.torch(1, 8, 3, 3);
  for (int z : {1, 2, 3}) S.hang(1, 12, z, 1, S.pick("weapons"), 2);
  S.set(5, 11, 4, "barrel"); S.set(5, 11, 5, "barrel"); S.torch(5, 13, 3, 1);
  // the lookout: lanterns and the town's banner, a flag in the wind (towns3.js, alpha 3)
  S.set(1, 16, 1, "lantern"); S.set(5, 16, 5, "lantern");
  S.set(5, 16, 1, st.fence); S.set(5, 17, 1, st.fence); S.set(5, 18, 1, st.fence);
  const int wd = townWind(S.v), ld = (wd - S.r + 4) & 3;
  S.set(5, 19, 1, st.fence); S.set(5 + DXL[ld], 19, 1 + DZL[ld], clothKey(st.banner[0], "flag"), wd);
  S.frontPath(3);
}
static void bSage(VS& S) {
  const VStyle& st = S.st;
  const std::string sb = VS::VB("stone_bricks", st.stone), mo = VS::VB("mossy_stone_bricks", sb);
  auto wall = [](int lx, int lz) { return (lx == 0 || lx == 6) ? lz >= 2 && lz <= 4 : (lz == 0 || lz == 6) ? lx >= 2 && lx <= 4 : (lx == 1 || lx == 5) && (lz == 1 || lz == 5); };
  auto inside = [&](int lx, int lz) { return lx >= 1 && lx <= 5 && lz >= 1 && lz <= 5 && !wall(lx, lz); };
  S.fill(1, 0, 1, 5, 0, 5, sb); S.fill(0, 0, 2, 6, 0, 4, sb); S.fill(2, 0, 0, 4, 0, 6, sb);
  for (int ly = 1; ly <= 10; ly++) for (int lz = 0; lz < 7; lz++) for (int lx = 0; lx < 7; lx++) {
    if (wall(lx, lz)) S.set(lx, ly, lz, (lx * 5 + lz * 3 + ly) % 4 == 0 ? mo : sb);
    else if (inside(lx, lz)) S.set(lx, ly, lz, ly == 5 ? st.planks : std::string("air"));
  }
  S.door(3, 0, st.door);
  for (auto [x, z, o] : {std::tuple{0, 3, 3}, {6, 3, 1}, {3, 6, 2}}) { S.win(x, 2, z, o); S.win(x, 7, z, o); }
  S.win(3, 7, 0, 0);
  const std::string rs = VS::VB("dark_prismarine_stairs", VS::VB("prismarine_brick_stairs", st.roofs[0][0])), rsl = VS::VB("dark_prismarine_slab", VS::VB("prismarine_brick_slab", st.roofs[0][1]));
  S.hipRoof(3, 3, 4, 11, rs, rsl, "");
  S.set(3, 16, 3, VS::VB("end_rod", "lantern")); S.set(3, 17, 3, VS::VB("amethyst_cluster", "lantern"));
  for (int ly = 1; ly <= 5; ly++) S.set(3, ly, 5, "ladder", S.d(2));
  // the study: shelves of books, a table of crystals, the enchanting table, the lectern
  for (auto [x, z, f] : {std::tuple{1, 2, 1}, {1, 4, 1}, {5, 2, 3}, {5, 4, 3}, {2, 5, 0}, {4, 5, 0}}) { S.bookcase(x, 1, z, f); S.bookcase(x, 2, z, f, 4); }
  S.set(3, 1, 4, VS::VB("enchanting_table", "lectern"));
  S.book(1, 1, 3, 1); S.standShelf(5, 3, 3, {S.pick("magic"), S.pick("potions"), S.pick("magic"), S.pick("potions"), S.pick("magic"), S.pick("potions")});
  S.set(2, 1, 1, VS::VB("amethyst_cluster", "candle")); S.set(4, 1, 1, "candle");
  S.set(3, 4, 3, "lantern", 1);
  // upstairs: his cot, star charts on the walls, a crystal on the desk
  S.bedAt(1, 6, 2, 2, st.bed[0]); S.table(5, 3, 6); S.pile(5, 7, 3, {S.pick("magic"), "candle", S.pick("desk"), ""}); S.chair(4, 3, 1, 6);
  S.hang(3, 7, 5, 0, hasItem("empty_map") ? "empty_map" : "paper"); S.hang(5, 8, 3, 3, hasItem("clock") ? "clock" : "compass");
  S.set(3, 9, 3, "lantern", 1); S.chest(4, 6, 1, 0, "library");
  // chimes and ivy outside
  for (auto [x, z] : {std::pair{-1, 3}, {7, 3}, {3, -1}, {3, 7}}) { S.set(x, 10, z, "chain"); S.set(x, 9, z, VS::VB("amethyst_cluster", "lantern")); }
  if (hasBlock("vine")) for (auto [x, z] : {std::pair{-1, 2}, {-1, 4}, {7, 2}, {7, 4}, {2, 7}, {4, 7}}) for (int ly = 2; ly <= 8; ly++) if ((ly + x + z) % 3) S.set(x, ly, z, "vine", 0);
  S.porch(3);
}
static void bFarm(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  for (int lz = 0; lz < 9; lz++) for (int lx = 0; lx < 9; lx++) {
    const bool edge = lx == 0 || lx == 8 || lz == 0 || lz == 8;
    if (edge) { S.set(lx, 0, lz, "dirt"); if (!(lz == 0 && lx == 4)) S.set(lx, 1, lz, st.fence); continue; }
    if (lz == 4) { S.set(lx, 0, lz, "water"); S.set(lx, -1, lz, "dirt"); continue; }
    S.set(lx, 0, lz, "farmland", 0);
    const std::string& crop = lx < 4 ? b.crop : b.crop2;
    const BlockId cid = bid(crop);
    int stages = 4;
    if (cid != NOBLOCK) { const auto& def = reg().block(cid); if (def.props.contains("crop") && def.props["crop"].contains("stages")) stages = def.props["crop"]["stages"].get<int>(); }
    const int m = std::min(stages - 1, jsFloor(S.rnd() * stages));
    S.set(lx, 1, lz, crop, m);
  }
  S.face(4, 1, 0, st.gate, 0);
  // the scarecrow
  S.set(1, 1, 1, st.fence); S.set(1, 2, 1, "hay_bale"); S.set(1, 3, 1, VS::VB("jack_o_lantern", "hay_bale"), S.d(2) & 3);
  S.set(7, 1, 7, "composter"); S.set(7, 1, 1, "hay_bale");
}
static void bPen(VS& S) {
  const VStyle& st = S.st;
  S.fill(0, 0, 0, 8, 0, 6, "grass");
  for (int lz = 0; lz < 7; lz++) for (int lx = 0; lx < 9; lx++) if (lx == 0 || lz == 0 || lx == 8 || lz == 6) S.set(lx, 1, lz, st.fence);
  S.face(4, 1, 0, st.gate, 0);
  for (int x : {1, 7}) { S.log(x, 1, 5, st.log, 0); S.log(x, 2, 5, st.log, 0); }
  S.fill(0, 3, 4, 8, 3, 6, st.slab);
  S.set(2, 1, 5, "hay_bale"); S.set(3, 1, 5, "hay_bale"); S.set(2, 2, 5, "hay_bale"); S.set(6, 1, 5, "cauldron");
  S.set(-1, 1, 1, st.fence); S.set(-1, 2, 1, "lantern");
}
static void bStable(VS& S) {
  const VStyle& st = S.st;
  S.floor(9, 7, st.planks);
  for (int ly = 1; ly <= 3; ly++) {
    for (int lz = 0; lz < 7; lz++) { S.set(0, ly, lz, lz % 3 == 0 ? st.log : st.planks); S.set(8, ly, lz, lz % 3 == 0 ? st.log : st.planks); }
    for (int lx = 1; lx < 8; lx++) S.set(lx, ly, 6, lx % 3 == 0 ? st.log : st.planks);
    for (int lx = 1; lx < 8; lx++) for (int lz = 0; lz < 6; lz++) S.set(lx, ly, lz, "air");
  }
  for (int x : {3, 5}) for (int lz = 2; lz <= 5; lz++) S.set(x, 1, lz, st.fence);
  for (int x : {1, 4, 7}) { S.set(x, 1, 5, "hay_bale"); S.set(x, 1, 1, "cauldron"); }
  S.set(2, 1, 5, "hay_bale"); S.set(2, 2, 5, "hay_bale");
  const auto& roof = st.thatch;
  for (int lz = 0; lz <= 6; lz++) for (int lx = 0; lx <= 8; lx++) if (lx == 0 || lx == 8 || lz == 0 || lz == 6) S.log(lx, 4, lz, !st.beamH.empty() ? st.beamH : st.log, lz == 0 || lz == 6 ? 1 : 2);
  if (!st.flat) S.roofG(9, 0, 6, 4, roof[0], roof[1], st.planks, st.frame); else S.fill(0, 4, 0, 8, 4, 6, st.roof);
  S.hang(4, 3, 5, 0, hasItem("saddle") ? "saddle" : hasItem("lead") ? "lead" : "leather"); S.set(4, 3, 3, "lantern", 1);
}
static void bWindmill(VS& S) {
  const VStyle& st = S.st;
  const std::string sb = !st.base2.empty() ? st.base2 : st.stone;
  auto wall = [](int lx, int lz) { return (lx == 0 || lx == 6) ? lz >= 2 && lz <= 4 : (lz == 0 || lz == 6) ? lx >= 2 && lx <= 4 : (lx == 1 || lx == 5) && (lz == 1 || lz == 5); };
  auto inside = [&](int lx, int lz) { return lx >= 1 && lx <= 5 && lz >= 1 && lz <= 5 && !wall(lx, lz); };
  S.fill(1, 0, 1, 5, 0, 5, sb); S.fill(0, 0, 2, 6, 0, 4, sb); S.fill(2, 0, 0, 4, 0, 6, sb);
  for (int ly = 1; ly <= 10; ly++) for (int lz = 0; lz < 7; lz++) for (int lx = 0; lx < 7; lx++) {
    if (wall(lx, lz)) S.set(lx, ly, lz, ly <= 6 ? sb : st.planks);
    else if (inside(lx, lz)) S.set(lx, ly, lz, ly == 5 ? st.planks : std::string("air"));
  }
  S.door(3, 0, st.door);
  for (int ly = 1; ly <= 5; ly++) S.set(3, ly, 5, "ladder", S.d(2));
  S.win(0, 3, 3, 3); S.win(6, 3, 3, 1); S.win(3, 8, 0, 0);
  const auto& roof = st.thatch; S.hipRoof(3, 3, 4, 11, roof[0], roof[1], "");
  // the sails: a hub on the front, four arms with their cloth
  S.log(3, 9, -1, st.log, 2);
  for (int k = 1; k <= 3; k++) { S.set(3, 9 + k, -1, st.fence); S.set(3, 9 - k, -1, st.fence); S.set(3 + k, 9, -1, st.fence); S.set(3 - k, 9, -1, st.fence); }
  for (int k = 1; k <= 3; k++) { S.set(4, 9 + k, -1, "wool"); S.set(2, 9 - k, -1, "wool"); S.set(3 - k, 10, -1, "wool"); S.set(3 + k, 8, -1, "wool"); }
  S.set(2, 1, 2, "hay_bale"); S.set(4, 1, 2, "barrel"); S.set(2, 1, 4, "barrel"); S.set(3, 1, 3, VS::VB("stonecutter", "barrel"));
  S.pile(4, 1, 4, {"wheat", "bread", "wheat", ""});
  S.frontPath(3);
}
static void bMarket(VS& S) {
  const VStyle& st = S.st; const Bld& b = S.b;
  const std::string sb = VS::VB("stone_bricks", st.stone);
  S.floor(11, 7, !st.base2.empty() ? st.base2 : st.stone);
  for (int ly = 1; ly <= 3; ly++) for (int lz = 0; lz < 7; lz++) for (int lx = 0; lx < 11; lx++) S.set(lx, ly, lz, "air");
  for (auto [x, z] : {std::pair{0, 0}, {5, 0}, {10, 0}, {0, 3}, {10, 3}, {0, 6}, {5, 6}, {10, 6}}) for (int ly = 1; ly <= 3; ly++) S.set(x, ly, z, sb);
  for (int lz = 0; lz <= 6; lz++) for (int lx = 0; lx <= 10; lx++) if (lx == 0 || lx == 10 || lz == 0 || lz == 6) S.log(lx, 4, lz, !st.beamH.empty() ? st.beamH : st.log, lz == 0 || lz == 6 ? 1 : 2);
  const auto& roof = st.roofs[b.roofI % st.roofs.size()];
  if (!st.flat) S.roofG(11, 0, 6, 4, roof[0], roof[1], !st.plaster.empty() ? st.plaster : st.planks, st.frame); else S.fill(0, 4, 0, 10, 4, 6, st.roof);
  const std::string slab = VS::VB(st.fwood + "_slab", st.slab);
  for (auto [x0, kind] : {std::pair{2, "food"}, {7, "goods"}}) {
    for (int x = x0; x <= x0 + 2; x++) { S.set(x, 1, 2, slab, 1); S.set(x, 1, 4, "barrel"); }
    S.pile(x0, 2, 2, {S.pick(kind), S.pick(kind), S.pick(kind), ""}); S.pile(x0 + 2, 2, 2, {S.pick(kind), "", S.pick(kind), S.pick(kind)});
    S.set(x0 + 1, 2, 4, "hay_bale");
  }
  S.pile(5, 1, 3, {"apple", "carrot", "potato", "bread"});
  S.set(5, 3, 3, "lantern", 1); S.set(2, 3, 3, "lantern", 1); S.set(8, 3, 3, "lantern", 1);
}

void townBuildingBuild(SP& P, const Town& v, const Bld& b) {
  VS S(P, v, b);
  const std::string& t = b.type;
  if (t == "house_s") bHouseS(S); else if (t == "workshop") bWorkshop(S); else if (t == "house_m") bHouseM(S); else if (t == "house_l") bHouseL(S);
  else if (t == "hall") bHall(S); else if (t == "tavern") bTavern(S); else if (t == "store") bStore(S); else if (t == "library") bLibrary(S);
  else if (t == "smithy") bSmithy(S); else if (t == "chapel") bChapel(S); else if (t == "council") bCouncil(S); else if (t == "tower") bTower(S);
  else if (t == "sage") bSage(S); else if (t == "farm") bFarm(S); else if (t == "pen") bPen(S); else if (t == "stable") bStable(S);
  else if (t == "windmill") bWindmill(S); else if (t == "market") bMarket(S);
}

} // namespace dq
