// ui/glyphs.cpp — the ink pictures (ui/glyphs.h): each drawn in a box from -1 to 1 about its centre (y down), in one colour.
#include "ui/glyphs.h"
#include <cmath>
#include <vector>

namespace dq::ui {

namespace {
struct Pen {
  ImDrawList* dl; ImVec2 c; float r; ImU32 col; float t;
  ImVec2 P(float x, float y) const { return ImVec2(c.x + x * r, c.y + y * r); }
  void line(float x0, float y0, float x1, float y1, float k = 1) const { dl->AddLine(P(x0, y0), P(x1, y1), col, t * k); }
  void disc(float x, float y, float rr) const { dl->AddCircleFilled(P(x, y), rr * r, col, 16); }
  void ring(float x, float y, float rr, float k = 1) const { dl->AddCircle(P(x, y), rr * r, col, 20, t * k); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2) const { dl->AddTriangleFilled(P(x0, y0), P(x1, y1), P(x2, y2), col); }
  void rect(float x0, float y0, float x1, float y1, float round = 0) const { dl->AddRectFilled(P(x0, y0), P(x1, y1), col, round * r); }
  void poly(std::initializer_list<ImVec2> pts, bool closed, float k = 1) const {
    std::vector<ImVec2> v; for (auto& p : pts) v.push_back(P(p.x, p.y));
    dl->AddPolyline(v.data(), (int)v.size(), col, closed ? ImDrawFlags_Closed : 0, t * k);
  }
  void fillConvex(std::initializer_list<ImVec2> pts) const {
    std::vector<ImVec2> v; for (auto& p : pts) v.push_back(P(p.x, p.y));
    dl->AddConvexPolyFilled(v.data(), (int)v.size(), col);
  }
  // a star of n points, filled as triangles from the middle (it isn't convex)
  void star(int n, float ro, float ri, float rot = -1.5708f) const {
    for (int i = 0; i < 2 * n; i++) {
      const float a0 = rot + i * 3.14159265f / n, a1 = rot + (i + 1) * 3.14159265f / n;
      const float r0 = (i & 1) ? ri : ro, r1 = (i & 1) ? ro : ri;
      dl->AddTriangleFilled(c, P(std::cos(a0) * r0, std::sin(a0) * r0), P(std::cos(a1) * r1, std::sin(a1) * r1), col);
    }
  }
};
} // namespace

void drawGlyph(ImDrawList* dl, Glyph g, ImVec2 c, float r, ImU32 col) {
  if (g == Glyph::None || r < 2) return;
  const Pen p{dl, c, r, col, std::max(1.f, r * 0.17f)};
  switch (g) {
    case Glyph::None: break;
    case Glyph::Heart: p.disc(-0.42f, -0.22f, 0.46f); p.disc(0.42f, -0.22f, 0.46f); p.tri(-0.86f, -0.08f, 0.86f, -0.08f, 0, 0.88f); break;
    case Glyph::Sword:
      p.line(-0.42f, 0.42f, 0.78f, -0.78f, 1.5f); p.tri(0.62f, -0.88f, 0.88f, -0.62f, 0.92f, -0.92f);
      p.line(-0.78f, 0.1f, -0.1f, 0.78f, 1.3f); p.line(-0.45f, 0.45f, -0.75f, 0.75f, 1.4f); p.disc(-0.82f, 0.82f, 0.13f); break;
    case Glyph::Dagger:
      p.line(-0.2f, 0.2f, 0.55f, -0.55f, 1.6f); p.tri(0.42f, -0.68f, 0.68f, -0.42f, 0.78f, -0.78f);
      p.line(-0.52f, -0.02f, 0.02f, 0.52f, 1.3f); p.line(-0.24f, 0.24f, -0.62f, 0.62f, 1.4f); p.disc(-0.7f, 0.7f, 0.14f); break;
    case Glyph::Shield:
      p.poly({{-0.72f, -0.78f}, {0.72f, -0.78f}, {0.72f, -0.08f}, {0.42f, 0.5f}, {0, 0.88f}, {-0.42f, 0.5f}, {-0.72f, -0.08f}}, true, 1.2f);
      p.line(0, -0.62f, 0, 0.66f); p.line(-0.56f, -0.2f, 0.56f, -0.2f); break;
    case Glyph::Drop: p.disc(0, 0.3f, 0.56f); p.tri(-0.52f, 0.12f, 0.52f, 0.12f, 0, -0.92f); break;
    case Glyph::Star: p.star(4, 0.95f, 0.3f); p.disc(0.62f, -0.62f, 0.12f); p.disc(-0.6f, 0.62f, 0.09f); break;
    case Glyph::Ward: p.ring(0, 0, 0.82f, 1.1f); { const Pen q{dl, c, r * 0.62f, col, p.t}; q.star(4, 0.95f, 0.32f); } break;
    case Glyph::Bolt: p.tri(0.25f, -0.95f, -0.58f, 0.14f, 0.08f, 0.14f); p.tri(-0.25f, 0.95f, 0.58f, -0.14f, -0.08f, -0.14f); break;
    case Glyph::Wing:
      p.poly({{-0.85f, 0.55f}, {-0.55f, -0.35f}, {0.1f, -0.75f}, {0.9f, -0.8f}}, false, 1.2f);
      p.line(-0.55f, -0.35f, 0.75f, -0.35f); p.line(-0.68f, 0.05f, 0.55f, 0.08f); p.line(-0.78f, 0.38f, 0.28f, 0.5f); break;
    case Glyph::Target: p.ring(0, 0, 0.78f); p.ring(0, 0, 0.42f); p.disc(0, 0, 0.13f);
      p.line(0, -0.98f, 0, -0.62f); p.line(0, 0.62f, 0, 0.98f); p.line(-0.98f, 0, -0.62f, 0); p.line(0.62f, 0, 0.98f, 0); break;
    case Glyph::Burst: p.star(8, 0.98f, 0.42f, -1.5708f + 0.2f); break;
    case Glyph::Arrow:
      p.line(-0.72f, 0.72f, 0.6f, -0.6f, 1.2f); p.tri(0.92f, -0.92f, 0.3f, -0.7f, 0.7f, -0.3f);
      p.line(-0.72f, 0.72f, -0.72f, 0.28f); p.line(-0.72f, 0.72f, -0.28f, 0.72f); p.line(-0.5f, 0.5f, -0.5f, 0.12f); p.line(-0.5f, 0.5f, -0.12f, 0.5f); break;
    case Glyph::Pierce:
      p.line(-0.95f, 0, 0.55f, 0, 1.2f); p.tri(0.98f, 0, 0.45f, -0.38f, 0.45f, 0.38f);
      p.line(-0.15f, -0.85f, -0.15f, -0.25f, 1.6f); p.line(-0.15f, 0.25f, -0.15f, 0.85f, 1.6f); break;
    case Glyph::Fist:
      p.rect(-0.3f, -0.55f, 0.8f, 0.5f, 0.22f); p.rect(-0.45f, 0.1f, 0.2f, 0.6f, 0.15f);
      p.line(-0.98f, -0.35f, -0.5f, -0.35f); p.line(-0.98f, 0, -0.55f, 0); p.line(-0.9f, 0.35f, -0.6f, 0.35f); break;
    case Glyph::Anvil:
      p.rect(-0.82f, -0.5f, 0.55f, -0.15f); p.tri(0.55f, -0.5f, 0.98f, -0.5f, 0.55f, -0.15f);
      p.rect(-0.28f, -0.15f, 0.22f, 0.38f); p.rect(-0.62f, 0.38f, 0.56f, 0.72f, 0.05f); break;
    case Glyph::Clover:
      p.disc(-0.36f, -0.1f, 0.34f); p.disc(0.36f, -0.1f, 0.34f); p.disc(0, -0.46f, 0.34f); p.disc(0, 0.26f, 0.34f);
      p.line(0.05f, 0.4f, 0.4f, 0.95f, 1.1f); break;
    case Glyph::Hook:
      p.ring(0.22f, -0.82f, 0.14f); p.line(0.22f, -0.68f, 0.22f, 0.3f, 1.1f);
      dl->PathArcTo(p.P(-0.15f, 0.3f), 0.37f * r, 0, 3.14159265f, 12); dl->PathStroke(col, 0, p.t * 1.1f);
      p.tri(-0.52f, 0.3f, -0.72f, -0.05f, -0.32f, 0.12f); break;
    case Glyph::Hourglass:
      p.line(-0.65f, -0.88f, 0.65f, -0.88f, 1.3f); p.line(-0.65f, 0.88f, 0.65f, 0.88f, 1.3f);
      p.poly({{-0.52f, -0.88f}, {0.52f, -0.88f}, {0.08f, 0}, {0.52f, 0.88f}, {-0.52f, 0.88f}, {-0.08f, 0}}, true);
      p.tri(-0.36f, 0.82f, 0.36f, 0.82f, 0, 0.35f); p.tri(-0.25f, -0.58f, 0.25f, -0.58f, 0, -0.25f); break;
    case Glyph::Skull:
      p.poly({{-0.38f, 0.42f}, {-0.38f, 0.82f}, {0.38f, 0.82f}, {0.38f, 0.42f}}, false);
      dl->PathArcTo(p.P(0, -0.15f), 0.72f * r, 2.25f, 2.25f + 4.9f, 24); dl->PathStroke(col, 0, p.t);
      p.disc(-0.3f, -0.12f, 0.2f); p.disc(0.3f, -0.12f, 0.2f); p.tri(0, 0.12f, -0.1f, 0.32f, 0.1f, 0.32f);
      p.line(-0.13f, 0.58f, -0.13f, 0.82f, 0.7f); p.line(0.13f, 0.58f, 0.13f, 0.82f, 0.7f); break;
    case Glyph::Flame:
      p.disc(0, 0.32f, 0.56f); p.tri(-0.53f, 0.16f, 0.53f, 0.16f, 0.12f, -0.96f);
      p.tri(-0.56f, 0.2f, -0.2f, 0.0f, -0.62f, -0.42f); p.tri(0.56f, 0.2f, 0.25f, 0.0f, 0.6f, -0.3f); break;
    case Glyph::Snow:
      for (int i = 0; i < 3; i++) {
        const float a = i * 1.0472f + 1.5708f, ca = std::cos(a), sa = std::sin(a);
        p.line(-ca * 0.95f, -sa * 0.95f, ca * 0.95f, sa * 0.95f, 1.1f);
        for (int s = -1; s <= 1; s += 2) for (int e = -1; e <= 1; e += 2) {
          const float bx = ca * 0.58f * s, by = sa * 0.58f * s, b2 = a + e * 0.75f + (s < 0 ? 3.14159265f : 0);
          p.line(bx, by, bx + std::cos(b2) * 0.3f, by + std::sin(b2) * 0.3f, 0.85f);
        }
      }
      break;
    case Glyph::Leaf:
      dl->AddEllipseFilled(p.P(0.08f, -0.08f), ImVec2(0.86f * r, 0.42f * r), col, -0.785f, 20);
      p.line(-0.5f, 0.5f, -0.92f, 0.92f, 1.1f); break;
    case Glyph::Eye:
      dl->AddBezierQuadratic(p.P(-0.96f, 0), p.P(0, -0.98f), p.P(0.96f, 0), col, p.t, 14);
      dl->AddBezierQuadratic(p.P(-0.96f, 0), p.P(0, 0.98f), p.P(0.96f, 0), col, p.t, 14);
      p.disc(0, 0, 0.3f); break;
    case Glyph::Sun:
      p.disc(0, 0, 0.44f);
      for (int i = 0; i < 8; i++) { const float a = i * 0.7854f; p.line(std::cos(a) * 0.62f, std::sin(a) * 0.62f, std::cos(a) * 0.96f, std::sin(a) * 0.96f, 1.1f); }
      break;
    case Glyph::Swirl: {
      for (int i = 0; i <= 40; i++) { const float u = i / 40.f, a = u * 2.6f * 6.2832f, rr = 0.08f + u * 0.86f; dl->PathLineTo(p.P(std::cos(a) * rr, std::sin(a) * rr)); }
      dl->PathStroke(col, 0, p.t * 1.1f);
      break;
    }
    case Glyph::Book:
      p.poly({{0, -0.5f}, {-0.92f, -0.68f}, {-0.92f, 0.6f}, {0, 0.78f}, {0.92f, 0.6f}, {0.92f, -0.68f}}, true);
      p.line(0, -0.5f, 0, 0.78f);
      for (int i = 0; i < 3; i++) { const float y = -0.3f + i * 0.3f; p.line(-0.72f, y - 0.04f, -0.2f, y + 0.06f, 0.7f); p.line(0.2f, y + 0.06f, 0.72f, y - 0.04f, 0.7f); }
      break;
    case Glyph::Paw:
      dl->AddEllipseFilled(p.P(0, 0.38f), ImVec2(0.46f * r, 0.38f * r), col, 0, 16);
      p.disc(-0.66f, -0.14f, 0.18f); p.disc(-0.26f, -0.52f, 0.19f); p.disc(0.26f, -0.52f, 0.19f); p.disc(0.66f, -0.14f, 0.18f); break;
    case Glyph::Crown:
      p.rect(-0.78f, 0.22f, 0.78f, 0.6f); p.tri(-0.78f, 0.24f, -0.78f, -0.62f, -0.28f, 0.24f);
      p.tri(-0.38f, 0.24f, 0, -0.8f, 0.38f, 0.24f); p.tri(0.28f, 0.24f, 0.78f, -0.62f, 0.78f, 0.24f);
      p.disc(-0.78f, -0.7f, 0.12f); p.disc(0, -0.88f, 0.12f); p.disc(0.78f, -0.7f, 0.12f); break;
    case Glyph::Note:
      dl->AddEllipseFilled(p.P(-0.3f, 0.58f), ImVec2(0.32f * r, 0.23f * r), col, -0.45f, 14);
      p.line(-0.02f, 0.52f, -0.02f, -0.85f, 1.1f); p.poly({{-0.02f, -0.85f}, {0.45f, -0.55f}, {0.55f, -0.15f}}, false, 1.1f); break;
    case Glyph::Wave:
      for (int k = 0; k < 2; k++) { const float y0 = -0.3f + k * 0.6f;
        for (int i = 0; i <= 24; i++) { const float u = i / 24.f; dl->PathLineTo(p.P(-0.95f + u * 1.9f, y0 + std::sin(u * 6.2832f * 1.5f) * 0.2f)); }
        dl->PathStroke(col, 0, p.t * 1.1f); }
      break;
    case Glyph::Mountain: p.tri(-0.96f, 0.72f, -0.25f, -0.62f, 0.46f, 0.72f); p.tri(0.0f, 0.72f, 0.5f, -0.18f, 0.98f, 0.72f); break;
    case Glyph::Orb: p.ring(0, 0, 0.8f, 1.1f); { const Pen q{dl, c, r * 0.5f, col, p.t}; q.star(4, 0.95f, 0.3f); } p.disc(-0.38f, -0.4f, 0.1f); break;
  }
}

Glyph glyphForSchool(const std::string& s) {
  if (s == "fire") return Glyph::Flame;
  if (s == "frost") return Glyph::Snow;
  if (s == "life") return Glyph::Heart;
  if (s == "storm") return Glyph::Bolt;
  if (s == "arcane") return Glyph::Star;
  if (s == "alter") return Glyph::Swirl;
  if (s == "illusion") return Glyph::Eye;
  if (s == "divine") return Glyph::Sun;
  if (s == "death") return Glyph::Skull;
  if (s == "nature") return Glyph::Leaf;
  return Glyph::Orb;
}

Glyph glyphForAttr(const std::string& a) {
  if (a.rfind("spell_power.", 0) == 0) return glyphForSchool(a.substr(12));
  static const std::pair<const char*, Glyph> M[] = {
    {"health", Glyph::Heart}, {"overheal", Glyph::Heart}, {"damage", Glyph::Sword}, {"attack_speed", Glyph::Dagger},
    {"armor", Glyph::Shield}, {"stun_armor", Glyph::Shield}, {"mana", Glyph::Drop}, {"mana_regen", Glyph::Drop},
    {"spell_power", Glyph::Star}, {"spell_resist", Glyph::Ward}, {"stamina", Glyph::Bolt}, {"stamina_regen", Glyph::Bolt},
    {"speed", Glyph::Wing}, {"dodge", Glyph::Wing}, {"step", Glyph::Wing}, {"crit_chance", Glyph::Target}, {"crit_damage", Glyph::Burst},
    {"arrow_damage", Glyph::Arrow}, {"arrow_speed", Glyph::Arrow}, {"draw_speed", Glyph::Arrow}, {"armor_pierce", Glyph::Pierce},
    {"posture_damage", Glyph::Fist}, {"knockback", Glyph::Fist}, {"knock_resist", Glyph::Anvil}, {"weight", Glyph::Anvil},
    {"luck", Glyph::Clover}, {"fishing", Glyph::Hook}, {"cooldown", Glyph::Hourglass}, {"cast_time", Glyph::Hourglass},
    {"summon_damage", Glyph::Paw},
  };
  for (auto& [k, g] : M) if (a == k) return g;
  return Glyph::None;
}

Glyph glyphForNode(const std::string& attr, const std::string& desc, const std::string& tag, int size) {
  static const std::pair<const char*, Glyph> TAGS[] = {
    {"Pyro", Glyph::Flame}, {"Cryo", Glyph::Snow}, {"Electro", Glyph::Bolt}, {"Aqua", Glyph::Wave}, {"Geo", Glyph::Mountain},
    {"Druid", Glyph::Leaf}, {"Bard", Glyph::Note}, {"Priest", Glyph::Sun}, {"Evoker", Glyph::Swirl}, {"Cultist", Glyph::Skull},
    {"Elder", Glyph::Book}, {"Ender", Glyph::Eye},
  };
  for (auto& [k, g] : TAGS) if (tag == k) return g;
  if (Glyph g = glyphForAttr(attr); g != Glyph::None) return g;
  static const std::pair<const char*, Glyph> DESC[] = {   // (first match wins: the more telling first)
    {"lethal poison", Glyph::Skull}, {"inflict ignite", Glyph::Flame}, {"stealth", Glyph::Eye}, {"incoming healing", Glyph::Heart},
    {"loot duplication", Glyph::Clover}, {"arrow retrieval", Glyph::Arrow}, {"projectile duplication", Glyph::Arrow},
    {"grant spell", Glyph::Star}, {"damage conversion", Glyph::Swirl}, {"damage avoidance", Glyph::Wing}, {"damage taken", Glyph::Shield},
    {"jump height", Glyph::Wing}, {"command", Glyph::Paw}, {"experience", Glyph::Orb}, {"effect duration", Glyph::Hourglass},
    {"usage speed", Glyph::Hourglass}, {"movement speed", Glyph::Wing}, {"inflict effect", Glyph::Skull}, {"attribute", Glyph::Book},
  };
  for (auto& [k, g] : DESC) if (desc.find(k) != std::string::npos) return g;
  if (size >= 2 || tag.find("Keystone") != std::string::npos || tag.find("Subclass") != std::string::npos) return Glyph::Crown;
  return Glyph::Orb;
}

} // namespace dq::ui
