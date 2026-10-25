// render/instruments.cpp — the compass, death compass and clock, painted and animated (game/instruments.js).
#include "render/instruments.h"
#include "render/item_layers.h"
#include "client/session.h"
#include "client/app.h"
#include "sim/content.h"

namespace dq {

InstrumentState& instruments() { static InstrumentState s; return s; }
bool isInstrument(const std::string& k) { return k == "compass" || k == "clock" || k == "death_compass"; }

// ------------------------------------------------------------------ painting (paintInstrumentFrames)
namespace {
struct Px16 {
  u8 d[16 * 16 * 4]{};
  void set(int x, int y, u32 c, u8 a = 255) { if (x < 0 || y < 0 || x > 15 || y > 15) return; u8* o = d + (y * 16 + x) * 4; o[0] = c & 255; o[1] = (c >> 8) & 255; o[2] = (c >> 16) & 255; o[3] = a; }
  u8 alpha(int x, int y) const { return x < 0 || y < 0 || x > 15 || y > 15 ? 0 : d[(y * 16 + x) * 4 + 3]; }
};
u32 H(const char* h) { return hexRGBA(h) & 0xFFFFFF; }
void disc(Px16& p, const char* rim, u32 face, float r = 6.2f) {
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
    float d = std::hypot(x - 7.5f, y - 7.5f);
    if (d <= r + 0.9f && d > r - 0.3f) p.set(x, y, H(rim)); else if (d <= r - 0.3f) p.set(x, y, face);
  }
}
void line(Px16& p, float ang, float from, float to, const char* col) {
  for (float t = from; t <= to; t += 0.25f) p.set((int)std::lround(7.5f + std::sin(ang) * t - 0.01f), (int)std::lround(7.5f - std::cos(ang) * t - 0.01f), H(col));
}
void outline(Px16& p) {   // doku.js outlineSprite
  std::vector<std::pair<int, int>> add;
  auto solid = [&](int x, int y) { return p.alpha(x, y) > 40; };
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
    if (solid(x, y)) continue;
    if (solid(x + 1, y) || solid(x - 1, y) || solid(x, y + 1) || solid(x, y - 1)) add.push_back({x, y});
  }
  for (auto [x, y] : add) p.set(x, y, 26 | 18 << 8 | 14 << 16, 230);
}
}

std::vector<u8> paintInstrumentFrame(const std::string& key, int f) {
  Px16 p;
  if (key == "compass" || key == "death_compass") {
    bool death = key == "death_compass";
    float a = (float)f / COMPASS_FRAMES * TAU;
    disc(p, death ? "#2a2430" : "#4a4a54", H(death ? "#4a4256" : "#d8d8e0"));
    for (int k = 0; k < 4; k++) { float q = k * PI / 2; p.set((int)std::lround(7.5f + std::sin(q) * 5.2f), (int)std::lround(7.5f - std::cos(q) * 5.2f), H(death ? "#6a6078" : "#8a8a96")); }
    line(p, a + PI, 0, 3.2f, death ? "#1a1620" : "#3a3a44");   // the tail of the needle
    line(p, a, 0, 4.8f, death ? "#b8f0ff" : "#d8202a");        // the point: home (or your grave)
    u32 c = H(death ? "#e8e8f0" : "#141414");
    p.set(7, 7, c); p.set(8, 8, c);
  } else {
    float phi = ((float)f / CLOCK_FRAMES - 0.25f) * TAU;
    disc(p, "#b98f15", H("#000000"));
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
      float d = std::hypot(x - 7.5f, y - 7.5f);
      if (d > 5.9f) continue;
      float alpha = std::atan2(x - 7.5f, -(y - 7.5f)), beta = alpha - phi;   // (0.9.0.61) the dial turns clockwise
      bool day = std::cos(beta) > 0;
      u32 c = day ? H("#5a9ae8") : H("#121a3a");
      if (std::fabs(std::cos(beta)) < 0.12f) c = H("#6a4a2a");               // the horizon
      float sx = std::sin(phi) * 3.3f, sy = -std::cos(phi) * 3.3f;          // the sun on the dial
      if (std::hypot(x - 7.5f - sx, y - 7.5f - sy) < 1.6f) c = H("#ffd84a");
      if (std::hypot(x - 7.5f + sx, y - 7.5f + sy) < 1.4f) c = H("#e8e8f0"); // the moon opposite
      if (!day && std::sin(beta * 7) > 0.97f && d > 2.5f) c = H("#d8d8f8");
      p.set(x, y, c);
    }
    p.set(7, 1, H("#3a2a10")); p.set(8, 1, H("#3a2a10"));                  // the marker at the top
  }
  outline(p);
  return std::vector<u8>(p.d, p.d + sizeof(p.d));
}

namespace {
struct Strip { GLuint tex = 0; int n = 0; };
Strip& strip(const std::string& key) {
  static std::unordered_map<std::string, Strip> s;
  Strip& st = s[key];
  if (!st.tex) {
    st.n = key == "clock" ? CLOCK_FRAMES : COMPASS_FRAMES;
    std::vector<u8> px((size_t)16 * st.n * 16 * 4);
    for (int f = 0; f < st.n; f++) {
      auto fr = paintInstrumentFrame(key, f);
      for (int y = 0; y < 16; y++) memcpy(&px[((size_t)y * 16 * st.n + f * 16) * 4], &fr[y * 16 * 4], 16 * 4);
    }
    st.tex = makeTexRGBA(16 * st.n, 16, px.data(), true, false);
  }
  return st;
}
}

bool instrumentIcon(const std::string& key, GLuint& tex, vec4& uv) {
  if (!isInstrument(key)) return false;
  Strip& s = strip(key);
  const InstrumentState& I = instruments();
  int f = key == "clock" ? I.clockF : key == "compass" ? I.compassF : I.deathF;
  tex = s.tex; uv = vec4((float)f / s.n, 0, (float)(f + 1) / s.n, 1);
  return tex != 0;
}

// ------------------------------------------------------------------ each frame: the needle (a spring) and the dial
void updateInstruments(GameSession& s, double dtd) {
  InstrumentState& I = instruments();
  float dt = (float)std::min(dtd, 0.1), time = (float)s.app.time;
  Player* p = s.player;
  bool inWorld = p && !p->dead;
  int dim = p ? jsDimOf(*p) : 0;
  float want = I.compassA; bool spin = !inWorld || dim != JS_OVERWORLD;
  if (!spin) {
    vec3 sp = p->spawnPoint.y >= 0 ? p->spawnPoint : s.level->info.spawn;
    float toYaw = std::atan2(-(sp.x - p->pos.x), -(sp.z - p->pos.z));
    float rel = std::fmod(std::fmod(toYaw - p->yaw, TAU) + TAU + PI, TAU) - PI;
    want = -rel;
  }
  if (spin) I.compassA += dt * (4 + std::sin(time * 1.7f) * 3);
  else {   // a spring: it swings past and settles, like a real needle
    float d = std::fmod(std::fmod(want - I.compassA, TAU) + TAU * 1.5f, TAU) - PI;
    I.compassV += d * dt * 40; I.compassV *= std::max(0.f, 1 - dt * 6); I.compassA += I.compassV * dt;
  }
  I.compassF = floormod((int)std::lround(I.compassA / TAU * COMPASS_FRAMES), COMPASS_FRAMES);
  // the death compass: the same needle, drawn to your grave (and lost when there's none in this world)
  // (the grave is Player::grave {realm, dim, x, y, z}, set by playerDie; it was read from stats before and never found)
  const json gr = inWorld && p->grave.is_object() ? p->grave : json();
  if (gr.is_object() && gr.value("dim", -1) == dim && gr.value("realm", p->realm) == p->realm) {
    float toYaw = std::atan2(-(gr.value("x", 0.f) + 0.5f - p->pos.x), -(gr.value("z", 0.f) + 0.5f - p->pos.z));
    float rel = std::fmod(std::fmod(toYaw - p->yaw, TAU) + TAU + PI, TAU) - PI;
    float d = std::fmod(std::fmod(-rel - I.deathA, TAU) + TAU * 1.5f, TAU) - PI;
    I.deathV += d * dt * 40; I.deathV *= std::max(0.f, 1 - dt * 6); I.deathA += I.deathV * dt;
  } else I.deathA += dt * (4 + std::sin(time * 1.3f) * 3);
  I.deathF = floormod((int)std::lround(I.deathA / TAU * COMPASS_FRAMES), COMPASS_FRAMES);
  if (inWorld && dim == JS_OVERWORLD) I.clockF = (int)std::floor(std::fmod(s.level->info.time, DAY_LEN) / DAY_LEN * CLOCK_FRAMES) % CLOCK_FRAMES;
  else I.clockF = (int)std::floor(time * 20) % CLOCK_FRAMES;
}

void registerInstruments() {
  static bool done = false;
  if (done) return;
  done = true;
  auto prev = HeldItemLayer::itemHook;
  HeldItemLayer::itemHook = [prev](GameSession& s, const FrameCtx& f, const mat4& proj, const ItemStack& st, const mat4& m, const VoxelModel& im) {
    if (prev) prev(s, f, proj, st, m, im);
    GLuint tex; vec4 uv;
    if (!instrumentIcon(st.id, tex, uv) || im.kind == VoxelModel::Block) return;
    // the live dial on both faces of the item (the picture spans the model's -0.5..0.5, scaled by its size)
    // (the dial model's own static picture plate sits at 0.068..0.078 x size, i.e. just in front of `depth`: the live
    // face goes clearly in front of it, the same 0.9 wide, or the plate hides it and the needle never moves)
    float sz = itemSize(st.id) * 0.9f, z = im.depth + 0.012f;
    Player* p = s.player;
    u8 l = p ? s.world().getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.62f), ifloor(p->pos.z)) : 0xF0;
    vec2 light((l >> 4) / 15.f, (l & 15) / 15.f);
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, proj, false)) return;
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1, -1);
    cd.picture(tex, glm::scale(glm::translate(m, vec3(0, 0, z)), vec3(sz, sz, 1)), uv, vec4(1), light);
    mat4 back = glm::rotate(glm::translate(m, vec3(0, 0, -z)), PI, vec3(0, 1, 0));
    cd.picture(tex, glm::scale(back, vec3(sz, sz, 1)), vec4(uv.z, uv.y, uv.x, uv.w), vec4(1), light);
    glDisable(GL_POLYGON_OFFSET_FILL);
    cd.end();
  };
}

} // namespace dq
