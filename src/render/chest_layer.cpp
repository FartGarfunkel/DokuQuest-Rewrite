// render/chest_layer.cpp — chest lids that lift while a chest is open, and what's inside lying in its rows. The chest
// block is a textured cube in the mesher; while it is open (or settling) the mesher skips it (chestHiddenAt) and this
// layer draws it as two voxel parts built from the block's own tiles: the base (with a dark inside) and the lid, which
// turns about its back edge. Opened by the container screen and the chest scene (yours: both halves of a large chest),
// and by the others working a chest (Player::stationKind 5, the chest nearest their hand).
#include "render/chest_layer.h"
#include "render/item_layers.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/tileset.h"
#include "assets/pack_models.h"
#include "assets/voxel_paint.h"
#include "core/registry.h"
#include "core/item_glint.h"
#include "sim/chest_work.h"
#include "sim/controller.h"
#include "sim/player.h"
#include "ui/cinematic.h"
#include <cstring>
#include "assets/local_models.h"
#include "sim/hands.h"
#include "render/hands_kit.h"
#include "render/content_draw.h"
#include "sim/physics.h"
#include <map>
#include <set>

namespace dq {
std::vector<std::string> itemTooltipLines(const ItemStack& s);   // render/hands_chests.cpp
GLuint mapPaperTexture();   // render/map_render.h

std::string stackModelKey(const ItemStack& s);   // render/pile_layer.cpp

std::atomic<int> g_chestHiddenN{0};
std::atomic<int> g_chestHidden[CHEST_HIDE_MAX][3];

namespace {
std::map<std::tuple<int, int, int>, int> g_wanted;   // the chests your screens have open (a count each)

// the lid: a chest's is the top 12 of the cube's 32 micro-voxel rows (the tiles' seam is at row 6 of 16), a barrel's the
// top board (sim/chest_work.h chestLidRow)
const int FACING_FACE[4] = {4, 0, 5, 1};   // as in the mesher: meta & 3 -> the face the front tile is on

struct ChestModels { VoxelModel base, lid; };

// one part of the chest (rows y0..y1-1 of the cube), every voxel taking the tile pixel of the face it lies on
// a large chest's half (DC, half 0/1: assets/pack_models.h) takes its front, back, lid top and underside and its inside
// floor from the pack's large-chest pictures, so the open pair reads as one chest.
// hollow (a chest, not a barrel): the base is a box with board-thick walls and its floor at CHEST_FLOOR_ROW (where its
// things lie: sim/chest_work.cpp chestFloorY), the lid hollowed from beneath under a board-thick top; a large chest's two
// halves have no wall where they meet, so the pair is one open chest
constexpr int CHEST_WALL = 2, CHEST_FLOOR_ROW = 8;
void buildPart(VoxelModel& out, const BlockDef& b, int frontFace, int y0, int y1, bool inside, const DoubleChestTiles* DC = nullptr, int half = -1, int facing = 0, bool hollow = false) {
  static const int OPP[6] = {1, 0, 3, 2, 5, 4};
  static const int FX[4] = {0, 1, 0, -1}, FZ[4] = {1, 0, -1, 0};
  const bool dbl = DC && half >= 0;
  const TileSet& ts = app().tiles;
  VoxelGrid g(ivec3(32, y1 - y0, 32));
  const int S = ts.size;
  // the well: inside the walls (out to the edge on the side the other half is)
  int sx = 0, sz = 0;
  if (dbl) { const int fd = facing & 3, k = half == 0 ? 1 : -1; sx = FZ[fd] * k; sz = -FX[fd] * k; }
  const int xlo = sx < 0 ? 0 : CHEST_WALL, xhi = sx > 0 ? 31 : 31 - CHEST_WALL, zlo = sz < 0 ? 0 : CHEST_WALL, zhi = sz > 0 ? 31 : 31 - CHEST_WALL;
  const int ceil = y1 - 1 - CHEST_WALL;   // (the lid: hollow up to under its top board)
  auto inWell = [&](int x, int z) { return x >= xlo && x <= xhi && z >= zlo && z <= zhi; };
  auto carved = [&](int x, int y, int z) {
    if (!hollow || x < 0 || x > 31 || z < 0 || z > 31 || y < y0 || y >= y1 || !inWell(x, z)) return false;
    return inside ? y >= CHEST_FLOOR_ROW : y <= ceil;
  };
  const bool barrel = b.key.size() >= 6 && b.key.compare(b.key.size() - 6, 6, "barrel") == 0;
  const int insideTile = barrel ? b.faces[3] : b.faces[2];   // (a barrel's inside: its boards, as its bottom)
  auto texel = [&](int tile, float u, float v) { return tileVoxel(ts, tile, std::min(S - 1, (int)(u * S)), std::min(S - 1, (int)(v * S))); };
  for (int z = 0; z < 32; z++) for (int ly = 0; ly < y1 - y0; ly++) for (int x = 0; x < 32; x++) {
    int y = y0 + ly;
    if (carved(x, y, z)) continue;
    int d[6] = {31 - x, x, y1 - 1 - y, y - y0, 31 - z, z};
    int f = 2; for (int i : {0, 1, 4, 5, 3}) if (d[i] < d[f]) f = i;
    int tile = f == 2 ? b.faces[2] : f == 3 ? b.faces[3] : f == frontFace ? b.faces[4] : b.faces[0];
    bool floor = false;
    if (dbl) {
      if (f == frontFace) tile = DC->front[half];
      else if (f == OPP[frontFace]) tile = DC->back[half];
      else if (f == 2) { floor = inside; tile = inside ? DC->floor[half][facing & 3] : DC->top[half][facing & 3]; }
      else if (f == 3 && !inside) tile = DC->under[half][facing & 3];
    }
    float u = f == 0 || f == 1 ? (f == 0 ? 1 - (z + 0.5f) / 32 : (z + 0.5f) / 32) : (f == 5 ? 1 - (x + 0.5f) / 32 : (x + 0.5f) / 32);
    float v = f == 2 || f == 3 ? (z + 0.5f) / 32 : 1 - (y + 0.5f) / 32;
    u32 c = texel(tile, u, v);
    if (!c) c = voxRGBA(138, 90, 42);
    const float tu = (x + 0.5f) / 32, tv = (z + 0.5f) / 32;
    if (hollow && carved(x, y + 1, z)) {   // the well's floor: the chest's own inside, shaded by its walls
      c = texel(dbl ? DC->floor[half][facing & 3] : insideTile, tu, tv); if (!c) c = voxRGBA(110, 72, 36);
      c = voxShade(c, dbl ? 0.62f : 0.45f);
    } else if (hollow && carved(x, y - 1, z)) {   // the lid's ceiling, over the well
      c = texel(dbl ? DC->under[half][facing & 3] : barrel ? insideTile : b.faces[3], tu, tv); if (!c) c = voxRGBA(110, 72, 36);
      c = voxShade(c, 0.6f);
    } else if (hollow && (carved(x - 1, y, z) || carved(x + 1, y, z) || carved(x, y, z - 1) || carved(x, y, z + 1))) {
      // a wall's inner face: plain boards (the inside's own picture, along the wall), out of the light
      const bool alongX = carved(x, y, z - 1) || carved(x, y, z + 1);
      c = texel(dbl ? DC->floor[half][facing & 3] : insideTile, alongX ? tu : tv, 1 - (y + 0.5f) / 32); if (!c) c = voxRGBA(110, 72, 36);
      c = voxShade(c, 0.55f);
    } else if (!hollow && floor && y == y1 - 1) c = voxShade(c, 0.7f);   // (the large chest's own inside, in the shade of its walls)
    else if (!hollow && inside && f == 2 && y == y1 - 1) {   // looking into the open barrel: its dark top, the boards' rim left light
      const bool rim = x < 2 || x > 29 || z < 2 || z > 29;
      if (!rim) c = voxShade(c, 0.32f);
    }
    g.set(x, ly, z, c | 0xFF000000u);
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  out.gl.upload(md);
  out.frame.voxel = 1.f / 32; out.frame.origin = vec3(0, y0 / 32.f, 0);
  out.size = g.size; out.kind = VoxelModel::Block; out.built = true; out.key = "chest_part";
}

ChestModels& modelsFor(BlockId id, int facing, int lidRow, int half = -1) {
  static auto& cache = *new std::map<std::string, std::unique_ptr<ChestModels>>();
  const TileSet& ts = app().tiles;
  std::string k = std::to_string(id) + "/" + std::to_string(facing) + "/" + std::to_string(lidRow) + "/" + std::to_string(half) + "/" + ts.packName + "#" + std::to_string(ts.count);
  auto it = cache.find(k);
  if (it != cache.end()) return *it->second;
  if (cache.size() > 24) cache.clear();
  auto m = std::make_unique<ChestModels>();
  const BlockDef& b = reg().block(id);
  const DoubleChestTiles* DC = half >= 0 ? doubleChestFor(b.faces[4]) : nullptr;
  const bool hollow = true;   // (chests and barrels alike: open, a well inside)
  buildPart(m->base, b, FACING_FACE[facing & 3], 0, lidRow, true, DC, half, facing, hollow);
  buildPart(m->lid, b, FACING_FACE[facing & 3], lidRow, 32, false, DC, half, facing, hollow);
  return *(cache[k] = std::move(m));
}

// a cabinet open (its model: data/shapes.json, the body 1..15 x 0..14 x 1..14.5, the top over it, the door on its front,
// facing +z): the body hollow with three shelf boards (sim/chest_work.h CABINET_SHELF), and the door with its handles on its
// own, to swing out about its left edge. In 32nds, the cabinet's own wood and door pictures.
ChestModels& cabinetModels(BlockId id) {
  static auto& cache = *new std::map<std::string, std::unique_ptr<ChestModels>>();
  const TileSet& ts = app().tiles;
  const std::string k = std::to_string(id) + "/" + ts.packName + "#" + std::to_string(ts.count);
  auto it = cache.find(k);
  if (it != cache.end()) return *it->second;
  if (cache.size() > 24) cache.clear();
  auto m = std::make_unique<ChestModels>();
  const BlockDef& b = reg().block(id);
  const int S = ts.size, wood = b.faces[0];
  int door = reg().tile(b.str("wood", "oak") + "_trapdoor"); if (door < 0) door = reg().tile("oak_trapdoor"); if (door < 0) door = wood;
  int iron = reg().tile("iron_block"); if (iron < 0) iron = wood;
  auto px = [&](int tile, float u, float v) { u32 c = tileVoxel(ts, tile, std::clamp((int)(u * S), 0, S - 1), std::clamp((int)(v * S), 0, S - 1)); return c ? c : voxRGBA(150, 110, 60); };
  auto finish = [&](VoxelModel& out, VoxelGrid& g) {
    VoxelMeshData md; buildVoxelMesh(g, md); out.gl.upload(md);
    out.frame.voxel = 1.f / 32; out.frame.origin = vec3(0);
    out.size = g.size; out.kind = VoxelModel::Block; out.built = true; out.key = "cabinet_part";
  };
  {   // the body
    VoxelGrid g(ivec3(32));
    for (int z = 0; z < 32; z++) for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
      const bool top = y >= 28 && y <= 30 && x >= 1 && x <= 30 && z >= 1 && z <= 30;
      const bool body = x >= 2 && x <= 29 && y <= 27 && z >= 2 && z <= 28;
      if (!top && !body) continue;
      const bool inside = body && x >= 4 && x <= 27 && y >= 3 && y <= 25 && z >= 4;   // (open to the front)
      const bool shelf = inside && (y == 17 || y == 10);                               // (the boards under 9 and 5.5)
      if (inside && !shelf) continue;
      u32 c;
      const bool face = z == 28 || z == 30, side = x == 2 || x == 29 || x == 1 || x == 30;
      if (face) c = px(wood, x / 32.f, 1 - y / 32.f);
      else if (side) c = px(wood, z / 32.f, 1 - y / 32.f);
      else c = px(wood, x / 32.f, z / 32.f);
      // the inside's faces in the shade
      const bool nearIn = !top && ((x == 3 || x == 28) || (y == 2 || y == 26) || z == 3 || shelf) && y >= 2 && y <= 26 && x >= 3 && x <= 28;
      if (shelf) c = voxShade(px(wood, x / 32.f, z / 32.f), 0.78f);
      else if (nearIn) c = voxShade(c, 0.6f);
      g.set(x, y, z, c | 0xFF000000u);
    }
    finish(m->base, g);
  }
  {   // the door (2..14 x 1.5..12.5, 14.5..15.2) and its two handles (x 7..7.8 and 8.2..9, y 6..8.5, out to 15.8)
    VoxelGrid g(ivec3(32));
    for (int z = 29; z <= 30; z++) for (int y = 3; y <= 24; y++) for (int x = 4; x <= 27; x++)
      g.set(x, y, z, px(door, (x - 4) / 24.f, 1 - (y - 3) / 22.f) | 0xFF000000u);
    for (int y = 12; y <= 16; y++) for (int x : {14, 15, 16, 17}) g.set(x, y, 31, px(iron, 0.5f, (y - 12) / 5.f) | 0xFF000000u);
    finish(m->lid, g);
  }
  return *(cache[k] = std::move(m));
}
bool isCabinetBlock(BlockId id) { const BlockDef& b = reg().block(id); return id && b.str("furniture") == "cabinet" && b.has("container"); }

// a chest (painted, ender, party) or a barrel: a store with a lid (a sealed barrel has none to lift); a cabinet (doors)
bool isChest(BlockId id) {
  if (!id) return false;
  const BlockDef& b = reg().block(id);
  if (isCabinetBlock(id)) return true;
  const std::string& k = b.key;
  auto ends = [&](const char* e) { const size_t n = std::strlen(e); return k.size() >= n && k.compare(k.size() - n, n, e) == 0; };
  return b.render == Render::Cube && (ends("chest") || (ends("barrel") && b.has("container")));
}

struct DoorThing { ivec3 at; mat4 dm; VoxelLight L; float e; };   // a cupboard door's inside: its note and sort button (the hands-on chest lid's)
struct Open { BlockId id = 0; int facing = 0, lidRow = 20; float t = 0, linger = 0; bool settling = false, mine = false, listed = false; };   // listed: left out of the mesh (drawn here)

class ChestLayer : public RenderLayer {
public:
  ChestLayer() { g_chestHiddenN = 0; g_wanted.clear(); }
  ~ChestLayer() override { g_chestHiddenN = 0; }
  const char* name() const override { return "chest-lids"; }
  int order() const override { return 130; }
  std::map<std::tuple<int, int, int>, Open> open;
  std::vector<ivec3> lidOpen; float lidScanT = 0;
  std::vector<DoorThing> doorThings;   // (lids left open about you)

  void update(GameSession& s, double dt) override {
    if (!s.player) return;
    World& w = s.world();
    const float fdt = (float)std::min(dt, 0.1);
    // what's open: yours (and each one's other half), the others' at work at a chest
    std::set<std::tuple<int, int, int>> want;
    const bool handsOn = s.app.settings.handsStations;
    auto add = [&](ivec3 p) {
      if (!isChest(w.getBlock(p.x, p.y, p.z))) return;
      if (handsOn && stationModel(w.getBlock(p.x, p.y, p.z)) && handsChest(w, p)) return;   // (drawn hands-on: render/hands_chests.cpp)
      ivec3 hs[2]; const int n = chestHalves(w, p, hs);
      if (!n) { want.insert({p.x, p.y, p.z}); return; }
      for (int i = 0; i < n; i++) want.insert({hs[i].x, hs[i].y, hs[i].z});
    };
    for (auto& [k, c] : g_wanted) if (c > 0) add(ivec3(std::get<0>(k), std::get<1>(k), std::get<2>(k)));
    std::set<std::tuple<int, int, int>> mineSet = want;   // (yours: an ender chest shows what's in it to you alone)
    // lids left open (sim/chest_work.h LID_OPEN): the stores about you with theirs up, nearest first, looked for now and then
    lidScanT -= fdt;
    if (lidScanT <= 0) {
      lidScanT = 0.4f; lidOpen.clear();
      const ivec3 me(glm::floor(s.player->pos));
      const int d = dimOfY(clampv(me.y, 0, WORLD_H - 1));
      std::vector<std::pair<float, ivec3>> found;
      for (int cz = (me.z >> 4) - 2; cz <= (me.z >> 4) + 2; cz++) for (int cx = (me.x >> 4) - 2; cx <= (me.x >> 4) + 2; cx++) {
        Chunk* ch = w.chunk(d, cx, cz);
        if (!ch) continue;
        for (auto& [ci, be] : ch->blockEntities) {
          const ivec3 q(cx * 16 + (ci & 15), dimBase(d) + (ci >> 8), cz * 16 + ((ci >> 4) & 15));
          if (!(w.getMeta(q.x, q.y, q.z) & LID_OPEN) || !chestLidOpen(w, q)) continue;
          const float dd = glm::distance(vec3(q) + vec3(0.5f), s.player->pos);
          if (dd < 28) found.push_back({dd, q});
        }
      }
      std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      for (auto& f : found) if ((int)lidOpen.size() < CHEST_HIDE_MAX - 8) lidOpen.push_back(f.second);   // (room kept for the ones in use)
    }
    for (const ivec3& q : lidOpen) if (want.size() + 8 <= (size_t)CHEST_HIDE_MAX && chestLidOpen(w, q)) add(q);
    if (s.target.hit && chestLidOpen(w, s.target.block)) add(s.target.block);   // (the one you look at: open at once, not at the next look round)
    if (s.level) for (auto& e : s.level->entities()) {
      if (!e || e->removed || e.get() == s.player || !e->isPlayer() || e->realm != s.player->realm) continue;
      const Player* o = static_cast<const Player*>(e.get());
      if (o->stationKind != 5) continue;
      // (the chest nearest their working hand)
      ivec3 best(0); float bd = 2.6f; bool found = false;
      const ivec3 c0 = ivec3(glm::floor(o->workAt));
      for (int dy = -1; dy <= 0; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        const ivec3 q = c0 + ivec3(dx, dy, dz);
        if (!isChest(w.getBlock(q.x, q.y, q.z))) continue;
        const float d = glm::distance(vec3(q) + vec3(0.5f), o->workAt);
        if (d < bd) { bd = d; best = q; found = true; }
      }
      if (found) add(best);
    }
    for (const auto& k : want) {
      const ivec3 p(std::get<0>(k), std::get<1>(k), std::get<2>(k));
      Open& o = open[k];
      o.mine = mineSet.count(k) > 0;
      if (!o.id || o.settling) { o.id = w.getBlock(p.x, p.y, p.z); o.facing = w.getMeta(p.x, p.y, p.z) & 3; o.lidRow = chestLidRow(w, p); o.settling = false; w.markBlockDirty(p.x, p.y, p.z, true); }
    }
    for (auto it = open.begin(); it != open.end();) {
      const ivec3 p(std::get<0>(it->first), std::get<1>(it->first), std::get<2>(it->first));
      Open& o = it->second;
      const bool there = isChest(w.getBlock(p.x, p.y, p.z));
      if (!there) { w.markBlockDirty(p.x, p.y, p.z, true); it = open.erase(it); continue; }   // (broken or swapped while open: its mesh back)
      if (w.getBlock(p.x, p.y, p.z) != o.id) { o.id = w.getBlock(p.x, p.y, p.z); o.facing = w.getMeta(p.x, p.y, p.z) & 3; o.lidRow = chestLidRow(w, p); }   // (another store put there)
      if (o.settling) { o.linger -= fdt; if (o.linger <= 0) { it = open.erase(it); continue; } ++it; continue; }
      o.t = clampv(o.t + (want.count(it->first) ? 1.f : -1.f) * fdt * 3.4f, 0.f, 1.f);
      if (!want.count(it->first) && o.t <= 0) { o.settling = true; o.linger = 0.7f; w.markBlockDirty(p.x, p.y, p.z, true); }   // shut: the mesher takes it back
      ++it;
    }
    // the mesher's list: the ones drawn here (not those settling back into the mesh)
    int n = 0;
    // yours first (so the one you're using is never crowded out by the lids left open about you), then the rest; only the
    // ones listed are drawn open here, so a store is never drawn both shut by the mesher and open by this layer
    for (auto& [k, o] : open) o.listed = false;
    for (int pass = 0; pass < 2; pass++)
      for (auto& [k, o] : open) if (!o.settling && !o.listed && (pass == 1 || o.mine) && n < CHEST_HIDE_MAX) {
        g_chestHidden[n][0] = std::get<0>(k); g_chestHidden[n][1] = std::get<1>(k); g_chestHidden[n][2] = std::get<2>(k); n++; o.listed = true;
      }
    g_chestHiddenN = n;
  }
  // what's inside, lying in its rows (or on a cabinet's shelves: sim/chest_work.h chestSlotCenter)
  void drawInside(GameSession& s, World& w, VoxelRenderer& VR, const Open& o, ivec3 pos, const mat4& m, VoxelLight L, float time) {
    ivec3 hs[2]; const int nh = chestHalves(w, pos, hs);
    if (!nh || !s.level) return;
    // your own chest open in front of you: its well not so dark that its things are lost (a little light falls in);
    // the slot you point at (mouse or d-pad: ui/cinematic_table.cpp) has its thing lifted up to you, turning slowly
    vec3 aim; const bool aiming = o.mine && cinematicAimPoint(aim);
    if (o.mine) { L.block = std::max(L.block, 0.62f); L.sky = std::max(L.sky, 0.35f); }
    const std::vector<ItemStack> items = chestHalfItems(*s.level, o.mine ? s.player : nullptr, w, pos);
    const int per = (int)items.size(), h = nh == 2 && hs[1] == pos ? 1 : 0;
    const float cs = chestSlotSize(w, pos);
    for (int i = 0; i < per; i++) {
      const ItemStack& st = items[i];
      if (st.empty()) continue;
      const VoxelModel& im = itemModel(stackModelKey(st));
      if (!im.ready()) continue;
      const vec3 c = chestSlotCenter(w, hs[0], h * per + i) - vec3(pos);
      const bool shelved = isCabinet(w, pos);   // (a cupboard's shelf: 3.5 px under the board above, one of it, kept under)
      const int layers = shelved ? 1 : st.n > 1 ? (st.n >= 16 ? 3 : 2) : 1;   // (a stack: a few of it piled)
      const bool lifted = aiming && glm::distance(c + vec3(pos), aim) < cs * 0.3f;
      // its own extent (a picture fills only part of its frame; a 3D one stands off its middle): sized to its slot
      const vec3 lo = im.frame.origin, hi = im.frame.origin + vec3(im.size) * im.frame.voxel, ctr = (lo + hi) * 0.5f;
      const bool cube = im.kind == VoxelModel::Block && !im.torch3d;
      if (lifted) {   // held up out of the well: bigger, turning, a gentle bob
        const float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 0.05f});
        const float sc = (cube ? cs * 1.5f : cs * 2.0f) / ext;
        mat4 im4 = glm::translate(m, c + vec3(0, cs * 1.0f + std::sin(time * 2.4f) * cs * 0.06f, 0));
        im4 = glm::rotate(im4, time * 1.3f, vec3(0, 1, 0));
        im4 = glm::translate(glm::scale(im4, vec3(sc)), -ctr);
        VR.draw(im.gl, im4 * im.matrix(), VoxelLight{std::max(L.sky, 0.6f), std::max(L.block, 0.85f)}, vec4(0), itemGlints(st) ? 1.f : 0.f);
        continue;
      }
      for (int ly = 0; ly < layers; ly++) {
        mat4 im4 = glm::translate(m, c + vec3((ly & 1) * cs * 0.08f, 0.002f, (ly == 2) * -cs * 0.08f));
        im4 = glm::rotate(im4, (o.facing & 3) * PI / 2 + ly * 0.35f, vec3(0, 1, 0));
        if (cube) { const float k = shelved ? std::min(cs * 0.8f, 3.f / 16) : cs * 0.8f; im4 = glm::scale(glm::translate(im4, vec3(0, k * 0.52f + ly * cs * 0.55f, 0)), vec3(k)); }
        else {   // lying flat, filling its slot, resting on the floor (not sunk into it)
          float sc = std::min(cs * 1.15f / std::max({hi.x - lo.x, hi.y - lo.y, 0.05f}), cs * 2.4f);
          if (shelved) sc = std::min(sc, (3.f / 16) / std::max(hi.z - lo.z, 0.01f));   // (its height under the shelf above)
          im4 = glm::translate(im4, vec3(0, -lo.z * sc + ly * std::min((hi.z - lo.z) * sc * 0.9f, cs * 0.12f), 0));   // (a round thing piled sits on the last, not over the shelf above)
          im4 = glm::rotate(im4, -PI / 2, vec3(1, 0, 0));
          im4 = glm::translate(glm::scale(im4, vec3(sc)), vec3(-ctr.x, -ctr.y, 0));
        }
        VR.draw(im.gl, im4 * im.matrix(), L, vec4(0), itemGlints(st) ? 1.f : 0.f);
      }
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (open.empty() || !s.player) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    static const vec3 FRONT[4] = {vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 0, -1), vec3(-1, 0, 0)};
    for (auto& [k, o] : open) {
      if (!o.listed && !o.settling) continue;   // (past the list: the mesher still draws it, shut)
      const ivec3 pos(std::get<0>(k), std::get<1>(k), std::get<2>(k));
      if (isCabinetBlock(o.id)) {   // its door swings out on its left edge (as you face it); the shelves inside
        ChestModels& cab = cabinetModels(o.id);
        if (!cab.base.ready()) continue;
        const vec3 rel = f.cam.rel(glm::dvec3(pos));
        if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) continue;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        // lit by the air before its door; faced to a wall (the wall holds no light: it was drawn black) the brightest
        // open side round it
        const ivec3 fr = pos + ivec3(FRONT[o.facing & 3]);
        u8 l = w.getLight(fr.x, fr.y, fr.z);
        if (reg().tOpaque[w.getBlock(fr.x, fr.y, fr.z)]) {
          int sk = 0, bl = 0;
          for (const ivec3 d : {ivec3(1, 0, 0), ivec3(-1, 0, 0), ivec3(0, 0, 1), ivec3(0, 0, -1), ivec3(0, 1, 0)}) {
            const ivec3 q = pos + d; if (reg().tOpaque[w.getBlock(q.x, q.y, q.z)]) continue;
            const u8 ql = w.getLight(q.x, q.y, q.z); sk = std::max(sk, ql >> 4); bl = std::max(bl, ql & 15);
          }
          l = (u8)((sk << 4) | bl);
        }
        const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
        mat4 m = glm::translate(mat4(1), rel + vec3(0.5f, 0, 0.5f));
        m = glm::rotate(m, (o.facing & 3) * PI / 2, vec3(0, 1, 0));
        m = glm::translate(m, vec3(-0.5f, 0, -0.5f));
        VR.draw(cab.base.gl, m * cab.base.matrix(), L);
        const float e = o.settling ? 0.f : 1 - (1 - o.t) * (1 - o.t);
        const vec3 hinge(2 / 16.f, 0, 15.2f / 16.f);
        mat4 dm = glm::translate(m, hinge); dm = glm::rotate(dm, -1.85f * e, vec3(0, 1, 0)); dm = glm::translate(dm, -hinge);
        VR.draw(cab.lid.gl, dm * cab.lid.matrix(), L);
        if (s.app.settings.handsStations && e > 0.6f) doorThings.push_back({pos, dm, L, e});   // (the note and sort button on its inside: below)
        if (o.settling || o.t < 0.2f) continue;
        drawInside(s, w, VR, o, pos, glm::translate(mat4(1), rel), L, (float)f.time);
        continue;
      }
      const int half = reg().block(o.id).str("shape").empty() && doubleChestFor(reg().block(o.id).faces[4])
        ? doubleChestHalf(o.facing, pos.x, pos.z, [&](int dx, int dz) { return w.getBlock(pos.x + dx, pos.y, pos.z + dz) == o.id && (w.getMeta(pos.x + dx, pos.y, pos.z + dz) & 3) == o.facing; }) : -1;
      ChestModels& cm = modelsFor(o.id, o.facing, o.lidRow, half);
      if (!cm.base.ready()) continue;
      const vec3 rel = f.cam.rel(glm::dvec3(pos));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      const u8 l = w.getLight(pos.x, pos.y + 1, pos.z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      const mat4 m = glm::translate(mat4(1), rel);
      VR.draw(cm.base.gl, m * cm.base.matrix(), L);
      // the lid turns about its back edge: the front rises
      const vec3 d = FRONT[o.facing & 3];
      const float e = o.settling ? 0.f : 1 - (1 - o.t) * (1 - o.t), ang = 1.25f * e;
      const vec3 pivot = vec3(0.5f, o.lidRow / 32.f, 0.5f) - 0.5f * d;
      mat4 lm = glm::translate(m, pivot);
      lm = glm::rotate(lm, ang, glm::cross(d, vec3(0, 1, 0)));
      lm = glm::translate(lm, -pivot);
      VR.draw(cm.lid.gl, lm * cm.lid.matrix(), L);
      // what's inside, lying in its rows (seen once the lid is up)
      if (o.settling || o.t < 0.2f) continue;
      if (!(s.app.settings.handsStations && handsBarrel(w, pos))) drawInside(s, w, VR, o, pos, m, L, (float)f.time);   // (a barrel's tray: render/hands_chests.cpp)
    }
    if (begun) VR.end();
    // a cupboard door's inside (the hands-on chest lid, on the door): our parchment with the name of what you look at, the sort button
    if (!doorThings.empty()) {
      ContentDraw& cd = contentDraw();
      if (cd.begin(f, f.cam.viewProj)) {
        const vec3 eye(s.player->pos.x, s.player->pos.y + s.player->eye, s.player->pos.z);
        const vec3 dir = lookVector(s.player->yaw, s.player->pitch);
        for (const DoorThing& d : doorThings) {
          const vec2 light(d.L.sky, std::max(d.L.block, 0.3f));
          { TPose p; p.m = d.dm; p.t(0.42f, 0.45f, 0.899f).s(0.42f, 0.5f, 1); cd.picture(mapPaperTexture(), p.m, vec4(0, 0, 1, 1), vec4(1), light, true); }
          if (const PackModel* b = handsPart("chest_button_sort")) { TPose p; p.m = d.dm; p.t(12.f / 16, 9.f / 16, 0.9f).rx(-90).t(-12.5f / 16, -10.25f / 16, -8.65f / 16); handsPartDraw(s.app, cd, *b, p.m, light); }
          if (s.target.hit && s.target.block == d.at) {
            const int slot = chestSlotAtRay(w, d.at, eye, dir);
            const std::vector<ItemStack> v = chestHalfItems(*s.level, s.player, w, d.at);
            if (slot >= 0 && slot < (int)v.size() && !v[slot].empty()) {
              const std::vector<std::string> lines = itemTooltipLines(v[slot]);
              TPose tp; tp.m = d.dm; tp.t(0, 0, 0.897f).rx(-90);
              const vec4 ink(0.15f, 0.1f, 0.05f, 1);
              handsText(cd, tp, 6.7f, 0.002f, 10.6f, lines[0], 0.004f, 6.f, ink, light);
              if (lines.size() > 1) handsText(cd, tp, 6.7f, 0.002f, 8.2f, lines[1], 0.0026f, 6.f, ink, light);
              handsText(cd, tp, 6.7f, 0.002f, 4.6f, "\xC3\x97" + std::to_string(v[slot].n), 0.0032f, 6.f, ink, light);
            }
          }
        }
        cd.end();
      }
      doorThings.clear();
    }
  }
};
} // namespace

void chestScreenOpened(ivec3 pos, bool open) {
  int& c = g_wanted[{pos.x, pos.y, pos.z}];
  c = std::max(0, c + (open ? 1 : -1));
  if (!c) g_wanted.erase({pos.x, pos.y, pos.z});
}
void registerChestLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<ChestLayer>(); }); }

} // namespace dq
