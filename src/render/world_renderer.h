// render/world_renderer.h — draws one realm's loaded sections: meshing on workers, uploading within
// a time budget, frustum culling, opaque / cutout / translucent passes.
// Every section's vertices live in ONE big vertex buffer (a free-list arena). Modern path (GL 4.3+):
// each pass is a single glMultiDrawElementsIndirect with per-draw section origins as an instanced
// attribute. Legacy path (GL 3.3): one glDrawElementsBaseVertex per visible section.
#pragma once
#include "render/frame.h"
#include "render/mesher.h"
#include <unordered_set>
#include <map>
#include <mutex>

namespace dq {

struct TileSet;

class WorldRenderer {
public:
  WorldRenderer() = default;
  ~WorldRenderer();
  bool init(const Settings& s, const TileTextures& tex);
  void setWorld(World* w);
  World* world() const { return w; }
  void update(const Camera& cam, ThreadPool& pool, double budgetMs);   // light flush, schedule meshing, upload
  void drawOpaque(const FrameCtx& f);
  void drawCutout(const FrameCtx& f);
  void drawTranslucent(const FrameCtx& f);
  void reloadShaders();
  void remeshAll();
  void setTiles(const TileSet& ts);          // the plant builder's copy of the tiles (+ remesh)
  void setTileTextures(const TileTextures& t) { tex = t; }
  // clip plane for portal views (render only what lies beyond the portal plane), all zero => off
  vec4 clipPlane{0};
  // same-realm portal windows into another land: the far ends to keep meshed (portal_view.cpp)
  std::vector<vec3> keepNear;
  // portal views draw the same sections from another camera in between the main passes: keep the main list
  void pushView() { viewStack.push_back({drawnSections, std::move(visible)}); visible.clear(); }
  void popView() { if (viewStack.empty()) return; drawnSections = viewStack.back().first; visible = std::move(viewStack.back().second); viewStack.pop_back(); }

  // what meshing found, for other systems (particles: waterfall splashes, drips, emitters)
  void forEachFx(const std::function<void(const SectionKey&, const MeshFx&)>& fn) const;
  std::vector<ivec3> waterfallBases(const vec3& near, float radius) const;

  // stats
  int drawnSections = 0, totalSections = 0, pendingJobs = 0;
  size_t arenaBytes = 0, usedBytes = 0;
  double meshMsAvg = 0; int meshCount = 0; double uploadMs = 0;
  bool timing = false; double gpuMs = 0;     // GL timer query around the terrain passes (stats mode)
  MesherOptions mopt;
  int microVoxels = 1;
  float microDist = 48;
  int plantDetail = 1;                       // 0 off, 1 near (PLANT_DETAIL_R), 2 all
  static constexpr float PLANT_DETAIL_R = 44;

private:
  struct SecMesh { u32 base = 0, cap = 0; u32 nq[3] = {0, 0, 0}; vec3 origin{0}; MeshFx fx; bool detail = true; };
  struct Result { SectionKey k; MeshOutput out; u32 gen = 0; };
  u32 generation = 0;   // bumped by setWorld: results from the world before are dropped
  struct Shared { std::mutex mx; std::vector<Result> done; };
  void submit(const SectionKey& k, ThreadPool& pool, float pri, bool detail);
  void upload(Result& r);
  void drawPass(const FrameCtx& f, int pass);
  bool wantDetail(const SectionKey& k, const vec3& cam) const;
  // arena (units: vertices)
  u32 alloc(u32 n);
  void release(u32 base, u32 n);
  void grow(u32 need);
  World* w = nullptr;
  std::unordered_map<SectionKey, std::unique_ptr<SecMesh>, SectionKeyHash> meshes;
  std::unordered_set<SectionKey, SectionKeyHash> inFlight, again, parked;   // parked: another land's sections, meshed when needed
  double parkT = 0;
  bool wanted(const SectionKey& k) const;
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();
  Shader shader;
  TileTextures tex;
  std::vector<std::pair<float, SecMesh*>> visible;
  std::vector<std::pair<int, std::vector<std::pair<float, SecMesh*>>>> viewStack;
  // the arena: pages of up to PAGE_V vertices, each its own buffer (one buffer past 4 GB fails on Mesa: width is 32-bit).
  // A vertex address is page * PAGE_V + offset; a free block never crosses a page.
  static inline u32 PAGE_V = 1u << 25;       // 32M vertices = 640 MB (DQ_ARENA_PAGE_MB to test small pages)
  struct Page { GLuint vao = 0, vbo = 0; u32 cap = 0; };
  std::vector<Page> pages;
  GLuint ibo = 0, instVbo = 0, cmdBuf = 0;
  void setupVao(Page& pg);
  void addFree(u32 base, u32 n);
  std::map<u32, u32> freeList;               // base -> count
  u32 maxQuads = 0;
  vec3 camPos{0};
  double detailT = 0;
  GLuint tq[2] = {0, 0}; int tqi = 0; bool tqActive = false, tqUsed[2] = {false, false};
};

void registerTerrainLayers();   // client start-up: the terrain RenderLayer (terrain_fx.cpp) and the "showcase" world type

} // namespace dq
