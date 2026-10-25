// render/content_draw.cpp — pictures and lines in the world; the little canvas.
#include "render/content_draw.h"

namespace dq {

ContentDraw& contentDraw() { static ContentDraw* d = new ContentDraw(); return *d; }   // kept past exit (GL objects)

bool ContentDraw::init() {
  if (tried) return sh && sh->ok();
  tried = true;
  sh = std::make_unique<Shader>();
  if (!sh->load("content.glsl", "content.glsl")) { DQ_LOG("content: shader failed"); return false; }
  const float Q[] = {-0.5f, -0.5f, 0, 0, 1, 0.5f, -0.5f, 0, 1, 1, 0.5f, 0.5f, 0, 1, 0, -0.5f, -0.5f, 0, 0, 1, 0.5f, 0.5f, 0, 1, 0, -0.5f, 0.5f, 0, 0, 0};
  glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
  glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Q), Q, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
  glGenVertexArrays(1, &lvao); glGenBuffers(1, &lvbo);
  glBindVertexArray(lvao); glBindBuffer(GL_ARRAY_BUFFER, lvbo);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
  glDisableVertexAttribArray(1); glVertexAttrib2f(1, 0, 0);
  glBindVertexArray(0);
  return true;
}

bool ContentDraw::begin(const FrameCtx& f, const mat4& viewProj, bool fog) {
  if (!init()) return false;
  sh->use();
  f.apply(*sh);
  if (!fog) { sh->set("uFogStart", 1e6f); sh->set("uFogEnd", 2e6f); }
  sh->set("uViewProj", viewProj);
  sh->set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  sh->set("uTex", 0);
  glDisable(GL_CULL_FACE);
  return true;
}
void ContentDraw::picture(GLuint tex, const mat4& model, vec4 uv, vec4 color, vec2 light, bool lit) {
  if (!sh || !sh->ok()) return;
  sh->set("uModel", model); sh->set("uUV", uv); sh->set("uColor", color); sh->set("uLight", light);
  sh->set("uUseTex", tex ? 1.f : 0.f); sh->set("uLit", lit ? 1.f : 0.f);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}
void ContentDraw::lines(const std::vector<vec3>& strip, vec4 color, vec2 light) {
  if (!sh || !sh->ok() || strip.size() < 2) return;
  sh->set("uModel", mat4(1)); sh->set("uUV", vec4(0, 0, 1, 1)); sh->set("uColor", color); sh->set("uLight", light);
  sh->set("uUseTex", 0.f); sh->set("uLit", 1.f);
  glBindVertexArray(lvao); glBindBuffer(GL_ARRAY_BUFFER, lvbo);
  glBufferData(GL_ARRAY_BUFFER, strip.size() * sizeof(vec3), strip.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)strip.size());
  glBindVertexArray(0);
}
void ContentDraw::end() { glEnable(GL_CULL_FACE); }

GLuint makeTexRGBA(int w, int h, const u8* rgba, bool nearest, bool mips) {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  if (mips) glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : nearest ? GL_NEAREST : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}
void updateTexRGBA(GLuint tex, int w, int h, const u8* rgba, bool mips) {
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  if (mips) glGenerateMipmap(GL_TEXTURE_2D);
}
u32 hexRGBA(const char* hex, u8 a) {
  unsigned v = 0;
  if (hex && hex[0] == '#') v = (unsigned)strtoul(hex + 1, nullptr, 16);
  return ((v >> 16) & 255) | (((v >> 8) & 255) << 8) | ((v & 255) << 16) | ((u32)a << 24);
}

// ------------------------------------------------------------------ canvas
Canvas::Canvas(int w, int h) : w(w), h(h), px((size_t)w * h * 4, 0.f) {}
void Canvas::translate(float x, float y) { glm::mat3 t(1); t[2] = glm::vec3(x, y, 1); xf = xf * t; }
void Canvas::rotate(float a) { glm::mat3 r(1); r[0] = glm::vec3(std::cos(a), std::sin(a), 0); r[1] = glm::vec3(-std::sin(a), std::cos(a), 0); xf = xf * r; }
void Canvas::scale(float sx, float sy) { glm::mat3 s(1); s[0][0] = sx; s[1][1] = sy; xf = xf * s; }
void Canvas::moveTo(float x, float y) { paths.push_back({{tp(x, y)}, false}); }
void Canvas::lineTo(float x, float y) { if (paths.empty()) moveTo(x, y); else paths.back().pts.push_back(tp(x, y)); }
void Canvas::quadTo(float cx, float cy, float x, float y) {
  if (paths.empty() || paths.back().pts.empty()) { moveTo(x, y); return; }
  glm::mat3 inv = glm::inverse(xf);
  glm::vec3 p0 = inv * glm::vec3(paths.back().pts.back(), 1);
  for (int i = 1; i <= 12; i++) {
    float t = i / 12.f, u = 1 - t;
    lineTo(u * u * p0.x + 2 * u * t * cx + t * t * x, u * u * p0.y + 2 * u * t * cy + t * t * y);
  }
}
void Canvas::arc(float cx, float cy, float r, float a0, float a1, bool ccw) {
  float span = a1 - a0;
  if (!ccw) { while (span < 0) span += TAU; if (span > TAU) span = TAU; }
  else { while (span > 0) span -= TAU; if (span < -TAU) span = -TAU; }
  int n = std::max(4, (int)std::ceil(std::fabs(span) / TAU * 32));
  for (int i = 0; i <= n; i++) {
    float a = a0 + span * i / n, x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
    if (i == 0 && (paths.empty() || paths.back().closed)) moveTo(x, y); else lineTo(x, y);
  }
}
void Canvas::rect(float x, float y, float rw, float rh) { moveTo(x, y); lineTo(x + rw, y); lineTo(x + rw, y + rh); lineTo(x, y + rh); closePath(); }
void Canvas::closePath() {
  if (paths.empty()) return;
  paths.back().closed = true;
  vec2 first = paths.back().pts.front();
  paths.push_back({{first}, false});   // a new subpath starts where the closed one began (a lone point draws nothing)
}

// coverage of a set of polygons (non-zero winding), 4 x 4 samples a pixel, composited source-over
void Canvas::cover(const std::vector<std::vector<vec2>>& polys, u32 col) {
  struct Edge { vec2 a, b; int dir; };
  std::vector<Edge> edges;
  float ymin = 1e9f, ymax = -1e9f;
  for (auto& p : polys) {
    if (p.size() < 3) continue;
    for (size_t i = 0; i < p.size(); i++) {
      vec2 a = p[i], b = p[(i + 1) % p.size()];
      if (a.y == b.y) continue;
      edges.push_back(a.y < b.y ? Edge{a, b, 1} : Edge{b, a, -1});
      ymin = std::min(ymin, std::min(a.y, b.y)); ymax = std::max(ymax, std::max(a.y, b.y));
    }
  }
  if (edges.empty()) return;
  float cr = (col & 255) / 255.f, cg = ((col >> 8) & 255) / 255.f, cb = ((col >> 16) & 255) / 255.f, ca = ((col >> 24) & 255) / 255.f;
  int y0 = std::max(0, (int)std::floor(ymin)), y1 = std::min(h - 1, (int)std::ceil(ymax));
  std::vector<float> cov(w);
  std::vector<std::pair<float, int>> xs;
  for (int y = y0; y <= y1; y++) {
    std::fill(cov.begin(), cov.end(), 0.f);
    bool any = false;
    for (int sy = 0; sy < 4; sy++) {
      float fy = y + (sy + 0.5f) / 4;
      xs.clear();
      for (auto& e : edges) if (fy >= e.a.y && fy < e.b.y) xs.push_back({e.a.x + (fy - e.a.y) / (e.b.y - e.a.y) * (e.b.x - e.a.x), e.dir});
      if (xs.size() < 2) continue;
      std::sort(xs.begin(), xs.end());
      int wind = 0;
      for (size_t i = 0; i + 1 < xs.size(); i++) {
        wind += xs[i].second;
        if (!wind) continue;
        float xa = xs[i].first, xb = xs[i + 1].first;
        for (int x = std::max(0, (int)std::floor(xa)); x <= std::min(w - 1, (int)std::floor(xb)); x++)
          for (int sx = 0; sx < 4; sx++) { float fx = x + (sx + 0.5f) / 4; if (fx >= xa && fx < xb) { cov[x] += 1.f / 16; any = true; } }
      }
    }
    if (!any) continue;
    for (int x = 0; x < w; x++) {
      float a = std::min(1.f, cov[x]) * ca;
      if (a <= 0) continue;
      float* d = &px[((size_t)y * w + x) * 4];
      d[0] = cr * a + d[0] * (1 - a); d[1] = cg * a + d[1] * (1 - a); d[2] = cb * a + d[2] * (1 - a); d[3] = a + d[3] * (1 - a);
    }
  }
}
void Canvas::fill(u32 col) {
  std::vector<std::vector<vec2>> polys;
  for (auto& p : paths) if (p.pts.size() >= 3) polys.push_back(p.pts);
  cover(polys, col);
}
void Canvas::stroke(u32 col, float width) {
  float s = std::sqrt(std::fabs(glm::determinant(glm::mat2(xf)))), hw = width * s * 0.5f;
  std::vector<std::vector<vec2>> polys;
  auto ccw = [](std::vector<vec2>& p) {   // all the same way round, so overlaps add up
    float a = 0; for (size_t i = 0; i < p.size(); i++) { vec2 u = p[i], v = p[(i + 1) % p.size()]; a += u.x * v.y - v.x * u.y; }
    if (a < 0) std::reverse(p.begin(), p.end());
  };
  for (auto& p : paths) {
    size_t n = p.pts.size();
    if (n < 2) continue;
    size_t segs = p.closed ? n : n - 1;
    for (size_t i = 0; i < segs; i++) {
      vec2 a = p.pts[i], b = p.pts[(i + 1) % n], d = b - a;
      float l = glm::length(d); if (l < 1e-5f) continue;
      vec2 nn = vec2(-d.y, d.x) / l * hw;
      std::vector<vec2> q = {a + nn, b + nn, b - nn, a - nn}; ccw(q); polys.push_back(q);
    }
    for (auto& v : p.pts) {   // round joins
      std::vector<vec2> c;
      for (int k = 0; k < 12; k++) c.push_back(v + vec2(std::cos(k * TAU / 12), std::sin(k * TAU / 12)) * hw);
      ccw(c); polys.push_back(c);
    }
  }
  cover(polys, col);
}
void Canvas::fillRect(float x, float y, float rw, float rh, u32 col) {
  auto saved = paths; paths.clear();
  rect(x, y, rw, rh); fill(col);
  paths = saved;
}
std::vector<u8> Canvas::rgba() const {
  std::vector<u8> out((size_t)w * h * 4);
  for (size_t i = 0; i < (size_t)w * h; i++) {
    float a = px[i * 4 + 3];
    for (int k = 0; k < 3; k++) out[i * 4 + k] = (u8)clampv((int)std::lround(a > 0 ? px[i * 4 + k] / a * 255 : 0), 0, 255);
    out[i * 4 + 3] = (u8)clampv((int)std::lround(a * 255), 0, 255);
  }
  return out;
}

} // namespace dq
