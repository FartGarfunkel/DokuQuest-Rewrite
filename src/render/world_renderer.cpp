#include "render/world_renderer.h"

namespace dq {

void FrameCtx::apply(Shader& s) const {
  s.set("uTime", (float)std::fmod(time, 3600.0));
  s.set("uDaylight", daylight);
  s.set("uSunDir", sunDir);
  s.set("uSkyColor", skyColor);
  s.set("uFogColor", fogColor);
  s.set("uFogStart", fogStart);
  s.set("uFogEnd", fogEnd);
  s.set("uBrightness", settings ? settings->brightness : 0.35f);
  s.set("uMicroDist", settings ? (float)settings->microVoxelDist : 48.f);
  s.set("uWind", wind);
  s.set("uBlockLightColor", blockLightColor);
}

static const std::vector<VertexAttrib> CHUNK_ATTRS = {
  {0, 3, GL_SHORT, GL_FALSE, false, 0},
  {1, 1, GL_UNSIGNED_SHORT, GL_FALSE, true, 6},
  {2, 2, GL_UNSIGNED_BYTE, GL_FALSE, false, 8},
  {3, 2, GL_UNSIGNED_BYTE, GL_FALSE, true, 10},
  {4, 3, GL_UNSIGNED_BYTE, GL_TRUE, false, 12},
  {5, 1, GL_UNSIGNED_BYTE, GL_FALSE, true, 15},
};

WorldRenderer::~WorldRenderer() = default;

void TileTextures::bind(Shader& s, int u) const {
  const char* names[4] = {"uTiles", "uTiles2", "uHeights", "uHeights2"};
  GLuint ids[4] = {color[0], color[1], height[0], height[1]};
  for (int i = 0; i < 4; i++) { glActiveTexture(GL_TEXTURE0 + u + i); glBindTexture(GL_TEXTURE_2D_ARRAY, ids[i]); s.set(names[i], u + i); }
  s.set("uPageSize", (float)page);
  glActiveTexture(GL_TEXTURE0);
}

bool WorldRenderer::init(const Settings& s, const TileTextures& t) {
  tex = t;
  microVoxels = s.microVoxels; microDist = (float)s.microVoxelDist;
  mopt.fancyLeaves = s.fancyLeaves; mopt.wavingPlants = s.wavingPlants;
  reloadShaders();
  return shader.ok();
}
void WorldRenderer::reloadShaders() {
  shader.load("chunk.glsl", "chunk.glsl", {"MICRO_VOXELS " + std::to_string(microVoxels)});
}
void WorldRenderer::setWorld(World* nw) {
  if (nw == w) return;
  w = nw; meshes.clear(); inFlight.clear(); again.clear();
  std::lock_guard<std::mutex> lk(shared->mx); shared->done.clear();
}
void WorldRenderer::remeshAll() {
  if (!w) return;
  for (auto& [k, c] : w->chunks()) for (int sy = 0; sy < SECS; sy++) w->markSectionDirty(k.dim, k.cx, sy, k.cz);
}

void WorldRenderer::submit(const SectionKey& k, ThreadPool& pool, float pri) {
  if (inFlight.count(k)) { again.insert(k); return; }
  auto in = std::make_shared<MeshInput>();
  if (!gatherMeshInput(*w, k, *in)) return;
  if (in->empty) { meshes.erase(k); return; }
  inFlight.insert(k);
  auto sh = shared; MesherOptions mo = mopt;
  pool.submit([sh, in, k, mo] {
    Result r; r.k = k;
    buildSectionMesh(*in, r.out, mo);
    std::lock_guard<std::mutex> lk(sh->mx);
    sh->done.push_back(std::move(r));
  }, pri);
}

void WorldRenderer::upload(Result& r) {
  inFlight.erase(r.k);
  if (!w || !w->chunk(r.k.dim, r.k.cx, r.k.cz)) { meshes.erase(r.k); return; }
  if (r.out.empty()) { meshes.erase(r.k); return; }
  auto& m = meshes[r.k];
  if (!m) { m = std::make_unique<SecMesh>(); m->o.setup(16, CHUNK_ATTRS); m->c.setup(16, CHUNK_ATTRS); m->t.setup(16, CHUNK_ATTRS); }
  m->origin = vec3(r.k.cx * 16, dimBase(r.k.dim) + r.k.sy * 16, r.k.cz * 16);
  auto up = [](Mesh& mesh, std::vector<ChunkVertex>& v, size_t& n) {
    n = v.size() / 4;
    if (n) mesh.uploadQuads(v.data(), v.size() * sizeof(ChunkVertex), n); else mesh.release();
  };
  up(m->o, r.out.opaque, m->no); up(m->c, r.out.cutout, m->nc); up(m->t, r.out.trans, m->nt);
  // the release() above drops the VAO, so re-setup on next upload is needed
  if (!m->no) m->o.setup(16, CHUNK_ATTRS);
  if (!m->nc) m->c.setup(16, CHUNK_ATTRS);
  if (!m->nt) m->t.setup(16, CHUNK_ATTRS);
}

void WorldRenderer::update(const Camera& cam, ThreadPool& pool, double budgetMs) {
  if (!w) return;
  for (auto& k : w->drainUnloaded()) for (int sy = 0; sy < SECS; sy++) meshes.erase({k.dim, k.cx, sy, k.cz});
  auto dirty = w->drainDirtySections();
  vec3 cp = vec3(cam.pos);
  for (auto& k : dirty) {
    vec3 c(k.cx * 16 + 8, dimBase(k.dim) + k.sy * 16 + 8, k.cz * 16 + 8);
    float d = glm::distance(c, cp);
    submit(k, pool, d - 10000);   // meshing beats generation in the shared pool
  }
  for (auto& k : std::vector<SectionKey>(again.begin(), again.end())) if (!inFlight.count(k)) { again.erase(k); submit(k, pool, -20000); }
  double t0 = nowSeconds();
  std::vector<Result> done;
  { std::lock_guard<std::mutex> lk(shared->mx); done.swap(shared->done); }
  size_t i = 0;
  for (; i < done.size(); i++) {
    upload(done[i]);
    if ((nowSeconds() - t0) * 1000 > budgetMs) { i++; break; }
  }
  if (i < done.size()) { std::lock_guard<std::mutex> lk(shared->mx); for (; i < done.size(); i++) shared->done.push_back(std::move(done[i])); }
  pendingJobs = (int)inFlight.size();
}

void WorldRenderer::drawPass(const FrameCtx& f, int pass) {
  if (!w) return;
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", f.cam.viewProj);
  glm::dvec3 wo = glm::mod(f.cam.origin, glm::dvec3(4096.0));
  shader.set("uWorldOrigin", vec3(wo));
  shader.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  shader.set("uPass", pass);
  tex.bind(shader);
  GLint locO = shader.loc("uOrigin");
  if (pass == 0) {
    visible.clear(); totalSections = (int)meshes.size();
    for (auto& [k, m] : meshes) {
      vec3 rel = vec3(glm::dvec3(m->origin) - f.cam.origin);
      if (!f.cam.frustum.box(rel, rel + vec3(16))) continue;
      if (clipPlane.w != 0 || clipPlane.x != 0 || clipPlane.y != 0 || clipPlane.z != 0) {
        // skip sections entirely behind the portal plane
        vec3 c = rel + vec3(8);
        if (glm::dot(vec3(clipPlane), c) + clipPlane.w < -14) continue;
      }
      visible.push_back({glm::length(rel + vec3(8) - vec3(f.cam.pos - f.cam.origin)), m.get()});
    }
    std::sort(visible.begin(), visible.end(), [](auto& a, auto& b) { return a.first < b.first; });
    drawnSections = (int)visible.size();
  }
  if (pass == 2) {
    for (auto it = visible.rbegin(); it != visible.rend(); ++it) {
      SecMesh* m = it->second; if (!m->nt) continue;
      glUniform3fv(locO, 1, &vec3(glm::dvec3(m->origin) - f.cam.origin)[0]);
      m->t.draw();
    }
    return;
  }
  for (auto& [d, m] : visible) {
    size_t n = pass == 0 ? m->no : m->nc;
    if (!n) continue;
    vec3 rel = vec3(glm::dvec3(m->origin) - f.cam.origin);
    glUniform3fv(locO, 1, &rel[0]);
    (pass == 0 ? m->o : m->c).draw();
  }
}

void WorldRenderer::drawOpaque(const FrameCtx& f) { drawPass(f, 0); }
void WorldRenderer::drawCutout(const FrameCtx& f) { glDisable(GL_CULL_FACE); drawPass(f, 1); glEnable(GL_CULL_FACE); }
void WorldRenderer::drawTranslucent(const FrameCtx& f) {
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  drawPass(f, 2);
  glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

} // namespace dq
