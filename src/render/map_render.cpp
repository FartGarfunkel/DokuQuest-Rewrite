// render/map_render.cpp — map pictures, paper, markers, and the map in your hands (game/maps.js).
#include "render/map_render.h"
#include "render/map_colors.h"
#include "render/char_layer.h"
#include "render/item_layers.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/voxel.h"

namespace dq {

// ------------------------------------------------------------------ colours from the tiles: render/map_colors.cpp
namespace {
std::string tileStamp() { return mapTileStamp(app().tiles); }
void mapRGB(BlockId id, u8 cl, float* out) { dq::mapRGB(app().tiles, id, cl, out); }
// blank paper, with a little grain
const std::vector<u8>& paperGrain() {
  static std::vector<u8> a;
  if (a.empty()) {
    a.resize(MAP_PIX);
    for (u32 i = 0; i < MAP_PIX; i++) { u32 h = (i ^ 0x5bd1e995u) * 0x2c1b3c6du; h ^= h >> 13; h *= 0x297a2d39u; a[i] = (h >> 24) & 7; }
  }
  return a;
}
struct Glyph { const char* rows[5]; int x, y; };
const Glyph COMPASS_INK[4] = {{{"1001", "1101", "1011", "1001", "1001"}, 62, 2}, {{"111", "100", "110", "100", "111"}, 123, 62},
                              {{"111", "100", "111", "001", "111"}, 63, 121}, {{"10001", "10001", "10101", "10101", "01010"}, 2, 62}};
const float MAP_SHADE[4] = {180 / 255.f, 220 / 255.f, 1, 135 / 255.f};

struct MapPic { std::vector<u8> rgba; u32 ver = 0; double at = -1; std::string stamp; GLuint tex = 0; u32 texVer = 0; };
std::unordered_map<std::string, MapPic>& pics() { static auto* m = new std::unordered_map<std::string, MapPic>(); return *m; }

void paint(const PaperMap& m, MapPic& out) {
  out.rgba.resize(MAP_PIX * 4);
  u8* d = out.rgba.data();
  const auto& grain = paperGrain();
  float rgb[3];
  for (int z = 0; z < MAP_N; z++) for (int x = 0; x < MAP_N; x++) {
    int i = z * MAP_N + x, o = i * 4, h = m.h[i];
    if (!h) { int n = grain[i]; d[o] = (u8)(222 + n); d[o + 1] = (u8)(207 + n); d[o + 2] = (u8)(166 + n); d[o + 3] = 255; continue; }
    BlockId id = m.ids[i]; u8 c = m.cl[i];
    int sh;
    if (mapColorKind(app().tiles, id) == 3) { float dd = (c & 15) * 0.1f + ((x + z) & 1) * 0.2f; sh = dd < 0.5f ? 2 : dd > 0.9f ? 0 : 1; }
    else { int hn = z > 0 && m.h[i - MAP_N] ? m.h[i - MAP_N] : h; sh = h > hn ? 2 : h < hn ? 0 : 1; }
    mapRGB(id, c, rgb);
    float f = MAP_SHADE[sh];
    for (int k = 0; k < 3; k++) d[o + k] = (u8)clampv((int)(rgb[k] * f), 0, 255);
    d[o + 3] = 255;
  }
  // (0.9.0.65) N, E, S and W inked at the edges so you know which way is which
  for (auto& g : COMPASS_INK) for (int r = 0; r < 5; r++) for (int q = 0; g.rows[r][q]; q++) if (g.rows[r][q] == '1') {
    int o = ((g.y + r) * MAP_N + g.x + q) * 4; d[o] = 74; d[o + 1] = 46; d[o + 2] = 24; d[o + 3] = 255;
  }
  out.ver = m.ver; out.at = nowSeconds(); out.stamp = tileStamp();
}
}

const std::vector<u8>& mapPicture(const PaperMap& m, bool force) {
  MapPic& p = pics()[m.id];
  bool stale = p.ver != m.ver || p.stamp != tileStamp();
  if (p.rgba.empty() || (stale && (force || nowSeconds() - p.at > 0.15))) paint(m, p);
  return p.rgba;
}
GLuint mapTexture(const PaperMap& m, bool force) {
  const auto& px = mapPicture(m, force);
  MapPic& p = pics()[m.id];
  if (!p.tex) { p.tex = makeTexRGBA(MAP_N, MAP_N, px.data(), true, true); p.texVer = p.ver; }
  else if (p.texVer != p.ver) { updateTexRGBA(p.tex, MAP_N, MAP_N, px.data(), true); p.texVer = p.ver; }
  return p.tex;
}

// ------------------------------------------------------------------ markers
void mapMarker(Canvas& c, const std::string& kind, float x, float y, float sz, float rot, u32 col) {
  c.save(); c.translate(x, y); if (rot) c.rotate(rot); c.scale(sz / 16, sz / 16);
  const u32 ink = hexRGBA("#1a1208");
  const float LW = 1.6f;
  c.beginPath();
  if (kind == "arrow") { c.moveTo(0, -7); c.lineTo(5.6f, 6.4f); c.lineTo(0, 3.4f); c.lineTo(-5.6f, 6.4f); c.closePath(); c.fill(col); c.stroke(ink, LW); }
  else if (kind == "dot") { c.arc(0, 0, 3.6f, 0, TAU); c.fill(col); c.stroke(ink, LW); }
  else if (kind == "home") {
    c.moveTo(-6.5f, 0.5f); c.lineTo(0, -6); c.lineTo(6.5f, 0.5f); c.lineTo(4.5f, 0.5f); c.lineTo(4.5f, 6.5f); c.lineTo(-4.5f, 6.5f); c.lineTo(-4.5f, 0.5f); c.closePath();
    c.fill(hexRGBA("#f2e6c8")); c.stroke(ink, LW);
    c.beginPath(); c.moveTo(-6.5f, 0.5f); c.lineTo(0, -6); c.lineTo(6.5f, 0.5f); c.closePath(); c.fill(hexRGBA("#b0402a")); c.stroke(ink, LW);
    c.fillRect(-1.3f, 2.4f, 2.6f, 4.1f, hexRGBA("#5a3a20"));
  } else if (kind == "grave") {
    c.moveTo(-4.5f, 6.5f); c.lineTo(-4.5f, -2); c.arc(0, -2, 4.5f, PI, 0); c.lineTo(4.5f, 6.5f); c.closePath();
    c.fill(hexRGBA("#a8a8b0")); c.stroke(ink, LW);
    c.fillRect(-0.7f, -3.5f, 1.4f, 6, hexRGBA("#3a3a44")); c.fillRect(-2.3f, -1.6f, 4.6f, 1.4f, hexRGBA("#3a3a44"));
  } else if (kind == "village") {
    struct H { float ox, w, h; const char* roof; } hs[2] = {{-6.5f, 6, 5, "#b0402a"}, {0.2f, 6.4f, 7, "#3a6ab0"}};
    for (auto& q : hs) {
      c.beginPath(); c.rect(q.ox, 6.5f - q.h + 2.5f, q.w, q.h - 2.5f); c.fill(hexRGBA("#f2e6c8")); c.stroke(ink, LW);
      c.beginPath(); c.moveTo(q.ox - 0.8f, 6.5f - q.h + 2.6f); c.lineTo(q.ox + q.w / 2, 6.5f - q.h - 1.6f); c.lineTo(q.ox + q.w + 0.8f, 6.5f - q.h + 2.6f); c.closePath();
      c.fill(hexRGBA(q.roof)); c.stroke(ink, LW);
    }
    c.fillRect(-4.4f, 4, 1.8f, 2.5f, hexRGBA("#5a3a20")); c.fillRect(2.6f, 3.6f, 1.8f, 2.9f, hexRGBA("#5a3a20"));
  } else if (kind == "dungeon") {
    c.moveTo(-6, 6.5f); c.lineTo(-6, -1); c.arc(0, -1, 6, PI, 0); c.lineTo(6, 6.5f); c.closePath(); c.fill(col ? col : hexRGBA("#8a8a92")); c.stroke(ink, LW);
    c.beginPath(); c.moveTo(-3, 6.5f); c.lineTo(-3, 0); c.arc(0, 0, 3, PI, 0); c.lineTo(3, 6.5f); c.closePath(); c.fill(hexRGBA("#120a0a"));
    c.fillRect(-1.2f, 1.4f, 2.4f, 2, hexRGBA("#e8d8a8")); c.fillRect(-0.9f, 2, 0.6f, 0.7f, hexRGBA("#120a0a")); c.fillRect(0.3f, 2, 0.6f, 0.7f, hexRGBA("#120a0a"));
  } else if (kind == "rift") {
    c.moveTo(0, -7.5f); c.quadTo(4.5f, 0, 0, 7.5f); c.quadTo(-4.5f, 0, 0, -7.5f); c.closePath(); c.fill(hexRGBA("#c040ff")); c.stroke(ink, LW);
    c.beginPath(); c.moveTo(0, -5); c.quadTo(2, 0, 0, 5); c.quadTo(-2, 0, 0, -5); c.fill(hexRGBA("#16001e"));
  } else if (kind == "pin") {
    c.moveTo(0, 7); c.lineTo(-2.2f, 0); c.lineTo(2.2f, 0); c.closePath(); c.fill(hexRGBA("#3a2a1a"));
    c.beginPath(); c.arc(0, -2.2f, 4.4f, 0, TAU); c.fill(col); c.stroke(ink, LW);
    c.beginPath(); c.arc(-1.3f, -3.4f, 1.2f, 0, TAU); c.fill(hexRGBA("#ffffff", 178));
  }
  c.restore();
}

GLuint mapPaperTexture() {
  static GLuint t = 0;
  if (t) return t;
  Canvas c(64, 64);
  c.fillRect(0, 0, 64, 64, hexRGBA("#7a5a34"));
  c.fillRect(1, 1, 62, 62, hexRGBA("#c9b07c"));
  c.fillRect(3, 3, 58, 58, hexRGBA("#ddc995"));
  for (int i = 0; i < 160; i++) {
    int x = (i * 37) % 62 + 1, y = (i * 53 + (i >> 3)) % 62 + 1;
    c.fillRect((float)x, (float)y, 1, 1, i & 1 ? (120u | 90u << 8 | 50u << 16 | 31u << 24) : (255u | 250u << 8 | 230u << 16 | 46u << 24));
  }
  auto px = c.rgba();
  t = makeTexRGBA(64, 64, px.data(), true, false);
  return t;
}
GLuint mapMarkTexture() {
  static GLuint t = 0;
  if (t) return t;
  Canvas k(32 * MAP_CELLS, 32);
  auto cell = [](int n) { return 16.f + 32 * n; };
  mapMarker(k, "arrow", cell(0), 16, 30, 0, hexRGBA("#ffffff"));
  mapMarker(k, "arrow", cell(1), 16, 30, 0, hexRGBA("#ffe08a"));
  mapMarker(k, "arrow", cell(2), 16, 30, 0, hexRGBA("#7ad0ff"));
  mapMarker(k, "dot", cell(3), 16, 30, 0, hexRGBA("#ffffff"));
  mapMarker(k, "home", cell(4), 16, 30, 0, 0);
  mapMarker(k, "grave", cell(5), 16, 30, 0, 0);
  for (int n = 0; n < 6; n++) mapMarker(k, "pin", cell(6 + n), 16, 30, 0, hexRGBA(MAP_PIN_COLS[n]));
  mapMarker(k, "village", cell(12), 16, 30, 0, 0);
  mapMarker(k, "dungeon", cell(13), 16, 30, 0, 0);
  mapMarker(k, "rift", cell(14), 16, 30, 0, 0);
  mapMarker(k, "dungeon", cell(15), 16, 30, 0, hexRGBA("#5a8a5a"));
  auto px = k.rgba();
  t = makeTexRGBA(32 * MAP_CELLS, 32, px.data(), false, false);
  return t;
}
vec4 mapCellUV(int cell) { return vec4((float)cell / MAP_CELLS, 0, (float)(cell + 1) / MAP_CELLS, 1); }

void drawMapSheet(ContentDraw& cd, Level& lv, const PaperMap* m, const Player* viewer, const mat4& M, float S, vec2 light) {
  cd.picture(mapPaperTexture(), glm::scale(M, vec3(S * 1.12f, S * 1.12f, 1)), vec4(0, 0, 1, 1), vec4(1), light);
  if (!m) return;
  mat4 mm = glm::scale(glm::translate(M, vec3(0, 0, 0.002f)), vec3(S, S, 1));
  cd.picture(mapTexture(*m), mm, vec4(0, 0, 1, 1), vec4(1), light);
  GLuint marks = mapMarkTexture();
  for (const MapMarker& k : mapMarkers(lv, *m, viewer)) {
    mat4 km = glm::translate(M, vec3((k.u / MAP_N - 0.5f) * S, (0.5f - k.v / MAP_N) * S, 0.004f + (k.cell == 0 ? 0.001f : 0)));
    if (k.cell <= 2) km = glm::rotate(km, k.rot, vec3(0, 0, 1));
    float ks = S * (k.small ? 0.045f : k.pin ? 0.075f : 0.07f);
    km = glm::scale(km, vec3(ks, ks, 1));
    if (k.pin) km = glm::translate(km, vec3(0, 0.35f, 0));
    cd.picture(marks, km, mapCellUV(k.cell), vec4(1), light);
  }
}

// ------------------------------------------------------------------ the map in your hands (maps.js handState)
namespace {
int heldMaps(HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
  Player* p = s.player;
  if (!p || p->dead || p->riding) return 0;
  ItemStack& main = p->held();
  ItemStack* off = p->offhand.empty() ? nullptr : &p->offhand[0];
  bool mainMap = isMapStack(main), offMap = off && isMapStack(*off);
  if (!mainMap && !offMap) return 0;
  Level& lv = *s.level;
  MapStore& st = mapStore(lv);
  const PaperMap* mm = mainMap ? st.forStack(*p, main) : nullptr;
  const PaperMap* om = offMap ? st.forStack(*p, *off) : nullptr;
  World& w = s.world();
  u8 l = w.getLight(ifloor(p->pos.x), ifloor(p->pos.y + EYE), ifloor(p->pos.z));
  vec2 light((l >> 4) / 15.f, (l & 15) / 15.f);
  float bobK = s.app.settings.viewBob ? L.bobAmt : 0;
  float bx = std::sin(L.bob * 1.1f) * 0.02f * bobK, by = -std::fabs(std::sin(L.bob * 2.2f)) * 0.02f * bobK;
  float eq = 1 - L.equip, look = clampv(-p->pitch / 0.9f, 0.f, 1.f), sw = std::sin(L.swing * PI);
  struct ArmAt { vec3 f; float yaw, pitch; };
  std::vector<ArmAt> arms;
  ContentDraw& cd = contentDraw();
  cd.begin(f, proj, false);
  auto pt = [](const mat4& M, float x, float y, float z) { return vec3(M * vec4(x, y, z, 1)); };
  int taken = 0;
  bool offBusy = off && !off->empty() && off->def();
  if (mainMap && !offBusy) {
    // both hands: held up in front of you, coming up into view as you look down
    mat4 M = glm::translate(mat4(1), vec3(bx, -0.43f + look * 0.24f + by - eq * 0.55f - sw * 0.08f, -0.6f + look * 0.03f - sw * 0.05f));
    M = glm::rotate(M, -0.95f + look * 0.72f, vec3(1, 0, 0));
    float S = 0.5f;
    drawMapSheet(cd, lv, mm, p, M, S, light);
    for (float side : {-1.f, 1.f}) arms.push_back({pt(M, side * 0.5f * S, -0.47f * S, 0.04f), side * 0.6f, 2.3f});
    taken = 3;
  } else {
    auto one = [&](const PaperMap* m, float side) {
      // one hand: off to that side, the hand at its outer lower corner
      mat4 M = glm::translate(mat4(1), vec3(side * (0.34f - look * 0.06f) + bx, -0.36f + look * 0.12f + by - eq * 0.5f - sw * 0.08f, -0.62f - sw * 0.06f));
      M = glm::rotate(M, -side * (0.28f - look * 0.12f), vec3(0, 1, 0));
      M = glm::rotate(M, -0.75f + look * 0.45f, vec3(1, 0, 0));
      M = glm::rotate(M, side * 0.06f, vec3(0, 0, 1));
      float S = 0.36f;
      drawMapSheet(cd, lv, m, p, M, S, light);
      arms.push_back({pt(M, side * 0.44f * S, -0.36f * S, 0.03f), side * 0.4f, 2.1f});
    };
    if (mainMap) { one(mm, 1); taken |= 1; }
    if (offMap) { one(om, -1); taken |= 2; }
  }
  cd.end();
  VoxelRenderer& VR = voxelRenderer();
  const VoxelModel& arm = firstPersonArm();
  VR.begin(f, proj, false);
  for (auto& a : arms) {
    mat4 am = glm::translate(mat4(1), a.f);
    am = glm::rotate(am, a.yaw, vec3(0, 1, 0)); am = glm::rotate(am, a.pitch, vec3(1, 0, 0));
    am = glm::translate(am, vec3(0, 0.3f, 0.02f));
    if (arm.ready()) VR.draw(arm.gl, am * arm.matrix(), VoxelLight{light.x, light.y});
    drawFirstPersonSleeve(am, VoxelLight{light.x, light.y});
  }
  VR.end();
  return taken;
}
}

void registerHeldMap() {
  static bool done = false;
  if (done) return;
  done = true;
  auto prev = HeldItemLayer::handsHook;
  HeldItemLayer::handsHook = [prev](HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
    int t = heldMaps(L, s, f, proj);
    if (prev) t |= prev(L, s, f, proj);
    return t;
  };
}

} // namespace dq
