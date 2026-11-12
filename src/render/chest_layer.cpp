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
#include "assets/voxel_paint.h"
#include "core/registry.h"
#include "core/item_glint.h"
#include "sim/chest_work.h"
#include "sim/controller.h"
#include "sim/player.h"
#include <cstring>
#include <map>
#include <set>

namespace dq {

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
void buildPart(VoxelModel& out, const BlockDef& b, int frontFace, int y0, int y1, bool inside) {
  const TileSet& ts = app().tiles;
  VoxelGrid g(ivec3(32, y1 - y0, 32));
  const int S = ts.size;
  for (int z = 0; z < 32; z++) for (int ly = 0; ly < y1 - y0; ly++) for (int x = 0; x < 32; x++) {
    int y = y0 + ly;
    int d[6] = {31 - x, x, y1 - 1 - y, y - y0, 31 - z, z};
    int f = 2; for (int i : {0, 1, 4, 5, 3}) if (d[i] < d[f]) f = i;
    int tile = f == 2 ? b.faces[2] : f == 3 ? b.faces[3] : f == frontFace ? b.faces[4] : b.faces[0];
    float u = f == 0 || f == 1 ? (f == 0 ? 1 - (z + 0.5f) / 32 : (z + 0.5f) / 32) : (f == 5 ? 1 - (x + 0.5f) / 32 : (x + 0.5f) / 32);
    float v = f == 2 || f == 3 ? (z + 0.5f) / 32 : 1 - (y + 0.5f) / 32;
    u32 c = tileVoxel(ts, tile, std::min(S - 1, (int)(u * S)), std::min(S - 1, (int)(v * S)));
    if (!c) c = voxRGBA(138, 90, 42);
    if (inside && f == 2 && y == y1 - 1) {   // looking into the open chest: its dark floor, the boards' rim left light
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

ChestModels& modelsFor(BlockId id, int facing, int lidRow) {
  static auto& cache = *new std::map<std::string, std::unique_ptr<ChestModels>>();
  const TileSet& ts = app().tiles;
  std::string k = std::to_string(id) + "/" + std::to_string(facing) + "/" + std::to_string(lidRow) + "/" + ts.packName + "#" + std::to_string(ts.count);
  auto it = cache.find(k);
  if (it != cache.end()) return *it->second;
  if (cache.size() > 24) cache.clear();
  auto m = std::make_unique<ChestModels>();
  const BlockDef& b = reg().block(id);
  buildPart(m->base, b, FACING_FACE[facing & 3], 0, lidRow, true);
  buildPart(m->lid, b, FACING_FACE[facing & 3], lidRow, 32, false);
  return *(cache[k] = std::move(m));
}

// a chest (painted, ender, party) or a barrel: a store with a lid (a sealed barrel has none to lift)
bool isChest(BlockId id) {
  if (!id) return false;
  const BlockDef& b = reg().block(id);
  const std::string& k = b.key;
  auto ends = [&](const char* e) { const size_t n = std::strlen(e); return k.size() >= n && k.compare(k.size() - n, n, e) == 0; };
  return b.render == Render::Cube && (ends("chest") || (ends("barrel") && b.has("container")));
}

struct Open { BlockId id = 0; int facing = 0, lidRow = 20; float t = 0, linger = 0; bool settling = false, mine = false; };

class ChestLayer : public RenderLayer {
public:
  ChestLayer() { g_chestHiddenN = 0; g_wanted.clear(); }
  ~ChestLayer() override { g_chestHiddenN = 0; }
  const char* name() const override { return "chest-lids"; }
  int order() const override { return 130; }
  std::map<std::tuple<int, int, int>, Open> open;

  void update(GameSession& s, double dt) override {
    if (!s.player) return;
    World& w = s.world();
    const float fdt = (float)std::min(dt, 0.1);
    // what's open: yours (and each one's other half), the others' at work at a chest
    std::set<std::tuple<int, int, int>> want;
    auto add = [&](ivec3 p) {
      if (!isChest(w.getBlock(p.x, p.y, p.z))) return;
      ivec3 hs[2]; const int n = chestHalves(w, p, hs);
      if (!n) { want.insert({p.x, p.y, p.z}); return; }
      for (int i = 0; i < n; i++) want.insert({hs[i].x, hs[i].y, hs[i].z});
    };
    for (auto& [k, c] : g_wanted) if (c > 0) add(ivec3(std::get<0>(k), std::get<1>(k), std::get<2>(k)));
    std::set<std::tuple<int, int, int>> mineSet = want;   // (yours: an ender chest shows what's in it to you alone)
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
    for (auto& [k, o] : open) if (!o.settling && n < CHEST_HIDE_MAX) { g_chestHidden[n][0] = std::get<0>(k); g_chestHidden[n][1] = std::get<1>(k); g_chestHidden[n][2] = std::get<2>(k); n++; }
    g_chestHiddenN = n;
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (open.empty() || !s.player) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    static const vec3 FRONT[4] = {vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 0, -1), vec3(-1, 0, 0)};
    for (auto& [k, o] : open) {
      const ivec3 pos(std::get<0>(k), std::get<1>(k), std::get<2>(k));
      ChestModels& cm = modelsFor(o.id, o.facing, o.lidRow);
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
      ivec3 hs[2]; const int nh = chestHalves(w, pos, hs);
      if (!nh || !s.level) continue;
      const std::vector<ItemStack> items = chestHalfItems(*s.level, o.mine ? s.player : nullptr, w, pos);
      const int per = (int)items.size(), h = nh == 2 && hs[1] == pos ? 1 : 0;
      const float cs = chestSlotSize(w, pos);
      for (int i = 0; i < per; i++) {
        const ItemStack& st = items[i];
        if (st.empty()) continue;
        const VoxelModel& im = itemModel(stackModelKey(st));
        if (!im.ready()) continue;
        const vec3 c = chestSlotCenter(w, hs[0], h * per + i) - vec3(pos);
        const int layers = st.n > 1 ? (st.n >= 16 ? 3 : 2) : 1;   // (a stack: a few of it piled)
        for (int ly = 0; ly < layers; ly++) {
          mat4 im4 = glm::translate(m, c + vec3((ly & 1) * cs * 0.08f, 0.002f, (ly == 2) * -cs * 0.08f));
          im4 = glm::rotate(im4, (o.facing & 3) * PI / 2 + ly * 0.35f, vec3(0, 1, 0));
          if (im.kind == VoxelModel::Block) im4 = glm::scale(glm::translate(im4, vec3(0, cs * 0.42f + ly * cs * 0.55f, 0)), vec3(cs * 0.8f));
          else { im4 = glm::translate(im4, vec3(0, (im.depth > 0 ? im.depth : 0.03f) * cs + 0.003f + ly * cs * 0.12f, 0)); im4 = glm::rotate(im4, -PI / 2, vec3(1, 0, 0)); im4 = glm::scale(im4, vec3(cs * 1.05f)); }
          VR.draw(im.gl, im4 * im.matrix(), L, vec4(0), itemGlints(st) ? 1.f : 0.f);
        }
      }
    }
    if (begun) VR.end();
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
