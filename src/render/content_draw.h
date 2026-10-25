// render/content_draw.h — a small drawing kit for the content features (maps, instruments, fishing, camping):
// flat pictures and lines in the world (assets/shaders/content.glsl), and a tiny 2D canvas that paints the
// original's canvas-drawn pictures (map markers, the paper) into RGBA.
#pragma once
#include "render/frame.h"

namespace dq {

class ContentDraw {
public:
  // binds the shader for a pass; viewProj maps render space (or view space for the hand overlay)
  bool begin(const FrameCtx& f, const mat4& viewProj, bool fog = true);
  // a picture on the unit quad (-0.5..0.5 in x and y, facing +z; v = 0 at the top), both sides
  void picture(GLuint tex, const mat4& model, vec4 uv = vec4(0, 0, 1, 1), vec4 color = vec4(1), vec2 light = vec2(1, 0), bool lit = true);
  void lines(const std::vector<vec3>& strip, vec4 color, vec2 light);   // a line strip in the pass' space
  void end();
private:
  bool init();
  bool tried = false;
  std::unique_ptr<Shader> sh;
  GLuint vao = 0, vbo = 0, lvao = 0, lvbo = 0;
};
ContentDraw& contentDraw();

GLuint makeTexRGBA(int w, int h, const u8* rgba, bool nearest, bool mips);
void updateTexRGBA(GLuint tex, int w, int h, const u8* rgba, bool mips);
u32 hexRGBA(const char* hex, u8 a = 255);   // "#rrggbb" -> r | g << 8 | b << 16 | a << 24

// The 2D canvas: paths (moveTo/lineTo/arc/quadTo/rect), fill and stroke (round joins), anti-aliased, in a
// transform stack like CanvasRenderingContext2D.
class Canvas {
public:
  Canvas(int w, int h);
  int w, h;
  std::vector<float> px;   // premultiplied RGBA 0..1
  void save() { stack.push_back(xf); }
  void restore() { if (!stack.empty()) { xf = stack.back(); stack.pop_back(); } }
  void translate(float x, float y);
  void rotate(float a);
  void scale(float sx, float sy);
  void beginPath() { paths.clear(); }
  void moveTo(float x, float y);
  void lineTo(float x, float y);
  void quadTo(float cx, float cy, float x, float y);
  void arc(float cx, float cy, float r, float a0, float a1, bool ccw = false);
  void rect(float x, float y, float rw, float rh);
  void closePath();
  void fill(u32 rgba);
  void stroke(u32 rgba, float width);
  void fillRect(float x, float y, float rw, float rh, u32 rgba);
  std::vector<u8> rgba() const;     // straight alpha
private:
  struct Path { std::vector<vec2> pts; bool closed = false; };
  std::vector<Path> paths;
  glm::mat3 xf{1};
  std::vector<glm::mat3> stack;
  vec2 tp(float x, float y) const { glm::vec3 v = xf * glm::vec3(x, y, 1); return vec2(v.x, v.y); }
  void cover(const std::vector<std::vector<vec2>>& polys, u32 rgba);
};

} // namespace dq
