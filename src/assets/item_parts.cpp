// assets/item_parts.cpp — items built from parts, as voxels: render/models3d.js (T3D: tools, weapons,
// food, gems, dusts...), render/sculpt.js models (M3D: potions, buckets, bowls, books, armour, arrows) and
// render/items3d.js (ingots, bows, fishing rods, backpacks). Same shapes and numbers as the original.
//
// Colour: while a model is built from a picture, every part takes its colour from where it lies over
// the picture "filled out" past its edges (sculpt.js MODEL_FILL), shaded by the part's factor k — so the
// resource pack's sprite shows on the 3D model. Round things (balls, rings, discs) are true voxel circles
// where the original stacked octagons.
#include "assets/item_build.h"
#include <regex>

namespace dq {

namespace {
const float RT2 = 0.70710678f;
// along the diagonal: u up the handle from the bottom-left corner, v across it, z out of the picture
vec3 diag(float u, float v = 0, float z = 0) { return vec3(-0.44f + RT2 * u - RT2 * v, -0.44f + RT2 * u + RT2 * v, z); }
float kk(float k) { return k > 1 ? std::min(k, 1.12f) : k; }   // brightening is capped like the original's face shades

struct M {
  ItemBuild& b;
  VoxelPainter& P;
  const SpriteInfo& I;
  bool glow = false;
  explicit M(ItemBuild& bb) : b(bb), P(bb.P), I(bb.I) {}
  PaintFn F(float k) { bool g = glow; return [this, k, g](const PaintPt& p) { u32 c = voxShade(I.fillAt(p.p.x, p.p.y), kk(k)); return g && c ? (c & 0x00FFFFFFu) | (u32)VOXEL_GLOW << 24 : c; }; }
  // sseg: a segment cut into short pieces, widths and depths easing from one end to the other
  void sseg(vec3 a, vec3 c, float w0, float w1, float d0, float d1, float k = 1, float step = 0.07f, float bevel = 0) {
    float L = std::hypot(c.x - a.x, c.y - a.y);
    int n = std::max(1, (int)std::lround(L / step));
    for (int s = 0; s < n; s++) {
      float t0 = (float)s / n, t1 = (float)(s + 1) / n, tm = (t0 + t1) / 2;
      P.seg(glm::mix(a, c, t0), glm::mix(a, c, t1), w0 + (w1 - w0) * tm, d0 + (d1 - d0) * tm, F(k), bevel);
    }
  }
  void seg(vec3 a, vec3 c, float w, float d, float k = 1) { P.seg(a, c, w, d, F(k)); }
  void cbox(float x0, float y0, float z0, float x1, float y1, float z1, float k = 1) { P.box(vec3(x0, y0, z0), vec3(x1, y1, z1), F(k)); }
  // round layer about the y axis at (cx, cz)
  void ring(float cx, float cz, float y0, float y1, float r, float k = 1, float inner = 0) {
    PaintFn f = F(k);
    P.box(vec3(cx - r, y0, cz - r), vec3(cx + r, y1, cz + r), [&](const PaintPt& p) {
      float d2 = (p.p.x - cx) * (p.p.x - cx) + (p.p.z - cz) * (p.p.z - cz);
      float rr = std::max(r, P.frame.voxel * 0.75f);
      if (d2 > rr * rr || (inner > 0 && d2 < inner * inner)) return 0u;
      return f(p);
    });
  }
  // a disc facing you (in the picture's plane)
  void disc(float cx, float cy, float z0, float z1, float r, float k = 1) {
    PaintFn f = F(k);
    P.box(vec3(cx - r, cy - r, z0), vec3(cx + r, cy + r, z1), [&](const PaintPt& p) {
      float d2 = (p.p.x - cx) * (p.p.x - cx) + (p.p.y - cy) * (p.p.y - cy);
      return d2 > r * r ? 0u : f(p);
    });
  }
  // a ball (sball): layers shaded a little brighter toward the top
  void ball(float cx, float cy, float cz, float r, int n = 7, float sx = 1) {
    P.box(vec3(cx - r * sx, cy - r, cz - r), vec3(cx + r * sx, cy + r, cz + r), [&](const PaintPt& p) {
      vec3 d = (p.p - vec3(cx, cy, cz)) / vec3(r * sx, r, r);
      if (glm::dot(d, d) > 1) return 0u;
      int k = clampv((int)((d.y + 1) * 0.5f * n), 0, n - 1);
      return voxShade(I.fillAt(p.p.x, p.p.y), kk(0.92f + k * 0.03f));
    });
  }
  // the item's own picture on a plate (the compass needle, the clock's dial)
  void picture(float z, float scale) {
    float s = scale / 2;
    P.box(vec3(-s, -s, z), vec3(s, s, z + 0.01f), [&](const PaintPt& p) { return I.rawAt(p.p.x / scale, p.p.y / scale); });
  }
};

const SpritePx* gemPx(const SpriteInfo& I) { return I.best([](const SpritePx& p) { return p.sat * 255 + p.lum * 0.3f; }); }

// ------------------------------------------------------------------ models3d.js T3D
void handle(M& m, float to, float w = 0.075f) {
  m.sseg(diag(-0.04f), diag(0.03f), w + 0.025f, w + 0.025f, w + 0.025f, w + 0.025f, 0.85f);
  m.sseg(diag(0.03f), diag(to), w, w * 0.92f, w, w * 0.92f);
  for (float u : {0.1f, 0.18f, 0.26f}) m.sseg(diag(u), diag(u + 0.025f), w + 0.012f, w + 0.012f, w + 0.012f, w + 0.012f, 0.72f);
}
float sword(M& m, float L = 1.22f, float bw = 0.15f, float guard = 0.22f) {
  m.sseg(diag(-0.04f), diag(0.04f), 0.11f, 0.11f, 0.11f, 0.11f, 0.9f);                  // the pommel
  m.sseg(diag(0.04f), diag(0.25f), 0.07f, 0.07f, 0.07f, 0.07f);                         // the grip
  for (float u : {0.09f, 0.15f, 0.21f}) m.sseg(diag(u), diag(u + 0.022f), 0.082f, 0.082f, 0.082f, 0.082f, 0.72f);
  m.sseg(diag(0.27f, -guard), diag(0.27f, guard), 0.075f, 0.075f, 0.11f, 0.11f, 0.85f, 0.11f);   // the crossguard
  m.sseg(diag(0.31f), diag(L - 0.14f), bw, bw * 0.8f, 0.045f, 0.04f, 1, 0.08f, 0.65f);   // the blade, bevelled to its edges
  m.sseg(diag(L - 0.14f), diag(L), bw * 0.8f, 0.02f, 0.04f, 0.02f, 1.05f, 0.05f, 0.65f); // its point
  for (float z : {0.022f, -0.022f}) m.sseg(diag(0.34f, 0, z), diag(L - 0.22f, 0, z), 0.035f, 0.03f, 0.012f, 0.012f, 0.78f);   // the fuller
  return 0.055f;
}
} // namespace

std::string model3dKind(const std::string& key, const ItemDef* def) {
  if (!def || !def->block.empty()) return "";
  const std::string t = def->hasTool ? def->tool.type : "";
  auto re = [&](const char* r) { return rxSearch(key, r); };
  if (t == "pickaxe") return "pickaxe";
  if (t == "axe") return "axe";
  if (t == "shovel") return "shovel";
  if (t == "hoe") return "hoe";
  if (key == "ogre_club") return "club";
  if (key.find("dagger") != std::string::npos) return "dagger";
  if (t == "sword") return "sword";
  if (def->has("staff") || t == "staff") return "staff";
  if (t == "shears") return "shears";
  if (key == "flint_and_steel") return "flint_and_steel";
  if (key == "spyglass") return "spyglass";
  if (key == "grappling_hook") return "hook";
  if (re("^(golden_)?apple$")) return "apple";
  if (re("carrot$")) return "carrot";
  if (re("potato$")) return "potato";
  if (key == "beetroot") return "beetroot";
  if (key == "bread") return "bread";
  if (key == "cookie") return "cookie";
  if (re("berries$")) return "berries";
  if (re("melon_slice$")) return "melon";
  if (key == "pumpkin_pie") return "pie";
  if (key == "egg") return "egg";
  if (re("ball$|^snowball$|^cinder_core$|^magma_cream$")) return "ball";
  if (re("^(diamond|emerald|ruby|sapphire|heart_crystal|aether_crystal|mese_crystal)$")) return "gem";
  if (re("shard$|^quartz$")) return "crystal";
  if (re("^(coal|charcoal|flint|raw_\\w+|crushed_\\w+)$")) return "lump";
  if (re("nugget$")) return "nugget";
  if (re("dust$|powder$|^sugar$|^flour$|^bone_meal$|^gunpowder$|^redstone$")) return "heap";
  if (re("seeds$|^cocoa_beans$|^nether_wart_item$")) return "seeds";
  if (re("_dye$")) return "pouch";
  if (re("^stick$|^bone$|^cinder_rod$")) return "rod";
  if (key == "wheat") return "wheat";
  if (key == "compass" || key == "clock" || key == "death_compass") return "dial";
  if (re("^music_disc_")) return "disc";
  if (re("charm$")) return "charm";
  return "";
}

bool buildModel3d(const std::string& k, ItemBuild& b) {
  M m(b);
  const SpriteInfo& I = b.I;
  float depth = 0; bool stand = false;
  if (k == "pickaxe") {
    handle(m, 0.86f);
    m.sseg(diag(0.72f), diag(0.86f), 0.12f, 0.12f, 0.12f, 0.12f, 0.8f);                  // the collar
    // the head: a curved bar across the top of the handle, thick in the middle, tapering to points
    std::vector<std::pair<float, vec3>> pts;
    for (float s = -1; s <= 1.0001f; s += 0.25f) pts.push_back({s, diag(0.86f - 0.2f * s * s + 0.04f, s * 0.47f, 0)});
    for (size_t i = 0; i + 1 < pts.size(); i++) {
      float mm = (std::abs(pts[i].first) + std::abs(pts[i + 1].first)) / 2, w = 0.14f - mm * 0.085f, d = 0.12f - mm * 0.06f;
      m.sseg(pts[i].second, pts[i + 1].second, w, w, d, d, 1, 0.2f);
    }
    depth = 0.07f;
  } else if (k == "axe") {
    handle(m, 0.93f);
    m.sseg(diag(0.6f), diag(0.9f), 0.11f, 0.11f, 0.11f, 0.11f, 0.8f);                    // the eye round the handle
    for (int i = 0; i < 5; i++) {                                                         // the blade fanning out to its edge
      float v0 = 0.04f + i * 0.065f, v1 = v0 + 0.065f, wd = 0.17f + i * 0.055f, dd = 0.09f - i * 0.012f;
      m.sseg(diag(0.75f, v0), diag(0.75f, v1), wd, wd + 0.055f, dd, dd - 0.012f, i == 4 ? 1.12f : 1, 0.2f);
    }
    m.sseg(diag(0.75f, 0.365f), diag(0.75f, 0.39f), 0.43f, 0.45f, 0.025f, 0.02f, 1.25f, 0.2f);   // the bright edge
    m.sseg(diag(0.75f, -0.02f), diag(0.75f, -0.13f), 0.12f, 0.1f, 0.1f, 0.09f, 0.85f, 0.2f);     // the poll
    depth = 0.06f;
  } else if (k == "shovel") {
    handle(m, 0.8f);
    m.sseg(diag(-0.02f, -0.11f), diag(-0.02f, 0.11f), 0.06f, 0.06f, 0.08f, 0.08f, 0.8f);  // a crossbar grip
    m.sseg(diag(0.74f), diag(0.88f), 0.09f, 0.12f, 0.09f, 0.07f, 0.85f);                  // the socket
    m.sseg(diag(0.88f), diag(1.12f), 0.27f, 0.27f, 0.05f, 0.045f, 1, 0.06f);              // the blade
    m.sseg(diag(1.12f), diag(1.18f), 0.21f, 0.12f, 0.04f, 0.035f, 1.1f);                  // rounded off at the tip
    m.sseg(diag(0.9f, 0, 0.03f), diag(1.08f, 0, 0.03f), 0.03f, 0.03f, 0.02f, 0.02f, 0.75f);   // a rib
    depth = 0.05f;
  } else if (k == "hoe") {
    handle(m, 0.9f);
    m.sseg(diag(0.76f), diag(0.9f), 0.1f, 0.1f, 0.1f, 0.1f, 0.8f);
    m.sseg(diag(0.84f, 0), diag(0.84f, 0.3f), 0.09f, 0.08f, 0.08f, 0.07f, 1, 0.1f);       // the arm
    m.sseg(diag(0.86f, 0.3f), diag(0.6f, 0.34f), 0.14f, 0.15f, 0.055f, 0.04f, 1.08f, 0.08f);   // the blade, turned down
    depth = 0.06f;
  } else if (k == "sword") depth = sword(m);
  else if (k == "dagger") depth = sword(m, 0.92f, 0.13f, 0.16f);
  else if (k == "club") {
    m.sseg(diag(-0.02f), diag(0.32f), 0.09f, 0.1f, 0.09f, 0.1f);
    m.sseg(diag(0.32f), diag(1.1f), 0.12f, 0.27f, 0.12f, 0.26f, 1, 0.1f);
    for (auto uv : {vec2(0.6f, 0.12f), vec2(0.75f, -0.15f), vec2(0.9f, 0.16f), vec2(1.0f, -0.1f), vec2(0.72f, 0.02f)})
      m.sseg(diag(uv.x, uv.y), diag(uv.x, uv.y * 1.6f), 0.05f, 0.02f, 0.05f, 0.02f, 1.15f);   // studs
    depth = 0.13f;
  } else if (k == "staff") {
    m.sseg(diag(-0.04f), diag(1.0f), 0.06f, 0.055f, 0.06f, 0.055f);
    for (float u : {0.2f, 0.28f, 0.6f}) m.sseg(diag(u), diag(u + 0.025f), 0.075f, 0.075f, 0.075f, 0.075f, 0.75f);
    for (float v : {-0.09f, 0.09f}) m.sseg(diag(0.98f, v * 0.4f), diag(1.16f, v), 0.04f, 0.03f, 0.04f, 0.03f, 0.9f);   // a claw
    vec3 c = diag(1.12f);
    m.glow = true;                                                                        // holding a glowing stone
    m.cbox(c.x - 0.08f, c.y - 0.08f, -0.08f, c.x + 0.08f, c.y + 0.08f, 0.08f);
    m.cbox(c.x - 0.05f, c.y - 0.05f, 0.08f, c.x + 0.05f, c.y + 0.05f, 0.1f, 1.2f);
    m.glow = false;
    depth = 0.1f;
  } else if (k == "shears") {
    for (int s : {-1, 1}) {
      m.sseg(vec3(0, 0, s * 0.025f), vec3(0.46f * (s > 0 ? 1 : 0.62f), 0.46f * (s > 0 ? 0.62f : 1), s * 0.025f), 0.15f, 0.05f, 0.04f, 0.035f, 1, 0.1f);   // the blades
      vec2 h = s > 0 ? vec2(-0.2f, -0.36f) : vec2(-0.36f, -0.2f);
      for (int i = 0; i < 8; i++) {
        float a = i / 8.f * TAU, c = (i + 1) / 8.f * TAU;
        m.sseg(vec3(h.x + std::cos(a) * 0.12f, h.y + std::sin(a) * 0.12f, s * 0.025f), vec3(h.x + std::cos(c) * 0.12f, h.y + std::sin(c) * 0.12f, s * 0.025f), 0.055f, 0.055f, 0.055f, 0.055f, 0.9f, 0.3f);
      }
      m.sseg(vec3(h.x + 0.09f, h.y + 0.09f, s * 0.025f), vec3(0, 0, s * 0.025f), 0.07f, 0.07f, 0.05f, 0.05f);
    }
    m.cbox(-0.045f, -0.045f, -0.07f, 0.045f, 0.045f, 0.07f, 0.8f);                        // the pivot
    depth = 0.06f;
  } else if (k == "flint_and_steel") {
    for (int i = 0; i < 7; i++) {                                                         // the steel: a ring open at one side
      float a = 0.6f + i / 7.f * 4.6f, c = 0.6f + (i + 1) / 7.f * 4.6f;
      m.sseg(vec3(-0.12f + std::cos(a) * 0.24f, -0.12f + std::sin(a) * 0.24f, 0), vec3(-0.12f + std::cos(c) * 0.24f, -0.12f + std::sin(c) * 0.24f, 0), 0.08f, 0.08f, 0.06f, 0.06f, 1, 0.3f);
    }
    m.cbox(0.08f, 0.06f, -0.08f, 0.36f, 0.34f, 0.08f);                                    // the flint, chipped
    m.cbox(0.14f, 0.3f, -0.06f, 0.32f, 0.4f, 0.06f, 1.15f);
    m.cbox(0.04f, 0.12f, -0.05f, 0.1f, 0.28f, 0.05f, 0.9f);
    depth = 0.08f;
  } else if (k == "spyglass") {
    m.sseg(diag(0.05f), diag(0.4f), 0.14f, 0.14f, 0.14f, 0.14f);
    m.sseg(diag(0.4f), diag(0.75f), 0.12f, 0.12f, 0.12f, 0.12f);
    m.sseg(diag(0.75f), diag(1.05f), 0.1f, 0.1f, 0.1f, 0.1f);
    for (float u : {0.05f, 0.4f, 0.75f}) m.sseg(diag(u), diag(u + 0.04f), 0.165f, 0.165f, 0.165f, 0.165f, 1.1f);
    depth = 0.08f;
  } else if (k == "hook") {
    m.sseg(diag(0.3f), diag(0.95f), 0.06f, 0.06f, 0.06f, 0.06f);
    for (int v : {-1, 0, 1}) {
      float z = v * 0.12f;
      m.sseg(diag(0.95f, 0, 0), diag(1.05f, v * 0.18f, z), 0.05f, 0.045f, 0.05f, 0.045f, 1, 0.1f);
      m.sseg(diag(1.05f, v * 0.18f, z), diag(0.92f, v * 0.24f, z * 1.3f), 0.045f, 0.03f, 0.045f, 0.03f, 1.1f, 0.1f);
    }
    vec3 c = diag(0.12f);                                                                  // a coil of rope
    for (int i = 0; i < 8; i++) {
      float a = i / 8.f * TAU, e = (i + 1) / 8.f * TAU;
      m.sseg(vec3(c.x + std::cos(a) * 0.18f, c.y + std::sin(a) * 0.18f, 0), vec3(c.x + std::cos(e) * 0.18f, c.y + std::sin(e) * 0.18f, 0), 0.07f, 0.07f, 0.07f, 0.07f, 1, 0.3f);
    }
    depth = 0.12f;
  } else if (k == "apple") {
    m.ball(0, -0.06f, 0, 0.36f, 8, 1.05f);
    m.cbox(-0.025f, 0.26f, -0.025f, 0.025f, 0.42f, 0.025f, 0.8f);                         // the stem
    m.cbox(0.02f, 0.3f, -0.02f, 0.2f, 0.38f, 0.02f);                                      // a leaf
    depth = 0.36f;
  } else if (k == "carrot") {
    m.sseg(diag(0.05f), diag(0.9f), 0.04f, 0.17f, 0.04f, 0.17f, 1, 0.08f);
    for (float v : {-0.08f, 0.f, 0.08f}) m.sseg(diag(0.9f, v * 0.4f), diag(1.18f, v * 1.6f, v * 0.5f), 0.05f, 0.03f, 0.04f, 0.02f, 1, 0.1f);
    for (float u : {0.35f, 0.55f, 0.72f}) { float w = 0.175f * (u / 0.9f) + 0.02f, d = 0.17f * (u / 0.9f) + 0.015f; m.sseg(diag(u), diag(u + 0.02f), w, w, d, d, 0.82f); }
    depth = 0.09f;
  } else if (k == "potato") {
    m.ball(0, -0.04f, 0, 0.3f, 6, 1.35f);
    for (auto e : {vec2(-0.18f, 0.05f), vec2(0.12f, -0.12f), vec2(0.24f, 0.08f)}) m.cbox(e.x, e.y, 0.17f, e.x + 0.05f, e.y + 0.04f, 0.2f, 0.7f);
    depth = 0.3f;
  } else if (k == "beetroot") {
    m.ball(0, -0.12f, 0, 0.27f, 7);
    m.cbox(-0.02f, -0.46f, -0.02f, 0.02f, -0.36f, 0.02f, 0.8f);
    for (float x : {-0.08f, 0.06f}) m.cbox(x, 0.12f, -0.03f, x + 0.05f, 0.42f, 0.03f);
    depth = 0.27f;
  } else if (k == "bread") {
    for (int i = 0; i < 6; i++) {
      float u0 = 0.12f + i * 0.13f, u1 = u0 + 0.13f, um = (u0 + u1) / 2, w = 0.3f * std::sqrt(std::max(0.2f, 1 - std::pow((um - 0.5f) / 0.45f, 2.f)));
      m.sseg(diag(u0), diag(u1), w, w, w * 0.8f, w * 0.8f, 1, 0.2f);
    }
    for (float u : {0.32f, 0.5f, 0.68f}) m.sseg(diag(u, -0.1f, 0.11f), diag(u + 0.06f, 0.1f, 0.11f), 0.03f, 0.03f, 0.02f, 0.02f, 0.75f);
    depth = 0.12f;
  } else if (k == "cookie") {
    m.disc(0, 0, -0.06f, 0.06f, 0.4f);
    for (auto c : {vec2(-0.15f, 0.12f), vec2(0.12f, 0.18f), vec2(0.05f, -0.12f), vec2(-0.2f, -0.15f), vec2(0.22f, -0.02f)}) m.cbox(c.x - 0.04f, c.y - 0.04f, 0.06f, c.x + 0.04f, c.y + 0.04f, 0.09f, 0.9f);
    depth = 0.09f;
  } else if (k == "berries") {
    for (auto c : {vec2(-0.12f, -0.1f), vec2(0.1f, -0.06f), vec2(0, 0.1f), vec2(0.16f, 0.16f), vec2(-0.2f, 0.12f), vec2(-0.02f, -0.28f), vec2(0.22f, -0.2f)}) {
      m.ball(c.x, c.y, 0, 0.11f, 4);
    }
    m.sseg(vec3(0, 0.12f, 0), vec3(0.05f, 0.42f, 0), 0.03f, 0.025f, 0.03f, 0.025f, 0.9f);
    depth = 0.13f;
  } else if (k == "melon") {
    for (int i = 0; i < 6; i++) { float x0 = -0.42f + i * 0.14f, w = 0.36f - std::abs(i - 2.5f) * 0.06f; m.cbox(x0, -0.3f, -0.07f, x0 + 0.14f, -0.3f + w, 0.07f); }
    m.cbox(-0.44f, -0.38f, -0.08f, 0.44f, -0.3f, 0.08f);                                  // the rind
    for (float x : {-0.2f, 0.f, 0.2f}) m.cbox(x, -0.12f, 0.07f, x + 0.04f, -0.07f, 0.085f, 0.7f);   // seeds
    depth = 0.085f;
  } else if (k == "pie") {
    m.disc(0, -0.02f, -0.12f, 0.12f, 0.42f, 0.95f);
    m.disc(0, -0.02f, 0.12f, 0.16f, 0.34f);
    depth = 0.16f;
  } else if (k == "egg") { m.ball(0, -0.04f, 0, 0.32f, 7, 0.82f); depth = 0.32f; }
  else if (k == "ball") { m.ball(0, 0, 0, 0.32f, 7); depth = 0.32f; }
  else if (k == "gem") {
    // a cut gem: a crown above, a point below (rings narrowing to the culet)
    const float B[3][3] = {{-0.4f, -0.3f, 0.07f}, {-0.3f, -0.18f, 0.16f}, {-0.18f, -0.06f, 0.25f}};
    const float L[4][3] = {{-0.06f, 0.06f, 0.18f}, {0.06f, 0.2f, 0.34f}, {0.2f, 0.28f, 0.3f}, {0.28f, 0.34f, 0.2f}};
    for (auto& r : B) m.ring(0, 0, r[0], r[1], r[2], 0.85f);
    for (auto& r : L) m.ring(0, 0, r[0], r[1], r[2], 1.05f);
    m.cbox(-0.1f, 0.34f, -0.06f, 0.1f, 0.355f, 0.06f, 1.25f);
    depth = 0.34f; stand = true;
  } else if (k == "crystal") {
    for (auto c : {vec4(0.1f, 0, 0.95f, 0.14f), vec4(0.25f, 0.14f, 0.55f, 0.1f), vec4(0.2f, -0.14f, 0.6f, 0.1f)}) {
      float u = c.x, v = c.y, len = c.z, w = c.w;
      m.sseg(diag(u, v * 0.3f), diag(u + len - 0.14f, v), w, w, w, w, 1, 0.12f);
      m.sseg(diag(u + len - 0.14f, v), diag(u + len, v * 1.1f), w, 0.02f, w, 0.02f, 1.15f);
    }
    depth = 0.07f;
  } else if (k == "lump") {
    for (auto c : {vec4(-0.12f, -0.1f, 0, 0.2f), vec4(0.12f, -0.05f, 0.05f, 0.17f), vec4(0, 0.14f, -0.04f, 0.16f), vec4(0.16f, 0.16f, 0.03f, 0.1f), vec4(-0.2f, 0.1f, 0.06f, 0.1f)})
      m.cbox(c.x - c.w, c.y - c.w, c.z - c.w, c.x + c.w, c.y + c.w, c.z + c.w, 0.9f + c.y);
    depth = 0.22f;
  } else if (k == "nugget") {
    for (auto c : {vec3(-0.12f, -0.12f, 0.13f), vec3(0.1f, -0.06f, 0.11f), vec3(0, 0.12f, 0.1f)})
      m.cbox(c.x - c.z, c.y - c.z, -c.z * 0.8f, c.x + c.z, c.y + c.z, c.z * 0.8f, 1 + c.y);
    depth = 0.11f;
  } else if (k == "heap") {
    for (int i = 0; i < 5; i++) { float y0 = -0.46f + i * 0.09f, r = 0.42f - i * 0.075f; m.ring(0, 0, y0, y0 + 0.09f, r, 0.9f + i * 0.05f); }
    depth = 0.42f; stand = true;
  } else if (k == "seeds") {
    for (int i = 0; i < 9; i++) {
      float x = std::sin(i * 2.4f) * 0.26f, y = std::cos(i * 1.9f) * 0.24f - 0.04f, z = std::sin(i * 1.3f) * 0.08f;
      m.cbox(x - 0.05f, y - 0.07f, z - 0.04f, x + 0.05f, y + 0.07f, z + 0.04f, 0.9f + (i % 3) * 0.08f);
    }
    depth = 0.12f;
  } else if (k == "pouch") {
    for (auto r : {vec3(-0.42f, -0.34f, 0.2f), vec3(-0.34f, -0.12f, 0.3f), vec3(-0.12f, 0.02f, 0.26f), vec3(0.02f, 0.1f, 0.16f)}) m.ring(0, 0, r.x, r.y, r.z);
    m.ring(0, 0, 0.1f, 0.15f, 0.1f, 0.8f);                                                // the tie
    m.ring(0, 0, 0.15f, 0.27f, 0.15f);
    depth = 0.3f; stand = true;
  } else if (k == "rod") {
    bool bone = b.key.find("bone") != std::string::npos;
    float w = bone ? 0.075f : 0.065f;
    m.sseg(diag(0.06f), diag(1.08f), w, w, w, w, 1, 0.08f);
    if (bone) for (float u : {0.04f, 1.08f}) for (float v : {-0.05f, 0.05f}) m.sseg(diag(u - 0.04f, v), diag(u + 0.04f, v), 0.07f, 0.07f, 0.08f, 0.08f, 1.05f);
    depth = 0.04f;
  } else if (k == "wheat") {
    for (float v : {-0.08f, 0.f, 0.08f}) {
      m.sseg(diag(0, v * 0.2f), diag(0.7f, v), 0.025f, 0.025f, 0.025f, 0.025f);
      m.sseg(diag(0.7f, v), diag(1.08f, v * 1.4f), 0.06f, 0.04f, 0.06f, 0.04f, 1, 0.08f);
    }
    m.sseg(diag(0.3f, -0.06f), diag(0.3f, 0.06f), 0.04f, 0.04f, 0.07f, 0.07f, 0.8f);
    depth = 0.04f;
  } else if (k == "dial") {
    m.disc(0, 0, -0.07f, 0.05f, 0.44f, 0.95f);
    m.disc(0, 0, 0.05f, 0.065f, 0.46f, 1.1f);
    m.picture(0.068f, 0.9f);
    depth = 0.07f;
  } else if (k == "disc") {
    m.disc(0, 0, -0.02f, 0.02f, 0.46f);
    m.disc(0, 0, 0.02f, 0.035f, 0.17f); m.disc(0, 0, -0.035f, -0.02f, 0.17f);              // the label
    m.cbox(-0.03f, -0.03f, -0.04f, 0.03f, 0.03f, 0.04f, 0.6f);
    depth = 0.04f;
  } else if (k == "charm") {
    m.disc(0, -0.08f, -0.05f, 0.05f, 0.28f);
    m.glow = gemPx(I) && gemPx(I)->sat > 0.5f;
    m.disc(0, -0.08f, 0.05f, 0.09f, 0.17f);
    m.glow = false;
    for (int i = 0; i < 7; i++) {
      float a = PI * (0.15f + i / 7.f * 0.7f), c = PI * (0.15f + (i + 1) / 7.f * 0.7f);
      m.seg(vec3(std::cos(a) * 0.32f, 0.12f + std::sin(a) * 0.3f, 0), vec3(std::cos(c) * 0.32f, 0.12f + std::sin(c) * 0.3f, 0), 0.03f, 0.03f);
    }
    depth = 0.09f;
  } else return false;
  b.depth = depth; b.stand = stand;
  return true;
}

// ------------------------------------------------------------------ sculpt.js M3D
std::string modelKind(const std::string& key, const ItemDef* def) {
  if (!def || !def->block.empty()) return "";
  auto re = [&](const char* r) { return rxSearch(key, r); };
  if (re("^(splash_)?potion_|^glass_bottle$|^honey_bottle$|^experience_bottle$|^hex_flask$")) return "potion";
  if (re("bucket$")) return "bucket";
  if (re("^bowl$|_stew$|_soup$")) return "bowl";
  if (re("^book$|^enchanted_book$|^written_book$|^writable_book$|^tome_")) return "book";
  if (def->props.contains("armor") && def->props["armor"].is_object() && !def->has("cape")) {
    int slot = def->props["armor"].value("slot", -1);
    if (slot >= 0 && slot <= 3) return "armor";
  }
  if (key == "arrow") return "arrow";
  return "";
}

bool buildModelM3d(const std::string& k, ItemBuild& b) {
  M m(b);
  const SpriteInfo& I = b.I;
  const std::string& key = b.key;
  auto cring = [&](float y0, float y1, float r, float kk = 1) { m.ring(0, 0, y0, y1, r, kk); };
  auto cwall = [&](float y0, float y1, float r, float t, float kk = 1) { m.ring(0, 0, y0, y1, r, kk, r - t); };
  if (k == "potion") {
    // a flask: round belly, shoulder, neck, lip and cork
    bool empty = key == "glass_bottle", splash = startsWith(key, "splash_");
    float liq = empty ? 0.9f : 1.f, glass = 1.05f;
    cring(-0.5f, -0.44f, 0.2f, liq);
    cring(-0.44f, -0.12f, 0.29f, liq);
    cring(-0.12f, -0.02f, 0.23f, empty ? glass : liq);
    cring(-0.02f, 0.06f, 0.15f, glass);
    cring(0.06f, splash ? 0.22f : 0.3f, 0.085f, glass);
    cring(splash ? 0.2f : 0.28f, splash ? 0.26f : 0.34f, splash ? 0.13f : 0.115f, glass);
    cring(splash ? 0.24f : 0.32f, splash ? 0.34f : 0.44f, 0.07f);                       // the cork
    m.cbox(-0.2f, -0.3f, 0.29f, -0.12f, -0.1f, 0.305f, 1.12f);                           // a glint on the glass
    b.stand = true; b.depth = 0.3f;
  } else if (k == "bucket") {
    // tapered and banded, its handle up over the top, whatever's in it filling it to the brim
    for (int i = 0; i < 6; i++) cring(-0.5f + i * 0.09f, -0.5f + (i + 1) * 0.09f, 0.24f + i * 0.022f, i == 1 || i == 4 ? 0.85f : 1);
    cwall(0.04f, 0.12f, 0.37f, 0.05f);
    cring(0.035f, 0.08f, 0.33f, key == "bucket" ? 0.7f : 1);
    m.cbox(-0.4f, 0.04f, -0.025f, -0.36f, 0.3f, 0.025f, 0.85f); m.cbox(0.36f, 0.04f, -0.025f, 0.4f, 0.3f, 0.025f, 0.85f);
    m.cbox(-0.4f, 0.3f, -0.025f, 0.4f, 0.34f, 0.025f, 0.85f);
    b.stand = true; b.depth = 0.37f;
  } else if (k == "bowl") {
    cring(-0.5f, -0.44f, 0.17f, 0.85f);
    cring(-0.44f, -0.34f, 0.27f);
    cring(-0.34f, -0.22f, 0.36f);
    cwall(-0.22f, -0.1f, 0.42f, 0.05f);
    cring(-0.22f, -0.13f, 0.38f, key == "bowl" ? 0.8f : 1);
    if (key != "bowl") { m.cbox(-0.12f, -0.13f, -0.05f, 0.02f, -0.09f, 0.08f, 1.1f); m.cbox(0.08f, -0.13f, -0.14f, 0.18f, -0.1f, -0.04f, 1.1f); }
    b.stand = true; b.depth = 0.42f;
  } else if (k == "book") {
    const float W = 0.3f, H = 0.4f, D = 0.11f;
    m.cbox(-W, -H, D - 0.035f, W + 0.02f, H, D);                                         // the front board
    m.cbox(-W, -H, -D, W + 0.02f, H, -D + 0.035f);                                       // the back board
    m.cbox(-W - 0.03f, -H, -D, -W + 0.01f, H, D, 0.9f);                                  // the spine
    m.cbox(-W + 0.01f, -H + 0.03f, -D + 0.035f, W, H - 0.03f, D - 0.035f, 1.12f);        // the pages
    m.cbox(-0.12f, 0.02f, D, 0.14f, 0.24f, D + 0.02f, 1.1f);                             // a mark on the cover
    m.cbox(-W - 0.035f, 0.2f, -D - 0.005f, -W + 0.015f, 0.25f, D + 0.005f, 1.1f);
    m.cbox(-W - 0.035f, -0.25f, -D - 0.005f, -W + 0.015f, -0.2f, D + 0.005f, 1.1f);
    b.depth = D + 0.02f;
  } else if (k == "armor") {
    const SpritePx* main = I.common([](const SpritePx& p) { return p.lum > 30 && p.lum < 235; });
    if (!main) main = I.common();
    const SpritePx* trim = main ? I.best([](const SpritePx& p) { return p.sat * 255; }, [&](const SpritePx& p) { return std::abs(p.r - main->r) + std::abs(p.g - main->g) + std::abs(p.b - main->b) > 80 && p.sat > 0.35f; }) : nullptr;
    int slot = b.def->props["armor"].value("slot", 0);
    if (slot == 0) {                                     // helmet: a dome with cheek guards, open at the face
      m.cbox(-0.33f, 0.05f, -0.33f, 0.33f, 0.26f, 0.33f);
      m.cbox(-0.27f, 0.26f, -0.27f, 0.27f, 0.33f, 0.27f, 0.95f);
      m.cbox(-0.33f, -0.28f, -0.33f, -0.25f, 0.05f, 0.33f); m.cbox(0.25f, -0.28f, -0.33f, 0.33f, 0.05f, 0.33f);
      m.cbox(-0.25f, -0.28f, -0.33f, 0.25f, 0.05f, -0.25f);
      m.cbox(-0.28f, 0.0f, 0.3f, 0.28f, 0.08f, 0.35f, 0.85f);
      m.cbox(-0.25f, -0.2f, 0.26f, -0.13f, 0.0f, 0.33f); m.cbox(0.13f, -0.2f, 0.26f, 0.25f, 0.0f, 0.33f);
      if (trim) m.cbox(-0.03f, 0.26f, -0.3f, 0.03f, 0.38f, 0.3f, 1.1f);                  // a crest
      b.depth = 0.36f;
    } else if (slot == 1) {                              // chestplate: the body, the shoulders, a belt
      m.cbox(-0.28f, -0.42f, -0.15f, 0.28f, 0.28f, 0.15f);
      m.cbox(-0.46f, 0.04f, -0.17f, -0.28f, 0.3f, 0.17f, 0.95f); m.cbox(0.28f, 0.04f, -0.17f, 0.46f, 0.3f, 0.17f, 0.95f);
      m.cbox(-0.44f, -0.12f, -0.14f, -0.3f, 0.04f, 0.14f); m.cbox(0.3f, -0.12f, -0.14f, 0.44f, 0.04f, 0.14f);
      m.cbox(-0.1f, 0.2f, 0.15f, 0.1f, 0.28f, 0.17f, 0.8f);                               // the neck
      m.cbox(-0.29f, -0.34f, -0.16f, 0.29f, -0.27f, 0.16f, 0.85f);                       // the belt
      m.cbox(-0.02f, -0.25f, 0.15f, 0.02f, 0.18f, 0.165f, 0.9f);                         // down the middle
      b.depth = 0.17f;
    } else if (slot == 2) {                              // leggings: a waistband and two legs
      m.cbox(-0.3f, 0.2f, -0.15f, 0.3f, 0.36f, 0.15f);
      m.cbox(-0.31f, 0.24f, -0.16f, 0.31f, 0.3f, 0.16f, 0.85f);
      m.cbox(-0.3f, -0.48f, -0.14f, -0.03f, 0.2f, 0.14f); m.cbox(0.03f, -0.48f, -0.14f, 0.3f, 0.2f, 0.14f);
      m.cbox(-0.31f, -0.2f, -0.15f, -0.02f, -0.12f, 0.15f, 0.9f); m.cbox(0.02f, -0.2f, -0.15f, 0.31f, -0.12f, 0.15f, 0.9f);
      b.depth = 0.16f;
    } else {                                             // boots: a pair, toes toward you
      for (int s : {-1, 1}) {
        float x0 = s < 0 ? -0.38f : 0.06f, x1 = s < 0 ? -0.06f : 0.38f;
        m.cbox(x0, -0.2f, -0.13f, x1, 0.18f, 0.11f);
        m.cbox(x0, -0.4f, -0.13f, x1, -0.2f, 0.3f);
        m.cbox(x0 - 0.01f, -0.46f, -0.14f, x1 + 0.01f, -0.4f, 0.31f, 0.8f);
        m.cbox(x0 - 0.01f, 0.12f, -0.14f, x1 + 0.01f, 0.2f, 0.12f, 1.1f);
      }
      b.depth = 0.31f;
    }
  } else if (k == "arrow") {
    auto at = [](float u) { return vec3(-0.48f + RT2 * u * 1.36f, -0.48f + RT2 * u * 1.36f, 0); };
    m.seg(at(0.02f), at(0.8f), 0.045f, 0.045f);                                          // the shaft
    m.seg(at(0.78f), at(0.93f), 0.1f, 0.04f); m.seg(at(0.93f), at(1.0f), 0.05f, 0.03f);   // the head
    m.seg(at(0.03f), at(0.2f), 0.12f, 0.012f); m.seg(at(0.03f), at(0.2f), 0.012f, 0.12f); // the fletching
    b.depth = 0.06f;
  } else return false;
  return true;
}

// ------------------------------------------------------------------ items3d.js
namespace {
// partSeg: a bar from a to b coloured from a patch (in 16-pixel units [x0, y0, x1, y1]) of a tile
PaintFn tileRect(const ItemBuild& b, int tile, vec4 r) {
  const TileSet& ts = b.ts; const float k = ts.size / 16.f;
  return [&ts, tile, r, k](const PaintPt& p) {
    float u = r.x + p.t * (r.z - r.x), v = r.w + (p.s + 1) * 0.5f * (r.y - r.w);
    return tileVoxel(ts, tile, clampv((int)(u * k), (int)(r.x * k), (int)(r.z * k) - 1), clampv((int)(v * k), (int)(r.y * k), (int)(r.w * k) - 1));
  };
}
// addBox: a box with a whole tile on each face (faces +x -x +y -y +z -z); each voxel takes the face it lies nearest
PaintFn tileBox(const ItemBuild& b, vec3 lo, vec3 hi, std::array<int, 6> tiles) {
  const TileSet& ts = b.ts;
  return [&ts, lo, hi, tiles](const PaintPt& p) {
    vec3 n = (p.p - lo) / glm::max(hi - lo, vec3(1e-5f));
    n = glm::clamp(n, vec3(0), vec3(0.999f));
    float d[6] = {hi.x - p.p.x, p.p.x - lo.x, hi.y - p.p.y, p.p.y - lo.y, hi.z - p.p.z, p.p.z - lo.z};
    int f = 4; for (int i : {5, 0, 1, 2, 3}) if (d[i] < d[f] - 1e-5f) f = i;
    float s, t;
    switch (f) {
      case 0: s = 1 - n.z; t = 1 - n.y; break;
      case 1: s = n.z; t = 1 - n.y; break;
      case 2: s = n.x; t = n.z; break;
      case 3: s = 1 - n.x; t = n.z; break;
      case 4: s = n.x; t = 1 - n.y; break;
      default: s = 1 - n.x; t = 1 - n.y; break;
    }
    int S = ts.size;
    return tileVoxel(ts, tiles[f], clampv((int)(s * S), 0, S - 1), clampv((int)(t * S), 0, S - 1));
  };
}
struct Parts {
  ItemBuild& b;
  void seg(vec3 a, vec3 c, float w, float d, const std::string& tile, vec4 rect = vec4(6, 6, 10, 10)) { b.P.seg(a, c, w, d, tileRect(b, b.tile(tile), rect)); }
  void segT(vec3 a, vec3 c, float w, float d, int tile, vec4 rect) { b.P.seg(a, c, w, d, tileRect(b, tile, rect)); }
};

vec2 bowPoint(float u, float pull) {
  vec2 t0 = vec2(-0.43f) * (1 - pull * 0.09f), t1 = vec2(0.43f) * (1 - pull * 0.09f), c = vec2(-0.47f, 0.47f) * (1 + pull * 0.3f);
  return (1 - u) * (1 - u) * t0 + 2 * (1 - u) * u * c + u * u * t1;
}
const char* STRING_TILE = "white_wool";

void buildRod(Parts& P, bool withLine, vec3 hang) {
  const vec2 A(-0.47f), T(0.47f), N(0.7071f, -0.7071f);
  auto at = [&](float u, float off = 0, float z = 0) { return vec3(A + (T - A) * u + N * off, z); };
  vec4 box4(4, 4, 8, 8), grain(4, 2, 7, 14);
  P.seg(at(0), at(0.025f), 0.095f, 0.095f, "dark_oak_planks", box4);                                 // butt cap
  for (int i = 0; i < 6; i++) P.seg(at(0.025f + i * 0.035f), at(0.06f + i * 0.035f), 0.085f, 0.085f, i % 2 ? "stripped_birch_log_side" : "stripped_oak_log_side", grain);   // cork grip in rings
  P.seg(at(0.235f), at(0.315f), 0.072f, 0.072f, "iron_block", box4);                                // reel seat
  P.seg(at(0.315f), at(0.37f), 0.07f, 0.07f, "stripped_birch_log_side", grain);                      // fore grip
  for (int i = 0; i < 10; i++) { float u0 = 0.37f + i * 0.063f, w = 0.05f - i * 0.0034f; P.seg(at(u0), at(u0 + 0.064f), w, w, "stripped_spruce_log_side", vec4(4, 2, 6, 14)); }   // the blank, tapering
  for (float u : {0.46f, 0.61f, 0.75f, 0.88f}) {
    P.seg(at(u - 0.014f), at(u + 0.014f), 0.058f, 0.058f, "red_wool", box4);                         // thread wraps
    P.seg(at(u, 0.025f), at(u, 0.07f), 0.012f, 0.03f, "iron_block", box4);                           // line guides
  }
  P.seg(at(0.993f), at(1.0f), 0.03f, 0.03f, "iron_block", box4);                                     // tip top
  // the reel under the seat: a foot, a spool of line, a crank with a knob
  P.seg(at(0.275f, 0.035f), at(0.275f, 0.085f), 0.025f, 0.03f, "iron_block", box4);
  P.seg(at(0.245f, 0.12f), at(0.305f, 0.12f), 0.1f, 0.1f, "iron_block", box4);
  P.seg(at(0.255f, 0.12f), at(0.295f, 0.12f), 0.105f, 0.07f, STRING_TILE);
  P.seg(at(0.24f, 0.12f), at(0.24f, 0.19f), 0.018f, 0.018f, "iron_block", box4);
  P.seg(at(0.232f, 0.19f, 0.03f), at(0.248f, 0.19f, 0.03f), 0.03f, 0.05f, "dark_oak_planks", box4);
  // the line: off the spool, through each guide, to the tip
  vec3 pts[6] = {at(0.3f, 0.12f), at(0.46f, 0.07f), at(0.61f, 0.07f), at(0.75f, 0.068f), at(0.88f, 0.066f), at(1.0f, 0.0f)};
  for (int i = 0; i < 5; i++) P.seg(pts[i], pts[i + 1], 0.008f, 0.008f, STRING_TILE);
  if (withLine) {
    // not cast: the line hangs from the tip, with the float and the hook on it
    vec3 t = at(1.0f); float hl = glm::length(hang); if (hl <= 0) hl = 1;
    auto Pp = [&](float d) { return t + hang / hl * d; };
    // the hanging line runs in 3D (z changes): pieces along it keep their own depth
    auto line = [&](vec3 a, vec3 c, float w, const std::string& tile, vec4 rect = vec4(6, 6, 10, 10)) {
      int n = std::max(1, (int)std::ceil(glm::length(c - a) / 0.03f));
      for (int i = 0; i < n; i++) { vec3 p0 = glm::mix(a, c, (float)i / n), p1 = glm::mix(a, c, (float)(i + 1) / n); P.seg(p0, p1, w, std::max(w, std::abs(p1.z - p0.z) + w), tile, rect); }
    };
    line(t, Pp(0.34f), 0.01f, STRING_TILE);
    line(Pp(0.34f), Pp(0.4f), 0.07f, "red_wool", box4);
    line(Pp(0.4f), Pp(0.45f), 0.07f, "white_wool", box4);
    line(Pp(0.45f), Pp(0.53f), 0.01f, STRING_TILE);
    line(Pp(0.53f), Pp(0.58f), 0.015f, "iron_block", box4);
    vec3 q = Pp(0.58f); P.seg(q, q + vec3(0.04f, 0.02f, 0), 0.015f, 0.015f, "iron_block", box4);
  }
}
} // namespace

bool buildItems3d(ItemBuild& b) {
  const std::string& key = b.key;
  const ItemDef* def = b.def;
  Parts P{b};
  static const std::regex barRe("_ingot$|^brick$|^nether_brick$|_bar$|^netherite_scrap$");
  if (def && def->block.empty() && std::regex_search(key, barRe)) {
    // a bar: Vintage Story's ingot, cast in a mould (narrower on top)
    int t = b.I.tile >= 0 ? b.I.tile : 0;
    P.segT(vec3(-0.42f, 0, 0), vec3(0.42f, 0, 0), 0.34f, 0.15f, t, vec4(5, 6, 11, 10));
    P.segT(vec3(-0.36f, 0, 0.06f), vec3(0.36f, 0, 0.06f), 0.26f, 0.12f, t, vec4(6, 5, 10, 9));
    b.depth = 0.135f;
    return true;
  }
  if (key == "bow" || startsWith(key, "bow_pulling_")) {
    float pull = key == "bow" ? 0.f : std::array<float, 3>{0.35f, 0.65f, 1.f}[clampv(key.back() - '0', 0, 2)];
    const int N = 10;
    for (int i = 0; i < N; i++) {
      float u0 = (float)i / N, u1 = (float)(i + 1) / N, mid = std::abs((u0 + u1) / 2 - 0.5f);
      vec2 a = bowPoint(u0, pull), c = bowPoint(u1, pull);
      bool grip = mid < 0.08f; float w = grip ? 0.1f : 0.085f - mid * 0.08f;
      P.seg(vec3(a, 0), vec3(c, 0), w, grip ? 0.1f : 0.07f, grip ? "leather" : "stripped_oak_log_side", grip ? vec4(5, 5, 9, 9) : vec4(4, 2, 6, 14));
    }
    for (vec2 t : {bowPoint(0, pull), bowPoint(1, pull)}) P.seg(vec3(t - 0.03f, 0), vec3(t + 0.03f, 0), 0.07f, 0.08f, "dark_oak_planks");   // nocks
    vec2 t0 = bowPoint(0, pull), t1 = bowPoint(1, pull), nk(pull * 0.27f, -pull * 0.27f);
    if (pull > 0) { P.seg(vec3(t0, 0), vec3(nk, 0), 0.016f, 0.016f, STRING_TILE); P.seg(vec3(t1, 0), vec3(nk, 0), 0.016f, 0.016f, STRING_TILE); }
    else P.seg(vec3(t0, 0), vec3(t1, 0), 0.016f, 0.016f, STRING_TILE);
    if (pull > 0) {
      // the arrow on the string, pointing through the grip
      vec2 d(-0.7071f, 0.7071f);
      auto at = [&](float u) { return vec3(nk + d * u, 0); };
      P.seg(at(0), at(0.74f), 0.035f, 0.035f, "stripped_oak_log_side", vec4(5, 3, 6, 12));
      P.seg(at(0.72f), at(0.86f), 0.07f, 0.03f, "iron_block", vec4(4, 4, 8, 8));
      P.seg(at(0.02f), at(0.16f), 0.09f, 0.012f, "white_wool", vec4(4, 4, 8, 8));
    }
    b.depth = 0.05f;
    return true;
  }
  if (key == "fishing_rod" || key == "fishing_rod_cast" || key == "fishing_rod_tp") {
    // in first person the line hangs one way from the tip; held by someone you look at, another
    buildRod(P, key != "fishing_rod_cast", key == "fishing_rod_tp" ? vec3(-1, 0.04f, 0) : vec3(0, -0.76f, 0.65f));
    b.depth = 0.05f;
    return true;
  }
  if (key == "leather_backpack" || key == "reinforced_backpack") {
    // a proper pack, with a flap, a pouch, side pockets and a bedroll, as it is held and dropped
    bool big = key == "reinforced_backpack";
    int body = b.tile(big ? "pack_body_r" : "pack_body_l"), flap = b.tile(big ? "pack_flap_r" : "pack_flap_l"), metal = b.tile(big ? "pack_iron" : "pack_brass"),
        roll = b.tile(big ? "pack_roll_r" : "pack_roll_l"), strap = b.tile("pack_strap"), iron = b.tile("pack_iron");
    auto tf = [](vec3 v) { return vec3(v.x * 0.1f, (v.y - 7) * 0.1f, (v.z - 4.2f) * 0.1f); };
    auto box = [&](vec3 a, vec3 c, std::array<int, 6> t) { vec3 p = tf(a), q = tf(c), lo = glm::min(p, q), hi = glm::max(p, q); b.P.box(lo, hi, tileBox(b, lo, hi, t)); };
    auto all = [](int t) { return std::array<int, 6>{t, t, t, t, t, t}; };
    float w = 3.5f + big * 0.3f, top = 10.5f + big * 0.4f, d = 5.2f + big * 0.4f;
    box({-w, 1.4f, 2.0f}, {w, top, d}, all(body));                                                  // the bag
    box({-w - 0.2f, top - 0.9f, 1.9f}, {w + 0.2f, top + 0.4f, d + 0.4f}, {body, body, flap, body, flap, body});   // the lid
    box({-3.0f, top - 4.2f, d}, {3.0f, top + 0.4f, d + 0.4f}, all(flap));                           // the flap
    box({-0.7f, top - 4.0f, d + 0.4f}, {0.7f, top - 3.0f, d + 0.6f}, all(metal));                   // its buckle
    box({-2.6f, 2.1f, d - 0.2f}, {2.6f, 5.8f, d + 1.4f}, all(body));                                // the pouch
    box({-2.7f, 4.7f, d - 0.2f}, {2.7f, 6.0f, d + 1.55f}, {flap, flap, flap, body, flap, flap});
    box({-0.45f, 4.6f, d + 1.55f}, {0.45f, 5.3f, d + 1.7f}, all(metal));
    for (int s : {-1, 1}) {
      box({s * w, 2.4f, 2.6f}, {s * (w + 1.1f), 6.6f, 4.8f}, all(body));                            // side pockets
      box({s * w, 6.0f, 2.5f}, {s * (w + 1.2f), 6.7f, 4.9f}, all(flap));
    }
    box({-4.4f, top + 0.4f, 2.5f}, {4.4f, top + 2.1f, 4.6f}, all(roll));                           // a bedroll on top
    for (float sx : {-2.6f, 2.6f}) box({sx - 0.35f, top + 0.3f, 2.4f}, {sx + 0.35f, top + 2.2f, 4.7f}, all(strap));
    if (big) for (float yy : {3.2f, 8.0f}) box({-w - 0.12f, yy, 1.95f}, {w + 0.12f, yy + 0.6f, d + 0.1f}, all(iron));   // iron bands
    for (float sx : {-2.3f, 2.3f}) box({sx - 0.6f, 3.0f, 1.6f}, {sx + 0.6f, 11.0f, 2.0f}, all(strap));   // the straps, loose
    b.kind = VoxelModel::Block;
    b.depth = 0.3f;
    return true;
  }
  return false;
}

} // namespace dq
