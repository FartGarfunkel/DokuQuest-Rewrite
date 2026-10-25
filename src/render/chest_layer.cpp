// render/chest_layer.cpp — a chest's lid lifts while its screen is open and drops when it closes. The chest
// block is a textured cube in the mesher; while it is open (or settling) the mesher skips that one block
// (chestHiddenAt) and this layer draws it as two voxel parts built from the block's own tiles: the base (with a
// dark inside) and the lid, which turns about its back edge.
#include "render/chest_layer.h"
#include "render/item_layers.h"
#include "client/app.h"
#include "assets/tileset.h"
#include "assets/voxel_paint.h"
#include "core/registry.h"
#include <map>

namespace dq {

std::atomic<bool> g_chestHide{false};
std::atomic<int> g_chestHideX{0}, g_chestHideY{0}, g_chestHideZ{0};

namespace {
bool g_want = false; ivec3 g_wantPos{0};

const int LID_Y0 = 20;   // the lid is the top 12 of the cube's 32 micro-voxel rows (the tiles' seam is at row 6 of 16)
const int FACING_FACE[4] = {4, 0, 5, 1};   // as in the mesher: meta & 3 -> the face the front tile is on

struct ChestModels { VoxelModel base, lid; };

// one part of the chest (rows y0..y1-1 of the cube), every voxel taking the tile pixel of the face it lies on
void buildPart(VoxelModel& out, const BlockDef& b, int frontFace, int y0, int y1, bool inside, u64 tileStamp) {
  const TileSet& ts = app().tiles;
  (void)tileStamp;
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
    if (inside && f == 2 && y == y1 - 1) c = voxShade(c, 0.32f);   // looking into the open chest
    g.set(x, ly, z, c | 0xFF000000u);
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  out.gl.upload(md);
  out.frame.voxel = 1.f / 32; out.frame.origin = vec3(0, y0 / 32.f, 0);
  out.size = g.size; out.kind = VoxelModel::Block; out.built = true; out.key = "chest_part";
}

ChestModels& modelsFor(BlockId id, int facing) {
  static auto& cache = *new std::map<std::string, std::unique_ptr<ChestModels>>();
  const TileSet& ts = app().tiles;
  std::string k = std::to_string(id) + "/" + std::to_string(facing) + "/" + ts.packName + "#" + std::to_string(ts.count);
  auto it = cache.find(k);
  if (it != cache.end()) return *it->second;
  if (cache.size() > 16) cache.clear();
  auto m = std::make_unique<ChestModels>();
  const BlockDef& b = reg().block(id);
  buildPart(m->base, b, FACING_FACE[facing & 3], 0, LID_Y0, true, 0);
  buildPart(m->lid, b, FACING_FACE[facing & 3], LID_Y0, 32, false, 0);
  return *(cache[k] = std::move(m));
}

bool isChest(BlockId id) { const std::string& k = reg().block(id).key; return id && k.size() >= 5 && k.compare(k.size() - 5, 5, "chest") == 0 && reg().block(id).render == Render::Cube; }

class ChestLayer : public RenderLayer {
public:
  ChestLayer() { g_chestHide = false; g_want = false; }
  const char* name() const override { return "chest-lids"; }
  int order() const override { return 130; }
  enum State { Idle, Open, Settling } st = Idle;
  ivec3 pos{0};
  BlockId id = 0; int facing = 0;
  float t = 0, linger = 0;

  void update(GameSession& s, double dt) override {
    if (!s.player) return;
    World& w = s.world();
    float fdt = (float)std::min(dt, 0.1);
    if (g_want && (st == Idle || st == Settling || pos != g_wantPos)) {
      BlockId b = w.getBlock(g_wantPos.x, g_wantPos.y, g_wantPos.z);
      if (isChest(b)) {
        if (st != Idle && pos != g_wantPos) { g_chestHide = false; w.markBlockDirty(pos.x, pos.y, pos.z, true); t = 0; }   // another one: put the old one back
        bool fresh = st == Idle || pos != g_wantPos;
        pos = g_wantPos; id = b; facing = w.getMeta(pos.x, pos.y, pos.z) & 3;
        if (fresh) t = 0;
        g_chestHideX = pos.x; g_chestHideY = pos.y; g_chestHideZ = pos.z; g_chestHide = true;
        if (st != Open) w.markBlockDirty(pos.x, pos.y, pos.z, true);
        st = Open;
      }
    }
    if (st == Open) {
      bool stillThere = isChest(w.getBlock(pos.x, pos.y, pos.z));
      t = clampv(t + ((g_want && stillThere && pos == g_wantPos) ? 1.f : -1.f) * fdt * 3.4f, 0.f, 1.f);
      if (!stillThere) { g_chestHide = false; st = Idle; }
      else if (!(g_want && pos == g_wantPos) && t <= 0) {   // shut: the mesher takes the chest back, we draw it until its mesh is there
        g_chestHide = false; w.markBlockDirty(pos.x, pos.y, pos.z, true);
        st = Settling; linger = 0.7f;
      }
    } else if (st == Settling) { linger -= fdt; if (linger <= 0) st = Idle; }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (st == Idle || !s.player) return;
    World& w = s.world();
    ChestModels& cm = modelsFor(id, facing);
    if (!cm.base.ready()) return;
    vec3 rel = f.cam.rel(glm::dvec3(pos));
    if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) return;
    u8 l = w.getLight(pos.x, pos.y + 1, pos.z);
    VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    mat4 m = glm::translate(mat4(1), rel);
    VR.draw(cm.base.gl, m * cm.base.matrix(), L);
    // the lid turns about its back edge: the front rises
    static const vec3 FRONT[4] = {vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 0, -1), vec3(-1, 0, 0)};
    vec3 d = FRONT[facing & 3];
    float e = 1 - (1 - t) * (1 - t), ang = 1.2f * e;
    vec3 pivot = vec3(0.5f, LID_Y0 / 32.f, 0.5f) - 0.5f * d;
    mat4 lm = glm::translate(m, pivot);
    lm = glm::rotate(lm, ang, glm::cross(d, vec3(0, 1, 0)));
    lm = glm::translate(lm, -pivot);
    VR.draw(cm.lid.gl, lm * cm.lid.matrix(), L);
    VR.end();
  }
};
} // namespace

void chestScreenOpened(ivec3 pos, bool open) {
  if (open) { g_want = true; g_wantPos = pos; }
  else if (g_want && g_wantPos == pos) g_want = false;
}
void registerChestLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<ChestLayer>(); }); }

} // namespace dq
