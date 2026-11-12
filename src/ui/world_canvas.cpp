// ui/world_canvas.cpp — see world_canvas.h
#include "ui/world_canvas.h"
#include "render/gl.h"
#include <imgui_impl_opengl3.h>
#include <map>

namespace dq {
namespace {
struct Entry { GLuint tex = 0, fbo = 0; int w = 0, h = 0; u64 ver = ~0ull; u64 used = 0; };
std::map<std::string, Entry>& entries() { static auto* m = new std::map<std::string, Entry>(); return *m; }   // (kept past exit: GL objects)
u64 g_clock = 0;
}  // namespace

CanvasPic canvasPaint(const std::string& key, u64 ver, int w, int h, const std::function<void(ImDrawList*, ImVec2)>& paint) {
  w = std::max(4, w); h = std::max(4, h);
  Entry& e = entries()[key];
  e.used = ++g_clock;
  if (e.tex && e.ver == ver && e.w == w && e.h == h) return {e.tex, e.w, e.h};
  if (!e.fbo) { glGenFramebuffers(1, &e.fbo); glGenTextures(1, &e.tex); }
  if (e.w != w || e.h != h) {
    glBindTexture(GL_TEXTURE_2D, e.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    GLint prev = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, e.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    e.w = w; e.h = h;
  }
  e.ver = ver;
  // the interface's drawing into a list of its own (the fonts and icons are the interface's)
  if (!ImGui::GetCurrentContext()) return {e.tex, e.w, e.h};
  ImDrawList dl(ImGui::GetDrawListSharedData());
  dl._ResetForNewFrame();
  dl.PushClipRect(ImVec2(0, 0), ImVec2((float)w, (float)h));
  dl.PushTextureID(ImGui::GetIO().Fonts->TexID);
  paint(&dl, ImVec2((float)w, (float)h));
  ImDrawData d; d.Valid = true; d.DisplayPos = ImVec2(0, 0); d.DisplaySize = ImVec2((float)w, (float)h); d.FramebufferScale = ImVec2(1, 1);
  d.AddDrawList(&dl);
  GLint prevFbo = 0, vp[4]; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, vp);
  GLboolean depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
  glBindFramebuffer(GL_FRAMEBUFFER, e.fbo);
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(&d);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (depth) glEnable(GL_DEPTH_TEST); if (cull) glEnable(GL_CULL_FACE);
  // (an old picture nobody has asked for in a long while goes)
  if (entries().size() > 96) for (auto it = entries().begin(); it != entries().end();) {
    if (g_clock - it->second.used > 2000) { glDeleteTextures(1, &it->second.tex); glDeleteFramebuffers(1, &it->second.fbo); it = entries().erase(it); }
    else ++it;
  }
  return {e.tex, e.w, e.h};
}

void canvasForget(const std::string& prefix) {
  for (auto it = entries().begin(); it != entries().end();) {
    if (it->first.rfind(prefix, 0) == 0) { glDeleteTextures(1, &it->second.tex); glDeleteFramebuffers(1, &it->second.fbo); it = entries().erase(it); }
    else ++it;
  }
}

// ---- the Standard Galactic Alphabet, as strokes on a 4 x 6 grid (x right, y down): each letter a few lines and dots
namespace {
struct Stroke { signed char x0, y0, x1, y1; };   // x1 < 0: a dot at x0, y0
const std::vector<Stroke>& sga(int c) {
  static const std::vector<std::vector<Stroke>> L = {
    {{0, 5, 2, 0}, {2, 0, 4, 5}, {1, 3, -1, 0}},                     // a ᔑ
    {{0, 0, 0, 5}, {0, 0, 3, 0}, {3, 0, 3, 2}},                       // b ʖ
    {{0, 0, 4, 0}, {2, 0, 2, 5}},                                     // c ᓵ
    {{0, 0, 4, 0}, {0, 0, 0, 3}, {2, 4, -1, 0}},                      // d ↸
    {{0, 0, 0, 5}, {0, 5, 4, 5}, {2, 2, -1, 0}},                      // e ᒷ
    {{0, 0, 4, 0}, {1, 2, 1, 5}, {3, 2, 3, 5}},                       // f ⎓
    {{0, 5, 4, 5}, {4, 5, 4, 0}, {2, 0, 2, 3}},                       // g ⊣
    {{0, 0, 0, 5}, {0, 2, 4, 2}, {4, 0, 4, 5}, {2, 4, -1, 0}},        // h ⍑
    {{2, 0, 2, 5}, {1, 1, -1, 0}, {3, 1, -1, 0}},                     // i ╎
    {{1, 0, 1, 5}, {3, 0, 3, 5}},                                     // j ⋮
    {{0, 0, 4, 0}, {0, 0, 0, 5}, {4, 0, 4, 5}, {2, 3, -1, 0}},        // k ꖌ
    {{0, 0, 0, 5}, {0, 0, 4, 0}, {4, 0, 4, 5}},                       // l ꖎ
    {{0, 5, 0, 0}, {0, 0, 4, 0}, {4, 0, 4, 5}, {0, 5, 4, 5}},         // m ᒲ
    {{0, 0, 4, 5}, {4, 0, 4, 5}},                                     // n リ
    {{0, 0, 4, 0}, {4, 0, 4, 3}, {0, 3, 4, 3}, {2, 5, -1, 0}},        // o 𝙹
    {{0, 0, 0, 5}, {0, 0, 4, 0}, {4, 0, 4, 2}, {0, 2, 4, 2}},         // p !¡
    {{0, 0, 4, 0}, {4, 0, 4, 5}, {0, 5, 4, 5}},                       // q ᑑ
    {{0, 0, 4, 2}, {0, 4, 4, 2}},                                     // r ∷
    {{2, 0, 2, 5}, {0, 5, 4, 5}},                                     // s ᓭ
    {{0, 0, 4, 0}, {2, 0, 2, 5}, {0, 0, 0, 2}},                       // t ℸ
    {{2, 0, 2, 5}, {0, 2, 4, 2}, {0, 2, 0, 4}},                       // u ⚍
    {{0, 0, 4, 0}, {0, 0, 0, 5}, {0, 5, 4, 5}},                       // v ⍊
    {{1, 0, 1, 5}, {3, 0, 3, 5}, {0, 0, 4, 0}},                       // w ∴
    {{0, 0, 4, 5}, {4, 0, 0, 5}},                                     // x ̇/
    {{0, 0, 4, 0}, {0, 3, 4, 3}, {1, 5, -1, 0}, {3, 5, -1, 0}},       // y ||
    {{0, 0, 4, 0}, {4, 0, 0, 5}, {0, 5, 4, 5}}};                      // z ⨅
  return L[((c % 26) + 26) % 26];
}
}  // namespace

void drawGlyph(ImDrawList* dl, int letter, ImVec2 at, float size, ImU32 col, float thick) {
  const float u = size / 6.f, t = thick > 0 ? thick : std::max(1.f, size * 0.09f);
  for (const Stroke& s : sga(letter)) {
    const ImVec2 a(at.x + s.x0 * u, at.y + s.y0 * u);
    if (s.x1 < 0) dl->AddCircleFilled(a, t * 0.8f, col, 6);
    else dl->AddLine(a, ImVec2(at.x + s.x1 * u, at.y + s.y1 * u), col, t);
  }
}

void drawGlyphRun(ImDrawList* dl, u32 seed, int n, ImVec2 at, float size, float w, ImU32 col) {
  float x = at.x, y = at.y;
  u32 r = seed * 2654435761u + 7;
  for (int i = 0; i < n; i++) {
    r = r * 1103515245u + 12345u;
    const int c = (int)((r >> 16) % 26);
    if ((r >> 8) % 7 == 0 && x > at.x) x += size * 0.5f;   // (a word's end)
    if (x + size * 0.8f > at.x + w) { x = at.x; y += size * 1.25f; }
    drawGlyph(dl, c, ImVec2(x, y), size, col);
    x += size * 0.95f;
  }
}

} // namespace dq
