// sim/villager_look.cpp — their skins: a face, hair and the clothes of their work (game/villagers.js npcSkinCanvas),
// painted pixel for pixel like the original (same random sequence, same rounding), as a 64x64 RGBA skin. Pure pixels
// (no GL) so the server and the tests have it; the client builds the voxel person from it (render/villager_layer).
#include "sim/villagers_internal.h"

namespace dq {

namespace {
struct Col { double r = 0, g = 0, b = 0; bool ok = false; };
Col hexc(const std::string& h) {
  Col c; if (h.size() < 7 || h[0] != '#') return c;
  unsigned v = (unsigned)std::stoul(h.substr(1, 6), nullptr, 16);
  c.r = (v >> 16) & 255; c.g = (v >> 8) & 255; c.b = v & 255; c.ok = true; return c;
}
Col rgb(double r, double g, double b) { Col c; c.r = r; c.g = g; c.b = b; c.ok = true; return c; }
Col dark(const Col& c, double k = 0.7) { return c.ok ? rgb(c.r * k, c.g * k, c.b * k) : c; }
const Col NONE;
}

void npcSkinPixels(u32 seed, const std::string& job, std::vector<u8>& px) {
  px.assign(64 * 64 * 4, 0);
  const json& JJ = VD()["jobs"];
  const json J = JJ.contains(job) ? JJ[job] : JJ.value("none", json::object());
  auto has = [&](const char* k) { return J.contains(k) && !J[k].is_null() && J[k] != false && J[k] != ""; };
  auto str = [&](const char* k) { return J.contains(k) && J[k].is_string() ? J[k].get<std::string>() : std::string(); };
  Mulberry32 R((u32)((u64)seed * 2654435761ull));
  auto pick = [&](const json& a) { return a[(size_t)std::floor(R() * a.size())].get<std::string>(); };
  auto put = [&](int x, int y, const Col& c, double v = 0.07) {
    if (!c.ok) return;
    double k = 1 + (R() - 0.5) * v;
    u8* p = px.data() + ((size_t)y * 64 + x) * 4;
    p[0] = (u8)(int)clampv(c.r * k, 0.0, 255.0); p[1] = (u8)(int)clampv(c.g * k, 0.0, 255.0); p[2] = (u8)(int)clampv(c.b * k, 0.0, 255.0); p[3] = 255;
  };
  enum F { TOP, BOTTOM, RIGHT, FRONT, LEFT, BACK };
  auto box = [&](int u, int v, int w, int h, int d, const std::function<Col(int f, int x, int y, int fw, int fh)>& fn) {
    const int faces[6][4] = {{u + d, v, w, d}, {u + d + w, v, w, d}, {u, v + d, d, h}, {u + d, v + d, w, h}, {u + d + w, v + d, d, h}, {u + d + w + d, v + d, w, h}};
    for (int f = 0; f < 6; f++) for (int y = 0; y < faces[f][3]; y++) for (int x = 0; x < faces[f][2]; x++) { Col c = fn(f, x, y, faces[f][2], faces[f][3]); put(faces[f][0] + x, faces[f][1] + y, c); }
  };
  const bool elder = has("elder"), robe = has("robe");
  const bool old = elder || R() < 0.12;
  Col skin = hexc(pick(VD()["skinTones"]));
  Col hair = hexc(old ? pick(json::array({"#d8d8d8", "#bcbcbc", "#9a9a9a"})) : pick(VD()["hair"]));
  Col eye = hexc(pick(VD()["eyes"]));
  const int style = has("hood") ? 4 : (int)std::floor(R() * 5);         // 0 short, 1 long, 2 cropped, 3 tied back, 4 hood
  const int beard = elder || R() < 0.3 ? (int)std::floor(R() * 3) + 1 : 0;   // 1 stubble, 2 short, 3 long
  Col shirt = hexc(has("shirt") ? str("shirt") : pick(json::array({"#6a8a5a", "#8a6a4a", "#5a6a8a", "#8a5a5a", "#7a7a5a", "#5a7a7a"}))), shirtD = dark(shirt, 0.78);
  Col trousers = hexc(pick(json::array({"#4a3a2a", "#3a3a4a", "#5a4a3a", "#2a2a2a", "#4a4a3a"}))), boots = hexc(pick(json::array({"#3a2414", "#2a1a10", "#4a3424"})));
  Col mouth = dark(skin, 0.62), nose = dark(skin, 0.86), white = rgb(240, 240, 236);
  // the head
  box(0, 0, 8, 8, 8, [&](int f, int x, int y, int, int) -> Col {
    if (f == TOP) return style == 2 ? (R() < 0.5 ? hair : dark(skin, 0.9)) : hair;
    if (f == BOTTOM) return beard >= 2 ? hair : skin;
    if (f == BACK) return style == 1 || style == 3 ? hair : y < (style == 2 ? 3 : 6) ? hair : skin;
    if (f == FRONT) {
      if (y < 2 - (style == 2 ? 1 : 0)) return hair;
      if (y == 2) return (x == 0 || x == 7) && style != 2 ? hair : (x == 1 || x == 2 || x == 5 || x == 6) && R() < 0.85 ? dark(hair, 0.85) : skin;   // brows
      if (y == 3) return x == 1 || x == 6 ? white : x == 2 || x == 5 ? eye : skin;
      if (y == 4) return x == 3 || x == 4 ? nose : beard == 1 && (x == 0 || x == 7) ? dark(hair, 1.1) : skin;
      if (y == 5) return beard >= 2 && (x <= 1 || x >= 6) ? hair : x >= 2 && x <= 5 ? (beard >= 2 && (x == 2 || x == 5) ? hair : mouth) : beard == 1 && R() < 0.4 ? dark(hair, 1.2) : skin;
      if (y >= 6) return beard >= 2 && (beard == 3 || y == 6 || (x > 0 && x < 7)) ? (y == 7 && beard == 2 && (x < 2 || x > 5) ? skin : hair) : beard == 1 && R() < 0.4 ? dark(hair, 1.2) : skin;
      return skin;
    }
    // the sides: hair over the ears, longer for long hair
    const int longH = style == 1 ? 8 : style == 3 ? 4 : style == 2 ? 2 : 4;
    if (y < longH && (f == RIGHT ? x < 5 : x > 2)) return hair;
    if (beard >= 2 && y >= 5 && (f == RIGHT ? x < 3 : x > 4)) return hair;
    return y < 2 ? hair : skin;
  });
  // the hat layer: hoods, caps, straw hats, the chef's hat, glasses
  const Col hatCol = has("hatCol") ? hexc(str("hatCol")) : NONE;
  const std::string hat = str("hat");
  if (has("hood")) { Col hc = hexc(str("hood")); box(32, 0, 8, 8, 8, [&](int f, int x, int y, int, int) { return f == FRONT ? (y < 2 || x == 0 || x == 7 ? hc : NONE) : f == BOTTOM ? NONE : hc; }); }
  else if (hat == "straw") { Col s = hexc("#d8b860"); box(32, 0, 8, 8, 8, [&](int f, int, int y, int, int) { return f == TOP ? (R() < 0.8 ? s : dark(s, 0.85)) : f == BOTTOM ? NONE : y < 2 ? (y == 1 && f == FRONT ? hexc("#8a3a2a") : s) : NONE; }); }
  else if (hat == "cap" || hat == "bucket") {
    Col c = hatCol.ok ? hatCol : hexc("#5a4a3a"); const bool feather = has("feather");
    box(32, 0, 8, 8, 8, [&](int f, int x, int y, int, int) { return f == TOP ? c : f == BOTTOM ? NONE : y < (hat == "bucket" ? 3 : 2) ? (feather && f == RIGHT && y == 0 && x == 3 ? hexc("#e84a2a") : c) : NONE; });
  } else if (hat == "chef") { Col c = rgb(250, 250, 248); box(32, 0, 8, 8, 8, [&](int f, int, int y, int, int) { return f == BOTTOM ? NONE : f == TOP ? c : y < 2 ? c : NONE; }); }
  if (has("glasses")) for (int x : {1, 2, 5, 6}) put(32 + 8 + x, 8 + 3, hexc("#2a2a2a"));   // spectacles over the eyes
  if (has("monocle")) { put(32 + 8 + 5, 8 + 3, hexc("#c8a040")); put(32 + 8 + 6, 8 + 3, hexc("#c8a040")); }
  if (elder) for (int y = 5; y < 8; y++) for (int x = 1; x < 7; x++) put(32 + 8 + x, 8 + y, hair);   // a long white beard over the chin
  // the body: shirt, belt, apron or robe
  const Col tab = has("tabard") ? hexc("#2a4a8a") : NONE;
  const bool vest = has("vest"), gold = has("gold");
  box(16, 16, 8, 12, 4, [&](int f, int x, int y, int, int h) -> Col {
    if (f == TOP) return shirt;
    if (f == BOTTOM) return robe ? shirt : trousers;
    if (y == h - 3 && !robe) return hexc("#3a2a1a");                    // the belt
    if (f == FRONT && y == 0 && (x == 3 || x == 4)) return robe || tab.ok ? shirtD : skin;   // the open collar
    if (vest && f == FRONT && (x < 2 || x > 5) && y < h - 3) return hexc(str("vest"));
    if (gold && f == FRONT && x == 3 && y % 3 == 1) return hexc("#e8c040");
    return y == h - 1 ? shirtD : shirt;
  });
  // the jacket layer: aprons, tabards
  const bool apron = has("apron");
  if (apron) {
    Col a = hexc(str("apron")); const bool blood = has("blood"), soot = has("soot");
    box(16, 32, 8, 12, 4, [&](int f, int x, int y, int, int) { return f == FRONT && x >= 1 && x <= 6 && y >= 3 ? (blood && R() < 0.12 ? hexc("#8a2a1a") : soot && R() < 0.15 ? dark(a, 0.6) : a) : f == FRONT && y == 2 && (x == 1 || x == 6) ? a : NONE; });
  }
  if (tab.ok) box(16, 32, 8, 12, 4, [&](int f, int x, int y, int, int) { return (f == FRONT || f == BACK) && x >= 2 && x <= 5 ? (y == 4 && (x == 3 || x == 4) ? hexc("#e8c040") : tab) : NONE; });
  // arms: sleeves, then hands
  const int sleeve = robe ? 10 : R() < 0.5 ? 4 : 6;
  auto arm = [&](int f, int, int y, int, int) { return f == TOP ? shirt : f == BOTTOM ? skin : y < sleeve ? (y == sleeve - 1 ? shirtD : shirt) : skin; };
  box(40, 16, 4, 12, 4, arm); box(32, 48, 4, 12, 4, arm);
  // legs: trousers and boots, or the robe down to the ankles
  auto leg = [&](int f, int, int y, int, int) { return f == TOP ? (robe ? shirt : trousers) : f == BOTTOM ? boots : y >= 10 ? boots : robe ? (y == 9 ? shirtD : shirt) : trousers; };
  box(0, 16, 4, 12, 4, leg); box(16, 48, 4, 12, 4, leg);
  if (apron && !robe) { Col a = hexc(str("apron")); for (auto [u, v] : {std::pair{0, 32}, {0, 48}}) box(u, v, 4, 12, 4, [&](int f, int, int y, int, int) { return f == FRONT && y < 7 ? a : NONE; }); }
}

// ---- their own touches (ours, not the original's), over the original's skin (npcSkinPixels stays pixel for pixel the
// original's: tools/test_villagers checks it). Freckles, a scar, an earring, a headband or a flower behind the ear, a scarf, a
// satchel strap, a patched knee or elbow. Only whole pieces go on the outer layers (a scarf, a pouch), which stand off the
// body as real bumps; small things are painted flat on the skin, and single stray pixels the original put on the outer
// layers (glasses, a monocle) are laid flat too: on the voxel person a lone outer pixel is a cube floating off the face
void npcSkinTouches(u32 seed, const std::string& job, std::vector<u8>& px) {
  if (px.size() != 64 * 64 * 4) return;
  (void)job;
  // the skin and clothes colours the original picked, read back off the skin: the face's cheek, the shirt, the trousers
  auto at0 = [&](int x, int y) { const u8* p = px.data() + ((size_t)y * 64 + x) * 4; return rgb(p[0], p[1], p[2]); };
  const Col skin = at0(8 + 1, 8 + 4), shirt = at0(20 + 4, 20 + 5), trousers = at0(4 + 1, 20 + 4);
  Mulberry32 Q((u32)((u64)(seed ^ 0x5bd1e995u) * 2246822519ull));
  auto px_ = [&](int x, int y) { return px.data() + ((size_t)y * 64 + x) * 4; };
  auto free_ = [&](int x, int y) { return px_(x, y)[3] == 0; };
  auto set = [&](int x, int y, const Col& c) { if (!c.ok) return; u8* p = px_(x, y); p[0] = (u8)clampv(c.r, 0.0, 255.0); p[1] = (u8)clampv(c.g, 0.0, 255.0); p[2] = (u8)clampv(c.b, 0.0, 255.0); p[3] = 255; };
  auto get = [&](int x, int y) { const u8* p = px_(x, y); return rgb(p[0], p[1], p[2]); };
  auto near_ = [&](const Col& a, const Col& b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) < 40; };
  auto pickc = [&](std::initializer_list<const char*> l) { size_t i = (size_t)std::floor(Q() * l.size()); return hexc(*(l.begin() + std::min(i, l.size() - 1))); };
  // freckles over the nose and cheeks
  if (Q() < 0.22) for (int y = 4; y <= 5; y++) for (int x : {1, 2, 5, 6}) if (Q() < 0.45 && near_(get(8 + x, 8 + y), skin)) set(8 + x, 8 + y, dark(skin, 0.78));
  // an old scar down a cheek
  if (Q() < 0.07) { const int x = Q() < 0.5 ? 1 : 6; for (int y = 3; y <= 5; y++) if (near_(get(8 + x, 8 + y), skin)) set(8 + x, 8 + y, rgb(std::min(255.0, skin.r * 1.18), skin.g * 1.02, skin.b * 1.0)); }
  // an earring, painted on the ear (both sides)
  if (Q() < 0.1) { Col e = pickc({"#e8c040", "#d8d8e0", "#c86a3a"}); set(0 + 4, 8 + 6, e); set(16 + 3, 8 + 6, e); }
  // a headband round the hair, or a flower behind the ear (only where no hat or hood is): painted on the head itself
  bool bare = true; for (int x = 0; x < 8; x++) if (!free_(40 + x, 8 + 1)) bare = false;
  if (bare && Q() < 0.12) { Col hb = pickc({"#8a2a2a", "#2a4a8a", "#3a6a3a", "#c89a3a", "#6a3a7a"}); for (int x = 0; x < 8; x++) { set(8 + x, 8 + 1, hb); set(24 + x, 8 + 1, hb); set(0 + x, 8 + 1, hb); set(16 + x, 8 + 1, hb); } }
  else if (bare && Q() < 0.1) { set(0 + 5, 8 + 1, pickc({"#e84a4a", "#f0d040", "#6a8ae8", "#f0f0f0"})); set(0 + 5, 8 + 2, hexc("#3a7a2a")); }
  // a scarf on the jacket layer, its end hanging down the front
  bool collarFree = true; for (int x = 0; x < 8; x++) if (!free_(20 + x, 36)) collarFree = false;
  if (collarFree && Q() < 0.15) {
    Col sc = pickc({"#a83a2a", "#3a5a9a", "#d8b040", "#4a7a4a", "#7a4a8a", "#e8e0d0"}); Col scD = dark(sc, 0.8);
    for (int x = 0; x < 8; x++) { set(20 + x, 36, sc); set(20 + x, 37, x % 2 ? scD : sc); set(32 + x, 36, sc); set(32 + x, 37, scD); }
    for (int x = 0; x < 4; x++) { set(16 + x, 36, sc); set(28 + x, 36, sc); }
    for (int x = 0; x < 8; x++) set(20 + x, 32 + (x < 4 ? 0 : 0), sc);   // round the top of the shoulders
    for (int y = 38; y <= 40; y++) set(20 + 5, y, y == 40 ? scD : sc);
  }
  // a satchel strap across the chest (jacket layer), shoulder to hip
  else if (Q() < 0.12) {
    Col st = pickc({"#5a3a1a", "#3a2a1a", "#6a4a2a"});
    for (int i = 0; i < 9; i++) { const int x = i * 7 / 8, y = 20 + i + 1; set(20 + x, y, st); set(32 + 7 - x, y, st); }   // (painted on the shirt)
    for (int y = 44; y <= 46; y++) for (int x = 0; x < 3; x++) if (free_(28 + x, y)) set(28 + x, y, dark(st, 1.15));   // the bag at the hip
  }
  // a patched knee, or a patched elbow
  if (Q() < 0.2) {
    const bool knee = Q() < 0.6;
    Col pc = Q() < 0.5 ? dark(knee ? trousers : shirt, 0.72) : pickc({"#8a6a4a", "#6a6a8a", "#8a4a4a", "#5a7a5a"});
    const int u = knee ? (Q() < 0.5 ? 4 : 20) : (Q() < 0.5 ? 44 : 36), v = knee ? (u == 4 ? 20 : 52) + 5 : (u == 44 ? 20 : 52) + 2;
    for (int y = 0; y < 2; y++) for (int x = 1; x < 3; x++) if (!free_(u + x, v + y)) set(u + x, v + y, pc);
  }
  // lone pixels on the hat and jacket layers lie flat on the skin under them (glasses, a monocle, a button)
  auto flatten = [&](int x0, int y0, int x1, int y1, int dx, int dy) {
   for (int pass = 0; pass < 6; pass++) {   // (taking some away can leave others alone: again until none are)
    std::vector<std::pair<int, int>> lone;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
      if (free_(x, y)) continue;
      int n = 0;
      for (auto [ox, oy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) { const int nx = x + ox, ny = y + oy; if (nx >= x0 && nx < x1 && ny >= y0 && ny < y1 && !free_(nx, ny)) n++; }
      if (n <= 1) lone.push_back({x, y});
    }
    for (auto [x, y] : lone) { u8* o = px_(x, y); u8* b = px_(x + dx, y + dy); b[0] = o[0]; b[1] = o[1]; b[2] = o[2]; b[3] = 255; o[3] = 0; }
    if (lone.empty()) break;
   }
  };
  flatten(32, 0, 64, 16, -32, 0);   // the hat layer over the head
  flatten(16, 32, 40, 48, 0, -16);  // the jacket layer over the body
}

} // namespace dq
