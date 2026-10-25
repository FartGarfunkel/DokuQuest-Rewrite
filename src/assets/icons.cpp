// assets/icons.cpp — the icon atlas: every item's voxel model rendered from a 3/4 view (blocks as the
// original's renderBlockIcons view: rotateX 30°, rotateY 45°), supersampled 2x. Flat sprites first as a
// fallback, so the atlas is valid even without FBOs.
#include "assets/icons.h"
#include "assets/tileset.h"
#include "assets/item_models.h"
#include "core/registry.h"
#include "render/gl.h"
#include "client/app.h"
#include <atomic>
#include <thread>
#include <cstring>
#include <cstdlib>

namespace dq {

ItemIcons& itemIcons() { static ItemIcons i; return i; }

// Isometric block icon like the original drawIsoBlock (render/textures.js): top, left and right faces.
static void drawIsoBlock(u8* dst, int stride, const TileSet& ts, const std::array<u16, 6>& faces) {
  const int S = ts.size;
  auto px = [&](int tile, int u, int v) -> const u8* { tile = clampv(tile, 0, ts.count - 1); return ts.tile(tile) + ((size_t)clampv(v, 0, S - 1) * S + clampv(u, 0, S - 1)) * 4; };
  auto put = [&](int x, int y, const u8* c, float shade) {
    if (x < 0 || y < 0 || x >= S || y >= S || c[3] < 128) return;
    u8* d = dst + (size_t)y * stride + x * 4;
    d[0] = (u8)(c[0] * shade); d[1] = (u8)(c[1] * shade); d[2] = (u8)(c[2] * shade); d[3] = 255;
  };
  float h = S / 4.f;
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    float fx = x + 0.5f, fy = y + 0.5f, cx = S / 2.f;
    float a = (fx - cx) / (S / 2.f), b = (fy - h) / h;
    float u = (a + b) * 0.5f + 0.5f, v = (b - a) * 0.5f + 0.5f;
    if (u >= 0 && u < 1 && v >= 0 && v < 1) { put(x, y, px(faces[2], (int)(u * S), (int)(v * S)), 1.0f); continue; }
    if (fx < cx) {
      float t = fx / cx; float top = h + t * h; float bot = top + S / 2.f;
      if (fy >= top && fy < bot) put(x, y, px(faces[4], (int)(t * S), (int)((fy - top) / (S / 2.f) * S)), 0.78f);
    } else {
      float t = (fx - cx) / cx; float top = 2 * h - t * h; float bot = top + S / 2.f;
      if (fy >= top && fy < bot) put(x, y, px(faces[0], (int)(t * S), (int)((fy - top) / (S / 2.f) * S)), 0.6f);
    }
  }
}

void ItemIcons::build(const TileSet& ts) {
  release();
  itemModelsReset(ts, app().pool.get());
  const Registry& R = reg();
  // ---- raw tiles (32px cells)
  tileSize = ts.size; tileCount = ts.count;
  int trows = (ts.count + tileCols - 1) / tileCols;
  tileW = tileCols * tileSize; tileH = std::max(1, trows) * tileSize;
  {
    std::vector<u8> img((size_t)tileW * tileH * 4, 0);
    for (int t = 0; t < ts.count; t++) {
      u8* d = img.data() + ((size_t)(t / tileCols) * tileSize * tileW + (t % tileCols) * tileSize) * 4;
      for (int y = 0; y < tileSize; y++) std::memcpy(d + (size_t)y * tileW * 4, ts.tile(t) + (size_t)y * tileSize * 4, (size_t)tileSize * 4);
    }
    tileTex = makeTexture2D(tileW, tileH, img.data(), true, false, false);
  }
  // ---- item icons: the flat fallback first (sprites and iso blocks, doubled up to the icon size)
  int n = (int)R.items.size();
  int rows = (n + cols - 1) / cols;
  atlasW = cols * size; atlasH = std::max(1, rows) * size;
  std::vector<u8> img((size_t)atlasW * atlasH * 4, 0), cell32((size_t)ts.size * ts.size * 4);
  int i = 0;
  for (auto& it : R.items) {
    std::fill(cell32.begin(), cell32.end(), 0);
    bool isBlockIcon = startsWith(it.icon, "block:") && !it.block.empty();
    if (isBlockIcon) {
      const BlockDef& b = R.block(R.blockId(it.block));
      if (b.render == Render::Cross || b.render == Render::Torch) isBlockIcon = false;
    }
    if (isBlockIcon) drawIsoBlock(cell32.data(), ts.size * 4, ts, R.block(R.blockId(it.block)).faces);
    else {
      int t = R.tile(startsWith(it.icon, "block:") ? R.block(R.blockId(it.block)).tex : it.icon);
      if (t < 0 && !it.block.empty()) t = R.block(R.blockId(it.block)).faces[0];
      if (t >= 0 && t < ts.count) std::memcpy(cell32.data(), ts.tile(t), cell32.size());
    }
    u8* d = img.data() + ((size_t)(i / cols) * size * atlasW + (i % cols) * size) * 4;
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++)
      std::memcpy(d + ((size_t)y * atlasW + x) * 4, &cell32[((size_t)(y * ts.size / size) * ts.size + x * ts.size / size) * 4], 4);
    slot[it.key] = i++;
  }
  tex = makeTexture2D(atlasW, atlasH, img.data(), false, false, false);
  // ---- then every item's voxel model, rendered over its fallback — or the same, cached from last time
  // (keyed by the tiles' pixels, the items and the builders' version)
  u64 h = 1469598103934665603ull;
  auto mix = [&](const void* p, size_t k) { const u8* b = (const u8*)p; for (size_t q = 0; q < k; q++) { h ^= b[q]; h *= 1099511628211ull; } };
  const char* VERSION = "dq-item-icons-7";
  mix(VERSION, std::strlen(VERSION)); mix(&size, sizeof size); mix(ts.rgba.data(), ts.rgba.size()); mix(ts.res.data(), ts.res.size());
  for (auto& it : R.items) { mix(it.key.data(), it.key.size()); mix(it.icon.data(), it.icon.size()); mix(it.block.data(), it.block.size()); h ^= 0xFF; }
  fs::path cacheDir = userSub("cache"), cacheFile = cacheDir / strFormat("icons-%016llx.png", (unsigned long long)h);
  int cw = 0, ch = 0; std::vector<u8> cached;
  double t0 = nowSeconds();
  if (loadPng(cacheFile, cw, ch, cached) && cw == atlasW && ch == atlasH) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlasW, atlasH, GL_RGBA, GL_UNSIGNED_BYTE, cached.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3);
    DQ_LOG("icons: %d voxel-model icons from the cache (%.0f ms)", n, (nowSeconds() - t0) * 1000);
    return;
  }
  std::vector<u8> out;
  render3D(ts, out);
  DQ_LOG("icons: %d items rendered as voxel models in %.0f ms", n, (nowSeconds() - t0) * 1000);
  if (!out.empty()) {
    std::error_code ec;
    // keep the newest few (switching packs back and forth stays quick)
    std::vector<std::pair<fs::file_time_type, fs::path>> old;
    for (auto& e : fs::directory_iterator(cacheDir, ec)) { std::string f = e.path().filename().string(); if (startsWith(f, "icons-") && endsWith(f, ".png")) old.push_back({fs::last_write_time(e.path(), ec), e.path()}); }
    std::sort(old.begin(), old.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (size_t k = 2; k < old.size(); k++) fs::remove(old[k].second, ec);
    savePng(cacheFile, atlasW, atlasH, out.data());
  }
}

void ItemIcons::render3D(const TileSet& ts, std::vector<u8>& px) {
  if (!voxelRenderer().init()) return;
  const Registry& R = reg();
  const int SS = size * 2;                      // supersampled render size
  GLint prevFbo = 0, prevVp[4]; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, prevVp);
  GLuint srcTex, depth, src, dst;
  glGenTextures(1, &srcTex); glBindTexture(GL_TEXTURE_2D, srcTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SS, SS, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, SS, SS);
  glGenFramebuffers(1, &src); glBindFramebuffer(GL_FRAMEBUFFER, src);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glGenFramebuffers(1, &dst); glBindFramebuffer(GL_FRAMEBUFFER, dst);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (ok) {
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    std::vector<std::string> keys; keys.reserve(R.items.size());
    for (auto& it : R.items) keys.push_back(it.key);
    ThreadPool* pool = app().pool.get();
    const size_t BATCH = 48;
    std::vector<std::unique_ptr<VoxelModel>> models(BATCH);
    VoxelMeshGL gm;
    for (size_t b0 = 0; b0 < keys.size(); b0 += BATCH) {
      size_t nb = std::min(BATCH, keys.size() - b0);
      for (auto& m : models) m = std::make_unique<VoxelModel>();
      // build the batch on the workers, the main thread helping
      std::atomic<size_t> next{0};
      auto work = [&]() { for (size_t i; (i = next++) < nb;) buildItemModel(keys[b0 + i], *models[i]); };
      int helpers = pool ? std::min(pool->threads(), 3) : 0;
      std::atomic<int> running{helpers};
      for (int h = 0; h < helpers; h++) pool->submit([&]() { work(); running--; }, -100.f);
      work();
      while (running > 0) std::this_thread::yield();   // every helper is out before the batch's state goes
      for (size_t i = 0; i < nb; i++) {
        VoxelModel& m = *models[i];
        if (m.data.verts.empty()) continue;
        gm.upload(m.data);
        // the view: blocks like the original's block icons, everything else turned a little to show it's solid
        bool block = m.kind == VoxelModel::Block;
        mat4 rot(1);
        if (block) { rot = glm::rotate(rot, PI / 6, vec3(1, 0, 0)); rot = glm::rotate(rot, PI / 4, vec3(0, 1, 0)); }
        else { rot = glm::rotate(rot, 0.32f, vec3(1, 0, 0)); rot = glm::rotate(rot, -0.55f, vec3(0, 1, 0)); }
        mat4 M = rot * m.matrix();
        // fit: blocks keep the original's size (E = 0.84); items fill the icon
        vec3 lo(1e9f), hi(-1e9f);
        for (int c = 0; c < 8; c++) {
          vec3 p = vec3(M * vec4(c & 1 ? m.size.x : 0, c & 2 ? m.size.y : 0, c & 4 ? m.size.z : 0, 1));
          lo = glm::min(lo, p); hi = glm::max(hi, p);
        }
        mat4 view(1);
        if (block) {
          // a unit cube projects 1.41 wide and 1.57 tall; shaped blocks taller or wider than a cube (doors) shrink to fit
          float k = std::max(1.f, std::max((hi.x - lo.x) / 1.45f, (hi.y - lo.y) / 1.62f));
          if (k > 1.001f) { vec3 c = (lo + hi) * 0.5f; view = glm::scale(mat4(1), vec3(1 / (0.84f * k))) * glm::translate(mat4(1), vec3(-c.x, -c.y, 0)); }
          else view = glm::translate(mat4(1), vec3(0, -0.02f, 0)) * glm::scale(mat4(1), vec3(1 / 0.84f));
        } else {
          float ext = std::max(hi.x - lo.x, hi.y - lo.y) * 0.5f / 0.9f;
          vec3 c = (lo + hi) * 0.5f;
          view = glm::scale(mat4(1), vec3(1 / std::max(ext, 1e-3f))) * glm::translate(mat4(1), vec3(-c.x, -c.y, 0));
        }
        float zr = std::max(std::abs(lo.z), std::abs(hi.z)) + 1;
        mat4 proj = glm::ortho(-1.f, 1.f, -1.f, 1.f, -zr * 4, zr * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, src);
        glViewport(0, 0, SS, SS);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        voxelRenderer().beginNeutral(proj * view);
        voxelRenderer().draw(gm, M);
        // into the atlas (rows there run top-down like the image; GL's run bottom-up, so flip)
        int s = slot[keys[b0 + i]];
        int x = (s % cols) * size, y = (s / cols) * size;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
        glBlitFramebuffer(0, 0, SS, SS, x, y + size, x + size, y, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      }
    }
    gm.release();
    voxelRenderer().end();
    // undo the dark fringe left by blending the edges with the clear colour (un-premultiply)
    px.assign((size_t)atlasW * atlasH * 4, 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (size_t k = 0; k < px.size(); k += 4) {
      u8 a = px[k + 3];
      if (a > 0 && a < 255) for (int c = 0; c < 3; c++) px[k + c] = (u8)std::min(255, px[k + c] * 255 / a);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlasW, atlasH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    if (const char* dump = std::getenv("DQ_ICON_DUMP")) {   // a look at the atlas (development)
      savePng(dump, atlasW, atlasH, px.data());
      json sl = json::object(); for (auto& [k, v] : slot) sl[k] = v;
      writeJson(std::string(dump) + ".json", sl, false);
    }
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3);
  } else DQ_WARN("icons: no framebuffer, keeping flat icons");
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
  glDeleteFramebuffers(1, &src); glDeleteFramebuffers(1, &dst); glDeleteRenderbuffers(1, &depth); glDeleteTextures(1, &srcTex);
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  (void)ts;
}

static IconRef refFor(unsigned tex, int i, int cols, int size, int w, int h) {
  IconRef r; r.tex = (void*)(uintptr_t)tex;
  float x = (float)(i % cols) * size, y = (float)(i / cols) * size;
  r.uv0 = vec2(x / w, y / h); r.uv1 = vec2((x + size) / w, (y + size) / h);
  return r;
}
IconRef ItemIcons::get(const std::string& key) const {
  auto it = slot.find(key);
  if (it == slot.end() || !tex) return {};
  return refFor(tex, it->second, cols, size, atlasW, atlasH);
}
IconRef ItemIcons::tile(int t) const {
  if (!tileTex || t < 0 || t >= tileCount) return {};
  return refFor(tileTex, t, tileCols, tileSize, tileW, tileH);
}
void ItemIcons::release() {
  if (tex) { glDeleteTextures(1, (GLuint*)&tex); tex = 0; }
  if (tileTex) { glDeleteTextures(1, (GLuint*)&tileTex); tileTex = 0; }
  slot.clear();
}

} // namespace dq
