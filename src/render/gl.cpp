#include "render/gl.h"
#include <SDL.h>
#include <regex>

namespace dq {

GfxCaps gfx;
static std::string g_shaderPack;
void setShaderPack(const std::string& name) { g_shaderPack = name; }

static void GLAD_API_PTR glDebugCb(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* msg, const void*) {
  if (severity == GL_DEBUG_SEVERITY_HIGH || type == GL_DEBUG_TYPE_ERROR) DQ_WARN("GL: %s", msg);
}

bool gfxInit(RendererKind kind) {
  int ver = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
  if (!ver) { DQ_ERR("could not load OpenGL"); return false; }
  gfx.major = GLAD_VERSION_MAJOR(ver); gfx.minor = GLAD_VERSION_MINOR(ver);
  gfx.vendor = (const char*)glGetString(GL_VENDOR);
  gfx.renderer = (const char*)glGetString(GL_RENDERER);
  gfx.version = (const char*)glGetString(GL_VERSION);
  int v = gfx.major * 10 + gfx.minor;
  gfx.modern = kind == RendererKind::Modern && v >= 43;
  gfx.mdi = gfx.modern && (v >= 43 || GLAD_GL_ARB_multi_draw_indirect);
  gfx.bufferStorage = gfx.modern && (v >= 44 || GLAD_GL_ARB_buffer_storage);
  gfx.anisotropic = v >= 46 || GLAD_GL_EXT_texture_filter_anisotropic || GLAD_GL_ARB_texture_filter_anisotropic;
  if (gfx.anisotropic) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &gfx.maxAniso);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &gfx.maxArrayLayers);
  if (v >= 43 || GLAD_GL_KHR_debug) {
    GLint flags = 0; glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) { glEnable(GL_DEBUG_OUTPUT); glDebugMessageCallback(glDebugCb, nullptr); }
  }
  DQ_LOG("OpenGL %d.%d (%s path) — %s / %s", gfx.major, gfx.minor, gfx.modern ? "modern" : "legacy", gfx.vendor.c_str(), gfx.renderer.c_str());
  return true;
}

void glCheck(const char* where) {
  GLenum e = glGetError();
  if (e != GL_NO_ERROR) DQ_WARN("GL error 0x%x at %s", e, where);
}

std::string shaderSource(const std::string& file) {
  std::optional<std::string> src;
  if (!g_shaderPack.empty()) src = readFile(userDir() / "shaderpacks" / g_shaderPack / file);
  if (!src) src = readFile(assetsDir() / "shaders" / file);
  if (!src) { DQ_ERR("missing shader %s", file.c_str()); return ""; }
  // #include "file"
  std::string out; size_t pos = 0;
  while (true) {
    size_t i = src->find("#include \"", pos);
    if (i == std::string::npos) { out += src->substr(pos); break; }
    out += src->substr(pos, i - pos);
    size_t e = src->find('"', i + 10);
    out += shaderSource(src->substr(i + 10, e - i - 10));
    pos = src->find('\n', e); if (pos == std::string::npos) break;
  }
  return out;
}

static GLuint compile(GLenum type, const std::string& src, const std::string& name) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
    DQ_ERR("shader %s failed:\n%s", name.c_str(), log);
    glDeleteShader(s); return 0;
  }
  return s;
}

Shader::~Shader() { if (prog) glDeleteProgram(prog); }
bool Shader::load(const std::string& vs, const std::string& fs, const std::vector<std::string>& defines) {
  vsf = vs; fsf = fs; defs = defines;
  return reload();
}
bool Shader::reload() {
  std::string head = gfx.modern ? "#version 450 core\n#define DQ_MODERN 1\n" : "#version 330 core\n";
  for (auto& d : defs) head += "#define " + d + "\n";
  GLuint v = compile(GL_VERTEX_SHADER, head + "#define VERTEX 1\n" + shaderSource(vsf), vsf);
  GLuint f = compile(GL_FRAGMENT_SHADER, head + "#define FRAGMENT 1\n" + shaderSource(fsf), fsf);
  if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return false; }
  GLuint p = glCreateProgram();
  glAttachShader(p, v); glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v); glDeleteShader(f);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log); DQ_ERR("link %s+%s failed:\n%s", vsf.c_str(), fsf.c_str(), log); glDeleteProgram(p); return false; }
  if (prog) glDeleteProgram(prog);
  prog = p; locs.clear();
  return true;
}
GLint Shader::loc(const char* name) {
  auto it = locs.find(name);
  if (it != locs.end()) return it->second;
  GLint l = glGetUniformLocation(prog, name);
  locs[name] = l;
  return l;
}

GLuint quadIndexBuffer(size_t quads) {
  static GLuint ibo = 0; static size_t have = 0;
  if (quads <= have && ibo) return ibo;
  size_t n = std::max<size_t>(quads, have * 2); n = std::max<size_t>(n, 16384);
  std::vector<u32> idx(n * 6);
  for (size_t q = 0; q < n; q++) { u32 b = (u32)q * 4; u32* o = &idx[q * 6]; o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3; }
  if (!ibo) glGenBuffers(1, &ibo);
  // bind through a temporary VAO-less binding is fine for ELEMENT_ARRAY in core if a VAO is bound; use COPY_WRITE
  glBindBuffer(GL_COPY_WRITE_BUFFER, ibo);
  glBufferData(GL_COPY_WRITE_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
  have = n;
  return ibo;
}

Mesh::~Mesh() { release(); }
void Mesh::release() {
  if (vao) glDeleteVertexArrays(1, &vao);
  if (vbo) glDeleteBuffers(1, &vbo);
  if (ibo && !sharedIbo) glDeleteBuffers(1, &ibo);
  vao = vbo = ibo = 0; count = 0;
}
void Mesh::setup(GLsizei stride, const std::vector<VertexAttrib>& attribs) { stride_ = stride; attrs = attribs; }
static void applyAttribs(GLsizei stride, const std::vector<VertexAttrib>& attrs) {
  for (auto& a : attrs) {
    glEnableVertexAttribArray(a.index);
    if (a.integer) glVertexAttribIPointer(a.index, a.size, a.type, stride, (void*)(uintptr_t)a.offset);
    else glVertexAttribPointer(a.index, a.size, a.type, a.normalized, stride, (void*)(uintptr_t)a.offset);
  }
}
void Mesh::upload(const void* verts, size_t vbytes, const u32* idx, size_t icount, GLenum usage) {
  if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vbytes, verts, usage);
  if (sharedIbo || !ibo) { glGenBuffers(1, &ibo); sharedIbo = false; }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * 4, idx, usage);
  applyAttribs(stride_, attrs);
  glBindVertexArray(0);
  count = icount;
}
void Mesh::uploadQuads(const void* verts, size_t vbytes, size_t quads, GLenum usage) {
  GLuint shared = quadIndexBuffer(quads);
  if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vbytes, verts, usage);
  if (ibo && !sharedIbo) glDeleteBuffers(1, &ibo);
  ibo = shared; sharedIbo = true;
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  applyAttribs(stride_, attrs);
  glBindVertexArray(0);
  count = quads * 6;
}
void Mesh::draw(GLenum mode) const { if (!count) return; glBindVertexArray(vao); glDrawElements(mode, (GLsizei)count, GL_UNSIGNED_INT, nullptr); }
void Mesh::drawRange(size_t first, size_t n, GLenum mode) const { if (!n) return; glBindVertexArray(vao); glDrawElements(mode, (GLsizei)n, GL_UNSIGNED_INT, (void*)(first * 4)); }

GLuint makeTexture2D(int w, int h, const void* rgba, bool nearest, bool mips, bool repeat) {
  GLuint t; glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  if (mips) glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? (nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR) : (nearest ? GL_NEAREST : GL_LINEAR));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  return t;
}
GLuint makeTextureArray(int w, int h, int layers, const void* rgba, bool mips) {
  GLuint t; glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D_ARRAY, t);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  if (mips) glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, mips ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
  if (mips) glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 4);
  return t;
}

} // namespace dq
