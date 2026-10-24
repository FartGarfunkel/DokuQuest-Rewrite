// assets/gear3d.cpp — the look in a skin's spare pixels, and the shapes of hair, beards, hats, clothes and
// armour pieces (render/gear3d.js), with the same numbers.
#include "assets/gear3d.h"
#include "assets/voxel_paint.h"
#include "assets/tileset.h"
#include "common/noise.h"

namespace dq {

namespace {
const std::vector<std::string> SPECIES = {"human", "elf", "dwarf", "halfling", "gnome", "dragonborn", "orc", "tiefling", "goliath", "sunmane", "kestrin", "mossborn", "lumini", "tidecursed", "veilborn"};
const std::vector<std::string> HAIRS = {"bald", "short", "crop", "long", "tail", "braids", "mohawk", "bun", "curly", "sides", "wild"};
const std::vector<std::string> BEARDS = {"none", "stubble", "short", "long", "braided", "moustache", "wizard", "forked"};
const std::vector<std::string> HORNS = {"none", "curled", "ram", "straight", "small"};
const std::vector<std::string> EARS = {"round", "pointed", "long"};
const std::vector<std::string> OUTFITS = {"gambeson", "furs", "doublet", "vestments", "leafrobe", "wraps", "surcoat", "ranger", "shadow", "silks", "pact", "archrobe", "traveller", "noble", "peasant"};
const std::vector<std::string> HATS = {"none", "wizard", "feather", "circlet", "crown", "straw", "cap", "bucket", "chef"};
bool isRobe(const std::string& o) { return o == "archrobe" || o == "pact" || o == "silks" || o == "vestments" || o == "leafrobe"; }
const int AT_X = 56, AT_Y = 16, NPX = 15;
int ix(const std::vector<std::string>& l, const std::string& v) { for (size_t i = 0; i < l.size(); i++) if (l[i] == v) return (int)i; return 0; }
}

static std::vector<int> gearBytes(const GearLook& L) {
  int out = -1; for (size_t i = 0; i < OUTFITS.size(); i++) if (OUTFITS[i] == L.outfit) out = (int)i;
  bool hood = L.hood || L.hat == "hood";
  int f1 = (hood ? 1 : 0) | (L.cloak ? 2 : 0) | (L.tusks ? 4 : 0) | (L.snout ? 8 : 0) | (L.tail ? 16 : 0) | (L.scales ? 32 : 0) | (L.nose ? 64 : 0) | (L.slim ? 128 : 0);
  int f2 = (L.robe ? 1 : 0) | (L.apron ? 2 : 0) | (L.tabard ? 4 : 0) | (L.helmHide ? 8 : 0) | (L.feather ? 16 : 0) | (L.elder ? 32 : 0) | (L.gloves ? 64 : 0) | (L.beltPick ? 128 : 0);
  std::vector<int> b = {71, 82, 1, ix(SPECIES, L.species), ix(HAIRS, L.hair), ix(BEARDS, L.beard), ix(HORNS, L.horns), ix(EARS, L.ears), out < 0 ? 255 : out, hood ? 0 : ix(HATS, L.hat), f1, f2};
  for (vec3 c : {L.hairCol, L.beardCol, L.hornCol, L.skin, L.cols[0], L.cols[1], L.cols[2], L.cols[3], L.extra, L.hatCol})
    for (int k = 0; k < 3; k++) b.push_back(clampv((int)std::lround(c[k]), 0, 255));
  int s = 0; for (int v : b) s = (s + v) & 255;
  b.push_back(s);
  return b;
}

void gearWrite(u8* px, const GearLook& L) {
  auto b = gearBytes(L);
  for (int i = 0; i < NPX; i++) {
    u8* o = px + ((size_t)(AT_Y + (i >> 3)) * 64 + AT_X + (i % 8)) * 4;
    for (int k = 0; k < 3; k++) o[k] = (u8)(i * 3 + k < (int)b.size() ? b[i * 3 + k] : 0);
    o[3] = 255;
  }
}

std::optional<GearLook> gearRead(const u8* px) {
  if (!px) return std::nullopt;
  std::vector<int> b;
  for (int i = 0; i < NPX; i++) {
    const u8* o = px + ((size_t)(AT_Y + (i >> 3)) * 64 + AT_X + (i % 8)) * 4;
    if (o[3] != 255) return std::nullopt;
    b.push_back(o[0]); b.push_back(o[1]); b.push_back(o[2]);
  }
  if (b[0] != 71 || b[1] != 82 || b[2] != 1) return std::nullopt;
  int s = 0; for (int i = 0; i < 42; i++) s = (s + b[i]) & 255;
  if (s != b[42]) return std::nullopt;
  auto C = [&](int i) { return vec3(b[i], b[i + 1], b[i + 2]); };
  auto at = [](const std::vector<std::string>& l, int i, const char* d) { return i >= 0 && i < (int)l.size() ? l[i] : std::string(d); };
  GearLook L;
  int f1 = b[10], f2 = b[11];
  L.species = at(SPECIES, b[3], "human"); L.hair = at(HAIRS, b[4], "bald"); L.beard = at(BEARDS, b[5], "none"); L.horns = at(HORNS, b[6], "none"); L.ears = at(EARS, b[7], "round");
  L.outfit = b[8] == 255 ? "" : at(OUTFITS, b[8], ""); L.hat = at(HATS, b[9], "none");
  L.hood = f1 & 1; L.cloak = f1 & 2; L.tusks = f1 & 4; L.snout = f1 & 8; L.tail = f1 & 16; L.scales = f1 & 32; L.nose = f1 & 64; L.slim = f1 & 128;
  L.robe = (f2 & 1) || isRobe(L.outfit); L.apron = f2 & 2; L.tabard = f2 & 4; L.helmHide = f2 & 8; L.feather = f2 & 16; L.elder = f2 & 32; L.gloves = f2 & 64; L.beltPick = f2 & 128;
  L.hairCol = C(12); L.beardCol = C(15); L.hornCol = C(18); L.skin = C(21); L.cols = {C(24), C(27), C(30), C(33)}; L.extra = C(36); L.hatCol = C(39);
  for (int i = 3; i < 12; i++) L.shape += (i > 3 ? "," : "") + std::to_string(b[i]);
  for (int i = 0; i < 42; i++) L.code += (i ? "," : "") + std::to_string(b[i]);
  return L;
}

// ------------------------------------------------------------------ the shape kit
namespace {
using Pts = std::array<vec3, 8>;
inline int gci(int cx, int cy, int cz) { return cx * 4 + cy * 2 + cz; }
Pts prismPts(float y0, vec4 r0, float y1, vec4 r1) {   // r = [x0, z0, x1, z1]: the bottom face at y0, the top at y1
  Pts P;
  for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) for (int cz = 0; cz < 2; cz++) {
    vec4 r = cy ? r1 : r0;
    P[gci(cx, cy, cz)] = vec3(cx ? r.z : r.x, cy ? y1 : y0, cz ? r.w : r.y);
  }
  return P;
}
Pts boxPts(vec3 a, vec3 b, vec2 t0, vec2 t1, vec2 sh) {   // narrowed toward the bottom (t0) or top (t1) about its middle, its top shifted by sh
  float cx = (a.x + b.x) / 2, cz = (a.z + b.z) / 2, hx = (b.x - a.x) / 2, hz = (b.z - a.z) / 2;
  auto r = [&](vec2 t, vec2 s) { return vec4(cx - hx * t.x + s.x, cz - hz * t.y + s.y, cx + hx * t.x + s.x, cz + hz * t.y + s.y); };
  return prismPts(a.y, r(t0, vec2(0)), b.y, r(t1, sh));
}
Pts segPts(vec3 p0, vec3 p1, vec2 W0, vec2 W1, vec3 ref) {   // a square-ish rod from p0 to p1, w = [across, deep]
  vec3 d = glm::normalize(p1 - p0);
  vec3 A = ref; if (std::abs(glm::dot(d, A)) > 0.95f) A = vec3(1, 0, 0);
  vec3 u = glm::normalize(glm::cross(d, A)), v = glm::cross(u, d);
  Pts P;
  for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) for (int cz = 0; cz < 2; cz++) {
    vec3 p = cy ? p1 : p0; vec2 W = cy ? W1 : W0;
    float su = (cx ? 0.5f : -0.5f) * W.x, sv = (cz ? 0.5f : -0.5f) * W.y;
    P[gci(cx, cy, cz)] = p + u * su + v * sv;
  }
  return P;
}
struct Rot { char axis; float a; vec3 pv; };
Pts rot(Pts P, const Rot& r) {
  float c = std::cos(r.a), s = std::sin(r.a);
  for (auto& p : P) {
    vec3 q = p - r.pv;
    if (r.axis == 'x') p = vec3(p.x, r.pv.y + q.y * c - q.z * s, r.pv.z + q.y * s + q.z * c);
    else if (r.axis == 'y') p = vec3(r.pv.x + q.x * c + q.z * s, p.y, r.pv.z - q.x * s + q.z * c);
    else p = vec3(r.pv.x + q.x * c - q.y * s, r.pv.y + q.x * s + q.y * c, p.z);
  }
  return P;
}
const std::map<std::string, std::string> MIRROR = {{"armR", "armL"}, {"foreR", "foreL"}, {"legR", "legL"}, {"shinR", "shinL"}, {"armL", "armR"}, {"foreL", "foreR"}, {"legL", "legR"}, {"shinL", "shinR"}};
GearShape mirror(const GearShape& s) {
  GearShape m = s;
  for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) for (int cz = 0; cz < 2; cz++) { vec3 q = s.pts[gci(1 - cx, cy, cz)]; m.pts[gci(cx, cy, cz)] = vec3(-q.x, q.y, q.z); }
  auto it = MIRROR.find(s.part); if (it != MIRROR.end()) m.part = it->second;
  if (m.fm.count(0) || m.fm.count(1)) { auto f0 = s.fm.count(0) ? s.fm.at(0) : "", f1 = s.fm.count(1) ? s.fm.at(1) : ""; m.fm.erase(0); m.fm.erase(1); if (!f1.empty()) m.fm[0] = f1; if (!f0.empty()) m.fm[1] = f0; }
  return m;
}
struct O { const char* part = nullptr; const char* tag = nullptr; bool mirror = false; vec2 t0{1, 1}, t1{1, 1}, sh{0, 0}; std::vector<Rot> r; std::map<int, std::string> fm; vec3 ref{0, 0, 1}; };
struct Kit {
  std::vector<GearShape>& S;
  const std::map<std::string, bool>* show;
  void put(const std::string& part, const std::string& mat, Pts P, const O& o) {
    if (o.tag && show) { auto it = show->find(o.tag); if (it != show->end() && !it->second) return; }
    for (auto& r : o.r) P = rot(P, r);
    GearShape s{part, mat, P, o.fm};
    S.push_back(s);
    if (o.mirror) S.push_back(mirror(s));
  }
  void box(const std::string& part, const std::string& mat, vec3 a, vec3 b, const O& o = {}) { put(part, mat, boxPts(a, b, o.t0, o.t1, o.sh), o); }
  void prism(const std::string& part, const std::string& mat, float y0, vec4 r0, float y1, vec4 r1, const O& o = {}) { put(part, mat, prismPts(y0, r0, y1, r1), o); }
  void seg(const std::string& part, const std::string& mat, vec3 p0, vec3 p1, vec2 w0, vec2 w1, const O& o = {}) { put(part, mat, segPts(p0, p1, w0, w1, o.ref), o); }
  void seg(const std::string& part, const std::string& mat, vec3 p0, vec3 p1, float w0, float w1, const O& o = {}) { seg(part, mat, p0, p1, vec2(w0), vec2(w1), o); }
};
}

std::vector<GearShape> gearShapes(const GearLook& L, float aw, bool helm, bool chest) {
  std::vector<GearShape> S;
  bool hood = L.hood && !helm;
  std::string hat = helm ? "none" : L.hat;
  bool hatCovers = hat == "wizard" || hat == "feather" || hat == "straw" || hat == "cap" || hat == "bucket" || hat == "chef";
  std::map<std::string, bool> show = {{"top", !helm && !hood && !hatCovers}, {"side", !helm && !hood}, {"fringe", !helm}, {"hang", !hood}, {"sh", !chest}, {"cuff", !chest && !L.gloves}, {"belt", !chest && !L.beltPick}};
  Kit K{S, &show};
  const float a0 = 4, a1 = 4 + aw;
  u32 sd = 17; for (char ch : L.shape) sd = sd * 31 + (u8)ch;
  Mulberry32 R(sd ? sd : 1);

  // ---- hair ----
  const std::string& hs = L.hair;
  auto H = [&](vec3 a, vec3 b, O o = {}) { K.box(o.part ? o.part : "head", "hair", a, b, o); };
  auto cap = [&] { H({-4.45f, 31.1f, -4.45f}, {4.45f, 32.75f, 4.45f}, {.tag = "top"}); };
  auto back = [&](float y0 = 25.6f, float z1 = 4.6f) { H({-4.45f, y0, 3.55f}, {4.45f, 31.2f, z1}, {.tag = "side"}); };
  auto sides = [&](float y0 = 27.6f) { H({-4.62f, y0, -3.4f}, {-3.55f, 31.2f, 4.5f}, {.tag = "side", .mirror = true}); };
  auto fringe = [&] { H({-4.45f, 30.3f, -4.75f}, {4.45f, 31.5f, -3.7f}, {.tag = "fringe"}); };
  if (hs == "short") { cap(); back(); sides(); fringe(); H({-2.6f, 29.6f, -4.9f}, {0.9f, 30.5f, -4.2f}, {.tag = "fringe"}); }
  else if (hs == "crop") { H({-4.3f, 31.5f, -4.3f}, {4.3f, 32.5f, 4.3f}, {.tag = "top"}); H({-4.3f, 28.6f, 3.6f}, {4.3f, 31.6f, 4.35f}, {.tag = "side"}); H({-4.3f, 30.8f, -4.5f}, {4.3f, 31.6f, -3.8f}, {.tag = "fringe"}); }
  else if (hs == "long") {
    cap(); back(24.2f, 4.9f); fringe();
    H({-4.78f, 24.4f, -2.6f}, {-3.55f, 31.2f, 4.6f}, {.tag = "side", .mirror = true});
    H({-4.6f, 17.0f, 2.25f}, {4.6f, 24.6f, 3.9f}, {.part = "neck", .tag = "hang", .t0 = {1.05f, 1}});
  } else if (hs == "tail") {
    cap(); back(); sides(); fringe();
    H({-1.1f, 28.2f, 4.4f}, {1.1f, 30.2f, 5.5f}, {.tag = "side"});
    H({-1.3f, 21.6f, 4.7f}, {1.3f, 28.6f, 6.3f}, {.tag = "hang", .t0 = {0.6f, 0.75f}, .r = {{'x', -0.22f, {0, 28.6f, 5.4f}}}});
  } else if (hs == "braids") {
    cap(); back(); sides(); fringe();
    for (int s : {-1, 1}) {
      float x = s * 2.2f;
      H({x - 0.95f, 21.6f, 2.9f}, {x + 0.95f, 24.9f, 4.5f}, {.part = "neck", .tag = "hang"});
      H({x - 0.8f, 18.6f, 2.85f}, {x + 0.8f, 21.8f, 4.3f}, {.part = "neck", .tag = "hang"});
      H({x - 0.65f, 16.2f, 2.8f}, {x + 0.65f, 18.8f, 4.1f}, {.part = "neck", .tag = "hang"});
      K.box("neck", "leather", {x - 0.75f, 15.5f, 2.75f}, {x + 0.75f, 16.3f, 4.2f}, {.tag = "hang"});
    }
  } else if (hs == "mohawk") {
    H({-0.9f, 31.5f, -3.9f}, {0.9f, 33.5f, -1.0f}, {.tag = "top"}); H({-1.0f, 31.5f, -1.2f}, {1.0f, 34.6f, 1.9f}, {.tag = "top"});
    H({-0.9f, 31.5f, 1.7f}, {0.9f, 33.9f, 4.45f}, {.tag = "top"}); H({-0.8f, 27.0f, 3.95f}, {0.8f, 31.7f, 4.9f}, {.tag = "side"});
  } else if (hs == "bun") { cap(); back(); sides(); fringe(); H({-2.0f, 32.4f, -0.2f}, {2.0f, 35.0f, 3.8f}, {.tag = "top", .t1 = {0.75f, 0.75f}}); }
  else if (hs == "curly") {
    cap();
    for (float x : {-3.2f, -1.05f, 1.05f, 3.2f}) for (float z : {-3.2f, -1.05f, 1.05f, 3.2f}) { float r = (float)R() * 0.6f; H({x - 1.3f, 31.6f + r, z - 1.3f}, {x + 1.3f, 33.3f + r, z + 1.3f}, {.tag = "top"}); }
    for (float z : {-2.3f, 0.f, 2.3f}) for (float y : {28.6f, 30.6f}) H({3.45f, y - 1.1f, z - 1.2f}, {4.95f, y + 1.1f, z + 1.2f}, {.tag = "side", .mirror = true});
    for (float x : {-2.4f, 0.f, 2.4f}) for (float y : {26.9f, 29.3f}) H({x - 1.25f, y - 1.15f, 3.5f}, {x + 1.25f, y + 1.15f, 5.0f}, {.tag = "side"});
    for (float x : {-3.1f, -1.05f, 1.05f, 3.1f}) H({x - 1.1f, 30.2f, -4.95f}, {x + 1.1f, 31.6f, -3.8f}, {.tag = "fringe"});
  } else if (hs == "sides") {
    H({-2.8f, 31.2f, -4.5f}, {2.8f, 33.3f, 4.5f}, {.tag = "top"}); H({-2.8f, 30.0f, -4.8f}, {2.8f, 31.4f, -3.8f}, {.tag = "fringe"}); H({-2.8f, 28.4f, 3.6f}, {2.8f, 31.4f, 4.65f}, {.tag = "side"});
  } else if (hs == "wild") {
    H({-4.8f, 31.0f, -4.8f}, {4.8f, 33.0f, 4.8f}, {.tag = "top"});
    for (vec2 c : {vec2(-2.6f, -2.4f), vec2(2.6f, -2.4f), vec2(-2.6f, 2.4f), vec2(2.6f, 2.4f), vec2(0, 0), vec2(0, -3.3f), vec2(-3.4f, 0), vec2(3.4f, 0), vec2(0, 3.4f)})
      K.seg("head", "hair", {c.x, 32.4f, c.y}, {c.x * 1.35f, 35.4f - std::hypot(c.x, c.y) * 0.3f, c.y * 1.35f + 0.7f}, 2.3f, 0.5f, {.tag = "top"});
    H({-5.0f, 24.2f, 3.4f}, {5.0f, 31.4f, 5.9f}, {.tag = "side", .t0 = {1.1f, 1}});
    H({-5.0f, 25.0f, -3.0f}, {-3.55f, 31.4f, 4.2f}, {.tag = "side", .mirror = true});
    H({-5.2f, 18.0f, 2.25f}, {5.2f, 24.6f, 4.6f}, {.part = "neck", .tag = "hang", .t0 = {1.1f, 1}});
    for (float x : {-2.6f, 0.f, 2.6f}) K.seg("head", "hair", {x, 31.2f, -4.1f}, {x * 1.1f, 29.6f, -5.3f}, 1.7f, 0.45f, {.tag = "fringe"});
  }

  // ---- beards ----
  const std::string& bd = L.beard;
  auto B = [&](const char* part, vec3 a, vec3 b, O o = {}) { K.box(part, "beard", a, b, o); };
  auto must = [&] { B("head", {-2.6f, 26.3f, -4.7f}, {2.6f, 27.2f, -4.0f}); B("head", {-3.1f, 24.8f, -4.65f}, {-2.2f, 26.6f, -4.05f}, {.mirror = true}); };
  auto jaw = [&] { B("head", {-4.25f, 23.9f, -4.65f}, {4.25f, 26.25f, -3.7f}); B("head", {-4.6f, 24.1f, -3.9f}, {-3.75f, 28.6f, 1.0f}, {.mirror = true}); B("head", {-2.6f, 23.0f, -4.9f}, {2.6f, 24.4f, -3.6f}); };
  if (bd == "moustache") must();
  else if (bd == "short") { jaw(); must(); }
  else if (bd == "long") { jaw(); must(); B("neck", {-3.8f, 18.4f, -5.0f}, {3.8f, 23.6f, -3.3f}, {.t0 = {0.62f, 0.85f}}); }
  else if (bd == "braided") {
    jaw(); must(); B("neck", {-1.5f, 16.6f, -4.85f}, {1.5f, 23.6f, -3.5f}, {.t0 = {0.8f, 0.9f}});
    K.box("neck", "gold", {-1.7f, 18.0f, -5.0f}, {1.7f, 19.0f, -3.35f}); K.box("neck", "gold", {-1.6f, 20.6f, -5.0f}, {1.6f, 21.5f, -3.35f});
  } else if (bd == "forked") { jaw(); must(); B("neck", {-3.3f, 17.4f, -4.95f}, {-0.5f, 23.6f, -3.4f}, {.mirror = true, .t0 = {0.45f, 0.8f}}); }
  else if (bd == "wizard") {
    jaw();
    B("head", {-2.7f, 26.3f, -4.75f}, {2.7f, 27.3f, -4.0f});
    B("head", {-3.5f, 22.4f, -4.75f}, {-2.3f, 26.7f, -4.1f}, {.mirror = true, .t0 = {0.7f, 0.9f}});
    B("head", {-4.5f, 22.6f, -5.05f}, {4.5f, 26.2f, -3.6f});
    B("neck", {-4.35f, 18.4f, -5.25f}, {4.35f, 23.2f, -3.4f}, {.t0 = {0.9f, 0.95f}});
    B("neck", {-3.8f, 14.4f, -5.3f}, {3.8f, 18.8f, -3.45f}, {.t0 = {0.8f, 0.9f}});
    B("neck", {-2.85f, 11.0f, -5.15f}, {2.85f, 14.8f, -3.5f}, {.t0 = {0.6f, 0.85f}});
    B("neck", {-1.6f, 8.6f, -4.95f}, {1.6f, 11.4f, -3.7f}, {.t0 = {0.3f, 0.6f}});
    B("neck", {-4.65f, 19.2f, -4.5f}, {-3.7f, 23.8f, -3.45f}, {.mirror = true, .t0 = {0.7f, 1}});
  }

  // ---- ears, horns, tusks, snout, nose, tail ----
  if (L.ears == "pointed") K.seg("head", "skin", {-4.0f, 27.6f, 0.4f}, {-5.5f, 30.6f, 1.5f}, vec2(0.7f, 2.2f), vec2(0.45f, 0.45f), {.mirror = true});
  else if (L.ears == "long") K.seg("head", "skin", {-4.0f, 27.8f, 0.4f}, {-7.6f, 30.4f, 1.9f}, vec2(0.7f, 2.3f), vec2(0.4f, 0.4f), {.mirror = true});
  if (L.horns == "small") K.seg("head", "horn", {-2.6f, 31.6f, -1.6f}, {-2.9f, 33.6f, -1.2f}, 1.5f, 0.7f, {.mirror = true});
  else if (L.horns == "straight") K.seg("head", "horn", {-2.6f, 31.5f, -1.4f}, {-3.8f, 36.8f, 0.8f}, 1.7f, 0.35f, {.mirror = true});
  else if (L.horns == "curled" || L.horns == "ram") {
    bool ram = L.horns == "ram";
    int n = ram ? 8 : 6; float R0 = ram ? 3.3f : 3.6f, ph0 = ram ? -1.0f : -1.1f, ph1 = ram ? 3.8f : 2.2f, cy = ram ? 29.4f : 30.4f, cz = ram ? 1.0f : 2.2f;
    vec3 prev; bool have = false;
    for (int i = 0; i <= n; i++) {
      float t = (float)i / n, ph = ph0 + (ph1 - ph0) * t, r = R0 * (1 - 0.25f * t);
      vec3 p(-(3.3f + t * (ram ? 2.3f : 1.2f)), cy + r * std::cos(ph), cz + r * std::sin(ph));
      if (have) K.seg("head", "horn", prev, p, 2.1f - 1.4f * (i - 1) / n, 2.1f - 1.4f * t, {.mirror = true});
      prev = p; have = true;
    }
  }
  if (L.tusks) K.seg("head", "bone", {-2.3f, 25.0f, -4.3f}, {-2.7f, 27.6f, -4.95f}, 0.95f, 0.4f, {.mirror = true});
  if (L.snout) {
    K.box("head", "skin", {-2.6f, 24.2f, -6.9f}, {2.6f, 28.3f, -3.8f}, {.t1 = {0.92f, 1}});
    K.box("head", "skin", {-4.3f, 29.4f, -4.65f}, {4.3f, 30.4f, -3.9f});
    K.seg("head", "horn", {-2.0f, 31.6f, 2.0f}, {-2.7f, 34.0f, 4.8f}, 1.3f, 0.3f, {.mirror = true});
  }
  if (L.nose) K.box("head", "skin", {-1.0f, 26.0f, -5.6f}, {1.0f, 28.3f, -3.9f});
  if (L.tail) {
    if (L.species == "dragonborn" || L.scales) {
      K.seg("tail", "skin", {0, 12.4f, 2.0f}, {0, 8.2f, 6.6f}, 3.0f, 2.4f); K.seg("tail", "skin", {0, 8.2f, 6.6f}, {0, 4.6f, 10.2f}, 2.4f, 1.6f); K.seg("tail", "skin", {0, 4.6f, 10.2f}, {0, 2.2f, 13.4f}, 1.6f, 0.6f);
    } else {
      K.seg("tail", "skin", {0, 12.2f, 2.0f}, {0, 7.2f, 5.6f}, 1.1f, 0.95f); K.seg("tail", "skin", {0, 7.2f, 5.6f}, {0, 4.2f, 9.6f}, 0.95f, 0.8f); K.seg("tail", "skin", {0, 4.2f, 9.6f}, {0, 4.8f, 12.8f}, 0.8f, 0.7f);
      K.box("tail", "skin", {-1.3f, 3.6f, 12.4f}, {1.3f, 6.2f, 14.2f}, {.t1 = {0.2f, 0.5f}});
    }
  }
  // (alpha 3) Eldenwood's own folk
  const std::string& SP = L.species;
  if (SP == "sunmane") {
    K.seg("head", "skin", {-2.8f, 31.2f, 0.2f}, {-3.6f, 34.6f, 0.6f}, vec2(1.9f, 1.0f), vec2(0.35f, 0.3f), {.mirror = true});
    K.seg("head", "hair", {0, 30.0f, 4.6f}, {0, 22.4f, 5.4f}, vec2(8.6f, 2.6f), vec2(7.0f, 1.6f));
    K.seg("head", "hair", {-4.3f, 29.6f, 1.6f}, {-4.9f, 24.6f, 2.6f}, vec2(1.6f, 3.6f), vec2(1.0f, 2.0f), {.mirror = true});
    K.box("head", "skin", {-1.6f, 25.3f, -5.1f}, {1.6f, 27.4f, -3.9f}, {.t1 = {0.92f, 1}});
  } else if (SP == "kestrin") {
    K.seg("head", "horn", {0, 27.0f, -4.0f}, {0, 25.4f, -7.4f}, vec2(2.2f, 1.6f), vec2(0.3f, 0.3f));
    for (vec3 c : {vec3(0, 32.0f, 0.5f), vec3(0, 31.4f, 3.0f), vec3(0, 30.0f, 4.6f)}) K.seg("head", "feather", c, c + vec3(0, 2.6f, 3.2f), vec2(0.8f, 2.2f), vec2(0.3f, 0.6f));
    K.seg("armR", "feather", {3.6f, 21.0f, 1.6f}, {4.6f, 13.0f, 3.6f}, vec2(0.6f, 3.2f), vec2(0.3f, 1.6f), {.mirror = true});
  } else if (SP == "mossborn") {
    K.seg("head", "leather", {-2.4f, 31.6f, 0.4f}, {-4.2f, 36.0f, 1.6f}, 1.2f, 0.6f, {.mirror = true});
    K.seg("head", "leather", {-3.4f, 34.2f, 1.0f}, {-5.8f, 35.8f, 0.2f}, 0.7f, 0.35f, {.mirror = true});
    K.seg("head", "leaf", {-4.0f, 36.0f, 1.6f}, {-4.6f, 37.2f, 2.4f}, 1.6f, 0.9f, {.mirror = true});
    K.seg("body", "leaf", {-3.8f, 24.6f, -1.6f}, {-4.8f, 23.4f, -2.2f}, 1.4f, 0.6f, {.mirror = true});
  } else if (SP == "lumini") {
    for (vec4 c : {vec4(-2.0f, 31.6f, 0.0f, 3.4f), vec4(0.6f, 32.0f, 1.2f, 4.6f), vec4(2.6f, 31.4f, -0.4f, 2.8f), vec4(-0.8f, 31.2f, 2.8f, 3.0f)})
      K.seg("head", "horn", {c.x, c.y, c.z}, {c.x * 1.25f, c.y + c.w, c.z * 1.2f}, 1.3f, 0.2f);
    K.seg("body", "horn", {-3.8f, 24.6f, 0.4f}, {-5.2f, 27.4f, 0.8f}, 1.0f, 0.2f, {.mirror = true});
  } else if (SP == "tidecursed") {
    K.seg("head", "skin", {-4.1f, 28.2f, 0.6f}, {-6.4f, 30.4f, 2.6f}, vec2(0.4f, 2.8f), vec2(0.3f, 1.0f), {.mirror = true});
    K.seg("head", "skin", {0, 32.0f, -1.0f}, {0, 31.2f, 4.6f}, vec2(0.4f, 2.0f), vec2(0.3f, 0.6f));
  } else if (SP == "veilborn") {
    K.seg("head", "cloak", {0, 30.6f, 4.0f}, {0, 22.0f, 8.6f}, vec2(5.6f, 1.0f), vec2(2.0f, 0.4f));
  }

  // ---- hoods and hats ----
  if (hood) {
    std::string m = L.outfit.empty() ? "extra" : "main";
    K.box("head", m, {-4.95f, 23.4f, 3.6f}, {4.95f, 33.4f, 5.0f});
    K.box("head", m, {-4.95f, 32.3f, -4.7f}, {4.95f, 33.6f, 5.0f});
    K.box("head", m, {-4.95f, 23.4f, -4.5f}, {-3.95f, 33.4f, 5.0f}, {.mirror = true});
    K.box("head", m, {-4.95f, 31.4f, -5.15f}, {4.95f, 33.6f, -4.3f});
    K.seg("head", m, {0, 32.8f, 4.0f}, {0, 34.4f, 7.4f}, vec2(3.4f, 2.6f), vec2(0.7f, 0.6f));
    K.box("body", m, {-4.7f, 18.6f, 2.1f}, {4.7f, 24.6f, 3.4f}, {.t0 = {1.03f, 1}});
    K.box("body", m, {-4.7f, 22.1f, -2.95f}, {4.7f, 24.6f, -2.1f});
  }
  if (hat == "wizard") {
    K.box("head", "hat", {-6.6f, 31.3f, -6.6f}, {6.6f, 32.3f, 6.6f});
    K.box("head", "trim", {-4.75f, 32.1f, -4.75f}, {4.75f, 33.2f, 4.75f});
    K.box("head", "hat", {-4.6f, 32.2f, -4.6f}, {4.6f, 35.4f, 4.6f}, {.t1 = {0.78f, 0.78f}});
    K.box("head", "hat", {-3.6f, 35.2f, -3.4f}, {3.6f, 38.6f, 3.8f}, {.t1 = {0.62f, 0.62f}, .sh = {0, 0.8f}});
    K.box("head", "hat", {-2.25f, 38.4f, -1.6f}, {2.25f, 41.2f, 2.9f}, {.t1 = {0.5f, 0.5f}, .sh = {0, 1.0f}});
    K.seg("head", "hat", {0, 40.9f, 1.6f}, {0, 42.4f, 4.8f}, 1.25f, 0.3f);
    K.box("head", "gold", {-0.7f, 33.4f, -4.2f}, {0.7f, 34.6f, -3.75f});
  } else if (hat == "feather") {
    K.box("head", "trim", {-4.65f, 30.9f, -4.65f}, {4.65f, 33.4f, 4.65f}, {.t1 = {0.9f, 0.9f}});
    K.box("head", "trim", {-4.65f, 30.9f, -6.1f}, {4.65f, 31.5f, -4.4f});
    K.seg("head", "feather", {3.3f, 32.4f, 1.0f}, {5.0f, 38.4f, 4.6f}, vec2(0.4f, 1.7f), vec2(0.2f, 0.5f));
  } else if (hat == "circlet" || hat == "crown") {
    bool crown = hat == "crown"; const char* mt = crown ? "gold" : "leaf";
    float y0 = crown ? 30.7f : 29.6f, y1 = crown ? 32.0f : 30.8f;
    K.box("head", mt, {-4.6f, y0, -4.75f}, {4.6f, y1, -4.15f}); K.box("head", mt, {-4.6f, y0, 4.15f}, {4.6f, y1, 4.75f});
    K.box("head", mt, {-4.75f, y0, -4.6f}, {-4.15f, y1, 4.6f}); K.box("head", mt, {4.15f, y0, -4.6f}, {4.75f, y1, 4.6f});
    if (crown) for (float x : {-3.f, 0.f, 3.f}) { K.box("head", "gold", {x - 0.45f, 31.9f, -4.75f}, {x + 0.45f, 33.3f, -4.3f}); K.box("head", "gold", {x - 0.45f, 31.9f, 4.3f}, {x + 0.45f, 33.3f, 4.75f}); }
    else {
      for (float x : {-3.0f, -0.6f, 1.8f}) K.box("head", "leaf", {x - 0.7f, 30.2f, -5.0f}, {x + 0.7f, 31.6f, -4.3f});
      K.seg("head", "leather", {-3.6f, 30.6f, -2.6f}, {-4.8f, 33.6f, -2.0f}, 0.6f, 0.3f, {.mirror = true});
    }
  } else if (hat == "straw") {
    K.box("head", "hat", {-6.9f, 31.0f, -6.9f}, {6.9f, 31.8f, 6.9f});
    K.box("head", "hat", {-4.4f, 31.6f, -4.4f}, {4.4f, 34.0f, 4.4f}, {.t1 = {0.85f, 0.85f}});
    K.box("head", "leather", {-4.5f, 31.7f, -4.5f}, {4.5f, 32.4f, 4.5f});
  } else if (hat == "cap") {
    K.box("head", "hat", {-4.55f, 31.0f, -4.55f}, {4.55f, 33.1f, 4.55f}, {.t1 = {0.94f, 0.94f}});
    K.box("head", "hat", {-3.8f, 31.0f, -6.5f}, {3.8f, 31.6f, -4.4f});
    if (L.feather) K.seg("head", "feather", {3.6f, 32.4f, 0.6f}, {4.6f, 36.4f, 3.6f}, vec2(0.4f, 1.4f), vec2(0.2f, 0.4f));
  } else if (hat == "bucket") {
    K.box("head", "hat", {-4.6f, 30.6f, -4.6f}, {4.6f, 33.4f, 4.6f});
    K.box("head", "hat", {-5.7f, 30.2f, -5.7f}, {5.7f, 30.9f, 5.7f});
  } else if (hat == "chef") {
    K.box("head", "hat", {-4.6f, 30.8f, -4.6f}, {4.6f, 32.5f, 4.6f});
    K.box("head", "hat", {-5.0f, 32.3f, -5.0f}, {5.0f, 36.0f, 5.0f}, {.t0 = {0.88f, 0.88f}});
    K.box("head", "hat", {-4.4f, 35.8f, -4.4f}, {4.4f, 36.8f, 4.4f});
  }

  // ---- a cloak: the mantle and clasps, and the cape in two pieces (the lower one swings further) ----
  if (L.cloak) {
    K.box("body", "cloak", {-4.75f, 22.8f, -2.8f}, {4.75f, 25.0f, 3.1f});
    K.box("body", "gold", {-3.3f, 22.7f, -3.0f}, {-2.2f, 23.8f, -2.55f}, {.mirror = true});
    K.box("cape", "cloak", {-4.7f, 13.3f, 2.35f}, {4.7f, 24.7f, 3.6f}, {.t0 = {1.08f, 0.96f}, .fm = {{5, "trim"}}});
    K.box("capeLo", "cloak", {-5.08f, 3.0f, 2.4f}, {5.08f, 13.5f, 3.55f}, {.t0 = {1.12f, 0.94f}, .fm = {{5, "trim"}}});
  }

  // ---- clothes ----
  const std::string& of = L.outfit;
  auto belt = [&](const char* mat = "leather", const char* buckle = "gold") {
    K.box("body", mat, {-4.55f, 11.3f, -2.65f}, {4.55f, 13.0f, 2.65f}, {.tag = "belt"});
    if (buckle) K.box("body", buckle, {-1.0f, 11.1f, -2.85f}, {1.0f, 13.2f, -2.55f}, {.tag = "belt"});
  };
  auto skirt = [&](const char* mat, const char* hem, float y0 = 1.3f) {
    K.prism("skirtF", mat, y0, {-5.15f, -3.35f, 5.15f, 0}, 12.4f, {-4.45f, -2.55f, 4.45f, 0});
    K.prism("skirtB", mat, y0, {-5.15f, 0, 5.15f, 3.35f}, 12.4f, {-4.45f, 0, 4.45f, 2.55f});
    if (hem) {
      K.prism("skirtF", hem, y0 - 0.2f, {-5.3f, -3.5f, 5.3f, 0.02f}, y0 + 1.0f, {-5.22f, -3.42f, 5.22f, 0.02f});
      K.prism("skirtB", hem, y0 - 0.2f, {-5.3f, -0.02f, 5.3f, 3.5f}, y0 + 1.0f, {-5.22f, -0.02f, 5.22f, 3.42f});
    }
  };
  auto sleeves = [&](const char* mat, const char* hem) {
    K.prism("foreR", mat, 13.6f, {a0 - 0.95f, -2.95f, a1 + 0.95f, 2.95f}, 17.4f, {a0 - 0.4f, -2.4f, a1 + 0.4f, 2.4f}, {.tag = "cuff", .mirror = true});
    if (hem) K.prism("foreR", hem, 13.4f, {a0 - 1.05f, -3.05f, a1 + 1.05f, 3.05f}, 14.4f, {a0 - 0.98f, -2.98f, a1 + 0.98f, 2.98f}, {.tag = "cuff", .mirror = true});
  };
  auto collar = [&](const char* mat = "main") {
    K.box("body", mat, {-4.45f, 22.6f, 1.7f}, {4.45f, 26.0f, 2.9f}, {.tag = "sh", .r = {{'x', 0.25f, {0, 22.6f, 2.3f}}}, .fm = {{5, "trim"}}});
    K.box("body", mat, {-4.6f, 22.6f, -1.9f}, {-3.7f, 25.2f, 2.4f}, {.tag = "sh", .mirror = true});
  };
  auto tabard = [&](const char* mat, float y0, const char* edge = nullptr) {
    K.box("body", mat, {-2.7f, 12.4f, -3.0f}, {2.7f, 23.4f, -2.7f}); K.box("body", mat, {-2.7f, 12.4f, 2.7f}, {2.7f, 23.4f, 3.0f});
    K.box("skirtF", mat, {-2.75f, y0, -3.3f}, {2.75f, 12.6f, -2.8f}); K.box("skirtB", mat, {-2.75f, y0, 2.8f}, {2.75f, 12.6f, 3.3f});
    if (edge) { K.box("skirtF", edge, {-2.85f, y0 - 0.1f, -3.35f}, {2.85f, y0 + 0.7f, -2.75f}); K.box("skirtB", edge, {-2.85f, y0 - 0.1f, 2.75f}, {2.85f, y0 + 0.7f, 3.35f}); }
  };
  auto shoulders = [&](const char* mat, bool big = false) { K.prism("armR", mat, big ? 21.0f : 21.3f, {a0 - 0.5f, -2.65f, a1 + (big ? 0.8f : 0.55f), 2.65f}, 24.7f, {a0 - 0.3f, -2.45f, a1 + 0.4f, 2.45f}, {.tag = "sh", .mirror = true}); };
  auto sash = [&](const char* mat, float a, float z = -2.37f) { K.box("body", mat, {-5.4f, 17.25f, z - 0.25f}, {5.4f, 18.75f, z + 0.25f}, {.r = {{'z', a, {0, 18, z}}}}); };
  if (L.robe) {
    skirt("main", of == "pact" ? "cloak" : of == "leafrobe" ? nullptr : "trim");
    if (of != "leafrobe") sleeves("main", of == "pact" ? "cloak" : "trim");
  }
  if (of == "archrobe") {
    belt("leather", "gold"); collar();
    K.box("skirtF", "leather", {2.2f, 9.2f, -3.15f}, {4.0f, 11.6f, -2.45f}); K.box("skirtF", "gold", {2.9f, 10.6f, -3.25f}, {3.3f, 11.0f, -3.1f});
  } else if (of == "pact") { belt("trim", nullptr); collar(); }
  else if (of == "silks") { belt("trim", "gold"); K.box("skirtF", "trim", {1.4f, 6.6f, -3.1f}, {2.6f, 11.6f, -2.6f}); collar(); }
  else if (of == "vestments") {
    belt("leather", "gold");
    for (float x : {-2.1f, 2.1f}) { K.box("body", "trim", {x - 0.6f, 12.4f, -2.5f}, {x + 0.6f, 24.3f, -2.2f}); K.prism("skirtF", "trim", 3.6f, {x - 0.6f, -3.3f, x + 0.6f, -3.0f}, 12.4f, {x - 0.6f, -2.7f, x + 0.6f, -2.4f}); }
    K.box("body", "trim", {-4.6f, 22.8f, -2.75f}, {4.6f, 24.6f, 2.75f}, {.tag = "sh"});
  } else if (of == "leafrobe") {
    K.box("body", "leather", {-4.5f, 11.6f, -2.6f}, {4.5f, 12.6f, 2.6f}, {.tag = "belt"});
    for (float x : {-3.5f, -1.2f, 1.2f, 3.5f}) {
      K.box("body", "leaf", {x - 1.2f, 22.6f, -2.95f}, {x + 1.2f, 24.8f, -1.9f}, {.tag = "sh"}); K.box("body", "leaf", {x - 1.2f, 22.6f, 1.9f}, {x + 1.2f, 24.8f, 2.95f}, {.tag = "sh"});
      K.box("skirtF", "leaf", {x - 1.3f, 0.9f, -3.55f}, {x + 1.3f, 2.6f, -2.9f}); K.box("skirtB", "leaf", {x - 1.3f, 0.9f, 2.9f}, {x + 1.3f, 2.6f, 3.55f});
    }
  } else if (of == "gambeson") { tabard("trim", 4.6f); belt("leather", "gold"); shoulders("main", true); }
  else if (of == "surcoat") {
    tabard("main", 3.2f, "trim"); belt("leather", "gold");
    K.box("body", "trim", {-0.6f, 15.4f, -3.15f}, {0.6f, 21.6f, -2.95f}); K.box("body", "trim", {-2.0f, 18.4f, -3.15f}, {2.0f, 19.6f, -2.95f});
    shoulders("legs");
  } else if (of == "furs") {
    K.prism("body", "fur", 21.3f, {-4.95f, -2.95f, 4.95f, 3.05f}, 25.3f, {-4.4f, -2.6f, 4.4f, 2.7f}, {.tag = "sh"});
    K.prism("armR", "fur", 21.0f, {a0 - 0.5f, -2.8f, a1 + 0.85f, 2.8f}, 24.9f, {a0 - 0.3f, -2.5f, a1 + 0.5f, 2.5f}, {.tag = "sh", .mirror = true});
    K.box("body", "leather", {-4.6f, 11.0f, -2.7f}, {4.6f, 13.2f, 2.7f}, {.tag = "belt"}); K.box("body", "gold", {-1.3f, 10.8f, -2.95f}, {1.3f, 13.4f, -2.6f}, {.tag = "belt"});
    K.box("skirtF", "main", {-2.3f, 6.0f, -2.75f}, {2.3f, 12.4f, -2.3f}); K.box("skirtB", "main", {-2.3f, 6.0f, 2.3f}, {2.3f, 12.4f, 2.75f});
    K.prism("shinR", "fur", 3.4f, {-0.45f, -2.6f, 4.45f, 2.6f}, 6.1f, {-0.3f, -2.45f, 4.3f, 2.45f}, {.mirror = true});
    K.box("foreR", "leather", {a0 - 0.25f, 13.4f, -2.25f}, {a1 + 0.25f, 15.8f, 2.25f}, {.tag = "cuff", .mirror = true});
  } else if (of == "doublet") {
    K.prism("armR", "main", 20.6f, {a0 - 0.5f, -2.75f, a1 + 0.6f, 2.75f}, 24.5f, {a0 - 0.3f, -2.45f, a1 + 0.4f, 2.45f}, {.tag = "sh", .mirror = true});
    K.prism("armR", "trim", 20.4f, {a0 - 0.55f, -2.8f, a1 + 0.65f, 2.8f}, 21.0f, {a0 - 0.53f, -2.78f, a1 + 0.63f, 2.78f}, {.tag = "sh", .mirror = true});
    sash("trim", 0.68f); belt();
  } else if (of == "wraps") {
    K.box("body", "trim", {-4.55f, 11.2f, -2.65f}, {4.55f, 13.0f, 2.65f}, {.tag = "belt"});
    K.box("skirtF", "trim", {1.4f, 6.6f, -2.95f}, {2.6f, 11.6f, -2.55f}); K.box("skirtF", "trim", {2.8f, 7.6f, -2.9f}, {3.8f, 11.6f, -2.55f});
    K.box("foreR", "bone", {a0 - 0.25f, 13.2f, -2.25f}, {a1 + 0.25f, 16.4f, 2.25f}, {.tag = "cuff", .mirror = true});
    K.prism("shinR", "legs", 1.2f, {-0.6f, -2.7f, 4.6f, 2.7f}, 4.4f, {-0.25f, -2.3f, 4.25f, 2.3f}, {.mirror = true});
  } else if (of == "ranger") {
    K.box("body", "main", {-4.6f, 22.2f, -2.85f}, {4.6f, 24.7f, 2.95f}, {.tag = "sh"});
    K.box("foreR", "leather", {a0 - 0.35f, 13.2f, -2.35f}, {a1 + 0.35f, 16.8f, 2.35f}, {.tag = "cuff", .mirror = true});
    belt("leather", nullptr); K.box("skirtF", "leather", {2.4f, 9.2f, -3.05f}, {4.2f, 11.8f, -2.4f}); sash("leather", -0.7f);
  } else if (of == "shadow") {
    K.box("body", "trim", {-4.55f, 22.6f, -2.8f}, {4.55f, 24.7f, 2.8f}, {.tag = "sh"}); K.box("body", "trim", {1.2f, 17.0f, 2.25f}, {2.6f, 23.0f, 2.75f});
    sash("leather", 0.7f, -2.35f); sash("leather", -0.7f, -2.45f); belt("leather", nullptr);
    K.box("skirtF", "leather", {-4.2f, 9.6f, -3.0f}, {-2.6f, 11.6f, -2.4f}); K.box("skirtF", "leather", {2.6f, 9.6f, -3.0f}, {4.2f, 11.6f, -2.4f});
    K.box("foreR", "leather", {a0 - 0.3f, 13.2f, -2.3f}, {a1 + 0.3f, 16.6f, 2.3f}, {.tag = "cuff", .mirror = true});
  } else if (of == "traveller") {
    belt("leather", "gold"); K.box("skirtB", "leather", {0.8f, 8.4f, 2.45f}, {4.0f, 12.2f, 3.9f});
    sash("leather", -0.72f, -2.35f); sash("leather", 0.72f, 2.35f);
    K.box("body", "main", {-4.55f, 22.8f, -2.75f}, {4.55f, 24.5f, 2.75f}, {.tag = "sh"});
  } else if (of == "noble") {
    K.prism("body", "bone", 23.2f, {-4.65f, -2.95f, 4.65f, 2.95f}, 25.0f, {-4.3f, -2.7f, 4.3f, 2.7f}, {.tag = "sh"});
    sash("trim", 0.68f); belt("leather", "gold");
    K.box("foreR", "bone", {a0 - 0.3f, 13.0f, -2.3f}, {a1 + 0.3f, 14.2f, 2.3f}, {.tag = "cuff", .mirror = true});
  } else if (of == "peasant") K.box("body", "leather", {-4.5f, 11.6f, -2.6f}, {4.5f, 12.6f, 2.6f}, {.tag = "belt"});
  else {
    // the townsfolk: aprons, a guard's tabard, a belt
    if (L.apron) {
      K.box("skirtF", "extra", {-3.3f, 4.4f, -3.0f}, {3.3f, 12.6f, -2.45f}); K.box("body", "extra", {-2.9f, 15.8f, -2.45f}, {2.9f, 21.8f, -2.15f});
      K.box("body", "extra", {-4.5f, 11.8f, -2.55f}, {4.5f, 12.8f, 2.55f}, {.tag = "belt"});
    } else if (L.tabard) { tabard("extra", 5.2f, "gold"); belt("leather", "gold"); }
    else belt("leather", nullptr);
  }
  return S;
}

std::string gearArmorClass(const std::string& m) {
  if (m == "leather" || m == "studded" || m == "padded" || m == "hide") return "soft";
  if (m == "chain" || m == "chainshirt" || m == "scale") return "mail";
  return "plate";
}

std::vector<GearShape> gearArmorShapes(const std::string& cls, int slot, float aw) {
  std::vector<GearShape> S;
  Kit K{S, nullptr};
  const float a0 = 4, a1 = 4 + aw;
  if (slot == 0) {
    if (cls == "plate") {
      K.box("head", "am", {-0.65f, 32.5f, -4.45f}, {0.65f, 34.1f, 4.25f}, {.t1 = {0.75f, 0.9f}});
      K.box("head", "at", {-4.95f, 29.7f, -5.1f}, {4.95f, 30.7f, -4.45f});
      K.box("head", "am", {-4.95f, 24.3f, -4.95f}, {-4.25f, 28.4f, -1.2f}, {.mirror = true});
    } else if (cls == "mail") {
      K.prism("body", "am", 21.4f, {-4.75f, -2.95f, 4.75f, 3.05f}, 24.8f, {-4.4f, -2.7f, 4.4f, 2.8f});
      K.box("head", "at", {-1, 32.5f, -1}, {1, 33.4f, 1});
    } else K.box("head", "at", {-4.85f, 29.9f, -5.35f}, {4.85f, 30.7f, -4.45f});
  } else if (slot == 1) {
    if (cls == "plate") {
      K.prism("body", "at", 23.4f, {-3.9f, -2.95f, 3.9f, 2.95f}, 25.0f, {-3.5f, -2.7f, 3.5f, 2.7f});
      K.box("body", "am", {-0.55f, 14.2f, -2.95f}, {0.55f, 23.2f, -2.55f});
      K.box("body", "at", {-4.75f, 11.5f, -2.85f}, {4.75f, 13.1f, 2.85f});
      K.prism("armR", "am", 21.4f, {a0 - 0.9f, -3.05f, a1 + 1.05f, 3.05f}, 25.0f, {a0 - 0.4f, -2.6f, a1 + 0.55f, 2.6f}, {.mirror = true});
      K.prism("armR", "am", 19.6f, {a0 - 0.2f, -2.95f, a1 + 1.25f, 2.95f}, 21.6f, {a0 - 0.5f, -3.0f, a1 + 1.1f, 3.0f}, {.mirror = true});
      K.prism("armR", "at", 21.2f, {a0 - 0.95f, -3.1f, a1 + 1.12f, 3.1f}, 21.8f, {a0 - 0.95f, -3.1f, a1 + 1.12f, 3.1f}, {.mirror = true});
      K.prism("foreR", "at", 12.6f, {a0 - 0.75f, -2.95f, a1 + 0.75f, 2.95f}, 14.4f, {a0 - 0.6f, -2.8f, a1 + 0.6f, 2.8f}, {.mirror = true});
    } else if (cls == "mail") {
      K.prism("armR", "am", 21.4f, {a0 - 0.55f, -2.8f, a1 + 0.65f, 2.8f}, 24.7f, {a0 - 0.35f, -2.55f, a1 + 0.45f, 2.55f}, {.mirror = true});
      K.prism("skirtF", "am", 7.2f, {-4.75f, -3.05f, 4.75f, 0}, 12.4f, {-4.55f, -2.75f, 4.55f, 0});
      K.prism("skirtB", "am", 7.2f, {-4.75f, 0, 4.75f, 3.05f}, 12.4f, {-4.55f, 0, 4.55f, 2.75f});
      K.box("body", "at", {-4.7f, 11.6f, -2.8f}, {4.7f, 13.0f, 2.8f});
    } else {
      K.prism("armR", "am", 21.8f, {a0 - 0.45f, -2.75f, a1 + 0.55f, 2.75f}, 24.6f, {a0 - 0.3f, -2.55f, a1 + 0.4f, 2.55f}, {.mirror = true});
      K.box("body", "at", {-4.7f, 11.6f, -2.8f}, {4.7f, 13.0f, 2.8f});
      K.box("body", "at", {-5.4f, 17.5f, -2.85f}, {5.4f, 18.5f, -2.6f}, {.r = {{'z', 0.7f, {0, 18, -2.7f}}}});
    }
  } else if (slot == 2) {
    if (cls == "plate") {
      K.prism("legR", "am", 7.4f, {-0.3f, -3.05f, 4.35f, -2.5f}, 12.2f, {-0.15f, -2.85f, 4.2f, -2.45f}, {.mirror = true});
      K.box("legR", "am", {4.05f, 7.6f, -2.5f}, {4.65f, 12.2f, 2.5f}, {.mirror = true});
      K.box("shinR", "at", {0.3f, 4.4f, -3.15f}, {3.7f, 7.4f, -2.55f}, {.mirror = true, .t1 = {0.85f, 1}});
    } else K.box("shinR", "at", {0.5f, 4.8f, -3.0f}, {3.5f, 7.0f, -2.5f}, {.mirror = true});
  } else {
    if (cls == "plate") {
      K.prism("shinR", "at", 3.5f, {-0.35f, -3.0f, 4.35f, 3.0f}, 5.1f, {-0.25f, -2.9f, 4.25f, 2.9f}, {.mirror = true});
      K.box("shinR", "am", {0.1f, 0, -3.9f}, {3.9f, 1.9f, -2.6f}, {.mirror = true, .t1 = {0.95f, 0.8f}});
    } else K.prism("shinR", "at", 4.0f, {-0.3f, -2.9f, 4.3f, 2.9f}, 5.6f, {-0.25f, -2.85f, 4.25f, 2.85f}, {.mirror = true});
  }
  return S;
}

std::map<std::string, vec3> gearPivots(float aw) {
  float ax = 4 + aw / 2;
  return {{"head", {0, 24, 0}}, {"neck", {0, 24, 0}}, {"body", {0, 12, 0}}, {"skirtF", {0, 12, 0}}, {"skirtB", {0, 12, 0}}, {"armR", {ax, 22, 0}}, {"foreR", {ax, 18, 0}},
          {"armL", {-ax, 22, 0}}, {"foreL", {-ax, 18, 0}}, {"legR", {2, 12, 0}}, {"shinR", {2, 6, 0}}, {"legL", {-2, 12, 0}}, {"shinL", {-2, 6, 0}},
          {"cape", {0, 24, 2.4f}}, {"capeLo", {0, 13, 2.4f}}, {"tail", {0, 12, 2}}};
}

vec2 gearStature(const std::string& sp) {
  static const std::map<std::string, vec2> T = {{"human", {1, 1}}, {"elf", {1.03f, 0.95f}}, {"dwarf", {0.78f, 0.98f}}, {"halfling", {0.6f, 0.66f}}, {"gnome", {0.57f, 0.62f}}, {"dragonborn", {1.1f, 1.08f}},
    {"orc", {1.06f, 1.07f}}, {"tiefling", {1, 1}}, {"goliath", {1.24f, 1.16f}}, {"sunmane", {1.05f, 1.02f}}, {"kestrin", {0.96f, 0.92f}}, {"mossborn", {1.08f, 1.07f}}, {"lumini", {0.97f, 0.95f}},
    {"tidecursed", {1, 1}}, {"veilborn", {1.01f, 0.96f}}};
  auto it = T.find(sp); return it == T.end() ? vec2(1) : it->second;
}

// ------------------------------------------------------------------ colour (gearPaint, per voxel)
u32 gearColor(const GearLook& L, const std::string& mat, vec3 p, const TileSet* ts) {
  // a steady "random" per position, like the painted texture's noise
  u32 h = (u32)(std::floor(p.x * 2) * 73856093) ^ (u32)(std::floor(p.y * 2) * 19349663) ^ (u32)(std::floor(p.z * 2) * 83492791);
  h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
  float rnd = (h & 0xFFFF) / 65535.f;
  // the brightness pattern of one of the game's pictures (about 1 on average)
  auto lum = [&](const char* name) -> float {
    if (!ts) return 1 + (rnd - 0.5f) * 0.14f;
    auto it = ts->index.find(name); if (it == ts->index.end()) return 1 + (rnd - 0.5f) * 0.14f;
    int S = ts->size, x = floormod((int)std::floor((p.x + p.z) * 2), S), y = floormod((int)std::floor(-p.y * 2), S);
    const u8* c = ts->tile(it->second) + ((size_t)y * S + x) * 4;
    float l = (0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]) / std::max(1.f, (float)(ts->avgColor(it->second) & 255) * 0.3f + (float)((ts->avgColor(it->second) >> 8) & 255) * 0.59f + (float)((ts->avgColor(it->second) >> 16) & 255) * 0.11f);
    return clampv(l, 0.55f, 1.45f);
  };
  auto rgb = [](vec3 c, float k, float add = 0) { return voxRGBA((int)(c.r * k + add), (int)(c.g * k + add), (int)(c.b * k + add)); };
  if (mat == "hair" || mat == "beard") { vec3 c = mat == "hair" ? L.hairCol : L.beardCol; return rgb(c, 0.85f + rnd * 0.3f, 5); }
  if (mat == "main") return rgb(L.cols[0], lum("wool"));
  if (mat == "trim") return rgb(L.cols[1], lum("wool"));
  if (mat == "legs") return rgb(L.cols[2], lum("wool"));
  if (mat == "leather") return rgb(L.cols[3], lum("leather") * 1.05f);
  if (mat == "fur") { vec3 c = L.outfit == "furs" ? glm::min(L.cols[0] * 1.15f, vec3(255)) : vec3(150, 120, 84); return rgb(c, 0.82f + rnd * 0.3f); }
  if (mat == "gold") return rgb(vec3(232, 192, 72), lum("gold_block"));
  if (mat == "horn") return rgb(L.hornCol, 0.9f + rnd * 0.16f, 8);
  if (mat == "skin") return rgb(L.skin, 0.97f + rnd * 0.06f);
  if (mat == "bone") return rgb(vec3(236, 228, 206), 0.92f + rnd * 0.1f);
  if (mat == "leaf") return rgb(vec3(86, 146, 52), lum("leaves"));
  if (mat == "hat") return rgb(L.hatCol, lum("wool"));
  if (mat == "cloak") return rgb(L.cols[0] * 0.74f, lum("wool"));
  if (mat == "feather") return rnd < 0.5f ? rgb(vec3(210, 70, 40), 1) : rgb(vec3(240, 236, 226), 1);
  if (mat == "extra") return rgb(L.extra, lum("wool"));
  if (mat == "am") return voxRGBA(190, 190, 196);
  if (mat == "at") return voxRGBA(120, 120, 128);
  return rgb(L.cols[0], 1);
}

void paintGearShapes(VoxelPainter& P, const std::vector<GearShape>& S, const GearLook& L, const TileSet* ts, const std::string& part) {
  for (auto& s : S) {
    if (!part.empty() && s.part != part) continue;
    P.hexa(s.pts, [&](const PaintPt& pt) { return gearColor(L, s.mat, pt.p, ts); });
  }
}

} // namespace dq
