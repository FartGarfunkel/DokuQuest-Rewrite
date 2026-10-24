// render/gl.h — the OpenGL backend for both renderer choices:
//   Legacy  = OpenGL 3.3 core (runs on almost any PC since ~2010, the potato path)
//   Modern  = OpenGL 4.6 core (falls back to 4.5/4.3) — enables multi-draw-indirect, persistent buffers
// Shaders are written once (assets/shaders/*.glsl, GLSL 330 subset) and get a version line plus
// defines (DQ_MODERN, quality levels) injected. A shader pack in userDir()/shaderpacks/<name>/ can
// override any file (OptiFine-style).
#pragma once
#include <glad/gl.h>
#include "common/settings.h"
#include <unordered_map>

namespace dq {

struct GfxCaps {
  int major = 3, minor = 3;
  bool modern = false;          // running the Modern (4.x) path
  bool mdi = false;             // glMultiDrawElementsIndirect
  bool bufferStorage = false;   // persistent mapped buffers
  bool anisotropic = false;
  float maxAniso = 1;
  int maxArrayLayers = 256;
  std::string vendor, renderer, version;
};
extern GfxCaps gfx;

bool gfxInit(RendererKind kind);           // after the context is current (loads GL with glad)
std::string shaderSource(const std::string& file);   // assets/shaders or the shader pack, #include resolved
void setShaderPack(const std::string& name);

class Shader {
public:
  Shader() = default;
  ~Shader();
  Shader(const Shader&) = delete; Shader& operator=(const Shader&) = delete;
  // vsFile/fsFile under assets/shaders; defines like "WAVING 1"
  bool load(const std::string& vsFile, const std::string& fsFile, const std::vector<std::string>& defines = {});
  bool reload();
  void use() const { glUseProgram(prog); }
  GLint loc(const char* name);
  void set(const char* n, int v) { glUniform1i(loc(n), v); }
  void set(const char* n, float v) { glUniform1f(loc(n), v); }
  void set(const char* n, const vec2& v) { glUniform2fv(loc(n), 1, &v.x); }
  void set(const char* n, const vec3& v) { glUniform3fv(loc(n), 1, &v.x); }
  void set(const char* n, const vec4& v) { glUniform4fv(loc(n), 1, &v.x); }
  void set(const char* n, const mat4& m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, &m[0][0]); }
  GLuint id() const { return prog; }
  bool ok() const { return prog != 0; }
private:
  GLuint prog = 0;
  std::string vsf, fsf; std::vector<std::string> defs;
  std::unordered_map<std::string, GLint> locs;
};

// A growable vertex+index buffer pair with a VAO (used by chunk meshes, models, particles, UI meshes)
struct VertexAttrib { GLuint index; GLint size; GLenum type; GLboolean normalized; bool integer; GLuint offset; };
class Mesh {
public:
  Mesh() = default;
  ~Mesh();
  Mesh(const Mesh&) = delete; Mesh& operator=(const Mesh&) = delete;
  void setup(GLsizei stride, const std::vector<VertexAttrib>& attribs);
  void upload(const void* verts, size_t vbytes, const u32* idx, size_t icount, GLenum usage = GL_STATIC_DRAW);
  void uploadQuads(const void* verts, size_t vbytes, size_t quadCount, GLenum usage = GL_STATIC_DRAW);  // shared quad index buffer
  void draw(GLenum mode = GL_TRIANGLES) const;
  void drawRange(size_t firstIndex, size_t count, GLenum mode = GL_TRIANGLES) const;
  size_t indexCount() const { return count; }
  void release();
private:
  GLuint vao = 0, vbo = 0, ibo = 0;
  size_t count = 0; GLsizei stride_ = 0;
  bool sharedIbo = false;
  std::vector<VertexAttrib> attrs;
};
GLuint quadIndexBuffer(size_t quads);   // shared 0,1,2, 0,2,3 pattern for quad lists

GLuint makeTexture2D(int w, int h, const void* rgba, bool nearest = true, bool mips = false, bool repeat = true);
GLuint makeTextureArray(int w, int h, int layers, const void* rgba, bool mips = true);
void glCheck(const char* where);

} // namespace dq
