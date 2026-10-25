#include "render/world_renderer.h"
#include "render/mesh_ctx.h"
#include "world/light.h"
#include "assets/tileset.h"
#include <cstring>

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
  s.set("uSea", sea);
  s.set("uMinLight", minLight);
  s.set("uEyeFluid", eyeFluid);
  s.set("uBlockLightColor", blockLightColor);
}

void TileTextures::bind(Shader& s, int u) const {
  const char* names[4] = {"uTiles", "uTiles2", "uHeights", "uHeights2"};
  GLuint ids[4] = {color[0], color[1], height[0], height[1]};
  for (int i = 0; i < 4; i++) { glActiveTexture(GL_TEXTURE0 + u + i); glBindTexture(GL_TEXTURE_2D_ARRAY, ids[i] ? ids[i] : ids[i & 2]); s.set(names[i], u + i); }
  s.set("uPageSize", (float)page);
  glActiveTexture(GL_TEXTURE0);
}

WorldRenderer::~WorldRenderer() {
  if (vao) glDeleteVertexArrays(1, &vao);
  if (tq[0]) glDeleteQueries(2, tq);
  GLuint b[3] = {vbo, instVbo, cmdBuf};
  for (GLuint x : b) if (x) glDeleteBuffers(1, &x);
}

bool WorldRenderer::init(const Settings& s, const TileTextures& t) {
  tex = t;
  microVoxels = s.microVoxels; microDist = (float)s.microVoxelDist;
  mopt.fancyLeaves = s.fancyLeaves; mopt.wavingPlants = s.wavingPlants;
  plantDetail = s.plantDetail == "off" || s.plantDetail == "flat" ? 0 : s.plantDetail == "all" ? 2 : 1;
  applyGrassSetting(s);
  reloadShaders();
  return shader.ok();
}
// settings "grass3d" (kept in Settings::extra; default on, off on the potato preset): blades per grass block
void applyGrassSetting(const Settings& s) {
  bool on = s.extra.value("grass3d", s.preset != "potato");
  setMeshGrassTufts(on ? (gfx.modern ? 11 : 7) : 0);
}
void WorldRenderer::reloadShaders() {
  std::vector<std::string> defs = {"MICRO_VOXELS " + std::to_string(microVoxels)};
  shader.load("chunk.glsl", "chunk.glsl", defs);
}
void WorldRenderer::setWorld(World* nw) {
  if (nw == w) return;
  w = nw;
  for (auto& [k, m] : meshes) release(m->base, m->cap);
  meshes.clear(); inFlight.clear(); again.clear();
  std::lock_guard<std::mutex> lk(shared->mx); shared->done.clear();
}
void WorldRenderer::remeshAll() {
  if (!w) return;
  for (auto& [k, c] : w->chunks()) for (int sy = 0; sy < SECS; sy++) w->markSectionDirty(k.dim, k.cx, sy, k.cz);
}
void WorldRenderer::setTiles(const TileSet& ts) { setMeshTiles(ts); remeshAll(); }

// ---------------------------------------------------------------- arena
static const std::vector<VertexAttrib> CHUNK_ATTRS = {
  {0, 3, GL_SHORT, GL_FALSE, false, 0},            // pos
  {1, 1, GL_UNSIGNED_SHORT, GL_FALSE, true, 6},    // tile | normal << 12
  {2, 2, GL_UNSIGNED_SHORT, GL_FALSE, false, 8},   // uv
  {3, 4, GL_UNSIGNED_BYTE, GL_FALSE, true, 12},    // sway, sky, blk, shade
  {4, 3, GL_UNSIGNED_BYTE, GL_TRUE, false, 16},    // tint
  {5, 1, GL_UNSIGNED_BYTE, GL_FALSE, true, 19},    // mat
};
void WorldRenderer::setupVao() {
  if (!vao) glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  for (auto& a : CHUNK_ATTRS) {
    glEnableVertexAttribArray(a.index);
    if (a.integer) glVertexAttribIPointer(a.index, a.size, a.type, sizeof(ChunkVertex), (void*)(uintptr_t)a.offset);
    else glVertexAttribPointer(a.index, a.size, a.type, a.normalized, sizeof(ChunkVertex), (void*)(uintptr_t)a.offset);
  }
  if (gfx.mdi) {
    if (!instVbo) glGenBuffers(1, &instVbo);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    glVertexAttribDivisor(6, 1);
  }
  ibo = quadIndexBuffer(std::max<u32>(maxQuads, 16384));
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void WorldRenderer::grow(u32 need) {
  u32 ncap = std::max<u32>(capacity ? capacity * 2 : (u32)(64u << 20) / sizeof(ChunkVertex), capacity + need + 65536);
  GLuint nb; glGenBuffers(1, &nb);
  glBindBuffer(GL_COPY_WRITE_BUFFER, nb);
  glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)ncap * sizeof(ChunkVertex), nullptr, GL_DYNAMIC_DRAW);
  if (vbo && capacity) {
    glBindBuffer(GL_COPY_READ_BUFFER, vbo);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, (GLsizeiptr)capacity * sizeof(ChunkVertex));
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glDeleteBuffers(1, &vbo);
  }
  glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
  vbo = nb;
  release(capacity, ncap - capacity);
  capacity = ncap;
  arenaBytes = (size_t)capacity * sizeof(ChunkVertex);
  setupVao();
}
u32 WorldRenderer::alloc(u32 n) {
  for (int pass = 0; pass < 2; pass++) {
    // best fit
    auto best = freeList.end();
    for (auto it = freeList.begin(); it != freeList.end(); ++it) if (it->second >= n && (best == freeList.end() || it->second < best->second)) { best = it; if (it->second == n) break; }
    if (best != freeList.end()) {
      u32 base = best->first, size = best->second;
      freeList.erase(best);
      if (size > n) freeList[base + n] = size - n;
      usedBytes += (size_t)n * sizeof(ChunkVertex);
      return base;
    }
    grow(n);
  }
  return 0;
}
void WorldRenderer::release(u32 base, u32 n) {
  if (!n) return;
  if (base < capacity) usedBytes -= std::min(usedBytes, (size_t)n * sizeof(ChunkVertex));
  auto it = freeList.emplace(base, n).first;
  // merge with the following and preceding blocks
  auto nx = std::next(it);
  if (nx != freeList.end() && it->first + it->second == nx->first) { it->second += nx->second; freeList.erase(nx); }
  if (it != freeList.begin()) { auto pv = std::prev(it); if (pv->first + pv->second == it->first) { pv->second += it->second; freeList.erase(it); } }
}

// ---------------------------------------------------------------- meshing
bool WorldRenderer::wantDetail(const SectionKey& k, const vec3& cam) const {
  if (plantDetail == 0) return false;
  if (plantDetail == 2) return true;
  float dx = k.cx * 16 + 8 - cam.x, dz = k.cz * 16 + 8 - cam.z;
  return std::sqrt(dx * dx + dz * dz) < PLANT_DETAIL_R + 12;
}

void WorldRenderer::submit(const SectionKey& k, ThreadPool& pool, float pri, bool detail) {
  if (inFlight.count(k)) { again.insert(k); return; }
  auto in = std::make_shared<MeshInput>();
  if (!gatherMeshInput(*w, k, *in)) return;
  if (in->empty) { auto it = meshes.find(k); if (it != meshes.end()) { release(it->second->base, it->second->cap); meshes.erase(it); } return; }
  inFlight.insert(k);
  auto sh = shared; MesherOptions mo = mopt; mo.detail = detail;
  pool.submit([sh, in, k, mo] {
    Result r; r.k = k;
    buildSectionMesh(*in, r.out, mo);
    std::lock_guard<std::mutex> lk(sh->mx);
    sh->done.push_back(std::move(r));
  }, pri);
}

void WorldRenderer::upload(Result& r) {
  inFlight.erase(r.k);
  auto it = meshes.find(r.k);
  if (!w || !w->chunk(r.k.dim, r.k.cx, r.k.cz) || r.out.empty()) {
    if (it != meshes.end()) { release(it->second->base, it->second->cap); meshes.erase(it); }
    return;
  }
  meshMsAvg = (meshMsAvg * meshCount + r.out.ms) / (meshCount + 1); meshCount = std::min(meshCount + 1, 500);
  u32 nq[3] = {(u32)r.out.opaque.size() / 4, (u32)r.out.cutout.size() / 4, (u32)r.out.trans.size() / 4};
  u32 nv = (nq[0] + nq[1] + nq[2]) * 4;
  auto& m = meshes[r.k];
  if (!m) m = std::make_unique<SecMesh>();
  if (m->cap < nv || m->cap > nv * 2 + 4096) {
    release(m->base, m->cap); m->cap = 0;
    m->base = alloc(nv); m->cap = nv;
  }
  for (int i = 0; i < 3; i++) { m->nq[i] = nq[i]; maxQuads = std::max(maxQuads, nq[i]); }
  if (maxQuads > 16384) quadIndexBuffer(maxQuads);
  m->origin = vec3(r.k.cx * 16, dimBase(r.k.dim) + r.k.sy * 16, r.k.cz * 16);
  m->fx = std::move(r.out.fx); m->detail = r.out.detail;
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  size_t off = (size_t)m->base * sizeof(ChunkVertex);
  const std::vector<ChunkVertex>* parts[3] = {&r.out.opaque, &r.out.cutout, &r.out.trans};
  for (auto* p : parts) { if (!p->empty()) glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)off, (GLsizeiptr)(p->size() * sizeof(ChunkVertex)), p->data()); off += p->size() * sizeof(ChunkVertex); }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void WorldRenderer::update(const Camera& cam, ThreadPool& pool, double budgetMs) {
  if (!w) return;
  if (!vbo) grow(0);
  if (auto* le = w->system<LightEngine>()) le->flush(*w);   // a placed torch lights up in this frame's meshes
  for (auto& k : w->drainUnloaded()) for (int sy = 0; sy < SECS; sy++) {
    auto it = meshes.find({k.dim, k.cx, sy, k.cz});
    if (it != meshes.end()) { release(it->second->base, it->second->cap); meshes.erase(it); }
  }
  auto dirty = w->drainDirtySections();
  camPos = vec3(cam.pos);
  std::unordered_set<SectionKey, SectionKeyHash> seen;
  for (auto& k : dirty) {
    if (!seen.insert(k).second) continue;
    vec3 c(k.cx * 16 + 8, dimBase(k.dim) + k.sy * 16 + 8, k.cz * 16 + 8);
    float d = glm::distance(c, camPos);
    submit(k, pool, d - 10000, wantDetail(k, camPos));   // meshing beats generation in the shared pool
  }
  for (auto& k : std::vector<SectionKey>(again.begin(), again.end())) if (!inFlight.count(k)) { again.erase(k); submit(k, pool, -20000, wantDetail(k, camPos)); }
  // as you go, the sections coming close are rebuilt with their 3D plants (original perfDetailTick)
  detailT -= 1.0 / 60;
  if (plantDetail == 1 && detailT <= 0) {
    detailT = 0.25;
    std::vector<std::pair<float, SectionKey>> list;
    for (auto& [k, m] : meshes) if (!m->detail && !inFlight.count(k) && wantDetail(k, camPos)) list.push_back({glm::distance(m->origin + vec3(8), camPos), k});
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < list.size() && i < 6; i++) submit(list[i].second, pool, list[i].first - 9000, true);
  }
  double t0 = nowSeconds();
  std::vector<Result> done;
  { std::lock_guard<std::mutex> lk(shared->mx); done.swap(shared->done); }
  size_t i = 0;
  for (; i < done.size(); i++) {
    upload(done[i]);
    if ((nowSeconds() - t0) * 1000 > budgetMs) { i++; break; }
  }
  if (i < done.size()) { std::lock_guard<std::mutex> lk(shared->mx); for (; i < done.size(); i++) shared->done.push_back(std::move(done[i])); }
  uploadMs = (nowSeconds() - t0) * 1000;
  pendingJobs = (int)inFlight.size();
}

// ---------------------------------------------------------------- drawing
struct DrawCmd { u32 count, instanceCount, firstIndex; i32 baseVertex; u32 baseInstance; };

void WorldRenderer::drawPass(const FrameCtx& f, int pass) {
  if (!w || !vao) return;
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", f.cam.viewProj);
  glm::dvec3 wo = glm::mod(f.cam.origin, glm::dvec3(4096.0));
  shader.set("uWorldOrigin", vec3(wo));
  shader.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  shader.set("uPass", pass);
  shader.set("uGrassFade", gfx.modern ? vec2(20, 34) : vec2(13, 22));
  tex.bind(shader);
  if (pass == 0) {
    visible.clear(); totalSections = (int)meshes.size();
    bool clip = clipPlane.x != 0 || clipPlane.y != 0 || clipPlane.z != 0 || clipPlane.w != 0;
    vec3 cf = vec3(f.cam.pos - f.cam.origin);
    // one land at a time (game/seamless.js): the band next door is made ready and meshed near its edge,
    // but only the camera's own band is drawn (other lands show through portal windows only)
    int band = f.dim;
    for (auto& [k, m] : meshes) {
      if (k.dim != band && band >= 0 && band < DIM_COUNT) continue;
      vec3 rel = vec3(glm::dvec3(m->origin) - f.cam.origin);
      if (!f.cam.frustum.box(rel, rel + vec3(16))) continue;
      if (clip && glm::dot(vec3(clipPlane), rel + vec3(8)) + clipPlane.w < -14) continue;   // entirely behind the portal plane
      visible.push_back({glm::length(rel + vec3(8) - cf), m.get()});
    }
    std::sort(visible.begin(), visible.end(), [](auto& a, auto& b) { return a.first < b.first; });   // nearest first: what's hidden costs little
    drawnSections = (int)visible.size();
  }
  glBindVertexArray(vao);
  auto passBase = [&](SecMesh* m) -> i32 { u32 o = m->base; for (int i = 0; i < pass; i++) o += m->nq[i] * 4; return (i32)o; };
  if (gfx.mdi) {
    static std::vector<DrawCmd> cmds; static std::vector<vec3> origins;
    cmds.clear(); origins.clear();
    auto add = [&](SecMesh* m) {
      if (!m->nq[pass]) return;
      cmds.push_back({m->nq[pass] * 6, 1, 0, passBase(m), (u32)origins.size()});
      origins.push_back(vec3(glm::dvec3(m->origin) - f.cam.origin));
    };
    if (pass == 2) for (auto it = visible.rbegin(); it != visible.rend(); ++it) add(it->second);   // far to near
    else for (auto& v : visible) add(v.second);
    if (cmds.empty()) { glBindVertexArray(0); return; }
    glBindBuffer(GL_ARRAY_BUFFER, instVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(origins.size() * sizeof(vec3)), origins.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!cmdBuf) glGenBuffers(1, &cmdBuf);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cmdBuf);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)(cmds.size() * sizeof(DrawCmd)), cmds.data(), GL_STREAM_DRAW);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, (GLsizei)cmds.size(), 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  } else {
    GLint locO = shader.loc("uOrigin");
    auto one = [&](SecMesh* m) {
      if (!m->nq[pass]) return;
      vec3 rel = vec3(glm::dvec3(m->origin) - f.cam.origin);
      glUniform3fv(locO, 1, &rel[0]);
      glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)(m->nq[pass] * 6), GL_UNSIGNED_INT, nullptr, passBase(m));
    };
    if (pass == 2) for (auto it = visible.rbegin(); it != visible.rend(); ++it) one(it->second);
    else for (auto& v : visible) one(v.second);
  }
  glBindVertexArray(0);
}

void WorldRenderer::drawOpaque(const FrameCtx& f) {
  if (timing) {
    if (!tq[0]) glGenQueries(2, tq);
    int prev = tqi ^ 1;   // read last frame's query (no stall)
    if (tqUsed[prev]) { GLint ready = 0; glGetQueryObjectiv(tq[prev], GL_QUERY_RESULT_AVAILABLE, &ready); if (ready) { GLuint64 ns = 0; glGetQueryObjectui64v(tq[prev], GL_QUERY_RESULT, &ns); gpuMs = ns / 1e6; tqUsed[prev] = false; } }
    if (!tqUsed[tqi]) { glBeginQuery(GL_TIME_ELAPSED, tq[tqi]); tqActive = true; }
  }
  drawPass(f, 0);
}
void WorldRenderer::drawCutout(const FrameCtx& f) { drawPass(f, 1); }
void WorldRenderer::drawTranslucent(const FrameCtx& f) {
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
  drawPass(f, 2);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  if (tqActive) { glEndQuery(GL_TIME_ELAPSED); tqUsed[tqi] = true; tqActive = false; tqi ^= 1; }
}

void WorldRenderer::forEachFx(const std::function<void(const SectionKey&, const MeshFx&)>& fn) const { for (auto& [k, m] : meshes) fn(k, m->fx); }
std::vector<ivec3> WorldRenderer::waterfallBases(const vec3& near, float radius) const {
  std::vector<ivec3> out; float r2 = radius * radius;
  for (auto& [k, m] : meshes) {
    if (m->fx.waterfalls.empty()) continue;
    vec3 c = m->origin + vec3(8);
    if (glm::dot(c - near, c - near) > (radius + 14) * (radius + 14)) continue;
    for (auto& p : m->fx.waterfalls) { vec3 d = vec3(p) - near; if (glm::dot(d, d) <= r2) out.push_back(p); }
  }
  return out;
}

} // namespace dq
