// render/post_layer.cpp — the picture after the world is drawn (before the colour-blind filter):
//  * the built-in shader levels (Settings > Shaders: off, low, medium, high, ultra; assets/shaders/post.glsl):
//    a film grade and vignette, then bloom, then sun rays and sharpening;
//  * Iris/OptiFine shader packs: their post passes (shaders/composite.fsh, composite1..15.fsh, final.fsh) run over our
//    frame, with colortex0 = the picture and depthtex0 = its depth, and the uniforms those passes read (the camera's
//    matrices, the sun, the time, the view size...). The rest of such a pack (gbuffers_*, shadow, deferred) is not run:
//    our own world shaders draw the world, so a pack's colour grading, bloom, tone mapping, depth of field and the like
//    work, and what needs the pack's own G-buffers (its lighting, shadows, reflections) doesn't. The pack's #version 120
//    GLSL is brought up to 330 core here (texture2D, varying, gl_FragData, gl_TexCoord, ftransform...).
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "render/sky.h"
#include "assets/pack_fs.h"
#include <regex>
#include <set>

namespace dq {

namespace {
struct Target {
  GLuint tex = 0, fbo = 0; int w = 0, h = 0;
  void make(int W, int H, bool linear) {
    if (tex && w == W && h == H) return;
    drop();
    w = W; h = H;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  }
  void drop() { if (fbo) glDeleteFramebuffers(1, &fbo); if (tex) glDeleteTextures(1, &tex); fbo = tex = 0; w = h = 0; }
};

// ---- an Iris/OptiFine pack's post passes ----
struct IrisPass { GLuint prog = 0; std::string name; std::vector<std::pair<std::string, GLenum>> uniforms; };

std::string rx(const std::string& s, const char* re, const char* to) { return std::regex_replace(s, std::regex(re), to); }

// the pack's files: a folder or a zip, with the shaders/ folder somewhere at its top
struct IrisFiles {
  std::unique_ptr<PackFS> fs; std::string root;   // root: the path to "shaders/" inside it
  bool open(const fs::path& p) {
    fs = PackFS::open(p);
    if (!fs) return false;
    for (const std::string& f : fs->list("")) {
      size_t i = f.find("shaders/");
      if (i == std::string::npos) continue;
      const std::string rest = f.substr(i + 8);
      if (rest == "final.fsh" || rest.rfind("composite", 0) == 0) { root = f.substr(0, i + 8); return true; }
    }
    return false;
  }
  std::optional<std::string> read(const std::string& rel) const {
    auto d = fs->read(root + rel);
    if (!d) return std::nullopt;
    return std::string(d->begin(), d->end());
  }
  // #include "/lib/x.glsl" (from shaders/) or "x.glsl" (next to the file), once each
  std::string expand(const std::string& src, const std::string& dir, std::set<std::string>& seen, int depth = 0) const {
    std::string out; size_t pos = 0;
    while (true) {
      size_t i = src.find("#include", pos);
      if (i == std::string::npos) { out += src.substr(pos); break; }
      size_t q0 = src.find('"', i), nl = src.find('\n', i);
      if (q0 == std::string::npos || (nl != std::string::npos && q0 > nl)) { out += src.substr(pos, (nl == std::string::npos ? src.size() : nl) - pos); pos = nl == std::string::npos ? src.size() : nl; continue; }
      size_t q1 = src.find('"', q0 + 1);
      out += src.substr(pos, i - pos);
      std::string inc = src.substr(q0 + 1, q1 - q0 - 1);
      std::string rel = inc[0] == '/' ? inc.substr(1) : dir + inc;
      // a/b/../c -> a/c
      for (size_t k; (k = rel.find("/../")) != std::string::npos;) { size_t s = rel.rfind('/', k - 1); rel = (s == std::string::npos ? "" : rel.substr(0, s + 1)) + rel.substr(k + 4); }
      if (depth < 16 && !seen.count(rel)) {
        seen.insert(rel);
        if (auto t = read(rel)) out += expand(*t, rel.substr(0, rel.rfind('/') + 1), seen, depth + 1) + "\n";
        else DQ_WARN("shader pack: missing include %s", rel.c_str());
      }
      pos = src.find('\n', q1); if (pos == std::string::npos) break;
    }
    return out;
  }
};

// a pack's GLSL (any #version, mostly 120) brought to 330 core: the old built-ins become ours, main() runs inside ours
std::string translate(std::string src, bool vertex) {
  std::string ext;
  {   // #extension lines go straight after #version
    std::string rest; size_t pos = 0;
    while (pos < src.size()) {
      size_t nl = src.find('\n', pos); if (nl == std::string::npos) nl = src.size();
      const std::string l = src.substr(pos, nl - pos);
      const size_t f = l.find_first_not_of(" \t");
      if (f != std::string::npos && l.compare(f, 10, "#extension") == 0) {
        if (l.find("GL_ARB_shader_texture_lod") == std::string::npos && l.find("GL_EXT_gpu_shader4") == std::string::npos) ext += l + "\n";
        rest += "\n";
      } else rest += l + "\n";
      pos = nl + 1;
    }
    src = rest;
  }
  src = rx(src, R"(#version[^\n]*)", "");
  const bool old = src.find("gl_FragData") != std::string::npos || src.find("gl_FragColor") != std::string::npos || !std::regex_search(src, std::regex(R"(\bout\s+vec4)"));
  src = rx(src, R"(\bvoid\s+main\s*\(\s*(void)?\s*\))", "void dqPackMain()");
  src = rx(src, R"(\btexture2DLod\b)", "textureLod");
  src = rx(src, R"(\btexture2DGradARB\b|\btexture2DGrad\b)", "textureGrad");
  src = rx(src, R"(\btexture2D\b|\btexture3D\b|\btextureRect\b)", "texture");
  src = rx(src, R"(\bshadow2D\s*\()", "dqShadow2D(");
  src = rx(src, R"(\bgl_TexCoord\b)", "dqTC");
  std::string head = "#version 330 core\n" + ext;
  if (vertex) {
    src = rx(src, R"(\battribute\b)", "in");
    src = rx(src, R"(\bvarying\b)", "out");
    src = rx(src, R"(\bgl_FrontColor\b)", "dqFrontColor");
    head += "layout(location = 0) in vec4 dqVertex;\nuniform mat4 dqMVP;\nout vec4 dqTC[2];\nvec4 dqFrontColor;\n"
            "#define gl_Vertex dqVertex\n#define gl_MultiTexCoord0 vec4(dqVertex.xy, 0.0, 1.0)\n#define gl_MultiTexCoord1 vec4(240.0, 240.0, 0.0, 1.0)\n"
            "#define gl_Color vec4(1.0)\n#define gl_Normal vec3(0.0, 0.0, 1.0)\n#define gl_ModelViewProjectionMatrix dqMVP\n#define gl_ProjectionMatrix dqMVP\n"
            "#define gl_ModelViewMatrix mat4(1.0)\n#define gl_NormalMatrix mat3(1.0)\n#define gl_TextureMatrix dqTexMat\n#define ftransform() (dqMVP * dqVertex)\n"
            "const mat4 dqTexMat[2] = mat4[2](mat4(1.0), mat4(1.0));\n";
    src += "\nvoid main() { dqTC[0] = vec4(dqVertex.xy, 0.0, 1.0); dqTC[1] = vec4(0.0); gl_Position = dqMVP * dqVertex; dqPackMain(); }\n";
  } else {
    src = rx(src, R"(\bvarying\b)", "in");
    head += "in vec4 dqTC[2];\nvec4 dqShadow2D(sampler2DShadow s, vec3 c) { return vec4(texture(s, c)); }\n";
    if (old) {
      src = rx(src, R"(\bgl_FragData\b)", "dqData");
      src = rx(src, R"(\bgl_FragColor\b)", "dqData[0]");
      head += "layout(location = 0) out vec4 dqOut0;\nvec4 dqData[8];\n";
      src += "\nvoid main() { for (int i = 0; i < 8; i++) dqData[i] = vec4(0.0); dqPackMain(); dqOut0 = dqData[0]; }\n";
    } else {
      // outputs declared the new way: the first one is what we keep (they get locations in order unless they have some)
      if (src.find("layout(location") == std::string::npos && src.find("layout (location") == std::string::npos) {
        std::regex o(R"((^|\n)([ \t]*)out\s+vec4\s+(\w+)\s*;)");
        std::string res; int n = 0; auto b = src.cbegin();
        for (auto it = std::sregex_iterator(src.begin(), src.end(), o); it != std::sregex_iterator(); ++it) {
          res.append(b, src.cbegin() + it->position());
          res += (*it)[1].str() + (*it)[2].str() + "layout(location = " + std::to_string(n++) + ") out vec4 " + (*it)[3].str() + ";";
          b = src.cbegin() + it->position() + it->length();
        }
        res.append(b, src.cend()); src = res;
      }
      src += "\nvoid main() { dqPackMain(); }\n";
    }
  }
  return head + src;
}

const char* DEFAULT_VSH =
  "#version 120\nvarying vec2 texcoord;\nvoid main() { gl_Position = ftransform(); texcoord = gl_MultiTexCoord0.st; }\n";
} // namespace

class PostLayer : public RenderLayer {
public:
  const char* name() const override { return "post"; }
  int order() const override { return 4900; }   // after the hand, before the colour-blind filter (5000)
  void drawOverlay(GameSession& s, const FrameCtx& f) override {
    if (f.portalDepth > 0) return;
    const int level = std::clamp(s.app.settings.extra.value("shaderLevel", 0), 0, 4);
    const std::string& pack = s.app.settings.shaderPack;
    if (pack != irisFor) loadIris(pack);
    if (level == 0 && iris.empty()) return;
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 16 || vp[3] <= 16) return;
    GLint drawFbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    if (!vao) glGenVertexArrays(1, &vao);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE);
    if (!iris.empty()) runIris(s, f, vp, drawFbo);
    if (level > 0) runLevels(s, f, level, vp, drawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindVertexArray(0); glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE);
  }
private:
  GLuint vao = 0;
  // the built-in levels
  bool triedLv = false; std::unique_ptr<Shader> post;
  Target scene, bloomA, bloomB;
  // an Iris/OptiFine pack
  std::string irisFor = "\x01";
  std::vector<IrisPass> iris;
  Target ping[2];
  GLuint depthTex = 0, blackTex = 0, whiteTex = 0, quadVbo = 0, quadVao = 0; int dw = 0, dh = 0;
  double startT = nowSeconds(), lastT = 0; int frames = 0;

  void tri() { glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); }
  void copyFrame(Target& t, const GLint* vp, GLint fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)fbo);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t.tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
  }

  // ---- off / low / medium / high / ultra ----
  void runLevels(GameSession& s, const FrameCtx& f, int level, const GLint* vp, GLint outFbo) {
    if (!triedLv) { triedLv = true; post = std::make_unique<Shader>(); if (!post->load("post.glsl", "post.glsl")) post.reset(); }
    if (!post) return;
    const int W = vp[2], H = vp[3], qw = std::max(1, W / 4), qh = std::max(1, H / 4);
    scene.make(W, H, true); bloomA.make(qw, qh, true); bloomB.make(qw, qh, true);
    copyFrame(scene, vp, outFbo);
    static const float BLOOM[5] = {0, 0, 0.32f, 0.42f, 0.55f}, RAYS[5] = {0, 0, 0, 0.55f, 0.75f}, SHARP[5] = {0, 0, 0.08f, 0.15f, 0.22f};
    static const int STEPS[5] = {0, 0, 0, 24, 40};
    post->use();
    post->set("uSrc", 0); post->set("uBloom", 1);
    glActiveTexture(GL_TEXTURE0);
    const bool needBloom = BLOOM[level] > 0 || RAYS[level] > 0;
    if (needBloom) {
      // the bright parts at a quarter of the size, blurred (twice at ultra)
      glBindFramebuffer(GL_FRAMEBUFFER, bloomA.fbo); glViewport(0, 0, qw, qh);
      glBindTexture(GL_TEXTURE_2D, scene.tex);
      post->set("uStep", 0); post->set("uTexel", vec2(1.f / W, 1.f / H)); post->set("uThreshold", 0.72f);
      tri();
      for (int it = 0; it < (level >= 4 ? 2 : 1); it++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomB.fbo); glBindTexture(GL_TEXTURE_2D, bloomA.tex);
        post->set("uStep", 1); post->set("uTexel", vec2(1.f / qw, 1.f / qh)); tri();
        glBindFramebuffer(GL_FRAMEBUFFER, bloomA.fbo); glBindTexture(GL_TEXTURE_2D, bloomB.tex);
        post->set("uStep", 2); tri();
      }
    }
    // the sun on the screen
    float rays = 0; vec2 sunUV(0.5f);
    const SkyEnv& env = skyOf(f);
    if (RAYS[level] > 0 && env.showSun && f.cam.realm == REALM_MAIN && f.portalDepth == 0) {
      vec3 eye = vec3(f.cam.pos - f.cam.origin);
      vec4 c = f.cam.viewProj * vec4(eye + f.sunDir * 1000.f, 1.f);
      if (c.w > 0 && f.sunDir.y > -0.05f) {
        sunUV = vec2(c.x / c.w, c.y / c.w) * 0.5f + 0.5f;
        float off = std::max(std::abs(sunUV.x - 0.5f), std::abs(sunUV.y - 0.5f));
        rays = RAYS[level] * smoothstepf(1.1f, 0.5f, off) * smoothstepf(-0.05f, 0.12f, f.sunDir.y) * clampv(f.daylight, 0.f, 1.f) * (1 - f.rain * 0.8f);
      }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outFbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomA.tex);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, scene.tex);
    post->set("uStep", 3); post->set("uTexel", vec2(1.f / W, 1.f / H));
    post->set("uBloomK", BLOOM[level]); post->set("uRaysK", rays); post->set("uRaySteps", rays > 0 ? STEPS[level] : 0);
    post->set("uSunUV", sunUV); post->set("uSunTint", vec3(1.0f, 0.92f, 0.78f) + env.sunGlow * 0.3f);
    post->set("uSharpK", SHARP[level]); post->set("uGradeK", level >= 1 ? 1.f : 0.f); post->set("uVignetteK", level >= 1 ? 0.22f : 0.f);
    tri();
    (void)s;
  }

  // ---- Iris / OptiFine ----
  void loadIris(const std::string& pack) {
    irisFor = pack;
    for (auto& p : iris) if (p.prog) glDeleteProgram(p.prog);
    iris.clear();
    if (pack.empty()) return;
    IrisFiles F;
    if (!F.open(userDir() / "shaderpacks" / pack)) return;   // one of our own packs (or nothing to run)
    std::vector<std::string> names;
    for (int i = 0; i < 16; i++) names.push_back(i ? "composite" + std::to_string(i) : "composite");
    names.push_back("final");
    int ok = 0, tried = 0;
    for (const std::string& n : names) {
      // the Overworld's own version first (world0/), as Iris does
      std::string base = F.read("world0/" + n + ".fsh") ? "world0/" + n : n;
      auto fsh = F.read(base + ".fsh");
      if (!fsh) continue;
      tried++;
      auto vsh = F.read(base + ".vsh");
      std::set<std::string> seen;
      const std::string dir = base.substr(0, base.rfind('/') + 1);
      std::string fsrc = translate(F.expand(*fsh, dir, seen), false);
      seen.clear();
      std::string vsrc = translate(vsh ? F.expand(*vsh, dir, seen) : std::string(DEFAULT_VSH), true);
      GLuint prog = buildProgram(vsrc, fsrc, pack + ":" + base);
      if (!prog) continue;
      IrisPass P; P.prog = prog; P.name = n;
      GLint cnt = 0; glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &cnt);
      for (GLint u = 0; u < cnt; u++) {
        char nm[256]; GLsizei len = 0; GLint size = 0; GLenum type = 0;
        glGetActiveUniform(prog, (GLuint)u, sizeof nm, &len, &size, &type, nm);
        std::string un(nm, (size_t)len); if (un.size() > 3 && un.substr(un.size() - 3) == "[0]") un.resize(un.size() - 3);
        P.uniforms.push_back({un, type});
      }
      iris.push_back(std::move(P)); ok++;
    }
    DQ_LOG("shader pack '%s': %d of %d post passes ready (Iris/OptiFine; its gbuffers and shadow passes aren't run)", pack.c_str(), ok, tried);
    if (!blackTex) {
      const u8 b[4] = {0, 0, 0, 0}, w[4] = {255, 255, 255, 255};
      blackTex = makeTexture2D(1, 1, b, true, false, false); whiteTex = makeTexture2D(1, 1, w, true, false, false);
      const float q[8] = {0, 0, 1, 0, 1, 1, 0, 1};
      glGenVertexArrays(1, &quadVao); glGenBuffers(1, &quadVbo);
      glBindVertexArray(quadVao); glBindBuffer(GL_ARRAY_BUFFER, quadVbo); glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
      glBindVertexArray(0);
    }
  }

  void runIris(GameSession& s, const FrameCtx& f, const GLint* vp, GLint outFbo) {
    const int W = vp[2], H = vp[3];
    ping[0].make(W, H, true); ping[1].make(W, H, true);
    copyFrame(ping[0], vp, outFbo);
    // the depth: copied out of the frame (if the driver won't, everything reads as far away)
    if (!depthTex || dw != W || dh != H) {
      if (depthTex) glDeleteTextures(1, &depthTex);
      glGenTextures(1, &depthTex); glBindTexture(GL_TEXTURE_2D, depthTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, W, H, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      dw = W; dh = H;
    }
    glBindTexture(GL_TEXTURE_2D, depthTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], W, H);
    const bool depthOk = glGetError() == GL_NO_ERROR;
    const double now = nowSeconds(); const float ft = lastT > 0 ? (float)(now - lastT) : 0.016f; lastT = now; frames++;
    const Camera& cam = f.cam;
    const vec3 eyeRel = vec3(cam.pos - cam.origin);
    // OptiFine's view space: the camera at the origin (our view matrix is relative to the camera's block)
    const mat4 mv = cam.view * glm::translate(mat4(1), eyeRel);
    const mat4 mvi = glm::inverse(mv), pr = cam.proj, pri = glm::inverse(cam.proj);
    const vec3 sunV = vec3(mv * vec4(f.sunDir * 100.f, 0.f)), moonV = -sunV, upV = vec3(mv * vec4(0, 100.f, 0, 0.f));
    const float sunAngle = (float)std::fmod(s.level->info.time / DAY_LEN + 0.75, 1.0);
    const int wt = (int)std::fmod(s.level->info.time / DAY_LEN * 24000.0 + 18000.0, 24000.0);
    const mat4 ortho = glm::ortho(0.f, 1.f, 0.f, 1.f, -1.f, 1.f);
    int src = 0;
    for (size_t i = 0; i < iris.size(); i++) {
      IrisPass& P = iris[i];
      const bool last = i + 1 == iris.size();
      glBindFramebuffer(GL_FRAMEBUFFER, last ? (GLuint)outFbo : ping[1 - src].fbo);
      glViewport(last ? vp[0] : 0, last ? vp[1] : 0, W, H);
      glUseProgram(P.prog);
      int unit = 0;
      auto bindTex = [&](GLint loc, GLuint tex) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, tex); glUniform1i(loc, unit); unit++; };
      for (auto& [n, type] : P.uniforms) {
        const GLint loc = glGetUniformLocation(P.prog, n.c_str());
        if (loc < 0) continue;
        if (type == GL_SAMPLER_2D) {
          if (n == "colortex0" || n == "gcolor" || n == "texture" || n == "tex" || n == "gtexture") bindTex(loc, ping[src].tex);
          else if (n == "depthtex0" || n == "depthtex1" || n == "depthtex2" || n == "gdepthtex") bindTex(loc, depthOk ? depthTex : whiteTex);
          else if (n == "noisetex") bindTex(loc, whiteTex);
          else bindTex(loc, blackTex);
          continue;
        }
        if (type == GL_SAMPLER_2D_SHADOW || type == GL_SAMPLER_3D) { glUniform1i(loc, 15); continue; }   // nothing there
        auto f1 = [&](float v) { if (type == GL_FLOAT) glUniform1f(loc, v); else if (type == GL_INT || type == GL_BOOL) glUniform1i(loc, (int)v); };
        if (n == "viewWidth") f1((float)W); else if (n == "viewHeight") f1((float)H); else if (n == "aspectRatio") f1((float)W / H);
        else if (n == "frameTimeCounter") f1((float)std::fmod(now - startT, 3600.0)); else if (n == "frameTime") f1(ft);
        else if (n == "frameCounter") f1((float)(frames % 720720));
        else if (n == "near") f1(cam.nearZ); else if (n == "far") f1(std::max(f.fogEnd, 64.f));
        else if (n == "rainStrength" || n == "wetness") f1(f.rain);
        else if (n == "sunAngle" || n == "shadowAngle") f1(n == "sunAngle" ? sunAngle : std::fmod(sunAngle, 0.5f));
        else if (n == "worldTime") f1((float)wt); else if (n == "worldDay") f1((float)s.level->info.day);
        else if (n == "moonPhase") f1((float)(s.level->info.day % 8));
        else if (n == "isEyeInWater") f1((float)f.eyeFluid);
        else if (n == "screenBrightness") f1(s.app.settings.brightness);
        else if (n == "eyeAltitude") f1((float)(cam.pos.y - dimBase(f.dim)));
        else if (n == "nightVision" || n == "blindness" || n == "darknessFactor" || n == "centerDepthSmooth") f1(n == "centerDepthSmooth" ? 0.98f : 0.f);
        else if ((n == "eyeBrightness" || n == "eyeBrightnessSmooth") && type == GL_INT_VEC2) glUniform2i(loc, 0, 240);
        else if (type == GL_FLOAT_VEC3) {
          vec3 v(0);
          if (n == "sunPosition" || n == "shadowLightPosition") v = f.sunDir.y > 0 || n == "sunPosition" ? sunV : moonV;
          else if (n == "moonPosition") v = moonV; else if (n == "upPosition") v = upV;
          else if (n == "cameraPosition" || n == "previousCameraPosition") v = vec3((float)std::fmod(cam.pos.x, 30000.0), (float)(cam.pos.y - dimBase(f.dim)), (float)std::fmod(cam.pos.z, 30000.0));
          else if (n == "skyColor") v = f.skyColor; else if (n == "fogColor") v = f.fogColor;
          glUniform3fv(loc, 1, &v.x);
        } else if (type == GL_FLOAT_MAT4) {
          const mat4* m = nullptr;
          if (n == "gbufferProjection" || n == "gbufferPreviousProjection") m = &pr;
          else if (n == "gbufferProjectionInverse") m = &pri;
          else if (n == "gbufferModelView" || n == "gbufferPreviousModelView") m = &mv;
          else if (n == "gbufferModelViewInverse") m = &mvi;
          else if (n == "dqMVP") m = &ortho;
          static const mat4 I(1);
          glUniformMatrix4fv(loc, 1, GL_FALSE, &(m ? *m : I)[0][0]);
        }
      }
      glBindVertexArray(quadVao); glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
      if (!last) src = 1 - src;
    }
    glUseProgram(0);
  }
};

void registerPostLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<PostLayer>(); }); }

} // namespace dq
