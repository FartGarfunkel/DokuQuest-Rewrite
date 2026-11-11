#include "render/world_renderer.h"
#include "render/mesh_ctx.h"
#include "render/tile_anim.h"
#include "render/sky.h"
#include "render/world_fx.h"
#include "world/light.h"
#include "assets/tileset.h"
#include <algorithm>
#include <cstring>
#include <climits>

namespace dq {

void FrameCtx::apply(Shader& s) const {
  s.set("uTime", (float)std::fmod(time, 3600.0));
  s.set("uDaylight", daylight);
  s.set("uSunDir", sunDir);
  s.set("uSkyColor", skyColor);
  s.set("uFogColor", fogColor);
  {   // far land fades into the sky behind it where there's an open sky (common.glsl fogSkyColour)
    const bool open = sky && sky->showSun && !sky->underwater && !underwater && eyeFluid == 0 && portalDepth == 0;
    s.set("uSkyHorizon", horizonColor); s.set("uSkyGlow", sky ? sky->sunGlow : vec3(0));
    s.set("uFogSky", open ? clampv(1.f - sky->mist * 2.f - sky->cloud * 2.f, 0.f, 1.f) : 0.f);
  }
  s.set("uFogStart", fogStart);
  s.set("uHeightFog", heightFog);
  s.set("uFogEnd", fogEnd);
  s.set("uNearFog", nearFog);
  s.set("uSeason", season);
  s.set("uBrightness", settings ? settings->brightness : 0.35f);
  s.set("uMicroDist", settings ? (float)settings->microVoxelDist : 48.f);
  s.set("uWind", wind);
  s.set("uSea", sea);
  s.set("uMinLight", minLight);
  s.set("uEyeFluid", eyeFluid);
  s.set("uAmbient", cubyzAmbient(daylight));
  s.set("uBlockLightColor", blockLightColor);
  // coloured block light: the volume round the camera (render/light_colour_layer.cpp) on unit 12
  glActiveTexture(GL_TEXTURE0 + 12); glBindTexture(GL_TEXTURE_3D, lightVol); glActiveTexture(GL_TEXTURE0);
  s.set("uLightVol", 12); s.set("uLightVolOn", lightVolOn && lightVol ? 1 : 0);
  s.set("uLightVolOrigin", cam.rel(lightVolMin)); s.set("uLightVolSize", vec3(64, 48, 64));
  // dynamic point lights (common.glsl dynLightRgb): xyz relative to the camera's block, w = level, colour separate
  vec4 d[8]; vec3 cols[8]; int n = 0;
  if (portalDepth == 0) for (const DynLight& l : dyn) { if (n >= 8) break; d[n] = vec4(cam.rel(l.pos), l.level); cols[n] = l.color; n++; }
  s.set("uDynN", n);
  if (n) { glUniform4fv(s.loc("uDyn"), n, &d[0].x); glUniform3fv(s.loc("uDynColor"), n, &cols[0].x); }
}

void TileTextures::bind(Shader& s, int u) const {
  const char* names[6] = {"uTiles", "uTiles2", "uHeights", "uHeights2", "uNormals", "uNormals2"};
  GLuint ids[6] = {color[0], color[1], height[0], height[1], normal[0], normal[1]};
  for (int i = 0; i < 6; i++) { glActiveTexture(GL_TEXTURE0 + u + i); glBindTexture(GL_TEXTURE_2D_ARRAY, ids[i] ? ids[i] : ids[i & ~1]); s.set(names[i], u + i); }
  s.set("uPageSize", (float)page);
  glActiveTexture(GL_TEXTURE0 + u + 6); glBindTexture(GL_TEXTURE_2D, tileAnimMap()); s.set("uAnimMap", u + 6);
  glActiveTexture(GL_TEXTURE0);
}

WorldRenderer::~WorldRenderer() {
  for (auto& pg : pages) { glDeleteVertexArrays(1, &pg.vao); glDeleteBuffers(1, &pg.vbo); }
  if (tq[0]) glDeleteQueries(2, tq);
  GLuint b[2] = {instVbo, cmdBuf};
  for (GLuint x : b) if (x) glDeleteBuffers(1, &x);
  if (atlasTex) glDeleteTextures(1, &atlasTex);
}

bool WorldRenderer::init(const Settings& s, const TileTextures& t) {
  tex = t;
  if (const char* e = std::getenv("DQ_ARENA_PAGE_MB")) if (pages.empty()) PAGE_V = (u32)(std::max(4, std::atoi(e)) * 1048576.0 / sizeof(ChunkVertex));
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
  ++generation;   // meshes still being built for the old world are dropped when they come back
  std::fill(climateOwner.begin(), climateOwner.end(), INT64_MIN);   // its climate goes in again as its sections come
  for (auto& [k, m] : meshes) release(m->base, m->cap);
  meshes.clear(); inFlight.clear(); again.clear(); parked.clear(); dirtyQ.clear();
  std::lock_guard<std::mutex> lk(shared->mx); shared->done.clear();
}
void WorldRenderer::remeshAll() {
  if (!w) return;
  for (auto& [k, c] : w->chunks()) for (int sy = 0; sy < SECS; sy++) w->markSectionDirty(k.dim, k.cx, sy, k.cz);
}
void WorldRenderer::setTiles(const TileSet& ts) { setMeshTiles(ts); rebuildAtlas(ts); remeshAll(); }

void WorldRenderer::setPackGeometry(bool on) {
  if (packGeom == on) return;
  packGeom = on;
  mopt.greedy = !on;
  remeshAll();
}

void WorldRenderer::rebuildAtlas(const TileSet& ts) {
  if (atlasTex) { glDeleteTextures(1, &atlasTex); atlasTex = 0; }
  atlasGridN = 1; atlasTile = std::max(1, ts.size);
  const size_t need = (size_t)ts.count * (size_t)ts.size * (size_t)ts.size * 4;
  if (ts.count <= 0 || ts.size <= 0 || ts.rgba.size() < need) return;
  int grid = 1;
  while (grid * grid < ts.count) grid++;
  if ((long)grid * ts.size > 16384) {
    DQ_WARN("tile atlas %d x %d is past the texture limit; shader packs keep the game picture", grid * ts.size, grid * ts.size);
    return;
  }
  atlasGridN = grid;
  const int S = ts.size, A = grid * S;
  std::vector<u8> img((size_t)A * A * 4, 0);
  for (int i = 0; i < ts.count; i++) {
    const int col = i % grid, row = i / grid;
    const u8* src = ts.tile(i);
    for (int y = 0; y < S; y++) {
      u8* dst = img.data() + ((size_t)(row * S + y) * A + col * S) * 4;
      std::memcpy(dst, src + (size_t)y * S * 4, (size_t)S * 4);
    }
  }
  glGenTextures(1, &atlasTex);
  glBindTexture(GL_TEXTURE_2D, atlasTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, A, A, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  DQ_LOG("tile atlas: %d tiles on a %d x %d sheet", ts.count, A, A);
}

// ---------------------------------------------------------------- arena
static const std::vector<VertexAttrib> CHUNK_ATTRS = {
  {0, 4, GL_UNSIGNED_INT, GL_FALSE, true, 0},      // a,b,c,d (mesher.h ChunkVertex). Pack shaders read only this.
  {1, 1, GL_UNSIGNED_INT, GL_FALSE, true, 16},     // e: Cubyz sun/block RGB, 5 bits per channel
};
void WorldRenderer::setupVao(Page& pg) {
  if (!pg.vao) glGenVertexArrays(1, &pg.vao);
  glBindVertexArray(pg.vao);
  glBindBuffer(GL_ARRAY_BUFFER, pg.vbo);
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
  // a new page of its own each time (64 MB, more only for one huge mesh), never a bigger copy of the last one: on a
  // laptop's shared graphics memory, copying a few hundred MB into a fresh buffer held the game for seconds (10 s at
  // 391 MB on an HD 520). Pages are cheap to draw from: the opaque passes bind them as they come.
  const u32 step = (u32)(64u << 20) / sizeof(ChunkVertex);
  const u32 ncap = std::min<u32>(PAGE_V, std::max<u32>(step, need + 65536));
  pages.push_back({});
  Page& pg = pages.back(); const u32 pi = (u32)pages.size() - 1;
  while (glGetError() != GL_NO_ERROR) {}
  glGenBuffers(1, &pg.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, pg.vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)ncap * sizeof(ChunkVertex), nullptr, GL_DYNAMIC_DRAW);
  GLenum ge = glGetError();
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  addFree(pi * PAGE_V, ncap);
  pg.cap = ncap;
  arenaBytes = 0; for (auto& p : pages) arenaBytes += (size_t)p.cap * sizeof(ChunkVertex);
  DQ_LOG("terrain arena: %.0f MB in %d page(s)%s", arenaBytes / 1048576.0, (int)pages.size(), ge == GL_OUT_OF_MEMORY ? " - OUT OF GPU MEMORY" : ge ? " - GL error" : "");
  setupVao(pg);
}
u32 WorldRenderer::alloc(u32 n) {
  for (int pass = 0; pass < 4; pass++) {
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
  usedBytes -= std::min(usedBytes, (size_t)n * sizeof(ChunkVertex));
  addFree(base, n);
}
void WorldRenderer::addFree(u32 base, u32 n) {
  if (!n) return;
  auto it = freeList.emplace(base, n).first;
  // merge with the following and preceding blocks of the same page
  auto nx = std::next(it);
  if (nx != freeList.end() && it->first + it->second == nx->first && it->first / PAGE_V == nx->first / PAGE_V) { it->second += nx->second; freeList.erase(nx); }
  if (it != freeList.begin()) { auto pv = std::prev(it); if (pv->first + pv->second == it->first && pv->first / PAGE_V == it->first / PAGE_V) { pv->second += it->second; freeList.erase(it); } }
}

// ---------------------------------------------------------------- meshing
bool WorldRenderer::wantDetail(const SectionKey& k, const vec3& cam) const {
  if (plantDetail == 0) return false;
  if (plantDetail == 2) return true;
  float dx = k.cx * 16 + 8 - cam.x, dz = k.cz * 16 + 8 - cam.z;
  return std::sqrt(dx * dx + dz * dz) < PLANT_DETAIL_R + 12;
}

bool WorldRenderer::wanted(const SectionKey& k) const {
  if (budgetR < 1e8f) {   // over the memory budget: only this near
    const float dx = k.cx * 16 + 8 - camPos.x, dz = k.cz * 16 + 8 - camPos.z;
    if (dx * dx + dz * dz > budgetR * budgetR) return false;
  }
  int band = dimOfY(clampv((int)std::floor(camPos.y), 0, WORLD_H - 1));
  if (k.dim == band) return true;
  float cy = (float)(dimBase(k.dim) + k.sy * 16 + 8);
  if (std::abs(cy - camPos.y) < 56) return true;
  for (auto& p : keepNear) if (dimOfY(clampv((int)std::floor(p.y), 0, WORLD_H - 1)) == k.dim && std::abs(cy - p.y) < 72) return true;
  return false;
}

void WorldRenderer::submit(const SectionKey& k, ThreadPool& pool, float pri, bool detail) {
  if (inFlight.count(k)) { again.insert(k); return; }
  auto in = std::make_shared<MeshInput>();
  if (!gatherMeshInput(*w, k, *in)) return;
  if (in->empty) { secVis.erase(k); auto it = meshes.find(k); if (it != meshes.end()) { release(it->second->base, it->second->cap); meshes.erase(it); } return; }
  inFlight.insert(k);
  auto sh = shared; MesherOptions mo = mopt; mo.detail = detail; u32 gen = generation;
  pool.submit([sh, in, k, mo, gen] {
    Result r; r.k = k; r.gen = gen;
    buildSectionMesh(*in, r.out, mo);
    std::lock_guard<std::mutex> lk(sh->mx);
    sh->done.push_back(std::move(r));
  }, pri);
}

// Cave culling (the idea of Tommaso Checchi's "advanced cave culling", as Minecraft does it): walk out from the camera's
// section, face to face, only through sections that let you see from the face you came in by to the face you leave
// by, never turning back against a way already gone, only into what the frustum holds. What the walk can't reach is
// shut away behind rock and isn't drawn.
void WorldRenderer::caveWalk(const FrameCtx& f, int band) {
  reachAll = false; reachN = 0;
  const glm::dvec3 cp = f.cam.pos;
  const SectionKey start{band, (int)std::floor(cp.x / 16), (int)std::floor((std::floor(cp.y) - dimBase(band)) / 16), (int)std::floor(cp.z / 16)};
  if (start.sy < 0 || start.sy >= SECS) { reachAll = true; return; }
  static const int DX[6] = {1, -1, 0, 0, 0, 0}, DY[6] = {0, 0, 1, -1, 0, 0}, DZ[6] = {0, 0, 0, 0, 1, -1};
  struct Q { SectionKey k; int8_t from; u8 dirs; };
  static thread_local std::vector<Q> q; q.clear();
  // sections out from the camera at most: the loaded ring (render distance, +1 kept loaded) and the far land's mask
  // edge (distant.cpp maskR = render distance + 3); past that the far land draws it
  const int R = std::clamp((f.settings ? f.settings->renderDist : 16) + 3, 4, 40), W = 2 * R + 1;
  reachAt = start; reachR = R;
  reachGrid.assign((size_t)W * W * SECS, 0);
  auto at = [&](const SectionKey& n) -> u8& { return reachGrid[((size_t)(n.cz - start.cz + R) * W + (n.cx - start.cx + R)) * SECS + n.sy]; };
  q.push_back({start, -1, 0}); at(start) = 1; reachN = 1;
  for (size_t i = 0; i < q.size(); i++) {
    const Q cur = q[i];
    auto vi = secVis.find(cur.k);
    const u16 vis = vi == secVis.end() ? 0x7FFF : vi->second;
    for (int d = 0; d < 6; d++) {
      if (cur.dirs & (1 << (d ^ 1))) continue;   // never back against a way already gone
      if (cur.from >= 0 && !(vis & (1u << sectionVisPair(cur.from, d)))) continue;
      SectionKey n{band, cur.k.cx + DX[d], cur.k.sy + DY[d], cur.k.cz + DZ[d]};
      if (n.sy < 0 || n.sy >= SECS || std::abs(n.cx - start.cx) > R || std::abs(n.cz - start.cz) > R) continue;
      u8& seen = at(n);
      if (seen) continue;
      const vec3 rel = vec3(glm::dvec3(n.cx * 16.0, dimBase(band) + n.sy * 16.0, n.cz * 16.0) - f.cam.origin);
      if (!f.cam.frustum.box(rel, rel + vec3(16))) continue;
      seen = 1; reachN++;
      q.push_back({n, (int8_t)(d ^ 1), (u8)(cur.dirs | (1 << d))});
    }
  }
}

// ---------------------------------------------------------------- climate and tint (what the vertex's tint kind reads)
static constexpr int CLIM = 1024, CLIM_SLOTS = CLIM / 16;
u32 climateTint(const char* kind, u8 t, u8 h);   // render/mesher.cpp
void WorldRenderer::buildTintLut() {
  // the colours the mesher used to put on each vertex (MeshCtx::cornerTint): grass, foliage (the pack's colormap, else
  // the grass colour a shade darker), water; indexed by the climate bytes
  std::vector<u8> px((size_t)256 * 256 * 4 * 3);
  u32 sig = 2166136261u;
  for (int layer = 0; layer < 3; layer++) for (int h = 0; h < 256; h++) for (int t = 0; t < 256; t++) {
    u32 c;
    if (layer == 2) c = climateTint("water", (u8)t, (u8)h);
    else if (layer == 0) c = climateTint("grass", (u8)t, (u8)h);
    else {
      bool ok = false; c = packTint("foliage", (u8)t, (u8)h, ok);
      if (!ok) { u32 g = climateTint("grass", (u8)t, (u8)h); c = (u32)std::lround((g & 255) * 0.8f * 0.82f) | (u32)std::lround(((g >> 8) & 255) * 0.8f * 0.86f) << 8 | (u32)std::lround(((g >> 16) & 255) * 0.8f * 0.8f) << 16; }
    }
    u8* p = &px[(((size_t)layer * 256 + h) * 256 + t) * 4];
    p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255; p[3] = 255;
    if ((t & 63) == 7 && (h & 63) == 7) sig = (sig ^ c) * 16777619u;
  }
  lutSig = sig;
  if (!tintLut) glGenTextures(1, &tintLut);
  glBindTexture(GL_TEXTURE_2D_ARRAY, tintLut);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 256, 256, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}
void WorldRenderer::climateFor(int dim, int cx, int cz) {
  if (!w || dim < 0 || dim >= DIM_COUNT) return;
  if (!climateTex) {
    glGenTextures(1, &climateTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, climateTex);
    std::vector<u8> mid((size_t)CLIM * CLIM * 2, 128);   // a mild climate where nothing's been seen yet
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RG8, CLIM, CLIM, DIM_COUNT, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    for (int d = 0; d < DIM_COUNT; d++) glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, d, CLIM, CLIM, 1, GL_RG, GL_UNSIGNED_BYTE, mid.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    climateOwner.assign((size_t)DIM_COUNT * CLIM_SLOTS * CLIM_SLOTS, INT64_MIN);
  }
  const int sx = floormod(cx, CLIM_SLOTS), sz = floormod(cz, CLIM_SLOTS);
  i64& own = climateOwner[((size_t)dim * CLIM_SLOTS + sz) * CLIM_SLOTS + sx];
  const i64 me = ((i64)cx << 32) ^ (u32)cz;
  if (own == me) return;
  const Chunk* ch = w->chunk(dim, cx, cz);
  if (!ch) return;
  u8 px[16 * 16 * 2];
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) { px[(z * 16 + x) * 2] = ch->climate[(z * 16 + x) * 2]; px[(z * 16 + x) * 2 + 1] = ch->climate[(z * 16 + x) * 2 + 1]; }
  glBindTexture(GL_TEXTURE_2D_ARRAY, climateTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, sx * 16, sz * 16, dim, 16, 16, 1, GL_RG, GL_UNSIGNED_BYTE, px);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  own = me;
}

void WorldRenderer::upload(Result& r) {
  if (r.gen != generation) return;   // built for the world shown before a realm change
  climateFor(r.k.dim, r.k.cx, r.k.cz);   // (empty sections too: a neighbour's edge reads this column's climate)
  inFlight.erase(r.k);
  auto it = meshes.find(r.k);
  if (w && w->chunk(r.k.dim, r.k.cx, r.k.cz)) { if (r.out.vis == 0x7FFF) secVis.erase(r.k); else secVis[r.k] = r.out.vis; } else secVis.erase(r.k);
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
  if (m->base / PAGE_V >= pages.size()) return;
  glBindBuffer(GL_ARRAY_BUFFER, pages[m->base / PAGE_V].vbo);
  size_t off = (size_t)(m->base % PAGE_V) * sizeof(ChunkVertex);
  const std::vector<ChunkVertex>* parts[3] = {&r.out.opaque, &r.out.cutout, &r.out.trans};
  for (auto* p : parts) { if (!p->empty()) glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)off, (GLsizeiptr)(p->size() * sizeof(ChunkVertex)), p->data()); off += p->size() * sizeof(ChunkVertex); }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void WorldRenderer::update(const Camera& cam, ThreadPool& pool, double budgetMs) {
  if (!w) return;
  if (pages.empty()) grow(0);
  int nSub = 0;
  if (auto* le = w->system<LightEngine>()) le->flush(*w);   // a placed torch lights up in this frame's meshes
  for (auto& k : w->drainUnloaded()) for (int sy = 0; sy < SECS; sy++) {
    parked.erase({k.dim, k.cx, sy, k.cz});
    secVis.erase({k.dim, k.cx, sy, k.cz});
    auto it = meshes.find({k.dim, k.cx, sy, k.cz});
    if (it != meshes.end()) { release(it->second->base, it->second->cap); meshes.erase(it); }
  }
  camPos = vec3(cam.pos);
  // sections to mesh: nearest first, a few milliseconds of copying them out of the world a frame (streaming in new
  // land marked hundreds at once, 20-90 ms on an i5-6300U); one dirtied again while it waits is meshed once
  for (auto& k : w->drainDirtySections()) dirtyQ.insert(k);
  for (auto it = again.begin(); it != again.end();) { if (!inFlight.count(*it)) { dirtyQ.insert(*it); it = again.erase(it); } else ++it; }
  if (!dirtyQ.empty()) {
    static std::vector<std::pair<float, SectionKey>> order; order.clear();
    for (auto it = dirtyQ.begin(); it != dirtyQ.end();) {
      if (!wanted(*it)) { parked.insert(*it); it = dirtyQ.erase(it); continue; }
      vec3 c(it->cx * 16 + 8, dimBase(it->dim) + it->sy * 16 + 8, it->cz * 16 + 8);
      order.push_back({glm::distance(c, camPos), *it}); ++it;
    }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
    const double ts = nowSeconds();
    for (auto& [d, k] : order) {
      if (nSub > 0 && (nowSeconds() - ts) * 1000 > 2.5) break;
      dirtyQ.erase(k);
      submit(k, pool, d - 10000, wantDetail(k, camPos));   // meshing beats generation in the shared pool
      nSub++;
    }
  }
  // the land next door is only drawn once you cross into it: mesh its sections near the edge (and at the far end of a
  // portal window into it); the rest wait here, and meshes left behind are let go
  parkT -= 1.0 / 60;
  if (parkT <= 0) {
    parkT = 0.5;
    // the memory budget: over it, mesh a chunk less far out; well under it, a chunk further again
    if (budgetBytes > 0) {
      float farthest = 0;
      for (auto& [k, m] : meshes) { const float dx = k.cx * 16 + 8 - camPos.x, dz = k.cz * 16 + 8 - camPos.z; farthest = std::max(farthest, std::sqrt(dx * dx + dz * dz)); }
      if (usedBytes > budgetBytes && farthest > 56) {
        const float was = budgetR; budgetR = std::max(48.f, std::min(budgetR, farthest) - 16.f);
        if (was >= 1e8f) DQ_LOG("terrain memory: %.0f MB is over the %.0f MB budget; meshing out to %.0f blocks (the far land shows the rest)", usedBytes / 1048576.0, budgetBytes / 1048576.0, budgetR);
      } else if (budgetR < 1e8f && usedBytes < budgetBytes * 0.72) {
        budgetR += 16.f;
        if (budgetR > farthest + 48.f) budgetR = 1e9f;
      }
    } else budgetR = 1e9f;
    for (auto it = parked.begin(); it != parked.end();) {
      if (!w->chunk(it->dim, it->cx, it->cz)) { it = parked.erase(it); continue; }
      if (wanted(*it)) {
        vec3 c(it->cx * 16 + 8, dimBase(it->dim) + it->sy * 16 + 8, it->cz * 16 + 8);
        submit(*it, pool, glm::distance(c, camPos) - 10000, wantDetail(*it, camPos)); it = parked.erase(it);
      } else ++it;
    }
    for (auto it = meshes.begin(); it != meshes.end();) {
      if (!inFlight.count(it->first) && !wanted(it->first)) { release(it->second->base, it->second->cap); parked.insert(it->first); it = meshes.erase(it); }
      else ++it;
    }
  }
  // as you go, the sections coming close are rebuilt with their 3D plants (original perfDetailTick)
  detailT -= 1.0 / 60;
  if (plantDetail == 1 && detailT <= 0) {
    detailT = 0.25;
    std::vector<std::pair<float, SectionKey>> list;
    for (auto& [k, m] : meshes) if (!m->detail && !inFlight.count(k) && wantDetail(k, camPos)) list.push_back({glm::distance(m->origin + vec3(8), camPos), k});
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < list.size() && i < 6; i++) submit(list[i].second, pool, list[i].first - 9000, true);
    // and the ones left well behind go back to flat plants, so their 3D plants don't pile up in memory as you travel
    // (3D plants were most of the Nether's terrain memory)
    std::vector<std::pair<float, SectionKey>> far;
    for (auto& [k, m] : meshes) if (m->detail && !inFlight.count(k)) {
      const float dx = k.cx * 16 + 8 - camPos.x, dz = k.cz * 16 + 8 - camPos.z;
      if (std::sqrt(dx * dx + dz * dz) > PLANT_DETAIL_R + 36) far.push_back({-std::sqrt(dx * dx + dz * dz), k});
    }
    std::sort(far.begin(), far.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < far.size() && i < 4; i++) submit(far[i].second, pool, 20000 - far[i].first, false);
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
  pendingJobs = (int)(inFlight.size() + dirtyQ.size());
}

// ---------------------------------------------------------------- drawing
struct DrawCmd { u32 count, instanceCount, firstIndex; i32 baseVertex; u32 baseInstance; };

void WorldRenderer::drawPass(const FrameCtx& f, int pass) {
  if (!w || pages.empty()) return;
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", f.cam.viewProj);
  glm::dvec3 wo = glm::mod(f.cam.origin, glm::dvec3(4096.0));
  shader.set("uWorldOrigin", vec3(wo));
  shader.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  shader.set("uPass", pass);
  shader.set("uGrassFade", gfx.modern ? vec2(20, 34) : vec2(13, 22));
  tex.bind(shader);
  if (pass == 0 && f.time - lutCheckT > 1.0) {   // the pack's colormaps may have changed: look again now and then
    lutCheckT = f.time;
    u32 sig = 2166136261u;
    bool ok = false; const u32 g = packTint("grass", 135, 135, ok), fo = packTint("foliage", 71, 199, ok);
    sig = (sig ^ g) * 16777619u; sig = (sig ^ fo) * 16777619u;
    if (!tintLut || sig != lutSeen) { lutSeen = sig; buildTintLut(); }
  }
  if (!climateTex && w) climateFor(DIM_OVERWORLD, 0, 0);   // (makes the texture)
  glActiveTexture(GL_TEXTURE0 + 13); glBindTexture(GL_TEXTURE_2D_ARRAY, climateTex);
  glActiveTexture(GL_TEXTURE0 + 14); glBindTexture(GL_TEXTURE_2D_ARRAY, tintLut);
  glActiveTexture(GL_TEXTURE0);
  shader.set("uClimate", 13); shader.set("uTintLut", 14);
  worldFxBind(shader, f, pass);   // sun shadows, water you see into (render/world_fx.cpp)
  if (pass == 0) {
    visible.clear(); totalSections = (int)meshes.size();
    bool clip = clipPlane.x != 0 || clipPlane.y != 0 || clipPlane.z != 0 || clipPlane.w != 0;
    vec3 cf = vec3(f.cam.pos - f.cam.origin);
    // one land at a time (game/seamless.js): the band next door is meshed and drawn only near its edge, so a hole
    // through the floor shows the land below while the Skylands stay out of sight from the ground
    int band = f.dim;
    // cave culling: not through a portal's window (its own view), and not when switched off (DQ_NO_CAVE_CULL)
    static const bool noCull = std::getenv("DQ_NO_CAVE_CULL") != nullptr;
    const bool cull = !clip && !noCull && band >= 0 && band < DIM_COUNT;
    if (cull) caveWalk(f, band);
    for (auto& [k, m] : meshes) {
      vec3 rel = vec3(glm::dvec3(m->origin) - f.cam.origin);
      if (k.dim != band && band >= 0 && band < DIM_COUNT && std::abs(m->origin.y + 8 - (float)f.cam.pos.y) >= 56) continue;   // the land next door: only near its edge
      if (cull && k.dim == band && !reached(k)) continue;   // shut away behind rock
      if (!f.cam.frustum.box(rel, rel + vec3(16))) continue;
      if (clip && glm::dot(vec3(clipPlane), rel + vec3(8)) + clipPlane.w < -14) continue;   // entirely behind the portal plane
      visible.push_back({glm::length(rel + vec3(8) - cf), m.get()});
    }
    std::sort(visible.begin(), visible.end(), [](auto& a, auto& b) { return a.first < b.first; });   // nearest first: what's hidden costs little
    drawnSections = (int)visible.size();
    static const bool dbgBands = std::getenv("DQ_DEBUG_BANDS") != nullptr;
    if (dbgBands && f.portalDepth == 0) {
      static double lastT = 0; double now = nowSeconds();
      if (now - lastT > 1.0) {
        lastT = now;
        int per[DIM_COUNT] = {0}, vis[DIM_COUNT] = {0}, chunks[DIM_COUNT] = {0};
        for (auto& [k, m] : meshes) if (k.dim >= 0 && k.dim < DIM_COUNT) per[k.dim]++;
        for (auto& v : visible) { int d = dimOfY((int)v.second->origin.y); if (d >= 0 && d < DIM_COUNT) vis[d]++; }
        for (auto& [k, c] : w->chunks()) if (k.dim >= 0 && k.dim < DIM_COUNT) chunks[k.dim]++;
        u64 q0 = 0, q1 = 0, q2 = 0, qd = 0; int nd = 0; for (auto& [kk, mm] : meshes) { q0 += mm->nq[0]; q1 += mm->nq[1]; q2 += mm->nq[2]; if (mm->detail) { nd++; qd += mm->nq[1]; } }
        DQ_LOG("bands: detailed sections %d with %llu cutout quads", nd, (unsigned long long)qd);
        u64 v0 = 0, v1 = 0, v2 = 0; for (auto& v : visible) { v0 += v.second->nq[0]; v1 += v.second->nq[1]; v2 += v.second->nq[2]; }
        DQ_LOG("bands: drawn quads %llu/%llu/%llu in %d sections", (unsigned long long)v0, (unsigned long long)v1, (unsigned long long)v2, (int)visible.size());
        DQ_LOG("bands: quads %llu/%llu/%llu | arena %.0f MB used %.0f MB | cam %.0f %.0f %.0f dim %d | chunks %d/%d/%d/%d meshes %d/%d/%d/%d visible %d/%d/%d/%d inFlight %d again %d", (unsigned long long)q0, (unsigned long long)q1, (unsigned long long)q2, arenaBytes / 1048576.0, usedBytes / 1048576.0, f.cam.pos.x, f.cam.pos.y, f.cam.pos.z, f.dim,
               chunks[0], chunks[1], chunks[2], chunks[3], per[0], per[1], per[2], per[3], vis[0], vis[1], vis[2], vis[3], (int)inFlight.size(), (int)again.size());
      }
    }
  }
  emitSections(f, pass, gfx.mdi ? -1 : shader.loc("uOrigin"));
}

void WorldRenderer::emitSections(const FrameCtx& f, int pass, GLint originLoc) {
  // in page runs: opaque passes page by page (nearest first within each), the translucent one far to near
  static std::vector<SecMesh*> order; order.clear();
  // (water, glass and ice only in the camera's own land: from under the cloud layer the Skylands' falls, which end at
  // its floor, must not hang in the sky; the solid land next door still shows near the edge)
  if (pass == 2) { for (auto it = visible.rbegin(); it != visible.rend(); ++it) if (it->second->nq[pass] && (f.dim < 0 || f.dim >= DIM_COUNT || dimOfY((int)it->second->origin.y) == f.dim)) order.push_back(it->second); }
  else if (!gfx.mdi) { for (auto& v : visible) if (v.second->nq[pass]) order.push_back(v.second); }   // nearest first, pages bound as they come
  else for (u32 p = 0; p < pages.size(); p++) for (auto& v : visible) if (v.second->nq[pass] && v.second->base / PAGE_V == p) order.push_back(v.second);
  if (order.empty()) return;
  auto passBase = [&](SecMesh* m) -> i32 { u32 o = m->base % PAGE_V; for (int i = 0; i < pass; i++) o += m->nq[i] * 4; return (i32)o; };
  if (gfx.mdi) {
    static std::vector<DrawCmd> cmds; static std::vector<vec3> origins;
    cmds.clear(); origins.clear();
    for (SecMesh* m : order) {
      cmds.push_back({m->nq[pass] * 6, 1, 0, passBase(m), (u32)origins.size()});
      origins.push_back(vec3(glm::dvec3(m->origin) - f.cam.origin));
    }
    glBindBuffer(GL_ARRAY_BUFFER, instVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(origins.size() * sizeof(vec3)), origins.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!cmdBuf) glGenBuffers(1, &cmdBuf);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cmdBuf);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, (GLsizeiptr)(cmds.size() * sizeof(DrawCmd)), cmds.data(), GL_STREAM_DRAW);
    for (size_t a = 0; a < order.size();) {
      u32 p = order[a]->base / PAGE_V; size_t b = a;
      while (b < order.size() && order[b]->base / PAGE_V == p) b++;
      glBindVertexArray(pages[p].vao);
      glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(a * sizeof(DrawCmd)), (GLsizei)(b - a), 0);
      a = b;
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  } else {
    u32 bound = ~0u;
    for (SecMesh* m : order) {
      u32 p = m->base / PAGE_V;
      if (p != bound) { glBindVertexArray(pages[p].vao); bound = p; }
      vec3 rel = vec3(glm::dvec3(m->origin) - f.cam.origin);
      if (originLoc >= 0) glUniform3fv(originLoc, 1, &rel[0]);
      glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)(m->nq[pass] * 6), GL_UNSIGNED_INT, nullptr, passBase(m));
    }
  }
  glBindVertexArray(0);
}

void WorldRenderer::drawSections(const FrameCtx& f, int pass) {
  if (!w || pages.empty() || visible.empty()) return;
  GLint loc = -1;
  if (!gfx.mdi) {
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog) loc = glGetUniformLocation((GLuint)prog, "uOrigin");
  }
  emitSections(f, pass, loc);
}

void WorldRenderer::drawShadowSections(const FrameCtx& f, int pass, float radius, vec3 centre, bool aroundCentre) {
  if (!w || pages.empty()) return;
  auto saved = std::move(visible);
  visible.clear();
  const float r = aroundCentre ? radius + 12.f : radius + 24.f;   // (the built-in map: just its own square, and its height round you)
  const float r2 = r * r;
  for (auto& [k, m] : meshes) {
    if (k.dim != f.dim || !m->nq[pass]) continue;
    vec3 rel = vec3(glm::dvec3(m->origin) - f.cam.origin) + vec3(8.f) - centre;
    if (rel.x * rel.x + rel.z * rel.z > r2 || (aroundCentre ? (rel.y < -r * 0.6f || rel.y > r * 1.2f) : std::abs(rel.y) > r)) continue;
    visible.push_back({0.f, m.get()});
  }
  GLint loc = -1;
  if (!gfx.mdi) {
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog) loc = glGetUniformLocation((GLuint)prog, "uOrigin");
  }
  emitSections(f, pass, loc);
  visible = std::move(saved);
}

void WorldRenderer::drawShadowMap(const FrameCtx& f, const mat4& lightVP, vec3 centre, float radius, bool cutout) {
  if (!w || pages.empty()) return;
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", lightVP);
  glm::dvec3 wo = glm::mod(f.cam.origin, glm::dvec3(4096.0));
  shader.set("uWorldOrigin", vec3(wo));
  shader.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  shader.set("uGrassFade", gfx.modern ? vec2(20, 34) : vec2(13, 22));
  tex.bind(shader);
  worldFxBind(shader, f, 0);
  shader.set("uShadowPass", 1); shader.set("uShadowK", 0.f);
  shader.set("uPass", 0); drawShadowSections(f, 0, radius, centre, true);
  if (cutout) { shader.set("uPass", 1); drawShadowSections(f, 1, radius, centre, true); }
  shader.set("uShadowPass", 0);
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
    for (auto& p : m->fx.waterfalls) { vec3 d = vec3(p) - near; if (glm::dot(d, d) <= r2 && dimOfY(p.y) == dimOfY((int)std::floor(near.y))) out.push_back(p); }   // (spray only in your own land)
  }
  return out;
}

} // namespace dq
