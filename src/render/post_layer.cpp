// render/post_layer.cpp — the picture after the world is drawn (before the colour-blind filter):
//  * Iris/OptiFine shader packs. A dimension folder (world0, world-1, world1) replaces shaders/ for that band.
//    When gbuffers_terrain (or textured_lit / textured / basic) links, opaque and cutout sections are drawn into its
//    DRAWBUFFERS with the game's clip position, so the pack's own lighting replaces the terrain. Translucent
//    sections (water, glass, ice) are drawn with gbuffers_water when that links: the pack keeps its clip position,
//    waves included, and writes depth. depthtex0 is that depth. depthtex1 stays the opaque depth the terrain pass
//    matched. texture.<stage>.<sampler>=png replaces that sampler for the Iris stage (gbuffers includes the
//    shadow pass, composite includes final), so a name like colortex3 can be a noise image in deferred and a
//    buffer afterwards. Sky and clouds stay the copied picture. Creatures, characters, chests and dropped
//    items are voxel meshes; they are drawn again with gbuffers_entities so the pack lights them. begin/prepare
//    run before the opaque draws. Deferred runs after those and before water: the cloud depth it writes is
//    what gbuffers_water tests, and a deferred pass after water replaces the surface. Then composite and final.
//    A pack whose gbuffer does not link
//    keeps the post-only path. When the pack's shadow program links, opaque and cutout terrain are drawn into a
//    sun shadow map with that program's distortion, and the same models cast with that program. The first-person
//    hand is drawn with gbuffers_hand into the buffer the final pass reads, using the hand's own depth image.
//    That depth is never copied into depthtex0: a pass that reconstructs the world from the hand's projection
//    turns the arm into sky. A pack that sets clouds=off draws its own sky, so the game's cloud
//    planes are not copied in. The voxel volume is not drawn by the pack yet.
//  * the built-in shader levels (Settings > Shaders: off, low, medium, high, ultra; assets/shaders/post.glsl):
//    a film grade and vignette, then bloom, then sun rays and sharpening. These run after the hand.
//    The pack's #version 120 GLSL is brought up to 330 core here (texture2D, varying, gl_FragData, gl_TexCoord, ftransform...).
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "render/pack_geom.h"
#include "render/sky.h"
#include "assets/pack_fs.h"
#include "assets/tileset.h"
#include "world/dims.h"
#include "core/registry.h"
#include "sim/inventory.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <regex>
#include <set>
#include <unordered_map>
#include <vector>

namespace dq {

// A screen-space texture() has real derivatives. A data tap (cloud noise, a shadow
// map, a generated-normal offset) often does not: the call sits in a helper, and the
// implicit LOD is then undefined. This GPU's GL driver picks a coarse mip, which
// bands the clouds and speckles the normals. Force LOD 0 unless the coordinate is a
// screen UV. Atlas samples keep their mips: their coordinate is named texCoord.
// prepGlsl runs this again; a second pass leaves textureLod alone.
std::string lod0ForDataTaps(const std::string& s) {
  if (s.find("colortex") == std::string::npos && s.find("gaux") == std::string::npos && s.find("noisetex") == std::string::npos)
    return s;
  std::string out;
  out.reserve(s.size() + 32);
  bool lineComment = false, blockComment = false;
  auto ident = [](char ch) { return std::isalnum((unsigned char)ch) || ch == '_'; };
  for (size_t i = 0; i < s.size(); ) {
    char c = s[i];
    if (lineComment) {
      out.push_back(c);
      if (c == '\n') lineComment = false;
      i++;
      continue;
    }
    if (blockComment) {
      out.push_back(c);
      if (c == '*' && i + 1 < s.size() && s[i + 1] == '/') { out.push_back(s[++i]); blockComment = false; }
      i++;
      continue;
    }
    if (c == '/' && i + 1 < s.size() && (s[i + 1] == '/' || s[i + 1] == '*')) {
      lineComment = s[i + 1] == '/';
      blockComment = s[i + 1] == '*';
      out.push_back(c);
      out.push_back(s[i + 1]);
      i += 2;
      continue;
    }
    if (c == '"') {
      out.push_back(c);
      i++;
      while (i < s.size() && s[i] != '"') { out.push_back(s[i]); if (s[i] == '\\' && i + 1 < s.size()) out.push_back(s[++i]); i++; }
      if (i < s.size()) out.push_back(s[i++]);
      continue;
    }
    bool isTexture = i + 7 <= s.size() && s.compare(i, 7, "texture") == 0 && (i == 0 || !ident(s[i - 1]));
    size_t j = i + 7;
    if (isTexture) while (j < s.size() && std::isspace((unsigned char)s[j])) j++;
    if (!isTexture || j >= s.size() || s[j] != '(') { out.push_back(c); i++; continue; }
    int par = 0, commas = 0;
    size_t k = j, commaAt = 0;
    for (; k < s.size(); k++) {
      if (s[k] == '(') par++;
      else if (s[k] == ')') { par--; if (par == 0) break; }
      else if (s[k] == ',' && par == 1) { commas++; if (commas == 1) commaAt = k; }
    }
    if (k >= s.size() || commas != 1) { out.push_back(c); i++; continue; }
    std::string coord = s.substr(commaAt + 1, k - commaAt - 1);
    if (coord.find("texCoord") != std::string::npos || coord.find("gl_FragCoord") != std::string::npos) {
      out.push_back(c);
      i++;
      continue;
    }
    out += "textureLod";
    out.append(s, j, k - j);
    out += ", 0.0)";
    i = k + 1;
  }
  return out;
}

namespace {
// One-shot pictures of each pack pass. Set DQ_PACK_DUMP=1. The files show which pass
// first paints the half-size scene into the top-left quadrant.
static int packDumpN = 0;
static bool packDumpDone = false;
static void packDump(const char* tag, GLuint tex, int w, int h) {
  if (!std::getenv("DQ_PACK_DUMP") || !tex || w < 2 || h < 2) return;
  std::vector<unsigned char> px((size_t)w * h * 4);
  GLint prev = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
  glBindTexture(GL_TEXTURE_2D, tex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
  char path[160];
  std::snprintf(path, sizeof(path), "/tmp/dq-pass/%02d-%s.ppm", packDumpN++, tag);
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      std::fwrite(px.data() + ((size_t)y * w + x) * 4, 1, 3, f);
  std::fclose(f);
}
struct Target {
  GLuint tex = 0, fbo = 0; int w = 0, h = 0; bool hdr = false;
  void make(int W, int H, bool linear, bool wantHdr = false) {
    if (tex && w == W && h == H && hdr == wantHdr) return;
    drop();
    w = W; h = H; hdr = wantHdr;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, W, H, 0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  }
  void drop() { if (fbo) glDeleteFramebuffers(1, &fbo); if (tex) glDeleteTextures(1, &tex); fbo = tex = 0; w = h = 0; }
};

// ---- an Iris/OptiFine pack's post passes ----
// nOut 0 draws the screen (final). Otherwise outs[i] is the colortex gl_FragData[i] lands in
// (Iris: DRAWBUFFERS:16 means gl_FragData[0] -> colortex1 and gl_FragData[1] -> colortex6).
struct IrisPass {
  GLuint prog = 0; std::string name; int outs[8]{}; int nOut = 0;
  std::vector<std::pair<std::string, GLenum>> uniforms; bool writesColor = true;
  // gbuffers_water blends like vanilla translucent unless shaders.properties says otherwise.
  // blendOff bit i turns blending off for colortex i (Complementary's normals and reflections).
  bool blend = false;
  GLenum bSrc = GL_SRC_ALPHA, bDst = GL_ONE_MINUS_SRC_ALPHA, bSrcA = GL_ONE, bDstA = GL_ONE_MINUS_SRC_ALPHA;
  uint32_t blendOff = 0;
};

std::string rx(const std::string& s, const char* re, const char* to) { return std::regex_replace(s, std::regex(re), to); }

// Enough of a #if for a shader pack: defined(name), comparisons, && and ||.
static int ifEval(const std::unordered_map<std::string, int>& defined, const std::string& d, size_t p);

// the pack's files: a folder or a zip, with the shaders/ folder somewhere at its top
struct IrisFiles {
  std::unique_ptr<PackFS> fs; std::string root;   // root: the path to "shaders/" inside it
  bool open(const fs::path& p) {
    fs = PackFS::open(p);
    if (!fs) return false;
    for (const std::string& f : fs->list("")) {
      size_t i = f.find("shaders/");
      if (i == std::string::npos || (i > 0 && f[i - 1] != '/')) continue;
      // Horizon: a pack is a shaders/ folder. world0/, world-1/ and world1/ replace that folder
      // for one dimension, so a pack whose programs live only there (Complementary) still counts.
      root = f.substr(0, i + 8);
      return true;
    }
    return false;
  }
  std::optional<std::string> read(const std::string& rel) const {
    auto d = fs->read(root + rel);
    if (!d) return std::nullopt;
    return std::string(d->begin(), d->end());
  }
  // #include only on a live branch, and only once. Pulling the file in from a dead
  // branch (both arms of #ifdef THE_END) used to swallow the real copy, so a later
  // call such as dayBlend compiled as a missing function.
  std::string expand(const std::string& src, const std::string& dir, std::set<std::string>& seen,
                     std::unordered_map<std::string, int>& defined, int depth = 0) const {
    struct Frame { bool parent; bool taken; };
    std::vector<Frame> stack;
    bool active = true;
    std::string out;
    // A continued directive (`#if A \` / `&& B`) must stay one line. Otherwise the
    // condition's tail is left in the shader and glslang sees a bare `&&`.
    std::string flat;
    flat.reserve(src.size());
    for (size_t k = 0; k < src.size(); ) {
      if (src[k] == '\\' && k + 1 < src.size() && (src[k + 1] == '\n' || src[k + 1] == '\r')) {
        k += (src[k + 1] == '\r' && k + 2 < src.size() && src[k + 2] == '\n') ? 3 : 2;
        flat.push_back(' ');
        continue;
      }
      flat.push_back(src[k++]);
    }
    size_t i = 0;
    auto word = [](const std::string& d, size_t& p) {
      size_t b = p;
      while (p < d.size() && (std::isalnum((unsigned char)d[p]) || d[p] == '_')) p++;
      return d.substr(b, p - b);
    };
    while (i < flat.size()) {
      size_t eol = flat.find('\n', i);
      if (eol == std::string::npos) eol = flat.size();
      std::string line = flat.substr(i, eol - i);
      i = eol < flat.size() ? eol + 1 : eol;
      size_t h = 0;
      while (h < line.size() && std::isspace((unsigned char)line[h])) h++;
      if (h < line.size() && line[h] == '#') {
        std::string d = line.substr(h + 1);
        size_t slash = d.find("//");
        if (slash != std::string::npos) d.resize(slash);
        size_t p = 0;
        while (p < d.size() && std::isspace((unsigned char)d[p])) p++;
        std::string kw = word(d, p);
        while (p < d.size() && std::isspace((unsigned char)d[p])) p++;
        if (kw == "include") {
          if (!active || depth >= 48) continue;
          size_t q0 = d.find_first_of("\"<", p);
          if (q0 == std::string::npos) continue;
          char close = d[q0] == '<' ? '>' : '"';
          size_t q1 = d.find(close, q0 + 1);
          // Rethinking Voxels' prepare vertex shaders leave the closing quote off. Iris reads to EOL.
          std::string inc = q1 == std::string::npos ? d.substr(q0 + 1) : d.substr(q0 + 1, q1 - q0 - 1);
          while (!inc.empty() && std::isspace((unsigned char)inc.back())) inc.pop_back();
          if (inc.empty()) continue;
          std::string rel = (!inc.empty() && inc[0] == '/') ? inc.substr(1) : dir + inc;
          for (size_t k; (k = rel.find("/../")) != std::string::npos;) {
            size_t s = rel.rfind('/', k - 1);
            rel = (s == std::string::npos ? "" : rel.substr(0, s + 1)) + rel.substr(k + 4);
          }
          if (seen.count(rel)) continue;
          seen.insert(rel);
          if (auto t = read(rel)) {
            std::string sub = rel.substr(0, rel.rfind('/') + 1);
            out += expand(*t, sub, seen, defined, depth + 1) + "\n";
          } else DQ_WARN("shader pack: missing include %s", rel.c_str());
          continue;
        }
        auto evalCond = [&](const std::string& kw2) {
          if (kw2 == "if" || kw2 == "elif") return ifEval(defined, d, p) != 0;
          std::string name = word(d, p);
          return (defined.count(name) != 0) == (kw2 == "ifdef");
        };
        if (kw == "define" && active) {
          std::string name = word(d, p);
          // The driver rejects a second #define. Iris's preprocessor lets the later one win,
          // which packs do on purpose (#define X 1, then #undef X, then #define X 0).
          if (!name.empty() && defined.count(name)) out += "#undef " + name + "\n";
          if (p < d.size() && d[p] == '(') {
            if (!name.empty()) defined[name] = 1;
            out += line + "\n";
            continue;
          }
          while (p < d.size() && std::isspace((unsigned char)d[p])) p++;
          int val = 1;
          if (p < d.size() && (std::isdigit((unsigned char)d[p]) || d[p] == '-' || d[p] == '+' || d[p] == '.')) {
            char* end = nullptr;
            const char* begin = d.c_str() + p;
            float fv = std::strtof(begin, &end);
            bool frac = end && end > begin && std::memchr(begin, '.', (size_t)(end - begin));
            val = frac ? (fv != 0.f ? 1 : 0) : (int)fv;
          } else {
            std::string other = word(d, p);
            auto it = defined.find(other);
            if (it != defined.end()) val = it->second;
          }
          if (!name.empty()) defined[name] = val;
          out += line + "\n";
          continue;
        }
        if (kw == "undef" && active) {
          std::string name = word(d, p);
          if (!name.empty()) {
            defined.erase(name);
            out += "#undef " + name + "\n";
          }
          continue;
        }
        if (kw == "ifdef" || kw == "ifndef" || kw == "if") {
          bool cond = evalCond(kw);
          stack.push_back({active, false});
          active = active && cond;
          if (active) stack.back().taken = true;
          continue;
        }
        if (kw == "elif" && !stack.empty()) {
          bool cond = evalCond(kw);
          active = stack.back().parent && !stack.back().taken && cond;
          if (active) stack.back().taken = true;
          continue;
        }
        if (kw == "else" && !stack.empty()) {
          active = stack.back().parent && !stack.back().taken;
          if (active) stack.back().taken = true;
          continue;
        }
        if (kw == "endif" && !stack.empty()) { active = stack.back().parent; stack.pop_back(); continue; }
      }
      if (active) out += line + "\n";
    }
    return out;
  }
};

// Enough of a #if for a shader pack: defined(name), comparisons, && and ||.
static int ifEval(const std::unordered_map<std::string, int>& defined, const std::string& d, size_t p) {
  struct P {
    const std::unordered_map<std::string, int>& defined;
    const std::string& d;
    size_t p;
    void skip() { while (p < d.size() && std::isspace((unsigned char)d[p])) p++; }
    std::string word() {
      skip();
      size_t b = p;
      while (p < d.size() && (std::isalnum((unsigned char)d[p]) || d[p] == '_')) p++;
      return d.substr(b, p - b);
    }
    int primary() {
      skip();
      if (p >= d.size()) return 0;
      if (d[p] == '(') { p++; int v = orExpr(); skip(); if (p < d.size() && d[p] == ')') p++; return v; }
      if (d[p] == '-') { p++; return -unary(); }
      if (d[p] == '+') { p++; return unary(); }
      if (std::isdigit((unsigned char)d[p]) || d[p] == '.') {
        char* end = nullptr;
        float fv = std::strtof(d.c_str() + p, &end);
        if (end) p = (size_t)(end - d.c_str());
        return (int)fv;
      }
      std::string name = word();
      if (name == "defined") {
        skip();
        bool paren = p < d.size() && d[p] == '(';
        if (paren) p++;
        name = word();
        if (paren) { skip(); if (p < d.size() && d[p] == ')') p++; }
        return defined.count(name) ? 1 : 0;
      }
      if (name == "true") return 1;
      if (name == "false") return 0;
      auto it = defined.find(name);
      return it == defined.end() ? 0 : it->second;
    }
    int unary() { skip(); if (p < d.size() && d[p] == '!') { p++; return !unary(); } return primary(); }
    int cmp() {
      int v = unary();
      skip();
      auto op2 = [&](char a, char b) { return p + 1 < d.size() && d[p] == a && d[p + 1] == b; };
      if (op2('=', '=')) { p += 2; return v == unary(); }
      if (op2('!', '=')) { p += 2; return v != unary(); }
      if (op2('>', '=')) { p += 2; return v >= unary(); }
      if (op2('<', '=')) { p += 2; return v <= unary(); }
      if (p < d.size() && d[p] == '>') { p++; return v > unary(); }
      if (p < d.size() && d[p] == '<') { p++; return v < unary(); }
      return v;
    }
    int andExpr() {
      int v = cmp();
      skip();
      if (p + 1 < d.size() && d[p] == '&' && d[p + 1] == '&') { p += 2; int r = andExpr(); return (v && r) ? 1 : 0; }
      return v ? 1 : 0;
    }
    int orExpr() {
      int v = andExpr();
      skip();
      if (p + 1 < d.size() && d[p] == '|' && d[p + 1] == '|') { p += 2; int r = orExpr(); return (v || r) ? 1 : 0; }
      return v ? 1 : 0;
    }
  };
  return P{defined, d, p}.orExpr();
}

// The game's wind, copied so a pack gbuffer's depth matches the terrain that was just drawn.
// Named dqWave so a pack function called waveOffset does not collide.
const char* DQ_WAVE = R"DQGB(
vec3 dqWave(float sway, vec3 world) {
  if (sway < 0.5) return vec3(0.0);
  vec3 o = vec3(0.0);
  float t = uTime;
  vec2 w = world.xz;
  float gust = 0.6 + 0.4 * sin(t * 0.35 + w.x * 0.02 + w.y * 0.015);
  if (sway > 79.5) {
    float k = (sway - 80.0) / 15.0;
    o.x += (sin(t * 2.3 + w.x * 0.9 + w.y * 0.4) + 0.5 * sin(t * 4.1 + w.x * 2.3 - w.y * 1.7)) * 0.05 * uWind * gust * k;
    o.z += cos(t * 1.9 + w.y * 0.8 + w.x * 0.3) * 0.04 * uWind * gust * k;
    return o;
  }
  if (sway > 47.5) {
    bool acrossX = sway > 63.5;
    float k = (sway - (acrossX ? 64.0 : 48.0)) / 15.0;
    float ph = w.x * 0.37 + w.y * 0.29;
    float wave = sin(t * 5.1 - k * 4.2 + ph) * 0.65 + sin(t * 3.3 - k * 2.6 + ph * 1.7) * 0.35;
    float a = k * (0.07 + 0.2 * uWind) * gust * wave;
    if (acrossX) o.x += a; else o.z += a;
    o.y -= abs(a) * 0.2 * k;
  } else if (sway > 31.5) {
    float k = (sway - 32.0) / 15.0, A = k * (0.04 + 0.14 * uSea);
    float ripple = sin(t * 1.6 + w.x * 0.8 + w.y * 0.55) * 0.035 + sin(t * 1.05 - w.x * 0.37 + w.y * 0.77) * 0.025;
    float swell = sin(t * 0.9 + w.x * 0.21 + w.y * 0.13) * 0.6 + sin(t * 0.63 - w.x * 0.09 + w.y * 0.17) * 0.4;
    float chop = sin(t * 2.7 + w.x * 1.3 - w.y * 0.9) * uSea * k;
    o.y += ripple * (1.0 + 0.6 * uSea * k) + swell * A + chop * 0.025 - A * 0.7 - 0.06;
  } else if (sway < 1.5 || sway > 15.5) {
    float pk = sway > 15.5 ? (sway - 16.0) / 15.0 : 1.0;
    o.x += (sin(t * 1.9 + w.x * 0.37 + w.y * 0.23) + 0.4 * sin(t * 3.3 + w.x * 1.1)) * 0.075 * uWind * gust * pk;
    o.z += cos(t * 1.5 + w.y * 0.31 + w.x * 0.11) * 0.055 * uWind * gust * pk;
  } else if (sway < 2.5) {
    float ph = w.x * 0.45 + w.y * 0.38 + world.y * 0.5;
    o.x += sin(t * 1.4 + ph) * 0.028 * uWind * gust;
    o.y += sin(t * 1.9 + ph * 1.3) * 0.018 * uWind * gust;
    o.z += cos(t * 1.2 + ph) * 0.028 * uWind * gust;
  } else if (sway < 3.5) {
    o.y += sin(t * 1.6 + w.x * 0.8 + w.y * 0.55) * 0.035 + sin(t * 1.05 - w.x * 0.37 + w.y * 0.77) * 0.025 - 0.06;
  } else {
    float k = (sway - 4.0) / 11.0;
    float ph = floor(w.x) * 1.7 + floor(w.y) * 2.3;
    o.x += sin(t * 1.7 + ph) * 0.07 * k * (0.35 + uWind) * gust;
    o.z += cos(t * 1.3 + ph * 0.7) * 0.05 * k * (0.35 + uWind) * gust;
  }
  return o;
}
)DQGB";

// ChunkVertex unpacked into the Iris attributes. File-scope, because the pack's main is textually
// separate and a #define of gl_Vertex cannot see a local. gl_Position is forced afterwards so the
// pack's TAA jitter cannot miss the depth the game already wrote.
std::string geomVertexHead(bool shadow, bool translucent) {
  std::string h;
  if (gfx.mdi) h += "#define DQ_MODERN 1\nlayout(location = 6) in vec3 aOrigin;\n";
  else h += "uniform vec3 uOrigin;\n";
  h += R"DQGB(
layout(location = 0) in uvec4 aV;
uniform mat4 uViewProj;
uniform vec3 uWorldOrigin;
uniform vec2 uGrassFade;
uniform float uTime, uWind, uSea, uAtlasGrid, uTileSize;
uniform sampler2DArray uGbClimate;
uniform sampler2DArray uGbTint;
uniform sampler2D dqEntityLut;
vec3 dqGbRel, dqGbN;
vec4 dqGbColor;
vec4 dqMcEntity;
vec2 dqGbUV, dqGbMid;
vec4 dqGbLM, dqGbTangent;
mat4 dqGbTexMat[8];
out vec4 dqFrontColor;
out vec4 dqTC[2];
#define gl_Vertex vec4(dqGbRel, 1.0)
#define gl_MultiTexCoord0 vec4(dqGbUV, 0.0, 1.0)
#define gl_MultiTexCoord1 dqGbLM
#define gl_Color dqGbColor
#define gl_Normal dqGbN
#define gl_TextureMatrix dqGbTexMat
)DQGB";
  // During the shadow pass the fixed-function matrices ARE the light matrices. The pack recovers
  // player space with shadowInverse * ftransform(), then writes the distorted light clip itself.
  if (shadow) {
    h += R"DQGB(
#define DQ_SHADOW 1
uniform vec3 uEyeRel;
void dqShadowBridge();
#define gl_ModelViewMatrix shadowModelView
#define gl_ProjectionMatrix shadowProjection
#define gl_ModelViewProjectionMatrix (shadowProjection * shadowModelView)
#define gl_ModelViewMatrixInverse shadowModelViewInverse
#define gl_ProjectionMatrixInverse shadowProjectionInverse
#define gl_NormalMatrix mat3(shadowModelView)
#define ftransform() (shadowProjection * shadowModelView * gl_Vertex)
)DQGB";
  } else {
    h += R"DQGB(
#define gl_ModelViewMatrix gbufferModelView
#define gl_ProjectionMatrix gbufferProjection
#define gl_ModelViewProjectionMatrix (gbufferProjection * gbufferModelView)
#define gl_ModelViewMatrixInverse gbufferModelViewInverse
#define gl_ProjectionMatrixInverse gbufferProjectionInverse
#define gl_NormalMatrix mat3(gbufferModelView)
#define ftransform() (gbufferProjection * gbufferModelView * gl_Vertex)
uniform vec3 uEyeRel;
)DQGB";
  }
  // gbufferModelView is rotation only. Subtracting the fractional eye makes gl_Vertex
  // camera-relative, so the pack's own gl_Position matches the game for an unwaved vertex.
  // Opaque keeps that offset in the position override below; uEyeRel is still the fog distance.
  if (translucent) h += "#define DQ_TRANSLUCENT 1\n";
  h += R"DQGB(
#define mc_midTexCoord vec4(dqGbMid, 0.0, 1.0)
#define at_tangent dqGbTangent
#define vaPosition gl_Vertex
#define vaColor gl_Color
#define vaUV0 gl_MultiTexCoord0
#define vaUV1 gl_MultiTexCoord1
#define vaUV2 gl_MultiTexCoord1
#define vaNormal gl_Normal
#define mc_Entity dqMcEntity
)DQGB";
  h += DQ_WAVE;
  return h;
}

const char* GEOM_MAIN = R"DQGB(
void main() {
  dqGbTexMat[0] = mat4(1.0);
  dqGbTexMat[1] = mat4(vec4(1.0 / 256.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0 / 256.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(1.0 / 32.0, 1.0 / 32.0, 0.0, 1.0));
  dqGbTexMat[2] = dqGbTexMat[1];
  dqGbTexMat[3] = mat4(1.0); dqGbTexMat[4] = mat4(1.0); dqGbTexMat[5] = mat4(1.0); dqGbTexMat[6] = mat4(1.0); dqGbTexMat[7] = mat4(1.0);
#ifdef DQ_MODERN
  vec3 origin = aOrigin;
#else
  vec3 origin = uOrigin;
#endif
  vec3 p = vec3(float(aV.x & 16383u), float((aV.x >> 14u) & 16383u), float(aV.y & 16383u)) / 256.0 - 16.0;
  vec3 rel = origin + p;
  vec3 world = rel + uWorldOrigin;
  float sw = float((aV.w >> 24u) & 127u);
  uint tk = (aV.y >> 26u) & 31u;
  vec3 tint = vec3(1.0);
  if (tk >= 4u) {
    float ff = float(tk - 4u) / 15.0;
    tint = vec3(ff * 0.6 + (ff > 0.0 ? 0.4 : 0.3), clamp(ff * ff * 0.7 - 0.5, 0.0, 1.0), clamp(ff * ff * 0.6 - 0.7, 0.0, 1.0)) * (204.0 / 255.0);
  } else if (tk > 0u) {
    float land = clamp(floor((origin.y + uWorldOrigin.y + 8.0) / 256.0), 0.0, 3.0);
    vec2 cl = textureLod(uGbClimate, vec3(world.xz / 1024.0, land), 0.0).rg;
    tint = textureLod(uGbTint, vec3((cl * 255.0 + 0.5) / 256.0, float(tk - 1u)), 0.0).rgb;
  }
#ifndef DQ_TRANSLUCENT
  vec3 wo = dqWave(sw, world);
  if (sw > 79.5) {
    float fade = smoothstep(uGrassFade.x, uGrassFade.y, length(rel));
    wo *= 1.0 - fade;
    rel.y -= (fract(world.y) + 0.04) * fade;
  }
  rel += wo;
#endif
#if defined(DQ_SHADOW) || defined(DQ_TRANSLUCENT)
  // Iris player space puts the camera at the origin. The shadow caster has to start there or the
  // shadow texel slides, and translucent clip space is the pack's (waves included) only if this matches.
  rel -= uEyeRel;
#endif
  dqGbRel = rel;
  vec2 tileUV = clamp(vec2(float(aV.z & 16383u), float((aV.z >> 14u) & 16383u)) / 256.0 - 16.0, vec2(0.0), vec2(1.0));
  float layer = float((aV.y >> 14u) & 4095u);
  float grid = max(uAtlasGrid, 1.0);
  float col = mod(layer, grid);
  float row = floor(layer / grid);
  float halfTex = 0.5 / max(uTileSize, 1.0);
  dqGbUV = (vec2(col, row) + mix(vec2(halfTex), vec2(1.0 - halfTex), tileUV)) / grid;
  dqGbMid = (vec2(col, row) + 0.5) / grid;
  dqGbLM = vec4(float((aV.w >> 8u) & 255u) / 255.0 * 240.0, float(aV.w & 255u) / 255.0 * 240.0, 0.0, 1.0);
  int fn = int((aV.x >> 28u) & 7u);
  vec3 N = vec3(0.0, 1.0, 0.0);
  if (fn == 0) N = vec3(1.0, 0.0, 0.0);
  else if (fn == 1) N = vec3(-1.0, 0.0, 0.0);
  else if (fn == 2) N = vec3(0.0, 1.0, 0.0);
  else if (fn == 3) N = vec3(0.0, -1.0, 0.0);
  else if (fn == 4) N = vec3(0.0, 0.0, 1.0);
  else if (fn == 5) N = vec3(0.0, 0.0, -1.0);
  dqGbN = N;
  vec3 T = abs(N.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), N));
  dqGbTangent = vec4(T, 1.0);
  dqGbColor = vec4(tint, 1.0);
  dqFrontColor = dqGbColor;
  dqTC[0] = vec4(dqGbUV, 0.0, 1.0);
  dqTC[1] = dqGbLM;
  dqMcEntity = vec4(texelFetch(dqEntityLut, ivec2(int(layer) & 4095, 0), 0).r, 0.0, 0.0, 0.0);
#ifdef DQ_FOG_COORD
  // OptiFine writes the eye distance before the pack's main. MakeUp's hand light is
  // (1 - fogFragCoord / 15). Zero lit the whole world; a pack that assigns it still overwrites this.
#if defined(DQ_SHADOW) || defined(DQ_TRANSLUCENT)
  dqFogFragCoord = length(dqGbRel);
#else
  dqFogFragCoord = length(dqGbRel - uEyeRel);
#endif
#endif
  dqPackMain();
#ifdef DQ_SHADOW
  // A shadow geometry shader would copy texCoordV onto texCoord. Without one, the fragment
  // reads the names the geometry stage would have written, so the vertex copies them across.
  dqShadowBridge();
#elif !defined(DQ_TRANSLUCENT)
  // The opaque pass must hit the depth the game already wrote. The pack's TAA jitter would miss it.
  // Translucent geometry writes its own depth, so the pack keeps the waved clip position.
  gl_Position = uViewProj * vec4(dqGbRel, 1.0);
#endif
}
)DQGB";

// Voxel meshes (creatures, characters, chests, drops) use a different vertex than terrain.
// gl_Vertex is player space so the pack's varyings match Iris. gl_Position is then the game's
// clip, or a depth test against the copy misses and the model stays game-lit.
// The hand is already in view space. Its fixed-function modelview stays identity so the normal
// is not rotated twice, while gbufferModelView remains the camera rotation for the sun and
// the world-space normal the fragment writes.
std::string entityVertexHead(bool shadow, bool hand = false) {
  std::string h = R"DQGB(
layout(location = 0) in vec3 dqEntPos;
layout(location = 1) in uint dqEntNA;
layout(location = 2) in vec4 dqEntCol;
uniform mat4 uViewProj;
uniform mat4 dqEntModel;
uniform vec2 dqEntLight;
uniform vec4 dqEntTint;
uniform vec3 uEyeRel;
vec3 dqGbRel, dqGbN;
vec4 dqGbColor;
vec4 dqMcEntity;
vec2 dqGbUV, dqGbMid;
vec4 dqGbLM, dqGbTangent;
mat4 dqGbTexMat[8];
out vec4 dqFrontColor;
out vec4 dqTC[2];
#define gl_Vertex vec4(dqGbRel, 1.0)
#define gl_MultiTexCoord0 vec4(dqGbUV, 0.0, 1.0)
#define gl_MultiTexCoord1 dqGbLM
#define gl_Color dqGbColor
#define gl_Normal dqGbN
#define gl_TextureMatrix dqGbTexMat
)DQGB";
  if (shadow) {
    h += R"DQGB(
#define DQ_SHADOW 1
void dqShadowBridge();
#define gl_ModelViewMatrix shadowModelView
#define gl_ProjectionMatrix shadowProjection
#define gl_ModelViewProjectionMatrix (shadowProjection * shadowModelView)
#define gl_ModelViewMatrixInverse shadowModelViewInverse
#define gl_ProjectionMatrixInverse shadowProjectionInverse
#define gl_NormalMatrix mat3(shadowModelView)
#define ftransform() (shadowProjection * shadowModelView * gl_Vertex)
)DQGB";
  } else if (hand) {
    h += R"DQGB(
#define gl_ModelViewMatrix mat4(1.0)
#define gl_ProjectionMatrix gbufferProjection
#define gl_ModelViewProjectionMatrix gbufferProjection
#define gl_ModelViewMatrixInverse mat4(1.0)
#define gl_ProjectionMatrixInverse gbufferProjectionInverse
#define gl_NormalMatrix mat3(1.0)
#define ftransform() (gbufferProjection * gl_Vertex)
)DQGB";
  } else {
    h += R"DQGB(
#define gl_ModelViewMatrix gbufferModelView
#define gl_ProjectionMatrix gbufferProjection
#define gl_ModelViewProjectionMatrix (gbufferProjection * gbufferModelView)
#define gl_ModelViewMatrixInverse gbufferModelViewInverse
#define gl_ProjectionMatrixInverse gbufferProjectionInverse
#define gl_NormalMatrix mat3(gbufferModelView)
#define ftransform() (gbufferProjection * gbufferModelView * gl_Vertex)
)DQGB";
  }
  h += R"DQGB(
#define mc_midTexCoord vec4(dqGbMid, 0.0, 1.0)
#define at_tangent dqGbTangent
#define vaPosition gl_Vertex
#define vaColor gl_Color
#define vaUV0 gl_MultiTexCoord0
#define vaUV1 gl_MultiTexCoord1
#define vaUV2 gl_MultiTexCoord1
#define vaNormal gl_Normal
#define mc_Entity dqMcEntity
)DQGB";
  return h;
}

const char* ENTITY_MAIN = R"DQGB(
void main() {
  dqGbTexMat[0] = mat4(1.0);
  dqGbTexMat[1] = mat4(vec4(1.0 / 256.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0 / 256.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(1.0 / 32.0, 1.0 / 32.0, 0.0, 1.0));
  dqGbTexMat[2] = dqGbTexMat[1];
  dqGbTexMat[3] = mat4(1.0); dqGbTexMat[4] = mat4(1.0); dqGbTexMat[5] = mat4(1.0); dqGbTexMat[6] = mat4(1.0); dqGbTexMat[7] = mat4(1.0);
  vec4 world = dqEntModel * vec4(dqEntPos, 1.0);
  dqGbRel = world.xyz - uEyeRel;
  int fn = int(dqEntNA & 7u);
  if (fn > 5) fn = 2;
  const vec3 NRM[6] = vec3[6](vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0));
  vec3 n = normalize(mat3(dqEntModel) * NRM[fn]);
  dqGbN = n;
  vec3 T = abs(n.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), n));
  dqGbTangent = vec4(T, 1.0);
  dqGbUV = vec2(0.5);
  dqGbMid = vec2(0.5);
  float sky = clamp(dqEntLight.x, 0.0, 1.0);
  float block = clamp(dqEntLight.y, 0.0, 1.0);
  if (dqEntCol.a < 0.999) { sky = 1.0; block = 1.0; }
  dqGbLM = vec4(block * 240.0, sky * 240.0, 0.0, 1.0);
  vec3 tint = mix(dqEntCol.rgb, dqEntTint.rgb, clamp(dqEntTint.a, 0.0, 1.0));
  dqGbColor = vec4(tint, 1.0);
  dqFrontColor = dqGbColor;
  dqTC[0] = vec4(dqGbUV, 0.0, 1.0);
  dqTC[1] = dqGbLM;
  dqMcEntity = vec4(1.0, 0.0, 0.0, 0.0);
#ifdef DQ_FOG_COORD
  dqFogFragCoord = length(dqGbRel);
#endif
  dqPackMain();
#ifdef DQ_SHADOW
  dqShadowBridge();
#else
  gl_Position = uViewProj * world;
#endif
}
)DQGB";

void stripPackAttribs(std::string& src) {
  // The standard Iris attributes are supplied by #define. A leftover `in` would be a second declaration
  // once the macro expands, and the VAO only feeds locations 0 and 6.
  src = std::regex_replace(src, std::regex(
    R"([ \t]*(?:layout\s*\([^)]*\)\s*)?(?:attribute|in|out)\s+(?:(?:flat|smooth|noperspective)\s+)?(?:(?:highp|mediump|lowp)\s+)?(?:vec[234]|ivec[234]|uvec[234]|float|int|uint)\s+(?:mc_Entity|mc_midTexCoord|at_tangent|vaPosition|vaColor|vaUV0|vaUV1|vaUV2|vaNormal)\s*;[ \t]*(?://[^\n]*)?\r?)"),
    "");
}

// Iris injects these. A second `uniform mat4` of the same name fails to compile on this Mesa even when the type matches.
// force declares a matrix the preamble's macros name; otherwise only a name the pack actually uses.
// Rethinking Voxels (and Complementary's shadow geometry stage) writes texCoordV from the vertex
// and reads texCoord in the fragment. The geometry shader is the voxel volume, which is not run.
// Copy each fragment input from the vertex output of the same name plus "V".
std::string shadowBridgeDef(const std::string& vsrc, const std::string& fsrc) {
  struct Var { std::string qual, type, name; };
  auto ident = [](const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    return true;
  };
  auto collect = [&](const std::string& src, const char* dir) {
    std::vector<Var> out;
    // Packs write `flat out vec3 a, b` and sometimes `varying`. translate() has already
    // turned varying into in/out. A Windows pack keeps the CR, so it counts as space.
    const std::regex re(std::string("(?:^|\\n)[ \\t\\r]*((?:centroid|sample|flat|smooth|noperspective)[ \\t]+)?") + dir +
                        "[ \\t]+((?:centroid|sample|flat|smooth|noperspective)[ \\t]+)?"
                        "(?:(?:highp|mediump|lowp)[ \\t]+)?([A-Za-z_][A-Za-z0-9_]*)[ \\t]+([^;\\n]+);");
    for (auto it = std::sregex_iterator(src.begin(), src.end(), re); it != std::sregex_iterator(); ++it) {
      auto trim = [](std::string s) {
        size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
        size_t b = s.size(); while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
        return s.substr(a, b - a);
      };
      std::string qual = trim((*it)[1].str());
      if (qual.empty()) qual = trim((*it)[2].str());
      const std::string type = (*it)[3].str();
      static const char* types[] = {"float", "int", "uint", "bool", "vec2", "vec3", "vec4",
                                     "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
                                     "mat2", "mat3", "mat4"};
      bool typeOk = false;
      for (const char* t : types) if (type == t) typeOk = true;
      if (!typeOk) continue;
      std::string names = (*it)[4].str();
      size_t p = 0;
      while (p < names.size()) {
        size_t c = names.find(',', p);
        std::string one = trim(names.substr(p, c == std::string::npos ? std::string::npos : c - p));
        p = c == std::string::npos ? names.size() : c + 1;
        size_t br = one.find('[');
        if (br != std::string::npos) one = trim(one.substr(0, br));
        if (!ident(one) || one.rfind("dq", 0) == 0 || one.rfind("gl_", 0) == 0) continue;
        out.push_back({qual, type, one});
      }
    }
    return out;
  };
  const auto vouts = collect(vsrc, "out");
  const auto fins = collect(fsrc, "in");
  auto findName = [](const std::vector<Var>& v, const std::string& n) -> const Var* {
    for (auto& x : v) if (x.name == n) return &x;
    return nullptr;
  };
  std::string decls, body;
  for (const auto& f : fins) {
    if (findName(vouts, f.name)) continue;
    const Var* src = findName(vouts, f.name + "V");
    if (!src || src->type != f.type) continue;
    const std::string& q = f.qual.empty() ? src->qual : f.qual;
    if (!q.empty()) decls += q + " ";
    decls += "out " + f.type + " " + f.name + ";\n";
    body += "  " + f.name + " = " + f.name + "V;\n";
  }
  return decls + "void dqShadowBridge() {\n" + body + "}\n";
}

void declareMat4(std::string& head, const std::string& src, const char* name, bool force) {
  // A pack may declare the matrix as `uniform mat4 a, name` or as a shader-storage member (`mat4 name`).
  // Either one is the declaration. A second `uniform mat4` fails on this Mesa even when the type matches.
  const std::regex decl(std::string("(?:uniform\\s+mat4\\s+(?:[A-Za-z_][A-Za-z0-9_]*\\s*,\\s*)*|\\bmat4\\s+)") + name + "\\b");
  if (std::regex_search(src, decl) || std::regex_search(head, decl)) return;
  if (!force && !std::regex_search(src, std::regex(std::string("\\b") + name + "\\b"))) return;
  head += std::string("uniform mat4 ") + name + ";\n";
}

// a pack's GLSL (any #version, mostly 120) brought to 330 core: the old built-ins become ours, main() runs inside ours.
// geom is a gbuffer: real terrain attributes, not the fullscreen quad. shadow is the sun pass (the pack keeps gl_Position).
std::vector<int> drawBuffers(const std::string& fsh, bool isFinal);

// How many fragment outputs this pass actually stores. The framebuffer is sized from the
// same DRAWBUFFERS / RENDERTARGETS list. Emitting all eight gl_FragData slots makes Vulkan
// build a pipeline with null attachments, which this GPU rejects. The shadow map keeps a
// single colour target; a second gl_FragData write is the light-shaft buffer and is discarded.
static int fragmentOutputCount(const std::string& src, bool shadow) {
  if (shadow) return 1;
  int n = (int)drawBuffers(src, false).size();
  if (n < 1) n = 1;
  if (n > 8) n = 8;
  return n;
}

std::string translate(std::string src, bool vertex, bool geom = false, bool shadow = false, bool translucent = false, const std::string& alphaKeep = {}, bool entity = false, bool hand = false) {
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
  // GLSL 1.20 fog built-in. Packs still write it from the vertex. A literal 0.0 is not
  // an l-value, so `gl_FogFragCoord = length(view)` died as "can't modify a const".
  src = rx(src, R"(\bgl_FogFragCoord\b)", "dqFogFragCoord");
  src = rx(src, R"(\bgl_Fog\s*\.\s*color\b)", "vec4(fogColor, 1.0)");
  src = rx(src, R"(\bgl_Fog\s*\.\s*density\b)", "0.0");
  src = rx(src, R"(\bgl_Fog\s*\.\s*start\b)", "near");
  src = rx(src, R"(\bgl_Fog\s*\.\s*end\b)", "far");
  src = rx(src, R"(\bgl_Fog\s*\.\s*scale\b)", "(1.0 / max(far - near, 0.001))");
  // A gbuffer's gl_Color is the biome tint. The fullscreen path has no per-vertex colour.
  if (!geom) src = rx(src, R"(\bgl_Color\b)", "vec4(1.0)");
  // gl_FragData / gl_FragColor is the OptiFine path (dqOut copies). A pack that already
  // declares out vec2/vec3/vec4 keeps those locations: Photon binds location 0 itself, and
  // a second out there fails the link. `out bool` parameters do not count.
  const bool legacyOut = src.find("gl_FragData") != std::string::npos || src.find("gl_FragColor") != std::string::npos;
  const bool userOut = std::regex_search(src, std::regex(R"(\bout\s+vec[234]\b)"));
  const bool old = legacyOut || !userOut;
  src = rx(src, R"(\bvoid\s+main\s*\(\s*(void)?\s*\))", "void dqPackMain()");
  // Vulkan reserves the word sampler. Photon names texture parameters `sampler`.
  src = rx(src, R"(\bsampler\b)", "dqSampler");
  // OptiFine names the block atlas sampler `texture`. After texture2D becomes texture(),
  // that name hides the built-in and glslang rejects the call. Rename the sampler only
  // in files that declare it. Photon uses the word as the texture() function in a macro.
  if (std::regex_search(src, std::regex(R"(\buniform\s+sampler[0-9A-Za-z]*\s+texture\s*;)")))
    src = rx(src, R"(\btexture\b(?!\s*\())", "gtexture");
  src = rx(src, R"(\btexture2DLod\b)", "textureLod");
  src = rx(src, R"(\btexture2DGradARB\b|\btexture2DGrad\b)", "textureGrad");
  src = rx(src, R"(\btexture2D\b|\btexture3D\b|\btextureRect\b)", "texture");
  src = rx(src, R"(\bshadow2D\s*\()", "dqShadow2D(");
  src = rx(src, R"(\btexture2DProj\b)", "textureProj");
  src = rx(src, R"(\bgl_TexCoord\b)", "dqTC");
  // 330 core rejects layout(binding) and shader storage blocks. Iris compiles these at 4.30+.
  std::string head = "#version 430 core\n" + ext;
  if (vertex) {
    src = rx(src, R"(\battribute\b)", "in");
    src = rx(src, R"(\bvarying\b)", "out");
    src = rx(src, R"(\bgl_FrontColor\b)", "dqFrontColor");
    if (geom) {
      // OptiFine's mc_midTexCoord is vec4 in some packs and vec2 in others. The vec4 macro
      // makes `uv - mc_midTexCoord` illegal for a pack that declared vec2.
      const bool midVec2 = std::regex_search(src, std::regex(R"(\bin\s+vec2\s+mc_midTexCoord\b)"));
      stripPackAttribs(src);
      if (entity) {
        head += entityVertexHead(shadow, hand);
        src += ENTITY_MAIN;
      } else {
        head += geomVertexHead(shadow, translucent);
        src += GEOM_MAIN;
      }
      if (midVec2) {
        const std::string from = "#define mc_midTexCoord vec4(dqGbMid, 0.0, 1.0)";
        auto at = head.find(from);
        if (at != std::string::npos) head.replace(at, from.size(), "#define mc_midTexCoord dqGbMid");
      }
    } else {
      head += "layout(location = 0) in vec4 dqVertex;\nuniform mat4 dqMVP;\nout vec4 dqTC[2];\nvec4 dqFrontColor;\n"
              "#define gl_Vertex dqVertex\n#define gl_MultiTexCoord0 vec4(dqVertex.xy, 0.0, 1.0)\n#define gl_MultiTexCoord1 vec4(240.0, 240.0, 0.0, 1.0)\n"
              "#define gl_Color vec4(1.0)\n#define gl_Normal vec3(0.0, 0.0, 1.0)\n#define gl_ModelViewProjectionMatrix dqMVP\n#define gl_ProjectionMatrix dqMVP\n"
              "#define gl_ModelViewMatrix mat4(1.0)\n#define gl_NormalMatrix mat3(1.0)\n#define gl_TextureMatrix dqTexMat\n#define ftransform() (dqMVP * dqVertex)\n"
              "const mat4 dqTexMat[2] = mat4[2](mat4(1.0), mat4(1.0));\n";
      // The pack places the quad, then z is pulled to the near plane. A post pass is not depth
      // tested; leaving it at window depth 0.5 hides the pass behind anything close to the camera.
      src += "\nvoid main() {\n#ifdef DQ_FOG_COORD\n dqFogFragCoord = 0.0;\n#endif\n"
             "dqTC[0] = vec4(dqVertex.xy, 0.0, 1.0); dqTC[1] = vec4(0.0);\n"
             "gl_Position = vec4(dqVertex.xy * 2.0 - 1.0, -1.0, 1.0); dqPackMain();\n"
             "gl_Position = vec4(gl_Position.xy / max(gl_Position.w, 1e-6), -1.0, 1.0); }\n";
    }
  } else {
    src = rx(src, R"(\bvarying\b)", "in");
    if (geom) stripPackAttribs(src);
    head += "in vec4 dqTC[2];\nvec4 dqShadow2D(sampler2DShadow s, vec3 c) { return vec4(texture(s, c)); }\n";
    if (geom) head += "in vec4 dqFrontColor;\n#define gl_Color dqFrontColor\n#define mc_Entity vec4(1.0, 0.0, 0.0, 0.0)\n"
                      "#define mc_midTexCoord vec4(0.5)\n#define at_tangent vec4(1.0, 0.0, 0.0, 1.0)\n";
    // File-scope `ivec2 texelCoord = ivec2(gl_FragCoord.xy)` is not a constant. Core GLSL
    // rejects it or runs it once as 0, so every texelFetch reads the corner. Iris evaluates
    // it per fragment. Hoist those to the start of main.
    std::string fragInit;
    {
      std::string rebuilt;
      int depth = 0;
      size_t pos = 0;
      const std::regex gre(R"(^[ \t]*((?:i|u)?vec[234]|float|int|uint)\s+(\w+)\s*=\s*([^;]*\bgl_FragCoord\b[^;]*);[ \t]*(?://.*)?$)");
      while (pos < src.size()) {
        size_t eol = src.find('\n', pos);
        if (eol == std::string::npos) eol = src.size();
        const std::string line = src.substr(pos, eol - pos);
        pos = eol < src.size() ? eol + 1 : src.size();
        std::smatch m;
        if (depth == 0 && std::regex_match(line, m, gre)) {
          rebuilt += m[1].str() + " " + m[2].str() + ";\n";
          fragInit += m[2].str() + " = " + m[3].str() + ";\n";
        } else rebuilt += line + "\n";
        for (char c : line) {
          if (c == '{') depth++;
          else if (c == '}') depth = std::max(0, depth - 1);
        }
      }
      src = std::move(rebuilt);
    }
    if (old) {
      // The hand is drawn into the one buffer final reads. Extra locations would be null
      // attachments on Vulkan, and this GPU faults on those.
      const int nOut = hand ? 1 : fragmentOutputCount(src, shadow);
      src = rx(src, R"(\bgl_FragData\b)", "dqData");
      src = rx(src, R"(\bgl_FragColor\b)", "dqData[0]");
      std::string outs, assigns;
      for (int i = 0; i < nOut; i++) {
        outs += "layout(location = " + std::to_string(i) + ") out vec4 dqOut" + std::to_string(i) + ";\n";
        if (i) assigns += " ";
        assigns += "dqOut" + std::to_string(i) + " = dqData[" + std::to_string(i) + "];";
      }
      head += outs + "vec4 dqData[8];\n";
      // OptiFine alphaTest runs on the color written to gl_FragData[0], before it is stored.
      const std::string alphaDiscard = alphaKeep.empty() ? std::string() : "if (!(" + alphaKeep + ")) discard;\n";
      src += "\nvoid main() {\n" + fragInit + "for (int i = 0; i < 8; i++) dqData[i] = vec4(0.0); dqPackMain();\n"
             + alphaDiscard + assigns + " }\n";
    } else {
      // outputs declared the new way. Give them locations in order when the pack did not.
      if (src.find("layout(location") == std::string::npos && src.find("layout (location") == std::string::npos) {
        std::regex o(R"((^|\n)([ \t]*)out\s+(vec[234])\s+(\w+)\s*;)");
        std::string res; int n = 0; auto b = src.cbegin();
        for (auto it = std::sregex_iterator(src.begin(), src.end(), o); it != std::sregex_iterator(); ++it) {
          res.append(b, src.cbegin() + it->position());
          res += (*it)[1].str() + (*it)[2].str() + "layout(location = " + std::to_string(n++) + ") out " + (*it)[3].str() + " " + (*it)[4].str() + ";";
          b = src.cbegin() + it->position() + it->length();
        }
        res.append(b, src.cend()); src = res;
      }
      src += "\nvoid main() {\n" + fragInit + "dqPackMain(); }\n";
    }
  }
  // The renamed fog varying has to exist in the stage that uses it. The vertex writes it
  // and the fragment reads it, so the two declarations are a matched pair.
  if (src.find("dqFogFragCoord") != std::string::npos && head.find("dqFogFragCoord") == std::string::npos &&
      !std::regex_search(src, std::regex(R"(\b(?:in|out)\s+float\s+dqFogFragCoord\b)"))) {
    if (vertex) head += "out float dqFogFragCoord;\n#define DQ_FOG_COORD 1\n";
    else head += "in float dqFogFragCoord;\n";
  }
  const bool camGeom = geom && !shadow;
  declareMat4(head, src, "gbufferModelView", camGeom);
  declareMat4(head, src, "gbufferModelViewInverse", camGeom);
  declareMat4(head, src, "gbufferProjection", camGeom);
  declareMat4(head, src, "gbufferProjectionInverse", camGeom);
  declareMat4(head, src, "gbufferPreviousModelView", false);
  declareMat4(head, src, "gbufferPreviousModelViewInverse", false);
  declareMat4(head, src, "gbufferPreviousProjection", false);
  declareMat4(head, src, "gbufferPreviousProjectionInverse", false);
  declareMat4(head, src, "shadowModelView", shadow);
  declareMat4(head, src, "shadowModelViewInverse", shadow);
  declareMat4(head, src, "shadowProjection", shadow);
  declareMat4(head, src, "shadowProjectionInverse", shadow);
  std::string out = head + src;
  if (!vertex) out = lod0ForDataTaps(out);
  return out;
}

const char* DEFAULT_VSH =
  "#version 120\nvarying vec2 texcoord;\nvoid main() { gl_Position = ftransform(); texcoord = gl_MultiTexCoord0.st; }\n";

// colortex0..7 and the old names. -1 is not one of them.
int colortexOf(const std::string& n) {
  if (n == "colortex0" || n == "gcolor" || n == "texture" || n == "tex" || n == "gtexture") return 0;
  if (n == "gdepth") return 1;
  if (n == "gnormal") return 2;
  if (n == "composite") return 3;
  if (n.rfind("colortex", 0) == 0) { int i = std::atoi(n.c_str() + 8); if (i >= 0 && i < 16) return i; }
  if (n.rfind("gaux", 0) == 0) { int i = std::atoi(n.c_str() + 4); if (i >= 1 && i <= 4) return 3 + i; }
  return -1;
}

// Every buffer in the last live DRAWBUFFERS / RENDERTARGETS. final draws the screen (empty).
// DRAWBUFFERS:16 is colortex1 then colortex6. RENDERTARGETS: 1,2 is the same idea with commas.
// No directive means colortex0, which is what Iris uses.
std::vector<int> drawBuffers(const std::string& fsh, bool isFinal) {
  if (isFinal) return {};
  std::vector<int> bufs{0};
  const std::string& s = fsh;
  // RENDERTARGETS is 13 letters. A 12-character compare never matches, so a pack that
  // uses it (Rethinking Voxels composite6) fell through to colortex0.
  for (size_t i = 0; i < s.size(); ++i) {
    const bool targets = i + 13 <= s.size() && s.compare(i, 13, "RENDERTARGETS") == 0;
    const bool buffers = i + 11 <= s.size() && s.compare(i, 11, "DRAWBUFFERS") == 0;
    if (!targets && !buffers) continue;
    if (i > 0) {
      const unsigned char prev = (unsigned char)s[i - 1];
      if (std::isalnum(prev) || prev == '_') continue;
    }
    size_t c = s.find(':', i);
    if (c == std::string::npos || c > i + 24) continue;
    size_t k = c + 1;
    while (k < s.size() && std::isspace((unsigned char)s[k])) k++;
    std::vector<int> one;
    if (targets) {
      while (k < s.size() && one.size() < 8) {
        while (k < s.size() && (std::isspace((unsigned char)s[k]) || s[k] == ',')) k++;
        if (k >= s.size() || !std::isdigit((unsigned char)s[k])) break;
        int v = std::atoi(s.c_str() + k);
        while (k < s.size() && std::isdigit((unsigned char)s[k])) k++;
        if (v >= 0 && v < 16) one.push_back(v);
      }
    } else {
      while (k < s.size() && one.size() < 8) {
        unsigned char ch = (unsigned char)s[k];
        int v = -1;
        if (ch >= '0' && ch <= '9') v = ch - '0';
        else if (ch >= 'a' && ch <= 'f') v = 10 + ch - 'a';
        else if (ch >= 'A' && ch <= 'F') v = 10 + ch - 'A';
        else break;
        one.push_back(v);
        k++;
      }
    }
    if (!one.empty()) bufs = std::move(one);
  }
  return bufs;
}

// Iris's built-in macros (StandardMacros). The pack's own #define later on the same name wins.
std::string irisHeader() {
  auto lower = [](const char* s) {
    std::string o = s ? s : "";
    for (char& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
  };
  const std::string vendor = lower((const char*)glGetString(GL_VENDOR));
  const std::string rend = lower((const char*)glGetString(GL_RENDERER));
  auto starts = [](const std::string& s, const char* p) { return s.rfind(p, 0) == 0; };
  std::string vdef = "MC_GL_VENDOR_OTHER";
  if (starts(vendor, "intel")) vdef = "MC_GL_VENDOR_INTEL";
  else if (starts(vendor, "nvidia")) vdef = "MC_GL_VENDOR_NVIDIA";
  else if (starts(vendor, "amd") || starts(vendor, "ati")) vdef = "MC_GL_VENDOR_AMD";
  std::string rdef = "MC_GL_RENDERER_OTHER";
  if (starts(rend, "amd") || starts(rend, "ati") || starts(rend, "radeon")) rdef = "MC_GL_RENDERER_RADEON";
  else if (starts(rend, "gallium")) rdef = "MC_GL_RENDERER_GALLIUM";
  else if (starts(rend, "intel")) rdef = "MC_GL_RENDERER_INTEL";
  else if (starts(rend, "geforce") || starts(rend, "nvidia")) rdef = "MC_GL_RENDERER_GEFORCE";
  else if (starts(rend, "mesa")) rdef = "MC_GL_RENDERER_MESA";
  std::string o;
  o += "#define MC_VERSION 12101\n";
  o += "#define MC_HAND_DEPTH 0.125\n";
  o += "#define IS_IRIS\n";
  o += "#define IRIS_HAS_TRANSLUCENCY_SORTING\n";
  o += "#define MC_OS_LINUX\n";
  o += "#define " + vdef + "\n";
  o += "#define " + rdef + "\n";
  o += "#define MC_GL_VERSION 460\n";
  o += "#define MC_GLSL_VERSION 460\n";
  o += "#define MC_NORMAL_MAP\n";
  o += "#define MC_SPECULAR_MAP\n";
  o += "#define MC_RENDER_QUALITY 1.0\n";
  o += "#define MC_SHADOW_QUALITY 1.0\n";
  o += "#define MC_MIPMAP_LEVEL 4\n";
  // OptiFine / Iris render-stage ordinals. Packs compare `renderStage` to these and do not define them.
  o += "#define MC_RENDER_STAGE_NONE 0\n";
  o += "#define MC_RENDER_STAGE_SKY 1\n";
  o += "#define MC_RENDER_STAGE_SUNSET 2\n";
  o += "#define MC_RENDER_STAGE_CUSTOM_SKY 3\n";
  o += "#define MC_RENDER_STAGE_SUN 4\n";
  o += "#define MC_RENDER_STAGE_MOON 5\n";
  o += "#define MC_RENDER_STAGE_STARS 6\n";
  o += "#define MC_RENDER_STAGE_VOID 7\n";
  o += "#define MC_RENDER_STAGE_TERRAIN_SOLID 8\n";
  o += "#define MC_RENDER_STAGE_TERRAIN_CUTOUT_MIPPED 9\n";
  o += "#define MC_RENDER_STAGE_TERRAIN_CUTOUT 10\n";
  o += "#define MC_RENDER_STAGE_ENTITIES 11\n";
  o += "#define MC_RENDER_STAGE_BLOCK_ENTITIES 12\n";
  o += "#define MC_RENDER_STAGE_DESTROY 13\n";
  o += "#define MC_RENDER_STAGE_OUTLINE 14\n";
  o += "#define MC_RENDER_STAGE_DEBUG 15\n";
  o += "#define MC_RENDER_STAGE_HAND_SOLID 16\n";
  o += "#define MC_RENDER_STAGE_TERRAIN_TRANSLUCENT 17\n";
  o += "#define MC_RENDER_STAGE_TRIPWIRE 18\n";
  o += "#define MC_RENDER_STAGE_PARTICLES 19\n";
  o += "#define MC_RENDER_STAGE_CLOUDS 20\n";
  o += "#define MC_RENDER_STAGE_RAIN_SNOW 21\n";
  o += "#define MC_RENDER_STAGE_WORLD_BORDER 22\n";
  o += "#define MC_RENDER_STAGE_HAND_TRANSLUCENT 23\n";
  return o;
}

// The last `const float name = N` left after inactive #if branches are stripped.
float lastConstFloat(const std::string& src, const char* name, float fallback) {
  const std::regex re(std::string("const\\s+float\\s+") + name + "\\s*=\\s*([-+0-9.eE]+)");
  float v = fallback;
  for (auto it = std::sregex_iterator(src.begin(), src.end(), re); it != std::sregex_iterator(); ++it)
    v = std::strtof((*it)[1].str().c_str(), nullptr);
  return std::isfinite(v) ? v : fallback;
}
int lastConstInt(const std::string& src, const char* name, int fallback) {
  const std::regex re(std::string("const\\s+int\\s+") + name + "\\s*=\\s*([-+0-9]+)");
  int v = fallback;
  for (auto it = std::sregex_iterator(src.begin(), src.end(), re); it != std::sregex_iterator(); ++it)
    v = std::atoi((*it)[1].str().c_str());
  return v;
}

// program.world0/composite5.enabled = BLOOM, or = false. Missing means the pass runs.
bool passEnabled(const std::string& props, const std::string& prefix, const std::string& name, const std::unordered_map<std::string, int>& defined) {
  // shaders.properties uses the same #if as the shaders. Complementary disables the shadow
  // program only when SHADOW_QUALITY is -1; reading that line raw turned shadows off for every profile.
  struct Frame { bool parent; bool taken; };
  std::vector<Frame> stack;
  bool active = true;
  std::string found;
  auto word = [](const std::string& d, size_t& p) {
    while (p < d.size() && std::isspace((unsigned char)d[p])) p++;
    size_t b = p;
    while (p < d.size() && (std::isalnum((unsigned char)d[p]) || d[p] == '_')) p++;
    return d.substr(b, p - b);
  };
  const std::string keyDim = "program." + prefix + name + ".enabled";
  const std::string keyBase = "program." + name + ".enabled";
  size_t from = 0;
  while (from < props.size()) {
    size_t eol = props.find('\n', from);
    if (eol == std::string::npos) eol = props.size();
    std::string line = props.substr(from, eol - from);
    from = eol < props.size() ? eol + 1 : eol;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t h = 0;
    while (h < line.size() && std::isspace((unsigned char)line[h])) h++;
    if (h < line.size() && line[h] == '#') {
      std::string d = line.substr(h + 1);
      size_t slash = d.find("//");
      if (slash != std::string::npos) d.resize(slash);
      size_t p = 0;
      std::string kw = word(d, p);
      while (p < d.size() && std::isspace((unsigned char)d[p])) p++;
      auto evalCond = [&](const std::string& kw2) {
        if (kw2 == "if" || kw2 == "elif") return ifEval(defined, d, p) != 0;
        std::string nm = word(d, p);
        return (defined.count(nm) != 0) == (kw2 == "ifdef");
      };
      if (kw == "ifdef" || kw == "ifndef" || kw == "if") {
        bool cond = evalCond(kw);
        stack.push_back({active, false});
        active = active && cond;
        if (active) stack.back().taken = true;
      } else if (kw == "elif" && !stack.empty()) {
        bool cond = evalCond(kw);
        active = stack.back().parent && !stack.back().taken && cond;
        if (active) stack.back().taken = true;
      } else if (kw == "else" && !stack.empty()) {
        active = stack.back().parent && !stack.back().taken;
        if (active) stack.back().taken = true;
      } else if (kw == "endif" && !stack.empty()) {
        active = stack.back().parent;
        stack.pop_back();
      }
      continue;
    }
    if (!active || h >= line.size()) continue;
    size_t cut = line.find("//");
    if (cut != std::string::npos) line.resize(cut);
    const std::string* key = nullptr;
    if (line.compare(h, keyDim.size(), keyDim) == 0) key = &keyDim;
    else if (line.compare(h, keyBase.size(), keyBase) == 0) key = &keyBase;
    if (!key) continue;
    size_t eq = h + key->size();
    while (eq < line.size() && std::isspace((unsigned char)line[eq])) eq++;
    if (eq >= line.size() || line[eq] != '=') continue;
    std::string v = line.substr(eq + 1);
    size_t a = 0; while (a < v.size() && std::isspace((unsigned char)v[a])) a++;
    size_t b = v.size(); while (b > a && std::isspace((unsigned char)v[b - 1])) b--;
    found = v.substr(a, b - a);
  }
  if (found.empty()) return true;
  if (found == "true") return true;
  if (found == "false") return false;
  return ifEval(defined, found, 0) != 0;
}

// Live assignments in shaders.properties. #if follows the same rules as passEnabled.
template <typename Fn>
void forActiveLines(const std::string& props, const std::unordered_map<std::string, int>& defined, Fn fn) {
  struct Frame { bool parent; bool taken; };
  std::vector<Frame> stack;
  bool active = true;
  auto word = [](const std::string& d, size_t& p) {
    while (p < d.size() && std::isspace((unsigned char)d[p])) p++;
    size_t b = p;
    while (p < d.size() && (std::isalnum((unsigned char)d[p]) || d[p] == '_')) p++;
    return d.substr(b, p - b);
  };
  size_t from = 0;
  while (from < props.size()) {
    size_t eol = props.find('\n', from);
    if (eol == std::string::npos) eol = props.size();
    std::string line = props.substr(from, eol - from);
    from = eol < props.size() ? eol + 1 : eol;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t h = 0;
    while (h < line.size() && std::isspace((unsigned char)line[h])) h++;
    if (h < line.size() && line[h] == '#') {
      std::string d = line.substr(h + 1);
      size_t slash = d.find("//");
      if (slash != std::string::npos) d.resize(slash);
      size_t p = 0;
      std::string kw = word(d, p);
      while (p < d.size() && std::isspace((unsigned char)d[p])) p++;
      auto evalCond = [&](const std::string& kw2) {
        if (kw2 == "if" || kw2 == "elif") return ifEval(defined, d, p) != 0;
        std::string nm = word(d, p);
        return (defined.count(nm) != 0) == (kw2 == "ifdef");
      };
      if (kw == "ifdef" || kw == "ifndef" || kw == "if") {
        bool cond = evalCond(kw);
        stack.push_back({active, false});
        active = active && cond;
        if (active) stack.back().taken = true;
      } else if (kw == "elif" && !stack.empty()) {
        bool cond = evalCond(kw);
        active = stack.back().parent && !stack.back().taken && cond;
        if (active) stack.back().taken = true;
      } else if (kw == "else" && !stack.empty()) {
        active = stack.back().parent && !stack.back().taken;
        if (active) stack.back().taken = true;
      } else if (kw == "endif" && !stack.empty()) {
        active = stack.back().parent;
        stack.pop_back();
      }
      continue;
    }
    if (!active || h >= line.size()) continue;
    size_t cut = line.find("//");
    if (cut != std::string::npos) line.resize(cut);
    while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
    if ((int)line.size() > (int)h) fn(line.substr(h));
  }
}

bool blendFactorName(const std::string& s, GLenum& o) {
  if (s == "ZERO") o = GL_ZERO;
  else if (s == "ONE") o = GL_ONE;
  else if (s == "SRC_COLOR") o = GL_SRC_COLOR;
  else if (s == "ONE_MINUS_SRC_COLOR") o = GL_ONE_MINUS_SRC_COLOR;
  else if (s == "DST_COLOR") o = GL_DST_COLOR;
  else if (s == "ONE_MINUS_DST_COLOR") o = GL_ONE_MINUS_DST_COLOR;
  else if (s == "SRC_ALPHA") o = GL_SRC_ALPHA;
  else if (s == "ONE_MINUS_SRC_ALPHA") o = GL_ONE_MINUS_SRC_ALPHA;
  else if (s == "DST_ALPHA") o = GL_DST_ALPHA;
  else if (s == "ONE_MINUS_DST_ALPHA") o = GL_ONE_MINUS_DST_ALPHA;
  else if (s == "SRC_ALPHA_SATURATE") o = GL_SRC_ALPHA_SATURATE;
  else return false;
  return true;
}

// blend.<program> and blend.<program>.colortexN. A missing program line leaves `on` as the caller set it
// (on for gbuffers_water, off otherwise). Per-buffer `off` is a bit in blendOff.
void readPackBlend(const std::string& props, const std::string& prefix, const std::string& name,
                   const std::unordered_map<std::string, int>& defined, bool& on, GLenum& src, GLenum& dst,
                   GLenum& srcA, GLenum& dstA, uint32_t& off) {
  const std::string dimKey = prefix.empty() ? std::string() : "blend." + prefix + name;
  const std::string baseKey = "blend." + name;
  forActiveLines(props, defined, [&](const std::string& line) {
    const std::string* key = nullptr;
    if (!dimKey.empty() && line.compare(0, dimKey.size(), dimKey) == 0) key = &dimKey;
    else if (line.compare(0, baseKey.size(), baseKey) == 0) key = &baseKey;
    if (!key) return;
    size_t n = key->size();
    if (n < line.size() && (std::isalnum((unsigned char)line[n]) || line[n] == '_')) return;
    int buf = -1;
    if (n < line.size() && line[n] == '.') {
      std::string which = line.substr(n + 1);
      size_t eq = which.find('=');
      if (eq == std::string::npos) return;
      std::string bname = which.substr(0, eq);
      while (!bname.empty() && std::isspace((unsigned char)bname.back())) bname.pop_back();
      buf = colortexOf(bname);
      which = which.substr(eq + 1);
      size_t a = 0; while (a < which.size() && std::isspace((unsigned char)which[a])) a++;
      if (buf >= 0 && buf < 16) {
        if (which.compare(a, 3, "off") == 0) off |= 1u << buf;
        else off &= ~(1u << buf);
      }
      return;
    }
    while (n < line.size() && std::isspace((unsigned char)line[n])) n++;
    if (n >= line.size() || line[n] != '=') return;
    std::string v = line.substr(n + 1);
    size_t a = 0; while (a < v.size() && std::isspace((unsigned char)v[a])) a++;
    v = v.substr(a);
    if (v == "off") { on = false; return; }
    std::vector<GLenum> f;
    size_t i = 0;
    while (i < v.size() && f.size() < 4) {
      while (i < v.size() && std::isspace((unsigned char)v[i])) i++;
      size_t b = i;
      while (i < v.size() && !std::isspace((unsigned char)v[i])) i++;
      if (b == i) break;
      GLenum e = 0;
      if (!blendFactorName(v.substr(b, i - b), e)) return;
      f.push_back(e);
    }
    if (f.size() == 2) { src = srcA = f[0]; dst = dstA = f[1]; on = true; }
    else if (f.size() == 4) { src = f[0]; dst = f[1]; srcA = f[2]; dstA = f[3]; on = true; }
  });
}

// alphaTest.<program>=GREATER 0.0001. Empty means the pass does not discard.
std::string readAlphaKeep(const std::string& props, const std::string& prefix, const std::string& name,
                          const std::unordered_map<std::string, int>& defined) {
  const std::string dimKey = prefix.empty() ? std::string() : "alphaTest." + prefix + name;
  const std::string baseKey = "alphaTest." + name;
  std::string expr;
  forActiveLines(props, defined, [&](const std::string& line) {
    const std::string* key = nullptr;
    if (!dimKey.empty() && line.compare(0, dimKey.size(), dimKey) == 0) key = &dimKey;
    else if (line.compare(0, baseKey.size(), baseKey) == 0) key = &baseKey;
    if (!key) return;
    size_t n = key->size();
    if (n < line.size() && line[n] != '=' && !std::isspace((unsigned char)line[n])) return;
    while (n < line.size() && std::isspace((unsigned char)line[n])) n++;
    if (n >= line.size() || line[n] != '=') return;
    std::string v = line.substr(n + 1);
    size_t a = 0; while (a < v.size() && std::isspace((unsigned char)v[a])) a++;
    size_t sp = a;
    while (sp < v.size() && !std::isspace((unsigned char)v[sp])) sp++;
    std::string func = v.substr(a, sp - a);
    while (sp < v.size() && std::isspace((unsigned char)v[sp])) sp++;
    std::string ref = v.substr(sp);
    size_t end = 0;
    while (end < ref.size() && (std::isdigit((unsigned char)ref[end]) || ref[end] == '.' || ref[end] == '-' || ref[end] == '+')) end++;
    ref.resize(end);
    const char* op = nullptr;
    if (func == "ALWAYS") { expr.clear(); return; }
    if (func == "NEVER") { expr = "false"; return; }
    if (func == "LESS") op = "<";
    else if (func == "LEQUAL") op = "<=";
    else if (func == "EQUAL") op = "==";
    else if (func == "GEQUAL") op = ">=";
    else if (func == "GREATER") op = ">";
    else if (func == "NOTEQUAL") op = "!=";
    if (!op || ref.empty()) return;
    expr = std::string("dqData[0].a ") + op + " " + ref;
  });
  return expr;
}

// Horizon's dimension folder: world0 overworld, world-1 nether, world1 end. The Skylands have no
// folder of their own, so they use the overworld set. Empty when the pack has only the base files.
std::string dimensionPrefix(const IrisFiles& F, int dim) {
  const char* name = "world0";
  if (dim == DIM_NETHER) name = "world-1";
  else if (dim == DIM_END) name = "world1";
  const std::string prefix = std::string(name) + "/";
  if (F.read(prefix + "final.fsh") || F.read(prefix + "composite.fsh") || F.read(prefix + "composite1.fsh")) return prefix;
  return "";
}

// OptiFine binds noisetex from shaders.properties. MakeUp names it texture.gbuffers.noisetex.
std::string packNoiseRel(const IrisFiles& F) {
  std::string rel = "lib/textures/noise.png";
  auto props = F.read("shaders.properties");
  if (!props) return rel;
  const char* keys[] = {"texture.noise", "texture.gbuffers.noisetex", "texture.composite.noisetex", "texture.final.noisetex"};
  for (const char* key : keys) {
    const size_t klen = std::strlen(key);
    size_t k = 0;
    bool found = false;
    while ((k = props->find(key, k)) != std::string::npos) {
      size_t line = props->rfind('\n', k);
      line = line == std::string::npos ? 0 : line + 1;
      size_t h = line;
      while (h < k && std::isspace((unsigned char)(*props)[h])) h++;
      size_t after = k + klen;
      if (h == k && (after >= props->size() || (!std::isalnum((unsigned char)(*props)[after]) && (*props)[after] != '_'))) {
        found = true; break;
      }
      k += klen;
    }
    if (!found) continue;
    size_t eq = k + klen;
    while (eq < props->size() && std::isspace((unsigned char)(*props)[eq])) eq++;
    if (eq >= props->size() || (*props)[eq] != '=') continue;
    size_t nl = props->find('\n', eq);
    std::string v = props->substr(eq + 1, (nl == std::string::npos ? props->size() : nl) - (eq + 1));
    size_t a = 0; while (a < v.size() && std::isspace((unsigned char)v[a])) a++;
    size_t b = v.size(); while (b > a && std::isspace((unsigned char)v[b - 1])) b--;
    v = v.substr(a, b - a);
    size_t cut = v.find("//"); if (cut != std::string::npos) v.resize(cut);
    b = v.size(); while (b > 0 && std::isspace((unsigned char)v[b - 1])) b--;
    v.resize(b);
    if (!v.empty() && v[0] != '#') return v;
  }
  return rel;
}

// const bool colortexNClear / colortexNMipmapEnabled, and the gaux1..4 names for the same buffers.
// A commented-out line does not count. colortex0ClearColor must not match colortex0Clear.
void noteTexFlags(const std::string& src, bool clear[16], bool mip[16]) {
  std::string o;
  o.reserve(src.size());
  for (size_t i = 0; i < src.size();) {
    if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
      size_t e = src.find("*/", i + 2);
      i = e == std::string::npos ? src.size() : e + 2;
      o.push_back('\n');
      continue;
    }
    if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
      size_t e = src.find('\n', i);
      i = e == std::string::npos ? src.size() : e;
      continue;
    }
    o.push_back(src[i++]);
  }
  auto apply = [&](const std::string& line, const std::string& key, int idx, bool isMip) {
    size_t k = 0;
    while ((k = line.find(key, k)) != std::string::npos) {
      size_t end = k + key.size();
      if (end < line.size() && (std::isalnum((unsigned char)line[end]) || line[end] == '_')) { k = end; continue; }
      size_t eq = line.find('=', end);
      if (eq == std::string::npos || eq > end + 6) return;
      const bool yes = line.find("true", eq) != std::string::npos && (line.find("false", eq) == std::string::npos || line.find("true", eq) < line.find("false", eq));
      if (isMip) mip[idx] = yes;
      else clear[idx] = yes;
      return;
    }
  };
  size_t i = 0;
  while (i < o.size()) {
    size_t eol = o.find('\n', i);
    if (eol == std::string::npos) eol = o.size();
    std::string line = o.substr(i, eol - i);
    i = eol < o.size() ? eol + 1 : eol;
    for (int n = 0; n < 16; n++) {
      apply(line, "colortex" + std::to_string(n) + "Clear", n, false);
      apply(line, "colortex" + std::to_string(n) + "MipmapEnabled", n, true);
    }
    for (int n = 1; n <= 4; n++) {
      apply(line, "gaux" + std::to_string(n) + "Clear", 3 + n, false);
      apply(line, "gaux" + std::to_string(n) + "MipmapEnabled", 3 + n, true);
    }
  }
}

// shaders.properties: uniform.float.name = expression. Enough of Iris's expression language for the
// mixes a post pass actually samples (dayNightMix, exposure helpers). Unknown names leave it unset.
struct IrisU { std::string name, expr; bool integer = false; bool upload = true; };

std::vector<IrisU> parseIrisUniforms(const std::string& props) {
  std::vector<IrisU> out;
  size_t from = 0;
  while (from < props.size()) {
    size_t eol = props.find('\n', from);
    if (eol == std::string::npos) eol = props.size();
    std::string line = props.substr(from, eol - from);
    from = eol < props.size() ? eol + 1 : eol;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t h = 0;
    while (h < line.size() && std::isspace((unsigned char)line[h])) h++;
    if (h >= line.size() || line[h] == '#') continue;
    size_t cut = line.find("//");
    if (cut != std::string::npos) line.resize(cut);
    const char* kinds[] = {"uniform.float.", "uniform.int.", "variable.float.", "variable.int."};
    int kind = -1;
    for (int i = 0; i < 4; i++) if (line.compare(h, std::strlen(kinds[i]), kinds[i]) == 0) { kind = i; break; }
    if (kind < 0) continue;
    size_t ns = h + std::strlen(kinds[kind]);
    size_t ne = ns;
    while (ne < line.size() && (std::isalnum((unsigned char)line[ne]) || line[ne] == '_')) ne++;
    size_t eq = ne;
    while (eq < line.size() && std::isspace((unsigned char)line[eq])) eq++;
    if (eq >= line.size() || line[eq] != '=') continue;
    std::string expr = line.substr(eq + 1);
    size_t a = 0; while (a < expr.size() && std::isspace((unsigned char)expr[a])) a++;
    size_t b = expr.size(); while (b > a && std::isspace((unsigned char)expr[b - 1])) b--;
    expr = expr.substr(a, b - a);
    if (expr.empty()) continue;
    IrisU u;
    u.name = line.substr(ns, ne - ns);
    u.expr = expr;
    u.integer = kind == 1 || kind == 3;
    u.upload = kind < 2;
    if (!u.name.empty()) out.push_back(std::move(u));
  }
  return out;
}

std::optional<double> evalIrisExpr(const std::string& expr, const std::unordered_map<std::string, double>& env) {
  struct P {
    const std::unordered_map<std::string, double>& env;
    const std::string& s;
    size_t p = 0;
    bool bad = false;
    void skip() { while (p < s.size() && std::isspace((unsigned char)s[p])) p++; }
    std::string word() {
      skip();
      size_t b = p;
      while (p < s.size() && (std::isalnum((unsigned char)s[p]) || s[p] == '_')) p++;
      return s.substr(b, p - b);
    }
    double orE() {
      double v = andE();
      skip();
      while (!bad && p + 1 < s.size() && s[p] == '|' && s[p + 1] == '|') { p += 2; double r = andE(); v = (v != 0.0 || r != 0.0) ? 1.0 : 0.0; skip(); }
      return v;
    }
    double andE() {
      double v = cmp();
      skip();
      while (!bad && p + 1 < s.size() && s[p] == '&' && s[p + 1] == '&') { p += 2; double r = cmp(); v = (v != 0.0 && r != 0.0) ? 1.0 : 0.0; skip(); }
      return v;
    }
    double cmp() {
      double v = add();
      skip();
      auto op2 = [&](char a, char b) { return p + 1 < s.size() && s[p] == a && s[p + 1] == b; };
      if (op2('<', '=')) { p += 2; return v <= add() ? 1.0 : 0.0; }
      if (op2('>', '=')) { p += 2; return v >= add() ? 1.0 : 0.0; }
      if (op2('=', '=')) { p += 2; return v == add() ? 1.0 : 0.0; }
      if (op2('!', '=')) { p += 2; return v != add() ? 1.0 : 0.0; }
      if (p < s.size() && s[p] == '<') { p++; return v < add() ? 1.0 : 0.0; }
      if (p < s.size() && s[p] == '>') { p++; return v > add() ? 1.0 : 0.0; }
      return v;
    }
    double add() {
      double v = mul();
      while (!bad) {
        skip();
        if (p < s.size() && (s[p] == '+' || s[p] == '-')) { char op = s[p++]; double r = mul(); v = op == '+' ? v + r : v - r; }
        else break;
      }
      return v;
    }
    double mul() {
      double v = unary();
      while (!bad) {
        skip();
        if (p < s.size() && (s[p] == '*' || s[p] == '/' || s[p] == '%')) {
          char op = s[p++]; double r = unary();
          if (op == '*') v *= r;
          else if (r == 0.0) v = 0.0;
          else v = op == '/' ? v / r : std::fmod(v, r);
        } else break;
      }
      return v;
    }
    double unary() {
      skip();
      if (p < s.size() && s[p] == '!') { p++; return unary() == 0.0 ? 1.0 : 0.0; }
      if (p < s.size() && s[p] == '-') { p++; return -unary(); }
      if (p < s.size() && s[p] == '+') { p++; return unary(); }
      return primary();
    }
    double call(const std::string& name) {
      p++;
      std::vector<double> a;
      skip();
      if (p < s.size() && s[p] == ')') p++;
      else while (!bad) {
        a.push_back(orE());
        skip();
        if (p < s.size() && s[p] == ',') { p++; continue; }
        if (p < s.size() && s[p] == ')') { p++; break; }
        bad = true; break;
      }
      auto n = [&](int i) { return i < (int)a.size() ? a[i] : 0.0; };
      if (name == "min") return std::min(n(0), n(1));
      if (name == "max") return std::max(n(0), n(1));
      if (name == "clamp") return std::min(std::max(n(0), n(1)), n(2));
      if (name == "abs") return std::fabs(n(0));
      if (name == "floor") return std::floor(n(0));
      if (name == "ceil") return std::ceil(n(0));
      if (name == "sqrt") return n(0) > 0 ? std::sqrt(n(0)) : 0.0;
      if (name == "exp") return std::exp(n(0));
      if (name == "log" || name == "ln") return n(0) > 0 ? std::log(n(0)) : 0.0;
      if (name == "pow") {
        if (n(0) == 0.0 && n(1) == 0.0) return 1.0;
        return std::pow(n(0), n(1));
      }
      if (name == "sin") return std::sin(n(0));
      if (name == "cos") return std::cos(n(0));
      if (name == "tan") return std::tan(n(0));
      if (name == "atan") return a.size() >= 2 ? std::atan2(n(0), n(1)) : std::atan(n(0));
      if (name == "fmod" || name == "mod") return n(1) != 0.0 ? std::fmod(n(0), n(1)) : 0.0;
      if (name == "sign") return n(0) < 0 ? -1.0 : n(0) > 0 ? 1.0 : 0.0;
      if (name == "round") return std::round(n(0));
      if (name == "frac" || name == "fract") return n(0) - std::floor(n(0));
      if (name == "if") return n(0) != 0.0 ? n(1) : n(2);
      if (name == "step") return n(1) >= n(0) ? 1.0 : 0.0;
      if (name == "mix") return n(0) * (1.0 - n(2)) + n(1) * n(2);
      // Iris: smooth([id], value, [fadeUp, [fadeDown]]). Four arguments means the first is an id.
      if (name == "smooth") return a.size() >= 4 ? n(1) : n(0);
      if (name == "int" || name == "float") return n(0);
      bad = true;
      return 0.0;
    }
    double primary() {
      skip();
      if (p >= s.size()) { bad = true; return 0; }
      if (s[p] == '(') { p++; double v = orE(); skip(); if (p < s.size() && s[p] == ')') p++; else bad = true; return v; }
      if (std::isdigit((unsigned char)s[p]) || s[p] == '.') {
        char* end = nullptr;
        double v = std::strtod(s.c_str() + p, &end);
        if (!end || end == s.c_str() + p) { bad = true; return 0; }
        p = (size_t)(end - s.c_str());
        return v;
      }
      std::string name = word();
      if (name.empty()) { bad = true; return 0; }
      skip();
      if (p < s.size() && s[p] == '(') return call(name);
      if (p < s.size() && s[p] == '.') {
        // eyeBrightness.x/y only. gbufferProjection.1.1 and other matrix swizzles stay rejected.
        if (name != "eyeBrightness" && name != "eyeBrightnessSmooth") { bad = true; return 0; }
        p++;
        const std::string comp = word();
        const bool sky = comp == "y" || comp == "g";
        if (!sky && comp != "x" && comp != "r") { bad = true; return 0; }
        auto it = env.find(name + (sky ? ".y" : ".x"));
        if (it == env.end()) { bad = true; return 0; }
        return it->second;
      }
      if (name == "true") return 1;
      if (name == "false") return 0;
      auto it = env.find(name);
      if (it == env.end()) { bad = true; return 0; }
      return it->second;
    }
  };
  P pr{env, expr, 0, false};
  double v = pr.orE();
  pr.skip();
  if (pr.bad || pr.p != expr.size() || !std::isfinite(v)) return std::nullopt;
  return v;
}

bool imageUniform(GLenum t) {
  switch (t) {
    case GL_IMAGE_2D: case GL_INT_IMAGE_2D: case GL_UNSIGNED_INT_IMAGE_2D:
    case GL_IMAGE_3D: case GL_INT_IMAGE_3D: case GL_UNSIGNED_INT_IMAGE_3D:
    case GL_IMAGE_2D_ARRAY: case GL_INT_IMAGE_2D_ARRAY: case GL_UNSIGNED_INT_IMAGE_2D_ARRAY:
      return true;
    default: return false;
  }
}

// block.properties id for each atlas tile, plus the optional blockIdMap ints (SSBO offset 192).
// Unmapped tiles stay 1 so a pack that divides by mc_Entity.x does not produce NaN.
struct PackIds {
  std::vector<float> lut;
  std::vector<int> blockMap;
  int tiles = 0, grass = -1, leaves = -1, water = -1, stone = -1;
};

PackIds buildPackIds(const IrisFiles& F) {
  PackIds ids;
  ids.lut.assign(4096, 1.f);
  struct Hit { int id, score, order; };
  std::unordered_map<std::string, std::vector<Hit>> byName;
  std::set<std::string> seen;
  std::unordered_map<std::string, int> defined;
  const std::string text = F.expand(irisHeader() + F.read("block.properties").value_or(""), "", seen, defined);
  int order = 0;
  size_t i = 0;
  auto scoreOf = [](const std::string& props) {
    if (props.empty()) return 1000;
    int s = 0;
    auto has = [&](const char* k) { return props.find(k) != std::string::npos; };
    if (has("waterlogged=false")) s += 40;
    if (has("waterlogged=true")) s -= 10;
    if (has("snowy=false")) s += 40;
    if (has("snowy=true")) s -= 10;
    if (has("variant=oak")) s += 15;
    if (has("half=lower")) s += 8;
    if (has("open=false")) s += 4;
    if (has("powered=false")) s += 4;
    return s;
  };
  while (i < text.size()) {
    size_t eol = text.find('\n', i);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(i, eol - i);
    i = eol < text.size() ? eol + 1 : eol;
    size_t b = 0;
    while (b < line.size() && std::isspace((unsigned char)line[b])) b++;
    if (line.compare(b, 6, "block.") != 0) continue;
    char* end = nullptr;
    int id = (int)std::strtol(line.c_str() + b + 6, &end, 10);
    if (!end || end == line.c_str() + b + 6 || id <= 0) continue;
    const char* eq = std::strchr(end, '=');
    if (!eq) continue;
    std::string rest = eq + 1;
    size_t p = 0;
    while (p < rest.size()) {
      while (p < rest.size() && std::isspace((unsigned char)rest[p])) p++;
      size_t e = p;
      while (e < rest.size() && !std::isspace((unsigned char)rest[e])) e++;
      if (e == p) break;
      std::string tok = rest.substr(p, e - p);
      p = e;
      if (tok.rfind("minecraft:", 0) == 0) tok = tok.substr(10);
      size_t c = tok.find(':');
      std::string name = c == std::string::npos ? tok : tok.substr(0, c);
      std::string props = c == std::string::npos ? std::string() : tok.substr(c + 1);
      if (name.empty()) continue;
      byName[name].push_back({id, scoreOf(props), order++});
    }
  }
  auto pick = [&](const std::string& name) -> int {
    auto it = byName.find(name);
    if (it == byName.end()) return 0;
    const Hit* best = nullptr;
    for (auto& h : it->second) if (!best || h.score > best->score || (h.score == best->score && h.order > best->order)) best = &h;
    return best ? best->id : 0;
  };
  // Names this game still uses from before 1.13. `grass` in a pack is the plant, not the block.
  auto packKey = [&](const std::string& key) {
    static const std::pair<const char*, const char*> alias[] = {
      {"grass", "grass_block"}, {"leaves", "oak_leaves"}, {"log", "oak_log"}, {"planks", "oak_planks"},
      {"sapling", "oak_sapling"}, {"tallgrass", "short_grass"}, {"yellow_flower", "dandelion"},
      {"red_flower", "poppy"}, {"waterlily", "lily_pad"}, {"reeds", "sugar_cane"},
      {"deadbush", "dead_bush"}, {"web", "cobweb"}, {"stonebrick", "stone_bricks"},
    };
    for (auto& a : alias) if (key == a.first && pick(a.second)) return std::string(a.second);
    return key;
  };
  struct Owner { std::string key; int id; int faces; };
  std::unordered_map<int, std::vector<Owner>> owners;
  for (const BlockDef& b : reg().blocks) {
    if (!b.id || b.key.empty()) continue;
    const std::string alias = packKey(b.key);
    int id = pick(alias);
    if (!id) id = pick(b.key);
    if (!id) continue;
    int seenFace[6] = {};
    int n = 0;
    for (int f = 0; f < 6; f++) {
      int t = b.faces[f];
      if (t <= 0 || t >= 4096 || seenFace[f]) continue;
      bool dup = false;
      for (int k = 0; k < f; k++) if (b.faces[k] == t) dup = true;
      if (dup) continue;
      seenFace[f] = 1;
      bool found = false;
      for (auto& o : owners[t]) if (o.key == b.key) { o.faces++; found = true; break; }
      if (!found) owners[t].push_back({b.key, id, 1});
      n++;
    }
    (void)n;
    if (b.faces[0] == 0 && !b.tex.empty()) {
      int t = reg().tile(b.tex);
      if (t > 0 && t < 4096) {
        bool found = false;
        for (auto& o : owners[t]) if (o.key == b.key) { found = true; break; }
        if (!found) owners[t].push_back({b.key, id, 1});
      }
    }
  }
  std::unordered_map<int, std::string> layerName;
  for (auto& [name, layer] : reg().tileIndex) if (!layerName.count(layer)) layerName[layer] = name;
  for (auto& [t, list] : owners) {
    if (t <= 0 || t >= 4096 || list.empty()) continue;
    const std::string tileName = layerName.count(t) ? layerName[t] : std::string();
    int bestId = 0, bestRank = -1;
    for (auto& o : list) {
      int rank = o.faces;
      if (list.size() == 1) rank += 50;
      auto bump = [&](const std::string& key) {
        if (key.empty() || tileName.empty()) return;
        if (tileName == key) rank += 300 + (int)key.size();
        else if (tileName.rfind(key + "_", 0) == 0) rank += 200 + (int)key.size();
        else if (key.rfind(tileName + "_", 0) == 0) rank += 100 + (int)key.size();
      };
      bump(o.key);
      bump(packKey(o.key));
      if (o.id > 0 && rank > bestRank) { bestRank = rank; bestId = o.id; }
    }
    if (bestId > 0 && (list.size() == 1 || bestRank >= 100)) {
      ids.lut[t] = (float)bestId;
      ids.tiles++;
    }
  }
  auto sample = [&](const char* tile) {
    int layer = reg().tile(tile);
    return layer > 0 && layer < 4096 ? (int)ids.lut[layer] : -1;
  };
  ids.grass = sample("grass_top");
  ids.leaves = sample("leaves");
  ids.water = sample("water");
  ids.stone = sample("stone");
  const int cap = ((1 << 20) - 192) / 4;
  ids.blockMap.assign((size_t)std::min(16384, cap), 0);
  if (auto png = F.read("lib/textures/blockIdMap.png")) {
    int w = 0, h = 0;
    std::vector<u8> rgba;
    if (loadPngMem((const u8*)png->data(), png->size(), w, h, rgba) && w > 0 && h > 0 && rgba.size() >= (size_t)w * h * 4) {
      for (int y = 0; y < h; y++) for (int x = 0; x < w && x < 512; x++) {
        int mat = y * 512 + x;
        if (mat < 0 || mat >= cap) continue;
        if ((int)ids.blockMap.size() <= mat) ids.blockMap.resize((size_t)mat + 1, 0);
        size_t px = ((size_t)y * (size_t)w + (size_t)x) * 4;
        ids.blockMap[mat] = (int)rgba[px] + ((int)rgba[px + 1] << 8);
      }
    }
  }
  return ids;
}

// Iris TextureStage. "gbuffers" also covers the shadow program. "composite" also covers final.
// A raw `texture.stage.sampler = file.dat TEXTURE_3D ...` line is a new sampler, not a buffer override.
int texStageId(const std::string& stage) {
  if (stage == "setup") return 1;
  if (stage == "begin") return 2;
  if (stage == "shadowcomp") return 3;
  if (stage == "prepare") return 4;
  if (stage == "gbuffers") return 5;
  if (stage == "deferred") return 6;
  if (stage == "composite") return 7;
  return 0;
}
int texStageOf(const std::string& program) {
  auto starts = [&](const char* p) { return program.rfind(p, 0) == 0; };
  if (starts("shadowcomp")) return 3;
  if (starts("gbuffers") || program == "shadow") return 5;
  if (starts("deferred")) return 6;
  if (starts("composite") || program == "final") return 7;
  if (starts("prepare")) return 4;
  if (starts("setup")) return 1;
  if (starts("begin")) return 2;
  return 0;
}
GLuint loadPackPng(const IrisFiles& F, const std::string& rel) {
  auto file = F.read(rel);
  if (!file) return 0;
  int w = 0, h = 0;
  std::vector<u8> px;
  if (!loadPngMem(reinterpret_cast<const u8*>(file->data()), file->size(), w, h, px) || w <= 0 || h <= 0) return 0;
  bool nearest = false, repeat = true;
  if (auto meta = F.read(rel + ".mcmeta")) {
    auto flag = [&](const char* name, const char* word) {
      size_t i = meta->find(name); if (i == std::string::npos) return false;
      size_t c = meta->find(':', i); if (c == std::string::npos) return false;
      return meta->substr(c, 24).find(word) != std::string::npos;
    };
    if (flag("\"blur\"", "false")) nearest = true;
    if (flag("\"clamp\"", "true")) repeat = false;
  }
  return makeTexture2D(w, h, px.data(), nearest, false, repeat);
}
} // namespace

class PostLayer : public RenderLayer {
public:
  const char* name() const override { return "post"; }
  int order() const override { return 850; }   // before the hand (900): final is drawn under the arm
  void drawOverlay(GameSession& s, const FrameCtx& f) override {
    // Capture stays on after this returns, so the next frame's creatures are recorded before we run.
    // The hand draws after us and uses another projection, so it is not recorded.
    auto armCapture = [&] { packVoxelSetCapture(gbuf.prog != 0); };
    if (f.portalDepth > 0) { packVoxelClear(); armCapture(); return; }
    const std::string& pack = s.app.settings.shaderPack;
    if (pack != irisFor || f.dim != irisDim) loadIris(pack, f.dim);
    s.worldRenderer.setPackGeometry(gbuf.prog != 0);
    s.worldRenderer.setColourCubes(pack.empty() && gbuf.prog == 0);
    if (iris.empty() && !gbuf.prog) { packVoxelClear(); armCapture(); return; }
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 16 || vp[3] <= 16) { packVoxelClear(); armCapture(); return; }
    GLint drawFbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE);
    runIris(s, f, vp, drawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindVertexArray(0); glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE);
    armCapture();
  }
private:
  // an Iris/OptiFine pack
  std::string irisFor = "\x01";
  int irisDim = -1;
  std::vector<IrisPass> iris, irisBegin, irisPrep, irisDef;
  IrisPass gbuf, gshadow, gwater, gentity, gshadowEnt, ghand;
  bool packHasFinal = false;
  bool toldShow = false;
  bool toldGeom = false;
  bool toldWater = false;
  bool toldShadow = false;
  bool toldEntity = false;
  bool toldEntShadow = false;
  int entityDrew = -1;
  float shDist = 128.f, shPath = 0.f, shInterval = 2.f, shMul = 1.f;
  int shRes = 1024;
  GLuint shTex = 0, shColor = 0, shFbo = 0;
  int shAlloc = 0;
  Target slot[16][2];
  int slotFront[16] = {};
  bool slotClear[16] = {};
  bool slotMip[16] = {};
  std::vector<IrisU> irisU;
  std::unordered_map<std::string, int> packItems;
  int sceneBuf = 0;
  GLuint passFbo = 0, packSsbo = 0, entityLut = 0;
  std::vector<int> blockIdInts;
  bool blockMapDirty = false;
  GLuint depthTex = 0, depthTrans = 0, handDepth = 0, blackTex = 0, whiteTex = 0, noiseTex = 0, quadVbo = 0, quadVao = 0; int dw = 0, dh = 0;
  int handW = 0, handH = 0;
  bool toldHand = false;
  bool toldHeld = false;
  struct StageTex { int stage = 0; std::string name; GLuint tex = 0; GLenum target = GL_TEXTURE_2D; };
  std::vector<StageTex> stageTex;
  std::vector<GLuint> customTexIds;
  GLuint shadowDummy = 0, shadowLit = 0, volDummy = 0, flatNormal = 0;
  double startT = nowSeconds(), lastT = 0; int frames = 0;
  bool havePrev = false;
  bool resetHistory = true;
  float eyeBlockSm = 0, eyeSkySm = 240;
  bool eyeSmInit = false;
  vec3 prevCam{0};
  mat4 prevMV{1}, prevP{1};

  void copyFrame(Target& t, const GLint* vp, GLint fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)fbo);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t.tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
  }

  // One depth map plus shadowcolor0. Resolution is the pack's, clamped to what this GPU can sample.
  // Compare is GL_LEQUAL: an equal depth is lit, so a face is not speckled by its own depth.
  bool ensureShadowMap(int res) {
    res = std::clamp(res, 256, 4096);
    if (shTex && shColor && shFbo && shAlloc == res) return true;
    if (shTex) { glDeleteTextures(1, &shTex); shTex = 0; }
    if (shColor) { glDeleteTextures(1, &shColor); shColor = 0; }
    if (shFbo) { glDeleteFramebuffers(1, &shFbo); shFbo = 0; }
    shAlloc = 0;
    glGenTextures(1, &shTex);
    glBindTexture(GL_TEXTURE_2D, shTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, res, res, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glGenTextures(1, &shColor);
    glBindTexture(GL_TEXTURE_2D, shColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, res, res, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glGenFramebuffers(1, &shFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shTex, 0);
    const GLenum att = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &att);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      glDeleteTextures(1, &shTex); shTex = 0;
      glDeleteTextures(1, &shColor); shColor = 0;
      glDeleteFramebuffers(1, &shFbo); shFbo = 0;
      return false;
    }
    shAlloc = res;
    return true;
  }

  // ---- Iris / OptiFine ----
  // One fullscreen or gbuffer program. geom unpacks terrain; otherwise the pass is a quad.
  // A missing file is quiet. A compile failure is logged by buildProgram and the pass is skipped.
  bool compilePass(const IrisFiles& F, const std::string& pack, const std::string& prefix,
                   const std::string& props, const std::string& n, bool geom, IrisPass& out, bool shadow = false, bool translucent = false, bool entity = false, bool hand = false) {
    try {
      std::string base = (!prefix.empty() && F.read(prefix + n + ".fsh")) ? prefix + n : n;
      auto fsh = F.read(base + ".fsh");
      if (!fsh) return false;
      if (geom && !F.read(base + ".vsh")) return false;
      std::set<std::string> seen;
      std::unordered_map<std::string, int> defined;
      const std::string dir = base.substr(0, base.rfind('/') + 1);
      const std::string expanded = F.expand(irisHeader() + *fsh, dir, seen, defined);
      noteTexFlags(expanded, slotClear, slotMip);
      if (!passEnabled(props, prefix, n, defined)) {
        DQ_LOG("shader pack '%s': %s disabled", pack.c_str(), n.c_str());
        return false;
      }
      bool doBlend = translucent;
      GLenum bSrc = GL_SRC_ALPHA, bDst = GL_ONE_MINUS_SRC_ALPHA, bSrcA = GL_ONE, bDstA = GL_ONE_MINUS_SRC_ALPHA;
      uint32_t blendOff = 0;
      std::string alphaKeep;
      if (geom) {
        readPackBlend(props, prefix, n, defined, doBlend, bSrc, bDst, bSrcA, bDstA, blendOff);
        if (!shadow) alphaKeep = readAlphaKeep(props, prefix, n, defined);
      }
      auto vsh = F.read(base + ".vsh");
      seen.clear();
      defined.clear();
      const std::string vexp = vsh ? F.expand(irisHeader() + *vsh, dir, seen, defined) : std::string(DEFAULT_VSH);
      if (shadow) {
        auto take = [&](const std::string& s) {
          shDist = lastConstFloat(s, "shadowDistance", shDist);
          shPath = lastConstFloat(s, "sunPathRotation", shPath);
          shInterval = lastConstFloat(s, "shadowIntervalSize", shInterval);
          shMul = lastConstFloat(s, "shadowDistanceRenderMul", shMul);
          shRes = lastConstInt(s, "shadowMapResolution", shRes);
        };
        take(expanded);
        take(vexp);
      }
      DQ_LOG("shader pack '%s': compiling %s%s", pack.c_str(), n.c_str(),
             entity && shadow ? " (entity shadow)" : hand ? " (hand)" : entity ? " (entities)" : shadow ? " (shadow)" : translucent ? " (translucent)" : geom ? " (gbuffer)" : "");
      std::string fsrc = translate(expanded, false, geom, shadow, translucent, alphaKeep, entity, hand);
      std::string vsrc = translate(vexp, true, geom, shadow, translucent, {}, entity, hand);
      // Only the shadow vertex calls dqShadowBridge. The camera gbuffer has no prototype.
      if (shadow) {
        const std::string bridge = shadowBridgeDef(vsrc, fsrc);
        if (bridge.find(" = ") != std::string::npos)
          DQ_LOG("shader pack '%s': shadow varyings bridged", pack.c_str());
        vsrc += bridge;
      }
      GLuint prog = buildProgram(vsrc, fsrc, pack + ":" + base);
      if (!prog) return false;
      IrisPass P; P.prog = prog; P.name = n;
      P.blend = doBlend; P.bSrc = bSrc; P.bDst = bDst; P.bSrcA = bSrcA; P.bDstA = bDstA; P.blendOff = blendOff;
      const bool userOut = std::regex_search(expanded, std::regex(R"(\bout\s+vec[234]\b)"));
      P.writesColor = expanded.find("gl_FragData") != std::string::npos || expanded.find("gl_FragColor") != std::string::npos || userOut;
      for (int b : drawBuffers(expanded, false)) if (b >= 0 && b < 16 && P.nOut < 8) P.outs[P.nOut++] = b;
      if (!P.nOut) { glDeleteProgram(prog); return false; }
      GLint cnt = 0; glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &cnt);
      for (GLint u = 0; u < cnt; u++) {
        char nm[256]; GLsizei len = 0; GLint size = 0; GLenum type = 0;
        glGetActiveUniform(prog, (GLuint)u, sizeof nm, &len, &size, &type, nm);
        std::string un(nm, (size_t)len); if (un.size() > 3 && un.substr(un.size() - 3) == "[0]") un.resize(un.size() - 3);
        P.uniforms.push_back({un, type});
      }
      std::string outs;
      for (int i = 0; i < P.nOut; i++) outs += (i ? "+" : "") + std::to_string(P.outs[i]);
      DQ_LOG("shader pack '%s': %s writes colortex%s", pack.c_str(), n.c_str(), outs.c_str());
      out = std::move(P);
      return true;
    } catch (const std::exception& ex) {
      DQ_WARN("shader pack '%s': pass %s failed (%s)", pack.c_str(), n.c_str(), ex.what());
      return false;
    }
  }

  void dropStageTextures() {
    for (GLuint t : customTexIds) if (t) glDeleteTextures(1, &t);
    customTexIds.clear();
    stageTex.clear();
  }
  GLuint stageSampler(int stage, const std::string& name, GLenum target = GL_TEXTURE_2D) const {
    for (int i = (int)stageTex.size() - 1; i >= 0; --i)
      if (stageTex[i].stage == stage && stageTex[i].name == name && stageTex[i].target == target) return stageTex[i].tex;
    return 0;
  }
  // texture.<stage>.<name> = file.dat TEXTURE_3D <internal> <w> <h> <d> <format> <type>
  // Photon stores the atmosphere LUT here and reads the sun color from one texel of it.
  GLuint loadPackRaw3D(const IrisFiles& F, const std::string& rel, const std::string& spec, bool& linear, bool& clamp) {
    linear = true; clamp = true;
    std::vector<std::string> tok;
    std::string cur;
    for (char c : spec) {
      if (c == ' ' || c == '\t') { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
      else cur.push_back(c);
    }
    if (!cur.empty()) tok.push_back(cur);
    if (tok.size() < 8 || tok[1] != "TEXTURE_3D") return 0;
    int w = std::atoi(tok[3].c_str()), h = std::atoi(tok[4].c_str()), d = std::atoi(tok[5].c_str());
    if (w < 1 || h < 1 || d < 1) return 0;
    const std::string& fmt = tok[6];
    const std::string& typ = tok[7];
    int comp = fmt == "RED" ? 1 : fmt == "RG" ? 2 : fmt == "RGB" ? 3 : fmt == "RGBA" ? 4 : 0;
    int bpc = typ == "UNSIGNED_BYTE" ? 1 : typ == "HALF_FLOAT" ? 2 : typ == "FLOAT" ? 4 : 0;
    if (!comp || !bpc) return 0;
    auto file = F.read(tok[0]);
    if (!file || file->size() < (size_t)w * h * d * comp * bpc) return 0;
    if (auto meta = F.read(tok[0] + ".mcmeta")) {
      auto flag = [&](const char* name, const char* word) {
        size_t i = meta->find(name); if (i == std::string::npos) return false;
        size_t c = meta->find(':', i); if (c == std::string::npos) return false;
        return meta->substr(c, 24).find(word) != std::string::npos;
      };
      if (flag("\"blur\"", "false")) linear = false;
      if (flag("\"clamp\"", "false")) clamp = false;
      if (flag("\"clamp\"", "true")) clamp = true;
    }
    const void* pixels = file->data();
    std::vector<uint16_t> expanded;
    GLenum internal = GL_R8, external = GL_RED, glType = GL_UNSIGNED_BYTE;
    if (typ == "HALF_FLOAT" && comp == 3) {
      // The backend stores RGB16F as RGBA16F. Expand so the upload stride matches.
      size_t n = (size_t)w * h * d;
      expanded.resize(n * 4);
      const auto* src = reinterpret_cast<const uint16_t*>(file->data());
      for (size_t i = 0; i < n; i++) {
        expanded[i * 4] = src[i * 3]; expanded[i * 4 + 1] = src[i * 3 + 1];
        expanded[i * 4 + 2] = src[i * 3 + 2]; expanded[i * 4 + 3] = 0x3C00;
      }
      pixels = expanded.data();
      internal = GL_RGBA16F; external = GL_RGBA; glType = GL_HALF_FLOAT;
    } else if (typ == "HALF_FLOAT" && comp == 4) {
      internal = GL_RGBA16F; external = GL_RGBA; glType = GL_HALF_FLOAT;
    } else if (typ == "FLOAT" && comp == 4) {
      internal = GL_RGBA32F; external = GL_RGBA; glType = GL_FLOAT;
    } else if (typ == "UNSIGNED_BYTE" && comp == 1) {
      internal = GL_R8; external = GL_RED; glType = GL_UNSIGNED_BYTE;
    } else return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glTexImage3D(GL_TEXTURE_3D, 0, internal, w, h, d, 0, external, glType, pixels);
    GLenum filter = linear ? GL_LINEAR : GL_NEAREST;
    GLenum wrap = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, wrap);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
    (void)rel;
    return tex;
  }
  // texture.<stage>.<sampler>=path.png. The same file can be named twice (gbuffers and deferred).
  // #if in shaders.properties follows the macros the terrain shader defined, so one cloud style wins.
  void loadStageTextures(const IrisFiles& F, const std::string& pack, const std::string& props,
                         const std::unordered_map<std::string, int>& defined) {
    dropStageTextures();
    std::unordered_map<std::string, GLuint> byPath;
    forActiveLines(props, defined, [&](const std::string& line) {
      if (line.rfind("texture.", 0) != 0) return;
      size_t eq = line.find('=');
      if (eq == std::string::npos) return;
      auto trim = [](std::string s) {
        size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
        size_t b = s.size(); while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
        return s.substr(a, b - a);
      };
      std::string key = trim(line.substr(8, eq - 8));
      std::string val = trim(line.substr(eq + 1));
      if (key.empty() || val.empty() || key == "noise") return;
      size_t sp = val.find_first_of(" \t");
      if (sp != std::string::npos) {
        if (val.find("TEXTURE_3D") == std::string::npos) {
          DQ_LOG("shader pack '%s': custom texture %s is raw image data, not bound over the buffer", pack.c_str(), key.c_str());
          return;
        }
        size_t dot = key.find('.');
        if (dot == std::string::npos || dot + 1 >= key.size()) return;
        int stage = texStageId(key.substr(0, dot));
        if (!stage) return;
        std::string sampler = key.substr(dot + 1);
        size_t dot2 = sampler.find('.');
        if (dot2 != std::string::npos) sampler.resize(dot2);
        bool linear = true, clamp = true;
        GLuint tex = loadPackRaw3D(F, sampler, val, linear, clamp);
        if (!tex) {
          DQ_WARN("shader pack '%s': custom texture %s could not be loaded", pack.c_str(), key.c_str());
          return;
        }
        customTexIds.push_back(tex);
        bool replaced = false;
        for (auto& e : stageTex) if (e.stage == stage && e.name == sampler) { e.tex = tex; e.target = GL_TEXTURE_3D; replaced = true; break; }
        if (!replaced) stageTex.push_back({stage, std::move(sampler), tex, GL_TEXTURE_3D});
        DQ_LOG("shader pack '%s': %s reads a 3D texture", pack.c_str(), key.c_str());
        return;
      }
      size_t dot = key.find('.');
      if (dot == std::string::npos || dot + 1 >= key.size()) return;
      int stage = texStageId(key.substr(0, dot));
      if (!stage) {
        DQ_LOG("shader pack '%s': custom texture stage '%s' is unknown", pack.c_str(), key.substr(0, dot).c_str());
        return;
      }
      std::string sampler = key.substr(dot + 1);
      size_t dot2 = sampler.find('.');
      if (dot2 != std::string::npos) sampler.resize(dot2);
      if (sampler.empty() || val.find(':') != std::string::npos) {
        if (val.find(':') != std::string::npos)
          DQ_LOG("shader pack '%s': custom texture %s is a resource path, skipped", pack.c_str(), val.c_str());
        return;
      }
      GLuint tex = 0;
      auto it = byPath.find(val);
      if (it != byPath.end()) tex = it->second;
      else {
        tex = loadPackPng(F, val);
        if (!tex) {
          DQ_WARN("shader pack '%s': custom texture %s missing (%s)", pack.c_str(), key.c_str(), val.c_str());
          return;
        }
        byPath[val] = tex;
        customTexIds.push_back(tex);
      }
      bool replaced = false;
      for (auto& e : stageTex) if (e.stage == stage && e.name == sampler) { e.tex = tex; replaced = true; break; }
      if (!replaced) stageTex.push_back({stage, std::move(sampler), tex});
      DQ_LOG("shader pack '%s': %s reads %s", pack.c_str(), key.c_str(), val.c_str());
    });
  }

  // item.properties: item.<id> = torch lava_bucket. Held-light uniforms use these ids.
  void loadPackItems(const IrisFiles& F) {
    packItems.clear();
    std::set<std::string> seen;
    std::unordered_map<std::string, int> defined;
    const std::string text = F.expand(irisHeader() + F.read("item.properties").value_or(""), "", seen, defined);
    size_t i = 0;
    int n = 0;
    while (i < text.size()) {
      size_t eol = text.find('\n', i);
      if (eol == std::string::npos) eol = text.size();
      std::string line = text.substr(i, eol - i);
      i = eol < text.size() ? eol + 1 : eol;
      size_t b = 0;
      while (b < line.size() && std::isspace((unsigned char)line[b])) b++;
      if (line.compare(b, 5, "item.") != 0) continue;
      char* end = nullptr;
      int id = (int)std::strtol(line.c_str() + b + 5, &end, 10);
      if (!end || end == line.c_str() + b + 5 || id <= 0) continue;
      const char* eq = std::strchr(end, '=');
      if (!eq) continue;
      std::string rest = eq + 1;
      size_t p = 0;
      while (p < rest.size()) {
        while (p < rest.size() && std::isspace((unsigned char)rest[p])) p++;
        size_t e = p;
        while (e < rest.size() && !std::isspace((unsigned char)rest[e])) e++;
        if (e > p) {
          std::string token = rest.substr(p, e - p);
          std::string name = token;
          size_t eqp = token.find('=');
          if (eqp != std::string::npos) {
            size_t c = token.find(':');
            if (c != std::string::npos && c < eqp) name = token.substr(0, c);
          } else {
            size_t c = token.rfind(':');
            if (c != std::string::npos) name = token.substr(c + 1);
          }
          if (!name.empty()) { packItems[name] = id; n++; }
        }
        p = e;
      }
    }
    if (n) DQ_LOG("shader pack: %d held item ids", n);
  }

  void loadIris(const std::string& pack, int dim) {
    irisFor = pack;
    irisDim = dim;
    packHasFinal = false;
    toldShow = false;
    havePrev = false;
    resetHistory = true;
    eyeSmInit = false;
    toldGeom = false;
    toldWater = false;
    toldShadow = false;
    toldEntity = false;
    toldEntShadow = false;
    entityDrew = -1;
    shDist = 128.f; shPath = 0.f; shInterval = 2.f; shMul = 1.f; shRes = 1024;
    auto dropPass = [](IrisPass& p) { if (p.prog) glDeleteProgram(p.prog); p = {}; };
    auto dropList = [&](std::vector<IrisPass>& v) { for (auto& p : v) dropPass(p); v.clear(); };
    dropList(iris); dropList(irisBegin); dropList(irisPrep); dropList(irisDef); dropPass(gbuf); dropPass(gshadow); dropPass(gwater); dropPass(gentity); dropPass(gshadowEnt); dropPass(ghand);
    packHandSetSuppress(false);
    packSetSkipClouds(false);
    toldHand = false;
    toldHeld = false;
    packItems.clear();
    dropStageTextures();
    if (pack.empty()) return;
    IrisFiles F;
    if (!F.open(userDir() / "shaderpacks" / pack)) return;   // one of our own packs (or nothing to run)
    const std::string prefix = dimensionPrefix(F, dim);
    const std::string props = F.read("shaders.properties").value_or("");
    loadPackItems(F);
    for (int i = 0; i < 16; i++) { slotClear[i] = true; slotMip[i] = false; }
    irisU = parseIrisUniforms(props);
    sceneBuf = 0;
    bool cloudsOff = false;
    {
      std::string tbase = (!prefix.empty() && F.read(prefix + "gbuffers_terrain.fsh")) ? prefix + "gbuffers_terrain" : "gbuffers_terrain";
      std::unordered_map<std::string, int> defined;
      if (auto tf = F.read(tbase + ".fsh")) {
        std::set<std::string> seen;
        const std::string dir = tbase.substr(0, tbase.rfind('/') + 1);
        const std::string expanded = F.expand(irisHeader() + *tf, dir, seen, defined);
        noteTexFlags(expanded, slotClear, slotMip);
        auto bufs = drawBuffers(expanded, false);
        if (!bufs.empty()) sceneBuf = bufs[0];
      }
      // OptiFine's shorthand. Complementary, Photon and MakeUp set this and raymarch the sky.
      forActiveLines(props, defined, [&](const std::string& line) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) return;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        auto trim = [](std::string s) {
          size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
          size_t b = s.size(); while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
          return s.substr(a, b - a);
        };
        key = trim(key); val = trim(val);
        if (key == "clouds" && (val == "off" || val == "false")) cloudsOff = true;
      });
      loadStageTextures(F, pack, props, defined);
    }
    DQ_LOG("shader pack '%s': scene color is colortex%d", pack.c_str(), sceneBuf);
    {
      const char* chain[] = {"gbuffers_terrain", "gbuffers_textured_lit", "gbuffers_textured", "gbuffers_basic"};
      for (const char* n : chain) if (compilePass(F, pack, prefix, props, n, true, gbuf)) break;
      if (!gbuf.prog) DQ_LOG("shader pack '%s': no gbuffer linked, post only", pack.c_str());
      else if (cloudsOff) {
        packSetSkipClouds(true);
        DQ_LOG("shader pack '%s': game clouds skipped", pack.c_str());
      }
      auto loadStage = [&](const char* base, std::vector<IrisPass>& dest) {
        for (int i = 0; i <= 16; i++) {
          IrisPass P;
          std::string n = i ? std::string(base) + std::to_string(i) : base;
          if (!compilePass(F, pack, prefix, props, n, false, P)) continue;
          bool image = false;
          for (auto& u : P.uniforms) if (imageUniform(u.second)) image = true;
          // An image-only pass (Rethinking Voxels prepare) would otherwise clear its DRAWBUFFERS to black.
          if (image && !P.writesColor) {
            DQ_LOG("shader pack '%s': %s uses image load/store, skipped", pack.c_str(), n.c_str());
            glDeleteProgram(P.prog);
            continue;
          }
          dest.push_back(std::move(P));
        }
      };
      // These run only once a gbuffer exists. Without it they grade empty buffers and can black the frame.
      if (gbuf.prog) {
        compilePass(F, pack, prefix, props, "shadow", true, gshadow, true);
        if (!compilePass(F, pack, prefix, props, "gbuffers_water", true, gwater, false, true))
          DQ_LOG("shader pack '%s': no gbuffers_water, translucent stays the copied picture", pack.c_str());
        {
          const char* entChain[] = {"gbuffers_entities", "gbuffers_textured_lit", "gbuffers_textured", "gbuffers_basic"};
          for (const char* n : entChain)
            if (compilePass(F, pack, prefix, props, n, true, gentity, false, false, true)) break;
          if (!gentity.prog) DQ_LOG("shader pack '%s': no entity program, creatures stay the copied picture", pack.c_str());
          else if (gshadow.prog) compilePass(F, pack, prefix, props, "shadow", true, gshadowEnt, true, false, true);
          const char* handChain[] = {"gbuffers_hand", "gbuffers_textured_lit", "gbuffers_textured", "gbuffers_basic"};
          for (const char* n : handChain)
            if (compilePass(F, pack, prefix, props, n, true, ghand, false, false, true, true)) break;
          packHandSetSuppress(ghand.prog != 0);
          if (!ghand.prog) DQ_LOG("shader pack '%s': no hand program, the arm stays the game picture", pack.c_str());
        }
        if (gshadow.prog)
          DQ_LOG("shader pack '%s': shadow map %d, distance %.0f, sun path %.1f", pack.c_str(), shRes, shDist, shPath);
        else DQ_LOG("shader pack '%s': no shadow program, sun shadow stays folded", pack.c_str());
        loadStage("begin", irisBegin);
        loadStage("prepare", irisPrep);
        loadStage("deferred", irisDef);
      }
    }
    std::vector<std::string> names;
    for (int i = 0; i < 32; i++) names.push_back(i ? "composite" + std::to_string(i) : "composite");
    names.push_back("final");
    int ok = 0, tried = 0;
    for (const std::string& n : names) {
      try {
      // The dimension folder replaces the base file when it exists, as Horizon's load_post does.
      std::string base = (!prefix.empty() && F.read(prefix + n + ".fsh")) ? prefix + n : n;
      auto fsh = F.read(base + ".fsh");
      if (!fsh) continue;
      const bool isFinal = n == "final";
      std::set<std::string> seen;
      std::unordered_map<std::string, int> defined;
      const std::string dir = base.substr(0, base.rfind('/') + 1);
      const std::string expanded = F.expand(irisHeader() + *fsh, dir, seen, defined);
      noteTexFlags(expanded, slotClear, slotMip);
      if (!passEnabled(props, prefix, n, defined)) {
        DQ_LOG("shader pack '%s': %s disabled", pack.c_str(), n.c_str());
        continue;
      }
      if (isFinal) packHasFinal = true;
      tried++;
      DQ_LOG("shader pack '%s': compiling %s", pack.c_str(), n.c_str());
      auto vsh = F.read(base + ".vsh");
      seen.clear();
      defined.clear();
      std::string fsrc = translate(expanded, false);
      std::string vsrc = translate(vsh ? F.expand(irisHeader() + *vsh, dir, seen, defined) : std::string(DEFAULT_VSH), true);
      GLuint prog = buildProgram(vsrc, fsrc, pack + ":" + base);
      if (!prog) continue;
      IrisPass P; P.prog = prog; P.name = n;
      if (!isFinal) {
        for (int b : drawBuffers(expanded, false)) if (b >= 0 && b < 16 && P.nOut < 8) P.outs[P.nOut++] = b;
        if (!P.nOut) { glDeleteProgram(prog); continue; }
      }
      GLint cnt = 0; glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &cnt);
      for (GLint u = 0; u < cnt; u++) {
        char nm[256]; GLsizei len = 0; GLint size = 0; GLenum type = 0;
        glGetActiveUniform(prog, (GLuint)u, sizeof nm, &len, &size, &type, nm);
        std::string un(nm, (size_t)len); if (un.size() > 3 && un.substr(un.size() - 3) == "[0]") un.resize(un.size() - 3);
        P.uniforms.push_back({un, type});
      }
      int samplers = 0;
      std::string sampNames;
      for (auto& u : P.uniforms) if (u.second == GL_SAMPLER_2D || u.second == GL_SAMPLER_2D_SHADOW || u.second == GL_SAMPLER_3D) {
        samplers++;
        if (!sampNames.empty()) sampNames += " ";
        sampNames += u.first;
      }
      if (!P.nOut) DQ_LOG("shader pack '%s': %s draws the screen (%d samplers: %s)", pack.c_str(), n.c_str(), samplers, sampNames.c_str());
      else {
        std::string outs;
        for (int i = 0; i < P.nOut; i++) outs += (i ? "+" : "") + std::to_string(P.outs[i]);
        DQ_LOG("shader pack '%s': %s writes colortex%s (%d samplers: %s)", pack.c_str(), n.c_str(), outs.c_str(), samplers, sampNames.c_str());
      }
      iris.push_back(std::move(P)); ok++;
      } catch (const std::exception& ex) {
        DQ_WARN("shader pack '%s': pass %s failed (%s)", pack.c_str(), n.c_str(), ex.what());
      }
    }
    DQ_LOG("shader pack '%s': %d of %d post passes ready, gbuffer %s, water %s, shadow %s, begin %d, prepare %d, deferred %d",
           pack.c_str(), ok, tried, gbuf.prog ? gbuf.name.c_str() : "off", gwater.prog ? "on" : "off", gshadow.prog ? "on" : "off",
           (int)irisBegin.size(), (int)irisPrep.size(), (int)irisDef.size());
    {
      PackIds ids = buildPackIds(F);
      blockIdInts = std::move(ids.blockMap);
      blockMapDirty = !blockIdInts.empty();
      if (entityLut) { glDeleteTextures(1, &entityLut); entityLut = 0; }
      glGenTextures(1, &entityLut);
      glBindTexture(GL_TEXTURE_2D, entityLut);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 4096, 1, 0, GL_RED, GL_FLOAT, ids.lut.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      DQ_LOG("shader pack '%s': block ids on %d tiles (grass %d, leaves %d, water %d, stone %d)",
             pack.c_str(), ids.tiles, ids.grass, ids.leaves, ids.water, ids.stone);
    }
    if (!blackTex) {
      const u8 b[4] = {0, 0, 0, 255}, w[4] = {255, 255, 255, 255};
      blackTex = makeTexture2D(1, 1, b, true, false, false); whiteTex = makeTexture2D(1, 1, w, true, false, false);
      // A sampler2DShadow or sampler3D with nothing bound is incomplete. On this Intel driver
      // that makes every lookup in the pass return 0, so Rethinking Voxels writes a black frame.
      glGenTextures(1, &shadowDummy);
      glBindTexture(GL_TEXTURE_2D, shadowDummy);
      const float shadowZero = 0.f;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &shadowZero);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      // Composites keep the depth-0 map (a cave stays unlit). The gbuffer compares against depth 1,
      // so its own shadow2D reads as lit until a real sun shadow is drawn.
      glGenTextures(1, &shadowLit);
      glBindTexture(GL_TEXTURE_2D, shadowLit);
      const float shadowOne = 1.f;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &shadowOne);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      const u8 fn[4] = {128, 128, 255, 255};
      flatNormal = makeTexture2D(1, 1, fn, true, false, false);
      const float z4[4] = {0, 0, 0, 0};
      glGenTextures(1, &volDummy);
      glBindTexture(GL_TEXTURE_3D, volDummy);
      glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, 1, 1, 1, 0, GL_RGBA, GL_FLOAT, z4);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
      const float q[8] = {0, 0, 1, 0, 1, 1, 0, 1};
      glGenVertexArrays(1, &quadVao); glGenBuffers(1, &quadVbo);
      glBindVertexArray(quadVao); glBindBuffer(GL_ARRAY_BUFFER, quadVbo); glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
      glBindVertexArray(0);
    }
    // The pack's own noisetex. A flat texel makes every pixel share one dither value, and the
    // per-frame rotation then flashes the whole far land on every renderer.
    if (noiseTex) glDeleteTextures(1, &noiseTex);
    noiseTex = 0;
    {
      const std::string rel = packNoiseRel(F);
      int nw = 0, nh = 0; std::vector<u8> px;
      bool nearest = false, repeat = true;
      auto file = F.read(rel);
      if (file && loadPngMem(reinterpret_cast<const u8*>(file->data()), file->size(), nw, nh, px) && nw > 0 && nh > 0) {
        if (auto meta = F.read(rel + ".mcmeta")) {
          auto flag = [&](const char* name, const char* word) {
            size_t i = meta->find(name); if (i == std::string::npos) return false;
            size_t c = meta->find(':', i); if (c == std::string::npos) return false;
            return meta->substr(c, 24).find(word) != std::string::npos;
          };
          if (flag("\"blur\"", "false")) nearest = true;
          if (flag("\"clamp\"", "true")) repeat = false;
        }
        DQ_LOG("shader pack '%s': noisetex %dx%d from %s", pack.c_str(), nw, nh, rel.c_str());
      } else {
        nw = nh = 256; px.assign((size_t)nw * nh * 4, 255);
        uint64_t state = 0x2545F4914F6CDD1Dull;
        for (size_t i = 0; i < px.size(); i++) {
          state ^= state << 13; state ^= state >> 7; state ^= state << 17;
          if (i % 4 != 3) px[i] = (u8)(state >> 56);
        }
        DQ_LOG("shader pack '%s': noisetex fallback 256x256 (no %s)", pack.c_str(), rel.c_str());
      }
      noiseTex = makeTexture2D(nw, nh, px.data(), nearest, false, repeat);
    }
  }

  void runIris(GameSession& s, const FrameCtx& f, const GLint* vp, GLint outFbo) {
    const int W = vp[2], H = vp[3];
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    bool need[16] = {};
    need[0] = true;
    if (sceneBuf > 0 && sceneBuf < 16) need[sceneBuf] = true;
    auto mark = [&](const IrisPass& P) {
      for (int i = 0; i < P.nOut; i++) if (P.outs[i] >= 0 && P.outs[i] < 16) need[P.outs[i]] = true;
      for (auto& u : P.uniforms) if (u.second == GL_SAMPLER_2D) {
        int ct = colortexOf(u.first);
        if (ct >= 0 && ct < 16) need[ct] = true;
      }
    };
    for (const IrisPass& P : irisBegin) mark(P);
    for (const IrisPass& P : irisPrep) mark(P);
    for (const IrisPass& P : irisDef) mark(P);
    if (gbuf.prog) mark(gbuf);
    if (gwater.prog) mark(gwater);
    if (gentity.prog) mark(gentity);
    for (const IrisPass& P : iris) mark(P);
    for (int b = 0; b < 16; b++) if (need[b]) {
      // Iris clears a buffer when the pack says so (the default). colortex2 and MakeUp's exposure
      // (gaux3) opt out, and the ping-pong front stays on the side the last pass wrote.
      const bool fresh = !slot[b][0].tex || slot[b][0].w != W || slot[b][0].h != H || !slot[b][0].hdr;
      slot[b][0].make(W, H, true, true); slot[b][1].make(W, H, true, true);
      if (!fresh && !slotClear[b]) continue;
      for (int side = 0; side < 2; side++) {
        glBindFramebuffer(GL_FRAMEBUFFER, slot[b][side].fbo);
        glViewport(0, 0, W, H);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
      }
    }
    resetHistory = false;
    copyFrame(slot[0][slotFront[0]], vp, outFbo);
    if (sceneBuf > 0 && sceneBuf < 16) copyFrame(slot[sceneBuf][slotFront[sceneBuf]], vp, outFbo);
    // DQ_PACK_DUMP_FRAME is the app frame to capture (the horizon shot is frame 95).
    // Without it, the first pack frame is captured, which is still the spawn view.
    int dumpAt = -1;
    if (const char* df = std::getenv("DQ_PACK_DUMP_FRAME")) dumpAt = std::atoi(df);
    const bool dumpFrame = std::getenv("DQ_PACK_DUMP") && !packDumpDone &&
                           (dumpAt < 0 || (int)s.app.frame == dumpAt);
    if (dumpFrame) packDump("copy0", slot[0][slotFront[0]].tex, W, H);
    auto genMips = [&](int b) {
      if (b < 0 || b >= 16 || !slotMip[b] || !slot[b][slotFront[b]].tex) return;
      int levels = 1, mw = slot[b][0].w, mh = slot[b][0].h;
      while (mw > 1 || mh > 1) { mw = std::max(1, mw / 2); mh = std::max(1, mh / 2); levels++; }
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outFbo);
      glBindTexture(GL_TEXTURE_2D, slot[b][slotFront[b]].tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
      glGenerateMipmap(GL_TEXTURE_2D);
    };
    for (int b = 0; b < 16; b++) if (need[b]) genMips(b);
    if (dumpFrame) packDump("mips0", slot[0][slotFront[0]].tex, W, H);
    if (!passFbo) glGenFramebuffers(1, &passFbo);
    if (!packSsbo) {
      std::vector<unsigned char> zeros(1 << 20, 0);
      glGenBuffers(1, &packSsbo);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, packSsbo);
      glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)zeros.size(), zeros.data(), GL_DYNAMIC_DRAW);
    }
    if (blockMapDirty && packSsbo && !blockIdInts.empty()) {
      const GLintptr off = 192; // three std430 mat4s, then int blockIdMap[]
      const GLsizeiptr bytes = (GLsizeiptr)blockIdInts.size() * (GLsizeiptr)sizeof(int);
      if (off + bytes <= (1 << 20)) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, packSsbo);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, off, bytes, blockIdInts.data());
      }
      blockMapDirty = false;
    }
    for (int i = 0; i < 8; i++) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, packSsbo);
    // This pass runs before the hand, so the framebuffer depth is still the world. The arm is
    // drawn on top afterwards; its own projection must not be read back as depthtex0.
    GLuint useDepth = 0;
    bool depthOk = false;
    bool transCopy = false;
    // Set after gbuffers_water writes. Until then every depth sampler is the opaque copy, so the
    // water pass cannot sample the depth image it is attached to.
    bool splitDepth = false;
    auto allocDepth = [&](GLuint& id) {
      glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, W, H, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    if (!depthTex || !depthTrans || dw != W || dh != H) {
      if (depthTex) glDeleteTextures(1, &depthTex);
      if (depthTrans) glDeleteTextures(1, &depthTrans);
      depthTex = depthTrans = 0;
      allocDepth(depthTex); allocDepth(depthTrans);
      dw = W; dh = H;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)outFbo);
    glBindTexture(GL_TEXTURE_2D, depthTex);
    glGetError();
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], W, H);
    depthOk = glGetError() == GL_NO_ERROR;
    useDepth = depthTex;
    // Filled from depthTex just before gbuffers_water. A second copy of the window depth
    // runs while that image is still a transfer source, and the result can stay clear.
    if (depthOk && gwater.prog && depthTrans) transCopy = true;
    const double now = nowSeconds(); const float ft = lastT > 0 ? (float)(now - lastT) : 0.016f; lastT = now; frames++;
    const Camera& cam = f.cam;
    const vec3 eyeRel = vec3(cam.pos - cam.origin);
    // OptiFine's view space: the camera at the origin (our view matrix is relative to the camera's block)
    const mat4 mv = cam.view * glm::translate(mat4(1), eyeRel);
    const mat4 mvi = glm::inverse(mv), pr = cam.proj, pri = glm::inverse(cam.proj);
    const vec3 sunV = vec3(mv * vec4(f.sunDir * 100.f, 0.f)), moonV = -sunV, upV = vec3(mv * vec4(0, 100.f, 0, 0.f));
    // Shader packs share the game clock: 0 sunrise, 0.25 noon, 0.5 sunset, 0.75 midnight.
    // worldTime is that fraction of 24000 (noon is 6000). A quarter-day shift lights the world as dawn.
    double dayF = std::fmod(s.level->info.time / DAY_LEN, 1.0);
    if (dayF < 0) dayF += 1.0;
    const float sunAngle = (float)dayF;
    const float shadowAngle = sunAngle > 0.5f ? sunAngle - 0.5f : sunAngle;
    int wt = (int)std::floor(dayF * 24000.0);
    if (wt < 0) wt = 0;
    if (wt > 23999) wt = 23999;
    const mat4 ortho = glm::ortho(0.f, 1.f, 0.f, 1.f, -1.f, 1.f);
    const vec3 camPos((float)std::fmod(cam.pos.x, 30000.0), (float)(cam.pos.y - dimBase(f.dim)), (float)std::fmod(cam.pos.z, 30000.0));
    const vec3 prevPos = havePrev ? prevCam : camPos;
    const mat4& pMV = havePrev ? prevMV : mv;
    const mat4& pP = havePrev ? prevP : pr;
    // Iris eyeBrightness is block light, then sky light, each 0..15 stored as 0..240.
    const u8 eyePacked = s.world().getLight(ifloor(cam.pos.x), ifloor(cam.pos.y + 1.62), ifloor(cam.pos.z));
    const int eyeBlock = (eyePacked & 15) * 16;
    const int eyeSky = (eyePacked >> 4) * 16;
    if (!eyeSmInit) { eyeBlockSm = (float)eyeBlock; eyeSkySm = (float)eyeSky; eyeSmInit = true; }
    else {
      const float blend = 1.f - std::exp(-ft * 4.f);
      eyeBlockSm += ((float)eyeBlock - eyeBlockSm) * blend;
      eyeSkySm += ((float)eyeSky - eyeSkySm) * blend;
    }
    const int eyeBlockI = (int)std::lround(eyeBlockSm);
    const int eyeSkyI = (int)std::lround(eyeSkySm);
    // Fold every sample onto one shadowed texel when the pack has no shadow program, or the map
    // could not be drawn. An identity matrix leaves a lit box (cave sun shafts, white terrain).
    mat4 shadowMV(1.f), shadowMVI(1.f), shadowP(1.f), shadowPI(1.f);
    auto foldShadow = [&]() {
      const float shadowScale = 1.0e-4f;
      shadowMV = mat4(1.f);
      shadowMV[0][0] = shadowScale; shadowMV[1][1] = shadowScale; shadowMV[2][2] = shadowScale;
      shadowMV[3] = vec4(0.f, 0.f, -1.f, 1.f);
      shadowMVI = mat4(1.f);
      const float shadowInv = 1.f / shadowScale;
      shadowMVI[0][0] = shadowInv; shadowMVI[1][1] = shadowInv; shadowMVI[2][2] = shadowInv;
      shadowMVI[3] = vec4(0.f, 0.f, shadowInv, 1.f);
      shadowP = mat4(1.f);
      shadowPI = mat4(1.f);
    };
    bool shadowDrew = false;
    float shHalf = 128.f;
    if (gshadow.prog && s.worldRenderer.blockAtlas()) {
      // Iris ShadowMatrices: light looks down at noon, and the snap keeps texels from swimming.
      if (shMul <= 0.f) shHalf = (float)std::max(32, s.app.settings.renderDist * 16);
      else shHalf = std::max(32.f, shDist * shMul);
      shHalf = std::clamp(shHalf, 32.f, 512.f);
      const float sky = shadowAngle < 0.25f ? shadowAngle + 0.75f : shadowAngle - 0.25f;
      shadowMV = mat4(1.f);
      shadowMV = glm::rotate(shadowMV, glm::radians(90.f), vec3(1.f, 0.f, 0.f));
      shadowMV = glm::rotate(shadowMV, glm::radians(sky * -360.f), vec3(0.f, 0.f, 1.f));
      shadowMV = glm::rotate(shadowMV, glm::radians(shPath), vec3(1.f, 0.f, 0.f));
      if (std::abs(shInterval) > 1.0e-4f) {
        const double interval = (double)shInterval;
        const double halfI = interval * 0.5;
        auto cell = [&](double v) { return (float)(std::fmod(v, interval) - halfI); };
        const double sy = cam.pos.y - dimBase(f.dim);
        shadowMV = glm::translate(shadowMV, vec3(cell(cam.pos.x), cell(sy), cell(cam.pos.z)));
      }
      // Symmetric depth so a low sun still reaches terrain beside the camera. The pack scales z itself.
      shadowP = glm::ortho(-shHalf, shHalf, -shHalf, shHalf, -shHalf, shHalf);
      shadowMVI = glm::inverse(shadowMV);
      shadowPI = glm::inverse(shadowP);
    } else foldShadow();
    bool wrote[16] = {};
    wrote[0] = true;
    if (sceneBuf > 0 && sceneBuf < 16) wrote[sceneBuf] = true;
    bool wroteScene = false;
    bool finalDrawn = false;
    std::unordered_map<std::string, float> customNow;
    {
      std::unordered_map<std::string, double> env = {
        {"viewWidth", (double)W}, {"viewHeight", (double)H}, {"aspectRatio", H ? (double)W / H : 1.0},
        {"frameTime", ft}, {"frameTimeCounter", std::fmod(now - startT, 3600.0)},
        {"frameCounter", (double)(frames % 720720)}, {"near", cam.nearZ}, {"far", std::max(64.f, cam.farZ)},
        {"renderDistance", (double)std::max(32, s.app.settings.renderDist * 16)},
        {"rainStrength", f.rain}, {"wetness", f.rain}, {"sunAngle", sunAngle},
        {"shadowAngle", shadowAngle}, {"worldTime", (double)wt}, {"worldDay", (double)s.level->info.day},
        {"moonPhase", (double)(s.level->info.day % 8)}, {"isEyeInWater", (double)f.eyeFluid},
        {"screenBrightness", s.app.settings.brightness}, {"eyeAltitude", cam.pos.y - dimBase(f.dim)},
        {"blindness", 0}, {"darknessFactor", 0}, {"nightVision", 0},
        {"eyeBrightness.x", (double)eyeBlock}, {"eyeBrightness.y", (double)eyeSky},
        {"eyeBrightnessSmooth.x", (double)eyeBlockI}, {"eyeBrightnessSmooth.y", (double)eyeSkyI},
      };
      std::vector<char> done(irisU.size(), 0);
      for (int pass = 0; pass < (int)irisU.size() + 2; pass++) {
        bool progress = false;
        for (size_t ui = 0; ui < irisU.size(); ui++) {
          if (done[ui]) continue;
          auto v = evalIrisExpr(irisU[ui].expr, env);
          if (!v) continue;
          env[irisU[ui].name] = *v;
          done[ui] = 1;
          progress = true;
          if (irisU[ui].upload) customNow[irisU[ui].name] = (float)*v;
        }
        if (!progress) break;
      }
    }
    const mat4 pMVI = glm::inverse(pMV);
    const mat4 pPI = glm::inverse(pP);
    auto shadowDepthName = [](const std::string& n) {
      return n == "shadowtex0" || n == "shadowtex1" || n == "shadowtex0HW" || n == "watershadow";
    };
    // Iris held-light uniforms. The game's dyn lights only reach the built-in shader.
    int heldId = 0, heldLv = 0, heldId2 = 0, heldLv2 = 0;
    if (s.player) {
      auto take = [&](const ItemStack& st, int& id, int& lv) {
        if (st.empty()) return;
        lv = std::clamp(itemLight(st.id), 0, 15);
        if (auto it = packItems.find(st.id); it != packItems.end()) id = it->second;
      };
      const Player& pl = *s.player;
      if (pl.selected >= 0 && pl.selected < (int)pl.inv.size()) take(pl.inv[pl.selected], heldId, heldLv);
      if (!pl.offhand.empty()) take(pl.offhand[0], heldId2, heldLv2);
      if ((heldLv || heldLv2) && !toldHeld) {
        toldHeld = true;
        DQ_LOG("shader pack: held light %d (id %d) off %d (id %d)", heldLv, heldId, heldLv2, heldId2);
      }
    }
    auto bindUniforms = [&](IrisPass& P, bool geomDraw, bool flatAlbedo) -> bool {
      glUseProgram(P.prog);
      int unit = 0;
      bool fed = false;
      auto bindTex = [&](GLint loc, GLenum target, GLuint tex) {
        if (unit >= 31 || !tex) return;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target, tex);
        glUniform1i(loc, unit);
        unit++;
      };
      for (auto& [n, type] : P.uniforms) {
        const GLint loc = glGetUniformLocation(P.prog, n.c_str());
        if (loc < 0) continue;
        if (type == GL_SAMPLER_2D) {
          if (n == "dqEntityLut") { bindTex(loc, GL_TEXTURE_2D, entityLut ? entityLut : whiteTex); continue; }
          // A stage image wins over the buffer of the same name. deferred's colortex3 is the
          // cloud/water map; composite's colortex3 is the buffer the water pass wrote.
          if (int stg = texStageOf(P.name)) {
            if (GLuint img = stageSampler(stg, n)) { bindTex(loc, GL_TEXTURE_2D, img); continue; }
          }
          if (shadowDrew && shadowDepthName(n)) { bindTex(loc, GL_TEXTURE_2D, shTex); continue; }
          if (shadowDrew && n == "shadowcolor0") { bindTex(loc, GL_TEXTURE_2D, shColor ? shColor : blackTex); continue; }
          if (shadowDrew && n == "shadowcolor1") { bindTex(loc, GL_TEXTURE_2D, blackTex); continue; }
          if (geomDraw && (n == "tex" || n == "texture" || n == "gtexture")) {
            // Voxel models have no atlas UV. A white texel times gl_Color is the voxel's own colour.
            GLuint albedo = flatAlbedo && whiteTex ? whiteTex : (s.worldRenderer.blockAtlas() ? s.worldRenderer.blockAtlas() : blackTex);
            bindTex(loc, GL_TEXTURE_2D, albedo);
            continue;
          }
          if (geomDraw && (n == "normals" || n == "gnormal")) {
            bindTex(loc, GL_TEXTURE_2D, flatNormal ? flatNormal : whiteTex);
            continue;
          }
          int ct = colortexOf(n);
          if (ct == 0 || (ct > 0 && ct < 16 && wrote[ct])) fed = true;
          if (ct >= 0 && ct < 16) {
            GLuint slotTex = slot[ct][slotFront[ct]].tex;
            bindTex(loc, GL_TEXTURE_2D, slotTex);
          }
          else if (n == "depthtex1") bindTex(loc, GL_TEXTURE_2D, depthOk ? useDepth : whiteTex);
          else if (n == "depthtex0" || n == "depthtex2" || n == "gdepthtex")
            bindTex(loc, GL_TEXTURE_2D, (splitDepth && depthTrans) ? depthTrans : (depthOk ? useDepth : whiteTex));
          else if (n == "noisetex") bindTex(loc, GL_TEXTURE_2D, noiseTex ? noiseTex : whiteTex);
          else bindTex(loc, GL_TEXTURE_2D, blackTex);
          continue;
        }
        if (type == GL_SAMPLER_2D_ARRAY) {
          if (n == "uGbClimate") bindTex(loc, GL_TEXTURE_2D_ARRAY, s.worldRenderer.climateTexture());
          else if (n == "uGbTint") bindTex(loc, GL_TEXTURE_2D_ARRAY, s.worldRenderer.tintTexture());
          continue;
        }
        // Own units. Sharing unit 15 left the 3D sampler on a 2D texture, and the pass wrote black.
        // A drawn shadow map is sampled by both sampler2D and sampler2DShadow. Until that draw
        // happens, the gbuffer's compare sampler is lit (depth 1) and composites stay shadowed (depth 0).
        if (type == GL_SAMPLER_2D_SHADOW) {
          bindTex(loc, GL_TEXTURE_2D, shadowDrew && shadowDepthName(n) ? shTex : (geomDraw && shadowLit ? shadowLit : shadowDummy));
          continue;
        }
        if (type == GL_SAMPLER_3D) {
          // A stage image (Photon's atmosphere LUT on depthtex0) wins over the 1x1 dummy.
          if (int stg = texStageOf(P.name)) {
            if (GLuint img = stageSampler(stg, n, GL_TEXTURE_3D)) { bindTex(loc, GL_TEXTURE_3D, img); continue; }
          }
          bindTex(loc, GL_TEXTURE_3D, volDummy);
          continue;
        }
        auto f1 = [&](float v) { if (type == GL_FLOAT) glUniform1f(loc, v); else if (type == GL_INT || type == GL_BOOL) glUniform1i(loc, (int)v); };
        if (n == "viewWidth") f1((float)W); else if (n == "viewHeight") f1((float)H); else if (n == "aspectRatio") f1((float)W / H);
        else if (n == "frameTimeCounter") f1((float)std::fmod(now - startT, 3600.0)); else if (n == "frameTime") f1(ft);
        else if (n == "frameCounter") f1((float)(frames % 720720));
        else if (n == "near") f1(cam.nearZ); else if (n == "far") f1(std::max(64.f, cam.farZ));
        else if (n == "renderDistance") f1((float)std::max(32, s.app.settings.renderDist * 16));
        else if (n == "rainStrength" || n == "wetness") f1(f.rain);
        else if (n == "sunAngle" || n == "shadowAngle") f1(n == "sunAngle" ? sunAngle : shadowAngle);
        else if (n == "worldTime") f1((float)wt); else if (n == "worldDay") f1((float)s.level->info.day);
        else if (n == "moonPhase") f1((float)(s.level->info.day % 8));
        else if (n == "isEyeInWater") f1((float)f.eyeFluid);
        else if (n == "screenBrightness") f1(s.app.settings.brightness);
        else if (n == "eyeAltitude") f1((float)(cam.pos.y - dimBase(f.dim)));
        else if (n == "nightVision" || n == "blindness" || n == "darknessFactor" || n == "centerDepthSmooth") f1(n == "centerDepthSmooth" ? 0.98f : 0.f);
        else if (n == "heldBlockLightValue") f1((float)heldLv);
        else if (n == "heldBlockLightValue2") f1((float)heldLv2);
        else if (n == "heldItemId") f1((float)heldId);
        else if (n == "heldItemId2") f1((float)heldId2);
        else if (n == "uTime") f1((float)std::fmod(f.time, 3600.0));
        else if (n == "uWind") f1(f.wind);
        else if (n == "uSea") f1(f.sea);
        else if (n == "uAtlasGrid") f1((float)std::max(1, s.worldRenderer.atlasGrid()));
        else if (n == "uTileSize") f1((float)std::max(1, s.worldRenderer.atlasTileSize()));
        else if (n == "eyeBrightness" && type == GL_INT_VEC2) glUniform2i(loc, eyeBlock, eyeSky);
        else if (n == "eyeBrightnessSmooth" && type == GL_INT_VEC2) glUniform2i(loc, eyeBlockI, eyeSkyI);
        else if (n == "atlasSize" && (type == GL_INT_VEC2 || type == GL_FLOAT_VEC2)) {
          // Iris: the atlas in pixels. Generated normals divide by this. Zero made them a flat face.
          int side = std::max(1, s.worldRenderer.atlasGrid()) * std::max(1, s.worldRenderer.atlasTileSize());
          if (type == GL_INT_VEC2) glUniform2i(loc, side, side);
          else { float a[2] = {(float)side, (float)side}; glUniform2fv(loc, 1, a); }
        }
        else if (type == GL_FLOAT_VEC2 && n == "uGrassFade") {
          vec2 g = gfx.modern ? vec2(20.f, 34.f) : vec2(13.f, 22.f);
          glUniform2fv(loc, 1, &g.x);
        } else if (type == GL_FLOAT_VEC3) {
          vec3 v(0);
          if (n == "sunPosition" || n == "shadowLightPosition") v = f.sunDir.y > 0 || n == "sunPosition" ? sunV : moonV;
          else if (n == "moonPosition") v = moonV; else if (n == "upPosition") v = upV;
          else if (n == "cameraPosition") v = camPos;
          else if (n == "previousCameraPosition") v = prevPos;
          else if (n == "sun_dir" || n == "sunDir") v = glm::normalize(f.sunDir);
          else if (n == "moon_dir" || n == "moonDir") v = -glm::normalize(f.sunDir);
          else if (n == "light_dir" || n == "lightDir") {
            vec3 s = glm::normalize(f.sunDir);
            v = s.y >= 0.f ? s : -s;
          }
          else if (n == "skyColor") v = f.skyColor; else if (n == "fogColor") v = f.fogColor;
          else if (n == "uWorldOrigin") v = vec3(glm::mod(f.cam.origin, glm::dvec3(4096.0)));
          else if (n == "uEyeRel") v = eyeRel;
          glUniform3fv(loc, 1, &v.x);
        } else if (type == GL_FLOAT_MAT4) {
          const mat4* m = nullptr;
          if (n == "gbufferProjection") m = &pr;
          else if (n == "gbufferPreviousProjection") m = &pP;
          else if (n == "gbufferProjectionInverse") m = &pri;
          else if (n == "gbufferPreviousProjectionInverse") m = &pPI;
          else if (n == "gbufferModelView") m = &mv;
          else if (n == "gbufferPreviousModelView") m = &pMV;
          else if (n == "gbufferModelViewInverse") m = &mvi;
          else if (n == "gbufferPreviousModelViewInverse") m = &pMVI;
          else if (n == "shadowModelView") m = &shadowMV;
          else if (n == "shadowModelViewInverse") m = &shadowMVI;
          else if (n == "shadowProjection") m = &shadowP;
          else if (n == "shadowProjectionInverse") m = &shadowPI;
          else if (n == "dqMVP") m = &ortho;
          else if (n == "uViewProj") m = &cam.viewProj;
          static const mat4 I(1);
          glUniformMatrix4fv(loc, 1, GL_FALSE, &(m ? *m : I)[0][0]);
        } else if (auto it = customNow.find(n); it != customNow.end()) f1(it->second);
      }
      return fed;
    };
    auto runFullscreen = [&](IrisPass& P) {
      const bool toScreen = P.nOut == 0;
      if (toScreen) {
        bool missing = false;
        for (auto& [n, type] : P.uniforms) {
          if (type != GL_SAMPLER_2D) continue;
          int ct = colortexOf(n);
          if (ct > 0 && ct < 16 && !wrote[ct]) missing = true;
        }
        if (missing) return;
      }
      if (toScreen) {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outFbo);
        glViewport(vp[0], vp[1], W, H);
      } else {
        glBindFramebuffer(GL_FRAMEBUFFER, passFbo);
        GLenum att[8];
        for (int o = 0; o < P.nOut; o++) {
          int b = P.outs[o];
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, slot[b][1 - slotFront[b]].tex, 0);
          att[o] = GL_COLOR_ATTACHMENT0 + o;
        }
        for (int o = P.nOut; o < 8; o++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, 0, 0);
        glDrawBuffers(P.nOut, att);
        glViewport(0, 0, W, H);
      }
      const bool fed = bindUniforms(P, false, false);
      // The world depth buffer is still bound when final draws the window. A quad at
      // window depth 0.5 loses to the nearest block face (that face stayed a raw copy
      // on every pack). Depth test off, and ALWAYS in case the disable is ignored.
      GLint depthFn = GL_LEQUAL;
      glGetIntegerv(GL_DEPTH_FUNC, &depthFn);
      if (!depthFn) depthFn = GL_LEQUAL;
      glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND);
      glDepthFunc(GL_ALWAYS);
      if (std::getenv("DQ_UBO_DUMP")) {
        static int nPass = 0;
        if (nPass < 40) { nPass++; DQ_LOG("draw pass %s", P.name.c_str()); }
      }
      glBindVertexArray(quadVao); glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
      glDepthFunc((GLenum)depthFn);
      if (dumpFrame) {
        GLint dv[4]; glGetIntegerv(GL_VIEWPORT, dv);
        DQ_LOG("pack dump %s viewport %d,%d %dx%d outs %d", P.name.c_str(), dv[0], dv[1], dv[2], dv[3], P.nOut);
      }
      if (toScreen) finalDrawn = true;
      else {
        for (int o = 0; o < P.nOut; o++) {
          int b = P.outs[o];
          slotFront[b] = 1 - slotFront[b];
          if (fed) wrote[b] = true;
          if (b == 0 && fed) wroteScene = true;
        }
        // Complementary's composite6 writes the blended picture to colortex3 and the same value to
        // colortex2 (the TAA history). Copy onto the side the next frame reads.
        if (P.name == "composite6" && fed && P.nOut > 0 && P.outs[0] == 3 && slot[2][slotFront[2]].fbo) {
          glBindFramebuffer(GL_READ_FRAMEBUFFER, slot[3][slotFront[3]].fbo);
          glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot[2][slotFront[2]].fbo);
          glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        for (int o = 0; o < P.nOut; o++) genMips(P.outs[o]);
        if (dumpFrame && P.nOut > 0) {
          int b = P.outs[0];
          packDump((P.name + "-ct" + std::to_string(b)).c_str(), slot[b][slotFront[b]].tex, W, H);
        }
      }
    };
    if (gshadow.prog && s.worldRenderer.blockAtlas()) {
      if (!ensureShadowMap(shRes)) {
        foldShadow();
        if (!toldShadow) { toldShadow = true; DQ_WARN("shader pack: shadow framebuffer incomplete"); }
      } else {
        glBindFramebuffer(GL_FRAMEBUFFER, shFbo);
        glViewport(0, 0, shAlloc, shAlloc);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // shadowDrew stays false here, so the caster reads the atlas instead of the map it is writing.
        bindUniforms(gshadow, true, false);
        GLint depthFn = GL_LEQUAL;
        glGetIntegerv(GL_DEPTH_FUNC, &depthFn);
        if (!depthFn) depthFn = GL_LEQUAL;
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        const float reach = shHalf * 1.5f + 32.f;
        s.worldRenderer.drawShadowSections(f, 0, reach);
        s.worldRenderer.drawShadowSections(f, 1, reach);
        if (gshadowEnt.prog && !packVoxelQueue().empty()) {
          bindUniforms(gshadowEnt, true, true);
          glEnable(GL_DEPTH_TEST);
          glDepthFunc(GL_LEQUAL);
          glDepthMask(GL_TRUE);
          glDisable(GL_BLEND);
          glDisable(GL_CULL_FACE);
          GLint locM = glGetUniformLocation(gshadowEnt.prog, "dqEntModel");
          GLint locL = glGetUniformLocation(gshadowEnt.prog, "dqEntLight");
          GLint locT = glGetUniformLocation(gshadowEnt.prog, "dqEntTint");
          int shadowEnts = 0;
          for (const PackVoxelDraw& d : packVoxelQueue()) {
            if (!d.mesh) continue;
            if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, &d.model[0][0]);
            if (locL >= 0) glUniform2f(locL, d.sky, d.block);
            if (locT >= 0) glUniform4fv(locT, 1, &d.tint.x);
            if (d.mirrored) glFrontFace(GL_CW);
            d.mesh->draw();
            if (d.mirrored) glFrontFace(GL_CCW);
            shadowEnts++;
          }
          if (!toldEntShadow) {
            toldEntShadow = true;
            DQ_LOG("shader pack: entity shadow drew %d voxel models", shadowEnts);
          }
        }
        glDepthFunc((GLenum)depthFn);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        shadowDrew = true;
        if (!toldShadow) { toldShadow = true; DQ_LOG("shader pack: shadow drew %d over %.0f blocks", shAlloc, shHalf); }
      }
    }
    // Set when the hand is drawn into the gbuffer deferred reads. The late splat is then
    // the packed gbuffer, which Photon shows as a magenta arm.
    bool handDrawn = false;
    if (gbuf.prog && s.worldRenderer.blockAtlas() && depthOk) {
      for (auto& P : irisBegin) runFullscreen(P);
      for (auto& P : irisPrep) runFullscreen(P);
      // prepare may have replaced the sky. Put the game picture back; the gbuffer overwrites terrain only.
      copyFrame(slot[0][slotFront[0]], vp, outFbo);
      if (sceneBuf > 0 && sceneBuf < 16) copyFrame(slot[sceneBuf][slotFront[sceneBuf]], vp, outFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, passFbo);
      GLenum att[8];
      for (int o = 0; o < gbuf.nOut; o++) {
        int b = gbuf.outs[o];
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, slot[b][slotFront[b]].tex, 0);
        att[o] = GL_COLOR_ATTACHMENT0 + o;
      }
      for (int o = gbuf.nOut; o < 8; o++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, 0, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
      glDrawBuffers(gbuf.nOut, att);
      glViewport(0, 0, W, H);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        bindUniforms(gbuf, true, false);
        GLint depthFn = GL_LEQUAL;
        glGetIntegerv(GL_DEPTH_FUNC, &depthFn);
        if (!depthFn) depthFn = GL_LEQUAL;
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        if (std::getenv("DQ_UBO_DUMP")) {
          static bool once = false;
          if (!once) { once = true; DQ_LOG("draw pass %s", gbuf.name.c_str()); }
        }
        s.worldRenderer.drawSections(f, 0);
        s.worldRenderer.drawSections(f, 1);
        // The game already wrote these models into the depth copy. Replace their colour in place.
        if (gentity.prog && !packVoxelQueue().empty()) {
          GLenum eatt[8];
          for (int o = 0; o < gentity.nOut; o++) {
            int b = gentity.outs[o];
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, slot[b][slotFront[b]].tex, 0);
            eatt[o] = GL_COLOR_ATTACHMENT0 + o;
          }
          for (int o = gentity.nOut; o < 8; o++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, 0, 0);
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
          glDrawBuffers(gentity.nOut, eatt);
          if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            bindUniforms(gentity, true, true);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_EQUAL);
            glDepthMask(GL_FALSE);
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
            GLint locM = glGetUniformLocation(gentity.prog, "dqEntModel");
            GLint locL = glGetUniformLocation(gentity.prog, "dqEntLight");
            GLint locT = glGetUniformLocation(gentity.prog, "dqEntTint");
            int drew = 0;
            for (const PackVoxelDraw& d : packVoxelQueue()) {
              if (!d.mesh) continue;
              if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, &d.model[0][0]);
              if (locL >= 0) glUniform2f(locL, d.sky, d.block);
              if (locT >= 0) glUniform4fv(locT, 1, &d.tint.x);
              if (d.mirrored) glFrontFace(GL_CW);
              d.mesh->draw();
              if (d.mirrored) glFrontFace(GL_CCW);
              drew++;
            }
            for (int o = 0; o < gentity.nOut; o++) wrote[gentity.outs[o]] = true;
            if (drew != entityDrew) {
              entityDrew = drew;
              DQ_LOG("shader pack: %s drew %d voxel models", gentity.name.c_str(), drew);
            }
          } else if (!toldEntity) {
            toldEntity = true;
            DQ_WARN("shader pack: entity framebuffer incomplete");
          }
        }
        // Iris: deferred is after opaque gbuffers and before translucent ones. It writes the
        // cloud depth gbuffers_water tests. Running it afterwards replaces the water colour.
        // A hand whose buffers deferred only reads (Photon's colortex1) has to be in that
        // gbuffer now. Deferred writes the shaded arm. Drawing the gbuffer afterwards is magenta.
        if (ghand.prog && ghand.nOut > 0 && !packHandPrev().draws.empty()) {
          bool read = false, overwritten = false;
          for (const IrisPass& P : irisDef) {
            for (const auto& u : P.uniforms) {
              if (u.second != GL_SAMPLER_2D) continue;
              int ct = colortexOf(u.first);
              for (int h = 0; h < ghand.nOut; h++) if (ct == ghand.outs[h]) read = true;
            }
            for (int o = 0; o < P.nOut; o++)
              for (int h = 0; h < ghand.nOut; h++) if (P.outs[o] == ghand.outs[h]) overwritten = true;
          }
          if (read && !overwritten) {
            const PackHandFrame& hand = packHandPrev();
            GLenum hatt[8];
            for (int o = 0; o < ghand.nOut; o++) {
              int b = ghand.outs[o];
              glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, slot[b][slotFront[b]].tex, 0);
              hatt[o] = GL_COLOR_ATTACHMENT0 + o;
            }
            for (int o = ghand.nOut; o < 8; o++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, 0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
            glDrawBuffers(ghand.nOut, hatt);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
              bindUniforms(ghand, true, true);
              // The recorded pose is view space. The hand's own projection (near 0.01, far 10)
              // writes a window depth behind a nearby wall in the world buffer (near 0.05, far
              // huge), so the arm vanished indoors. The world projection keeps it in front.
              auto setM = [&](const char* n, const mat4& m) {
                GLint loc = glGetUniformLocation(ghand.prog, n);
                if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &m[0][0]);
              };
              setM("gbufferProjection", pr);
              setM("gbufferProjectionInverse", pri);
              setM("uViewProj", pr);
              GLint eyeLoc = glGetUniformLocation(ghand.prog, "uEyeRel");
              if (eyeLoc >= 0) glUniform3f(eyeLoc, 0.f, 0.f, 0.f);
              glEnable(GL_DEPTH_TEST);
              glDepthFunc(GL_LEQUAL);
              glDepthMask(GL_TRUE);
              glDisable(GL_BLEND);
              glEnable(GL_CULL_FACE);
              GLint locM = glGetUniformLocation(ghand.prog, "dqEntModel");
              GLint locL = glGetUniformLocation(ghand.prog, "dqEntLight");
              GLint locT = glGetUniformLocation(ghand.prog, "dqEntTint");
              int drew = 0;
              for (const PackVoxelDraw& d : hand.draws) {
                if (!d.mesh) continue;
                if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, &d.model[0][0]);
                if (locL >= 0) glUniform2f(locL, d.sky, d.block);
                if (locT >= 0) glUniform4fv(locT, 1, &d.tint.x);
                if (d.mirrored) glFrontFace(GL_CW);
                d.mesh->draw();
                if (d.mirrored) glFrontFace(GL_CCW);
                drew++;
              }
              glDepthMask(GL_FALSE);
              for (int o = 0; o < ghand.nOut; o++) wrote[ghand.outs[o]] = true;
              handDrawn = true;
              if (!toldHand) {
                toldHand = true;
                DQ_LOG("shader pack: hand drawing %d meshes into the gbuffer before deferred", drew);
              }
            }
          }
        }
        for (int o = 0; o < gbuf.nOut; o++) { wrote[gbuf.outs[o]] = true; genMips(gbuf.outs[o]); }
        if (gentity.prog) for (int o = 0; o < gentity.nOut; o++) genMips(gentity.outs[o]);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        for (auto& P : irisDef) runFullscreen(P);
        // The opaque redraw replaced water pixels with the seabed. gbuffers_water blends the
        // surface back on top and writes its depth into a second image, so depthtex0 and depthtex1 differ.
        if (gwater.prog && transCopy) {
          GLenum watt[8];
          for (int o = 0; o < gwater.nOut; o++) {
            int b = gwater.outs[o];
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, slot[b][slotFront[b]].tex, 0);
            watt[o] = GL_COLOR_ATTACHMENT0 + o;
          }
          for (int o = gwater.nOut; o < 8; o++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, 0, 0);
          // depthTex just passed the opaque EQUAL test, so it holds the land. Copy that,
          // not the window depth, into the image this pass tests and writes.
          {
            static GLuint depthBlitFbo = 0;
            if (!depthBlitFbo) glGenFramebuffers(1, &depthBlitFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, depthBlitFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, depthBlitFbo);
            glBindTexture(GL_TEXTURE_2D, depthTrans);
            glGetError();
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, W, H);
            transCopy = glGetError() == GL_NO_ERROR;
          }
          glBindFramebuffer(GL_FRAMEBUFFER, passFbo);
          if (transCopy) {
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTrans, 0);
          glDrawBuffers(gwater.nOut, watt);
          if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            bindUniforms(gwater, true, false);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            if (gwater.blend) {
              glEnable(GL_BLEND);
              glBlendFuncSeparate(gwater.bSrc, gwater.bDst, gwater.bSrcA, gwater.bDstA);
              for (int o = 0; o < gwater.nOut && o < 8; o++) {
                if (gwater.blendOff & (1u << gwater.outs[o])) glDisablei(GL_BLEND, (GLuint)o);
                else glEnablei(GL_BLEND, (GLuint)o);
              }
            } else glDisable(GL_BLEND);
            s.worldRenderer.drawSections(f, 2);
            glDisable(GL_BLEND);
            glDepthMask(GL_FALSE);
            splitDepth = true;
            for (int o = 0; o < gwater.nOut; o++) wrote[gwater.outs[o]] = true;
            if (!toldWater) {
              toldWater = true;
              DQ_LOG("shader pack: %s drew translucent (blend %s, off %u)", gwater.name.c_str(), gwater.blend ? "on" : "off", gwater.blendOff);
            }
          } else if (!toldWater) {
            toldWater = true;
            DQ_WARN("shader pack: water framebuffer incomplete");
          }
          }
        }
        glDisable(GL_CULL_FACE);
        glDepthFunc((GLenum)depthFn);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        if (dumpFrame && gbuf.nOut > 0)
          packDump("gbuffer-premip", slot[gbuf.outs[0]][slotFront[gbuf.outs[0]]].tex, W, H);
        for (int o = 0; o < gbuf.nOut; o++) { wrote[gbuf.outs[o]] = true; genMips(gbuf.outs[o]); }
        if (splitDepth) for (int o = 0; o < gwater.nOut; o++) genMips(gwater.outs[o]);
        if (gentity.prog) for (int o = 0; o < gentity.nOut; o++) genMips(gentity.outs[o]);
        wroteScene = true;
        if (dumpFrame && gbuf.nOut > 0) packDump("gbuffer-ct0", slot[gbuf.outs[0]][slotFront[gbuf.outs[0]]].tex, W, H);
        if (!toldGeom) { toldGeom = true; DQ_LOG("shader pack: %s drew terrain", gbuf.name.c_str()); }
      } else if (!toldGeom) {
        toldGeom = true;
        DQ_WARN("shader pack: gbuffer framebuffer incomplete");
      }
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    }
    // The hand's depth stays on its own image. Final reads a color buffer that no longer
    // consults depth, so the arm is tonemapped with the scene and cannot be reconstructed as sky.
    // handDrawn is set above when deferred shades the hand from its gbuffer.
    auto drawPackHand = [&](int target) {
      const PackHandFrame& hand = packHandPrev();
      if (handDrawn || !ghand.prog || hand.draws.empty()) return;
      if (target < 0 || target > 15 || !slot[target][slotFront[target]].tex) return;
      if (!handDepth || handW != W || handH != H) {
        if (handDepth) glDeleteTextures(1, &handDepth);
        glGenTextures(1, &handDepth);
        glBindTexture(GL_TEXTURE_2D, handDepth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, W, H, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        handW = W;
        handH = H;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, passFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot[target][slotFront[target]].tex, 0);
      for (int o = 1; o < 8; o++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + o, GL_TEXTURE_2D, 0, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, handDepth, 0);
      GLenum one = GL_COLOR_ATTACHMENT0;
      glDrawBuffers(1, &one);
      glViewport(0, 0, W, H);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        packHandSetSuppress(false);
        if (!toldHand) { toldHand = true; DQ_WARN("shader pack: hand framebuffer incomplete"); }
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        return;
      }
      glDepthMask(GL_TRUE);
      glClear(GL_DEPTH_BUFFER_BIT);
      bindUniforms(ghand, true, true);
      // bindUniforms uploaded the camera rotation and the world projection. The recorded
      // matrices are view space, so only the projection and the eye offset change: the sun
      // vector and ViewToPlayer keep using the camera.
      const mat4 ip = glm::inverse(hand.proj);
      auto setM = [&](const char* n, const mat4& m) {
        GLint loc = glGetUniformLocation(ghand.prog, n);
        if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &m[0][0]);
      };
      setM("gbufferProjection", hand.proj);
      setM("gbufferProjectionInverse", ip);
      setM("uViewProj", hand.proj);
      GLint eye = glGetUniformLocation(ghand.prog, "uEyeRel");
      if (eye >= 0) glUniform3f(eye, 0.f, 0.f, 0.f);
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LEQUAL);
      glDisable(GL_BLEND);
      glEnable(GL_CULL_FACE);
      GLint locM = glGetUniformLocation(ghand.prog, "dqEntModel");
      GLint locL = glGetUniformLocation(ghand.prog, "dqEntLight");
      GLint locT = glGetUniformLocation(ghand.prog, "dqEntTint");
      if (!toldHand)
        DQ_LOG("shader pack: hand drawing %d meshes into colortex%d", (int)hand.draws.size(), target);
      int drew = 0;
      for (const PackVoxelDraw& d : hand.draws) {
        if (!d.mesh) continue;
        if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, &d.model[0][0]);
        if (locL >= 0) glUniform2f(locL, d.sky, d.block);
        if (locT >= 0) glUniform4fv(locT, 1, &d.tint.x);
        if (d.mirrored) glFrontFace(GL_CW);
        d.mesh->draw();
        if (d.mirrored) glFrontFace(GL_CCW);
        drew++;
      }
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
      handDrawn = true;
      if (!toldHand) {
        toldHand = true;
        DQ_LOG("shader pack: %s drew %d hand meshes into colortex%d", ghand.name.c_str(), drew, target);
      }
    };
    auto handTargetOf = [&](const IrisPass& P) {
      // The first sampler can be an aux buffer (MakeUp's gaux3 is exposure). The arm
      // belongs on the hand's own colour target when this pass reads it, else the scene.
      bool seen[16] = {};
      int best = -1;
      bool primary = false, scene = false;
      for (auto& u : P.uniforms) {
        if (u.second != GL_SAMPLER_2D) continue;
        int ct = colortexOf(u.first);
        if (ct < 0 || ct >= 16 || !wrote[ct] || seen[ct]) continue;
        seen[ct] = true;
        if (ghand.nOut > 0 && ct == ghand.outs[0]) primary = true;
        if (ct == sceneBuf) scene = true;
        if (best < 0 || ct < best) best = ct;
      }
      if (primary) return ghand.outs[0];
      if (scene) return sceneBuf;
      return best >= 0 ? best : 0;
    };
    for (auto& P : iris) {
      if (P.nOut == 0) drawPackHand(handTargetOf(P));
      runFullscreen(P);
    }
    if (dumpFrame) packDumpDone = true;
    prevCam = camPos; prevMV = mv; prevP = pr; havePrev = true;
    // Horizon shows colortex0 when the pack has no final. A pack that has one, but it did not
    // draw, keeps the picture: its other passes are intermediates (a linear colortex0, a bloom buffer).
    if (!handDrawn) drawPackHand(0);
    if (!finalDrawn && !packHasFinal && wroteScene) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, slot[0][slotFront[0]].fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)outFbo);
      glBlitFramebuffer(0, 0, W, H, vp[0], vp[1], vp[0] + W, vp[1] + H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      finalDrawn = true;
    }
    if (!toldShow) {
      toldShow = true;
      if (auto it = customNow.find("dayNightMix"); it != customNow.end())
        DQ_LOG("shader pack: dayNightMix %.3f volumetricDayMixer %.3f (%d custom uniforms)",
               it->second, customNow.count("volumetricDayMixer") ? customNow["volumetricDayMixer"] : 0.f, (int)customNow.size());
      if (finalDrawn && packHasFinal) DQ_LOG("shader pack: final drew the screen");
      else if (finalDrawn) DQ_LOG("shader pack: no final, colortex0 drew the screen");
      else DQ_LOG("shader pack: picture unchanged (final did not run)");
    }
    packVoxelClear();
    glUseProgram(0);
    if (scissor) glEnable(GL_SCISSOR_TEST);
  }
};

// Built-in grade, bloom and sun rays. After the hand, so the arm is in the picture. The pack is not:
// it has already drawn underneath the arm.
class GradeLayer : public RenderLayer {
public:
  const char* name() const override { return "grade"; }
  int order() const override { return 4900; }
  void drawOverlay(GameSession& s, const FrameCtx& f) override {
    if (f.portalDepth > 0) return;
    const int level = std::clamp(s.app.settings.extra.value("shaderLevel", 0), 0, 4);
    if (level <= 0) return;
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 16 || vp[3] <= 16) return;
    GLint drawFbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    if (!vao) glGenVertexArrays(1, &vao);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE);
    run(s, f, level, vp, drawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindVertexArray(0); glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE);
  }
private:
  GLuint vao = 0;
  bool tried = false;
  std::unique_ptr<Shader> post;
  Target scene, bloomA, bloomB;
  void tri() { glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); }
  void run(GameSession& s, const FrameCtx& f, int level, const GLint* vp, GLint outFbo) {
    if (!tried) { tried = true; post = std::make_unique<Shader>(); if (!post->load("post.glsl", "post.glsl")) post.reset(); }
    if (!post) return;
    const int W = vp[2], H = vp[3], qw = std::max(1, W / 4), qh = std::max(1, H / 4);
    scene.make(W, H, true); bloomA.make(qw, qh, true); bloomB.make(qw, qh, true);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)outFbo);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, scene.tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    static const float BLOOM[5] = {0, 0, 0.32f, 0.42f, 0.55f}, RAYS[5] = {0, 0, 0, 0.55f, 0.75f}, SHARP[5] = {0, 0, 0.08f, 0.15f, 0.22f};
    static const int STEPS[5] = {0, 0, 0, 24, 40};
    post->use();
    post->set("uSrc", 0); post->set("uBloom", 1);
    glActiveTexture(GL_TEXTURE0);
    const bool needBloom = BLOOM[level] > 0 || RAYS[level] > 0;
    if (needBloom) {
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
};

void registerPostLayer() {
  registerRenderLayer([](GameSession&) { return std::make_shared<PostLayer>(); });
  registerRenderLayer([](GameSession&) { return std::make_shared<GradeLayer>(); });
}

} // namespace dq
