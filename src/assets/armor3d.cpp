// assets/armor3d.cpp — see armor3d.h
#include "assets/armor3d.h"
#include "assets/pack_fs.h"
#include "assets/tileset.h"
#include "common/fs.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace dq {
namespace {
struct Cube { vec3 o, sz; float inflate = 0; vec2 uv{0}; bool mirror = false; bool hasRot = false; vec3 pivot{0}, rot{0}; };
struct Bone { std::string name, parent; vec3 pivot{0}, rot{0}; std::vector<Cube> cubes; };
struct Set { std::vector<Bone> bones; int texW = 64, texH = 64; int w = 0, h = 0; std::vector<u8> rgba; };
std::map<std::string, Set> g_sets;

// each metal as one set, and the colour its grey metal is turned to (0: keep the set's own)
struct Metal { const char* mat; const char* set; int r, g, b; };
// (each its own helmet: a plain helm, a crown, horns, spikes and wings, a hood, a crest... few plumes)
const Metal METALS[] = {
  {"iron", "grave_sentinel_armor", 0, 0, 0}, {"gold", "golden_execution_armor", 0, 0, 0}, {"diamond", "dragonslayer_armor", 0, 0, 0},
  {"cinderite", "dark_lord_armor", 0, 0, 0}, {"copper", "crucible_knight_armor", 0, 0, 0}, {"bronze", "old_knight_armor", 0, 0, 0},
  {"silver", "silver_knight_armor", 0, 0, 0}, {"steel", "twinned_armor", 0, 0, 0}, {"mithril", "sunset_wings_armor", 0, 0, 0},
  {"adamantite", "forgotten_trace_armor", 0, 0, 0}, {"emerald", "golden_horns_armor", 0, 0, 0}, {"zanite", "eclipse_soldier_armor", 0, 0, 0},
  {"gravitite", "redeemer_armor", 0, 0, 0}, {"thallasium", "dark_cover_armor", 0, 0, 0}};
const Metal* metalOf(const std::string& mat) { for (auto& m : METALS) if (mat == m.mat) return &m; return nullptr; }

vec3 v3(const json& a, vec3 d = vec3(0)) { return a.is_array() && a.size() >= 3 ? vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()) : d; }

bool parseSet(const std::string& txt, Set& S) {
  json d = json::parse(txt, nullptr, false);
  if (!d.is_object() || !d.contains("minecraft:geometry") || !d["minecraft:geometry"].is_array() || d["minecraft:geometry"].empty()) return false;
  const json& g = d["minecraft:geometry"][0];
  if (g.contains("description")) { S.texW = g["description"].value("texture_width", 64); S.texH = g["description"].value("texture_height", 64); }
  for (const json& b : g.value("bones", json::array())) {
    Bone B; B.name = b.value("name", std::string()); B.parent = b.value("parent", std::string());
    B.pivot = v3(b.value("pivot", json())); B.rot = v3(b.value("rotation", json()));
    for (const json& c : b.value("cubes", json::array())) {
      Cube C; C.o = v3(c.value("origin", json())); C.sz = v3(c.value("size", json())); C.inflate = c.value("inflate", 0.f); C.mirror = c.value("mirror", false);
      const json& uv = c.value("uv", json());
      if (!uv.is_array() || uv.size() < 2) continue;   // (only box UV: all these sets use it)
      C.uv = vec2(uv[0].get<float>(), uv[1].get<float>());
      if (c.contains("rotation")) { C.hasRot = true; C.rot = v3(c["rotation"]); C.pivot = v3(c.value("pivot", json())); }
      B.cubes.push_back(C);
    }
    S.bones.push_back(std::move(B));
  }
  return !S.bones.empty();
}

// GeckoLib's way with a Bedrock rotation: z, then y, then x, with x and y turned the other way
mat4 rotAbout(vec3 pivot, vec3 deg) {
  mat4 m = glm::translate(mat4(1), pivot);
  m = glm::rotate(m, glm::radians(deg.z), vec3(0, 0, 1));
  m = glm::rotate(m, glm::radians(-deg.y), vec3(0, 1, 0));
  m = glm::rotate(m, glm::radians(-deg.x), vec3(1, 0, 0));
  return glm::translate(m, -pivot);
}

// a box UV's texel for face f (0 -x right side, 1 +x left side, 2 top, 3 bottom, 4 front -z, 5 back +z) at (s, t) 0..1
u32 boxTexel(const Set& S, const Cube& C, int f, float s, float t) {
  const float w = C.sz.x, h = C.sz.y, d = C.sz.z, u = C.uv.x, v = C.uv.y;
  if (C.mirror) { if (f == 0) f = 1; else if (f == 1) f = 0; s = 1 - s; }
  float x0, y0, fw, fh;
  switch (f) {
    case 0: x0 = u; y0 = v + d; fw = d; fh = h; break;
    case 1: x0 = u + d + w; y0 = v + d; fw = d; fh = h; break;
    case 2: x0 = u + d; y0 = v; fw = w; fh = d; break;
    case 3: x0 = u + d + w; y0 = v; fw = w; fh = d; break;
    case 4: x0 = u + d; y0 = v + d; fw = w; fh = h; break;
    default: x0 = u + 2 * d + w; y0 = v + d; fw = w; fh = h; break;
  }
  const float kx = (float)S.w / S.texW, ky = (float)S.h / S.texH;
  const int px = std::clamp((int)std::floor((x0 + std::clamp(s, 0.f, 0.999f) * std::max(fw, 0.01f)) * kx), 0, S.w - 1);
  const int py = std::clamp((int)std::floor((y0 + std::clamp(t, 0.f, 0.999f) * std::max(fh, 0.01f)) * ky), 0, S.h - 1);
  const u8* q = S.rgba.data() + ((size_t)py * S.w + px) * 4;
  if (q[3] < 128) return 0;
  return voxRGBA(q[0], q[1], q[2]);
}

// the piece's own colour (its icon's) over the set: its grey metal turned to it, light and shade kept; coloured parts (cloth,
// trims) pulled part way toward it so the whole reads as the item in your bag; the darkest lines kept
u32 recolour(u32 c, vec3 tint, float meanGrey) {
  if (tint.x < 0) return c;
  const int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, mx = std::max({r, g, b}), mn = std::min({r, g, b});
  const float lum = 0.3f * r + 0.59f * g + 0.11f * b;
  if (lum < 30) return c;
  const float k = std::clamp(lum / std::max(meanGrey, 1.f), 0.3f, 1.6f);
  const vec3 t = glm::min(tint * k, vec3(255));
  const bool grey = !(mx > 24 && (mx - mn) > mx * 0.22f);
  const vec3 o = grey ? t : glm::mix(vec3(r, g, b), t, 0.5f);
  return voxRGBA((int)o.r, (int)o.g, (int)o.b);
}

bool readSet(const PackFS& P, const std::string& name, Set& S) {
  auto g = P.read("assets/dokuquest/armor3d/" + name + ".geo.json"), t = P.read("assets/dokuquest/armor3d/" + name + ".png");
  if (!g || !t) return false;
  if (!parseSet(std::string(g->begin(), g->end()), S)) return false;
  return loadPngMem(t->data(), t->size(), S.w, S.h, S.rgba) && S.w > 0;
}
}  // namespace

int loadArmor3d(const PackFS& P) {
  g_sets.clear();
  const std::unique_ptr<PackFS> base = PackFS::open(dataDir() / "resourcepacks" / "DokuQuest Base");   // (the game's own pack under any other)
  for (auto& m : METALS) {
    Set S;
    if (readSet(P, m.set, S) || (base && readSet(*base, m.set, S))) g_sets[m.mat] = std::move(S);
  }
  return (int)g_sets.size();
}

bool armor3dHas(const std::string& mat) { return g_sets.count(mat) > 0; }

std::map<std::string, Armor3dPart> armor3dSlot(const std::string& mat, int slot, float armW, int vpp, vec3 tint) {
  std::map<std::string, Armor3dPart> out;
  auto it = g_sets.find(mat);
  const Metal* M = metalOf(mat);
  if (it == g_sets.end() || !M) return out;
  const Set& S = it->second;
  // the grey's own mean (so the recolour keeps the set's light and dark)
  float greySum = 0; int greyN = 0;
  for (size_t i = 0; i + 3 < S.rgba.size(); i += 4) {
    const u8* q = &S.rgba[i]; if (q[3] < 128) continue;
    const int mx = std::max({q[0], q[1], q[2]}), mn = std::min({q[0], q[1], q[2]});
    (void)mx; (void)mn;
    greySum += 0.3f * q[0] + 0.59f * q[1] + 0.11f * q[2]; greyN++;
  }
  const float meanGrey = greyN ? greySum / greyN : 128.f;
  // which of the set's root pieces belong to this slot, and to which side of the body
  auto rootOf = [&](const Bone& b) -> std::string {
    std::map<std::string, const Bone*> byName; for (auto& x : S.bones) byName[x.name] = &x;
    const Bone* c = &b;
    for (int guard = 0; c && guard < 32; guard++) {
      const std::string& n = c->name;
      if (n == "armorHead" || n == "armorBody" || n == "armorCape" || n == "armorRightArm" || n == "armorLeftArm" || n == "armorRightLeg" || n == "armorLeftLeg" ||
          n == "armorRightLegCloth" || n == "armorLeftLegCloth" || n == "armorRightBoot" || n == "armorLeftBoot") return n;
      auto p = byName.find(c->parent); c = p == byName.end() ? nullptr : p->second;
    }
    return "";
  };
  auto slotOf = [](const std::string& r) {
    if (r == "armorHead") return 0;
    if (r == "armorBody" || r == "armorCape" || r == "armorRightArm" || r == "armorLeftArm") return 1;
    if (r == "armorRightBoot" || r == "armorLeftBoot") return 3;
    return r.empty() ? -1 : 2;
  };
  // a bone's whole transform (its own rotation and its parents')
  std::map<std::string, const Bone*> byName; for (auto& x : S.bones) byName[x.name] = &x;
  auto boneM = [&](const Bone& b) {
    mat4 m(1); const Bone* c = &b;
    std::vector<const Bone*> chain;
    for (int guard = 0; c && guard < 32; guard++) { chain.push_back(c); auto p = byName.find(c->parent); c = p == byName.end() ? nullptr : p->second; }
    for (auto i = chain.rbegin(); i != chain.rend(); ++i) if ((*i)->rot != vec3(0)) m = m * rotAbout((*i)->pivot, (*i)->rot);
    return m;
  };
  const float ax = 4 + armW / 2;
  struct Acc { std::vector<std::pair<ivec3, u32>> v; };
  std::map<std::string, Acc> acc;
  const float step = 1.f / vpp;
  for (const Bone& b : S.bones) {
    const std::string root = rootOf(b);
    if (slotOf(root) != slot || b.cubes.empty()) continue;
    const mat4 BM = boneM(b);
    const bool right = root.find("Right") != std::string::npos, left = root.find("Left") != std::string::npos;
    const bool arm = root.find("Arm") != std::string::npos;
    for (const Cube& C : b.cubes) {
      const mat4 M2 = C.hasRot ? BM * rotAbout(C.pivot, C.rot) : BM;
      const vec3 lo = C.o - vec3(C.inflate), hi = C.o + C.sz + vec3(C.inflate);
      ivec3 n = glm::max(ivec3(glm::round((hi - lo) * (float)vpp)), ivec3(1));
      for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
        if (x > 0 && y > 0 && z > 0 && x < n.x - 1 && y < n.y - 1 && z < n.z - 1) continue;   // (the shell only)
        const vec3 p = lo + (vec3(x, y, z) + 0.5f) * (hi - lo) / vec3(n);
        // the nearest face that has an extent (a flat cape: its two big faces)
        const float dd[6] = {p.x - lo.x, hi.x - p.x, hi.y - p.y, p.y - lo.y, p.z - lo.z, hi.z - p.z};
        int f = -1;
        for (int i = 0; i < 6; i++) {
          const float ext = i < 2 ? C.sz.z * C.sz.y : i < 4 ? C.sz.x * C.sz.z : C.sz.x * C.sz.y;
          if (ext <= 0) continue;
          if (f < 0 || dd[i] < dd[f]) f = i;
        }
        if (f < 0) continue;
        auto fr = [](float q, float a, float b2) { return b2 - a > 1e-4f ? (q - a) / (b2 - a) : 0.5f; };
        const float sx = fr(p.x, lo.x, hi.x), sy = fr(p.y, lo.y, hi.y), sz = fr(p.z, lo.z, hi.z);
        float s = 0, t = 0;
        switch (f) {
          case 0: s = 1 - sz; t = 1 - sy; break;
          case 1: s = sz; t = 1 - sy; break;
          case 2: s = sx; t = 1 - sz; break;
          case 3: s = sx; t = sz; break;
          case 4: s = sx; t = 1 - sy; break;
          default: s = 1 - sx; t = 1 - sy; break;
        }
        u32 col = boxTexel(S, C, f, s, t);
        if (!col) continue;
        col = recolour(col, tint, meanGrey);
        vec3 q = vec3(M2 * vec4(p, 1));
        q.x = -q.x;   // (the set's right is -x; ours +x)
        // which of our parts, and its pivot
        std::string part; vec3 pv(0);
        if (root == "armorHead") { part = "head"; pv = vec3(0, 24, 0); }
        else if (root == "armorBody" || root == "armorCape") { part = "body"; pv = vec3(0, 12, 0); }
        else if (arm) {
          const float sx2 = right ? 1.f : -1.f;
          q.x -= sx2 * (6 - ax);   // (a slim arm's middle)
          const bool up = q.y >= 18;
          if (q.y < 14.5f) continue;   // (the sleeve ends at the wrist: the hand, the arm's last 2 px, is bare)
          part = std::string(right ? "armR" : "armL") + (up ? "_u" : "_d"); pv = vec3(sx2 * ax, up ? 22 : 18, 0);
        } else {
          const float sx2 = right ? 1.f : -1.f; (void)left;
          const bool up = q.y >= 6 && slot != 3;
          part = std::string(right ? "legR" : "legL") + (up ? "_u" : "_d"); pv = vec3(sx2 * 2, up ? 12 : 6, 0);
        }
        acc[part].v.push_back({ivec3(glm::floor((q - pv) * (float)vpp)), col | 0xFF000000u});
      }
    }
  }
  for (auto& [part, A] : acc) {
    if (A.v.empty()) continue;
    ivec3 mn(1 << 20), mx(-(1 << 20));
    for (auto& [c, col] : A.v) { mn = glm::min(mn, c); mx = glm::max(mx, c); }
    Armor3dPart P; P.g = VoxelGrid(mx - mn + 1); P.lo = vec3(mn) * step;
    for (auto& [c, col] : A.v) { const ivec3 l = c - mn; if (!P.g.at(l.x, l.y, l.z)) P.g.set(l.x, l.y, l.z, col); }
    out[part] = std::move(P);
  }
  return out;
}

} // namespace dq
