// render/world_renderer.h — draws one realm's loaded sections: meshing on workers, uploading within
// a time budget, frustum culling, opaque / cutout / translucent passes.
#pragma once
#include "render/frame.h"
#include "render/mesher.h"
#include <unordered_set>
#include <mutex>

namespace dq {

class WorldRenderer {
public:
  WorldRenderer() = default;
  ~WorldRenderer();
  bool init(const Settings& s, const TileTextures& tex);
  void setWorld(World* w);
  World* world() const { return w; }
  void update(const Camera& cam, ThreadPool& pool, double budgetMs);   // schedule + upload
  void drawOpaque(const FrameCtx& f);
  void drawCutout(const FrameCtx& f);
  void drawTranslucent(const FrameCtx& f);
  void reloadShaders();
  void remeshAll();
  // clip plane for portal views (render only what lies beyond the portal plane), w = 0 => off
  vec4 clipPlane{0};
  // stats
  int drawnSections = 0, totalSections = 0, pendingJobs = 0;
  MesherOptions mopt;
  int microVoxels = 1;
  float microDist = 48;

private:
  struct SecMesh { Mesh o, c, t; vec3 origin{0}; size_t no = 0, nc = 0, nt = 0; };
  struct Result { SectionKey k; MeshOutput out; };
  struct Shared { std::mutex mx; std::vector<Result> done; };
  void submit(const SectionKey& k, ThreadPool& pool, float pri);
  void upload(Result& r);
  void drawPass(const FrameCtx& f, int pass);
  World* w = nullptr;
  std::unordered_map<SectionKey, std::unique_ptr<SecMesh>, SectionKeyHash> meshes;
  std::unordered_set<SectionKey, SectionKeyHash> inFlight, again;
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();
  Shader shader;
  TileTextures tex;
  std::vector<std::pair<float, SecMesh*>> visible;
};

} // namespace dq
