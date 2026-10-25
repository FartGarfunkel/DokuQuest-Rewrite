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

} // namespace dq
