// assets/char_paint.cpp — the creator's skin painter (ui/creator.js ccPaintSkin, same pixels and the same
// random sequence) and the 3D look it writes into the skin (render/gear3d.js's ccPaintSkin wrapper).
#include "assets/char_creator.h"
#include "common/noise.h"

namespace dq {

namespace {
// a colour to paint (on = there is one; acc = an accessory pixel)
struct Px { vec3 c{0}; bool on = false, acc = false; };
const Px NONE{};
Px P(vec3 c) { return {c, true, false}; }
Px H(const std::string& h) {
  if (h.size() < 7 || h[0] != '#') return P(vec3(128));
  unsigned v = (unsigned)std::strtoul(h.c_str() + 1, nullptr, 16);
  return P(vec3((v >> 16) & 255, (v >> 8) & 255, v & 255));
}
Px A(Px c) { c.acc = true; return c; }
Px dark(Px c, float k = 0.72f) { c.c *= k; return c; }
Px light(Px c, float k = 1.22f) { c.c = glm::min(c.c * k, vec3(255)); return c; }
bool in(const std::string& v, std::initializer_list<const char*> l) { for (auto s : l) if (v == s) return true; return false; }

struct Painter {
  SkinImage& out;
  std::vector<u8>* acc;
  Mulberry32 R;
  Painter(SkinImage& o, std::vector<u8>* a, u32 seed) : out(o), acc(a), R(seed) {}
  float rnd() { return (float)R(); }
  void put(int x, int y, const Px& c, float v = 0.06f) {
    if (!c.on) return;
    if (acc) (*acc)[y * 64 + x] = c.acc ? 1 : 0;
    float k = 1 + (rnd() - 0.5f) * v;
    u8* o = out.at(x, y);
    for (int i = 0; i < 3; i++) o[i] = (u8)clampv((int)(c.c[i] * k), 0, 255);
    o[3] = 255;
  }
  // fn(face, x, y, w, h); faces in the original's order: top bottom right front left back
  template <class Fn> void box(int u, int v, int w, int h, int d, Fn fn) {
    struct F { const char* n; int x0, y0, fw, fh; } faces[6] = {{"top", u + d, v, w, d}, {"bottom", u + d + w, v, w, d}, {"right", u, v + d, d, h},
      {"front", u + d, v + d, w, h}, {"left", u + d + w, v + d, d, h}, {"back", u + d + w + d, v + d, w, h}};
    for (auto& f : faces) for (int y = 0; y < f.fh; y++) for (int x = 0; x < f.fw; x++) put(f.x0 + x, f.y0 + y, fn(std::string_view(f.n), x, y, f.fw, f.fh));
  }
};

const std::map<std::string, std::string>& hairBase() {
  static const std::map<std::string, std::string> M = {{"pixie", "crop"}, {"afro", "curly"}, {"dreads", "long"}, {"undercut", "sides"}, {"pigtails", "short"},
                                                      {"spiky", "short"}, {"slicked", "short"}, {"twinbuns", "bob"}};
  return M;
}
}

void ccPaintSkin(const CharLook& Lin, SkinImage& out, std::vector<u8>* accOut) {
  // the flat picture paints a wizard's or a forked beard as a long one (a 3D character's beard is the real thing)
  CharLook L = Lin;
  if (L.model3d && (L.beard == "wizard" || L.beard == "forked")) L.beard = "long";
  out = SkinImage();
  if (accOut) accOut->assign(4096, 0);
  bool fem = L.sex == "f";
  Painter G(out, accOut, (u32)((u64)L.seed * 2654435761ull) ? (u32)((u64)L.seed * 2654435761ull) : 99991u);
  Px skin = H(L.skin), hair = H(L.hairCol), eye1 = H(L.eyeCol), eye2 = L.eyeCol2.empty() ? eye1 : H(L.eyeCol2), beardC = H(L.beardCol.empty() ? L.hairCol : L.beardCol);
  std::array<std::string, 4> cs = L.cols;
  if (cs[0].empty()) if (auto* o = ccOutfit(L.outfit)) cs = o->cols;
  Px cMain = H(cs[0]), cTrim = H(cs[1]), cLegs = H(cs[2]), cBoots = H(cs[3]);
  auto scaleTint = [&](int x, int y) { return L.scales ? ((x + y) % 2 == 0 ? dark(skin, 0.86f) : skin) : skin; };
  Px white = P(vec3(242, 240, 232));
  bool slim = L.body == "slim";
  const std::string& hx = L.hair;
  std::string hs = hairBase().count(hx) ? hairBase().at(hx) : hx;
  bool bald = hs == "bald";
  auto R = [&]() { return G.rnd(); };

  // ---- the head ----
  G.box(0, 0, 8, 8, 8, [&](std::string_view f, int x, int y, int, int) -> Px {
    Px sk = scaleTint(x, y);
    if (f == "top") return bald ? (L.snout ? dark(sk, 0.9f) : sk) : hs == "sides" || hs == "mohawk" ? (x >= 2 && x <= 5 ? hair : sk) : hair;
    if (f == "bottom") return in(L.beard, {"long", "braided"}) ? beardC : sk;
    if (f == "back") {
      if (bald) return sk;
      if (in(hs, {"long", "tail", "braids", "wild", "curly"})) return hair;
      if (hs == "mohawk") return x >= 3 && x <= 4 ? hair : sk;
      return y < (hs == "crop" ? 3 : hs == "sides" ? 4 : hs == "bob" ? 7 : 6) ? hair : sk;
    }
    if (f == "front") {
      if (!bald && y == 0 && hs != "mohawk" && hs != "sides") return hair;
      if (!bald && y == 1 && in(hs, {"long", "wild", "curly", "braids", "short", "bob"}) && (x <= 1 || x >= 6 || (hs == "wild" && x % 3 == 0))) return hair;
      if (!bald && in(hs, {"long", "wild", "braids", "bob"}) && (x == 0 || x == 7) && y < (hs == "bob" ? 5 : 6)) return hair;
      if (fem && y == 3 && (x == 0 || x == 7)) return P(vec3(34, 24, 22));   // lashes
      if (y == 2) {   // brows (the voxel creator: their weight)
        Px bc = L.species == "orc" || L.species == "dwarf" ? dark(beardC, 0.8f) : L.snout ? dark(sk, 0.8f) : dark(hair, 0.85f);
        if (L.brows == "none") return sk;
        if (L.brows == "thick") return x <= 2 || x >= 5 ? bc : sk;
        if (L.brows == "thin") return x == 2 || x == 5 ? bc : sk;
        if ((x >= 1 && x <= 2) || (x >= 5 && x <= 6)) return bc;
        return sk;
      }
      if (y == 3) {   // the eyes (the blink row)
        Px eye = x >= 4 ? eye2 : eye1;
        const std::string& E = L.eyes;
        if (E == "glow") return (x == 1 || x == 2 || x == 5 || x == 6) ? light(eye, 1.4f) : sk;
        if (E == "slit") return x == 1 || x == 6 ? eye : x == 2 || x == 5 ? P(vec3(20, 16, 12)) : sk;
        if (E == "narrow") return x == 2 || x == 5 ? eye : x == 1 || x == 6 ? dark(sk, 0.82f) : sk;
        if (E == "wide") return x == 1 || x == 6 ? white : x == 2 || x == 5 ? eye : sk;
        return x == 1 || x == 6 ? white : x == 2 || x == 5 ? eye : sk;
      }
      if (y == 4) {
        Px eye = x >= 4 ? eye2 : eye1;
        if (L.eyes == "wide" && (x == 1 || x == 2 || x == 5 || x == 6)) return x == 2 || x == 5 ? dark(eye, 0.7f) : white;
        if (x == 3 || x == 4) return L.nose ? dark(sk, 0.8f) : L.snout ? dark(sk, 0.78f) : dark(sk, 0.88f);
        if (L.marks == "freckles" && (x == 1 || x == 6 || x == 2) && R() < 0.6f) return dark(sk, 0.78f);
        if (L.marks == "blush" && (x == 1 || x == 6)) return P(vec3(std::min(255.f, sk.c.r * 1.12f), sk.c.g * 0.82f, sk.c.b * 0.82f));
        if (L.marks == "paint" && (x <= 2 || x >= 5)) return H("#c03030");
        if (L.marks == "tattoo" && (x == 1 || x == 6)) return dark(sk, 0.6f);
        if (L.marks == "scar" && x == 5) return P(vec3(std::min(255.f, sk.c.r * 1.15f), sk.c.g * 0.75f, sk.c.b * 0.75f));
        if (L.beard == "stubble" && (x == 0 || x == 7)) return dark(beardC, 1.1f);
        return sk;
      }
      if (y == 5) {
        if (L.marks == "scar" && x == 5) return P(vec3(std::min(255.f, sk.c.r * 1.15f), sk.c.g * 0.75f, sk.c.b * 0.75f));
        if (in(L.beard, {"short", "long", "braided", "moustache"}) && x >= 1 && x <= 6) return x >= 2 && x <= 5 ? beardC : (L.beard == "moustache" ? sk : beardC);
        Px mc = fem ? P(vec3(std::min(255.f, sk.c.r * 0.86f + 34), sk.c.g * 0.56f, sk.c.b * 0.6f)) : dark(sk, 0.58f);
        const std::string& m = L.mouth;
        if (L.tusks && (x == 2 || x == 5)) return white;
        if (m == "smile") return x == 2 || x == 5 ? mc : x == 3 || x == 4 ? dark(sk, 0.68f) : sk;
        if (m == "grin") return x >= 2 && x <= 5 ? (x == 3 || x == 4 ? white : mc) : sk;
        if (m == "stern") return x >= 2 && x <= 5 ? dark(sk, 0.66f) : sk;
        return x >= 3 && x <= 4 ? mc : sk;
      }
      if (y >= 6) {
        if (L.beard == "stubble" && R() < 0.5f) return dark(beardC, 1.15f);
        if (in(L.beard, {"short", "long", "braided"})) { if (y == 7 && L.beard == "short" && (x < 2 || x > 5)) return sk; return beardC; }
        if (L.tusks && y == 6 && (x == 2 || x == 5)) return white;
        if (L.mouth == "smile" && y == 6 && x >= 3 && x <= 4) return dark(sk, 0.6f);
        return sk;
      }
      return sk;
    }
    // the sides: hair over the ears
    bool front = f == "right" ? x >= 5 : x <= 2;
    int earX = f == "right" ? 4 : 3;
    if (L.ears != "round" && (y == 3 || y == 4) && x == earX) return light(sk, 1.08f);
    if (!bald) {
      if (hs == "sides" || hs == "mohawk") { if (y < 1) return hair; }
      else {
        int lenH = in(hs, {"long", "wild", "braids", "curly"}) ? 8 : hs == "bob" ? 6 : hs == "crop" ? 2 : hs == "tail" || hs == "bun" ? 3 : 4;
        if (y < lenH && !(front && y >= 2 && !in(hs, {"long", "wild"}))) return hair;
      }
    }
    if (in(L.beard, {"short", "long", "braided"}) && y >= 5 && front) return beardC;
    if (L.marks == "tattoo" && y >= 4 && y <= 6 && x % 2 == 0) return dark(sk, 0.62f);
    return y < 1 && !bald ? hair : scaleTint(x, y);
  });
  // spectacles, a monocle, an eyepatch or a face scarf, on the face itself
  const std::string& ew = L.eyewear;
  auto F = [&](int x, int y, Px c) { G.put(8 + x, 8 + y, c, 0.02f); };
  if (ew == "glasses") { Px fr = A(P(vec3(60, 52, 44))); for (int x : {0, 3, 4, 7}) F(x, 3, fr); for (int x : {1, 2, 5, 6}) F(x, 2, fr); for (int x = 4; x < 8; x++) G.put(x, 11, fr, 0.02f); for (int x = 0; x < 4; x++) G.put(16 + x, 11, fr, 0.02f); }
  if (ew == "monocle") { Px gd = A(P(vec3(216, 176, 64))); F(4, 3, gd); F(7, 3, gd); F(5, 2, gd); F(6, 2, gd); F(5, 4, gd); F(6, 4, gd); F(7, 5, gd); F(7, 6, gd); }
  if (ew == "eyepatch") {
    Px bk = A(P(vec3(26, 22, 20))), st = A(P(vec3(44, 34, 28)));
    F(5, 3, bk); F(6, 3, bk); F(5, 4, bk); F(6, 4, bk);
    for (int x = 0; x < 8; x++) { if (x < 5 || x > 6) F(x, 2, st); G.put(x, 10, st, 0.02f); G.put(16 + x, 10, st, 0.02f); G.put(24 + x, 10, st, 0.02f); }
  }
  if (ew == "mask") {
    Px mt = A(cTrim);
    for (int y = 5; y < 8; y++) { for (int x = 0; x < 8; x++) F(x, y, y == 5 ? light(mt, 1.1f) : mt); for (int x = 4; x < 8; x++) G.put(x, 8 + y, mt); for (int x = 0; x < 4; x++) G.put(16 + x, 8 + y, mt); }
  }
  // ---- the outer layer of the head: hair volume, hood, hats, horns, tusks, snout, ears ----
  Px hornC = H(L.hornCol.empty() ? "#2a2a30" : L.hornCol);
  G.box(32, 0, 8, 8, 8, [&](std::string_view f, int x, int y, int, int) -> Px {
    bool side = f == "right" || f == "left";
    if (L.hood) { Px hc = cMain; if (f == "bottom") return NONE; if (f == "front") return y < 2 || x == 0 || x == 7 ? (y == 0 || x == 0 || x == 7 ? hc : dark(hc, 0.85f)) : NONE; return hc; }
    if (L.horns != "none" && !L.horns.empty()) {   // horns rise from the brow
      const std::string& Hn = L.horns;
      if (f == "top" && (x == 1 || x == 6) && y >= 4 && y <= 6) return hornC;
      if (f == "front" && y == 0 && (x == 1 || x == 6)) return hornC;
      if (Hn == "ram" && side && y >= 0 && y <= 3 && x >= 2 && x <= 5 && (y == 0 || x == (f == "right" ? 5 : 2) || y == 3)) return y == 3 ? dark(hornC, 0.8f) : hornC;
      if (Hn == "curled" && side && y <= 1 && x >= 3 && x <= 5) return hornC;
      if (Hn == "straight" && f == "top" && (x == 1 || x == 6) && y >= 3) return light(hornC, 1.1f);
      if (Hn == "small" && f == "top" && (x == 2 || x == 5) && y == 5) return hornC;
    }
    const std::string& hat = L.hat;
    bool rim = f != "top" && f != "bottom";
    if (hat == "wizard") { Px hc = cMain, tc = H(cs[1].empty() ? "#e8d070" : cs[1]); if (f == "top") return (x + y) % 5 == 0 ? tc : hc; if (f != "bottom" && y < 2) return y == 1 ? tc : hc; }
    if (hat == "feather") { Px hc = cTrim; if (f == "top" && x >= 1 && x <= 6) return hc; if (f != "bottom" && y == 0) return hc; if (f == "right" && y == 1 && x >= 2 && x <= 5) return H("#e84a2a"); }
    if (hat == "circlet") { if (rim && y == 1) return x % 3 == 0 ? H("#5ac03a") : H("#8a6a3a"); }
    if (hat == "crown") { if (rim && (y == 0 || y == 1)) return y == 0 && x % 2 ? NONE : H("#e8c040"); }
    if (hat == "straw") { Px st = (x + y) % 2 ? H("#d8c070") : H("#c0a050"); if (f == "top") return st; if (f != "bottom" && y <= 1) return y == 1 ? H("#8a2a2a") : st; }
    if (hat == "cap") { if (f == "top") return cMain; if (f != "bottom" && y == 0) return cMain; if (f == "front" && y == 1) return dark(cMain, 0.75f); }
    if (hat == "bucket") { if (f == "top" || (f != "bottom" && y <= 2)) return y == 2 && f != "top" ? dark(cTrim, 0.8f) : cTrim; }
    if (hat == "chef") { if (f == "top" || (f != "bottom" && y <= 1)) return y == 1 && f != "top" ? H("#dcdcd4") : H("#f4f4f0"); }
    if (hat == "bandana") { if (f == "top" || (f != "bottom" && y == 0) || (f == "back" && y <= 3 && x >= 3 && x <= 4)) return (x + y) % 4 == 0 ? H("#f0f0e8") : cTrim; }
    if (hat == "flowers") { if (rim && y == 0) return x % 3 == 0 ? H("#e86aa0") : x % 3 == 1 ? H("#f0e070") : H("#4a8a2a"); }
    if (hat == "hood" && f != "bottom") { if (f == "front") return y < 2 || x == 0 || x == 7 ? cMain : NONE; return cMain; }
    if (!bald) {   // hair that stands out from the head
      if (hx == "afro" && f != "bottom" && (f != "front" || y == 0)) return (x + y) % 3 ? hair : light(hair, 1.12f);
      if (hx == "dreads" && (f == "back" || side) && x % 2 == 0 && !(f == "right" ? x >= 5 : f == "left" && x <= 2)) return y % 3 == 2 ? dark(hair, 0.75f) : dark(hair, 0.9f);
      if (hx == "pigtails" && side && y >= 2 && x == (f == "right" ? 1 : 6)) return y % 2 ? dark(hair, 0.85f) : hair;
      if (hx == "spiky" && (f == "top" ? (x + y) % 2 == 0 : f != "bottom" && y == 0 && x % 2 == 0)) return light(hair, 1.1f);
      if (hx == "twinbuns" && f == "top" && (x <= 1 || x >= 6) && y >= 2 && y <= 5) return light(hair, 1.06f);
      if (hx == "undercut" && (f == "top" || (f == "front" && y == 0 && x >= 1))) return f == "top" && x == 6 ? light(hair, 1.15f) : hair;
      if (hx == "slicked" && f == "top" && (x == 3 || x == 4)) return light(hair, 1.2f);
      if (hx == "slicked" && f == "back" && y == 6) return hair;
      if (hx == "pixie" && f == "front" && y == 1 && x <= 3) return hair;
      if (hs == "wild" && f != "bottom" && f != "front" && R() < 0.75f) return hair;
      if (hs == "wild" && f == "front" && y == 0) return hair;
      if (hs == "curly" && (f == "top" || ((side || f == "back") && y < 5)) && R() < 0.6f) return light(hair, 1.08f);
      if (hs == "mohawk" && f == "top" && x >= 3 && x <= 4) return hair;
      if (hs == "mohawk" && (f == "front" || f == "back") && y == 0 && x >= 3 && x <= 4) return hair;
      if (hs == "bun" && f == "top" && x >= 3 && x <= 4 && y >= 5) return hair;
      if (hs == "tail" && f == "back" && x >= 3 && x <= 4 && y >= 2) return hair;
      if (hs == "braids" && f == "back" && (x == 2 || x == 5) && y >= 2) return y % 2 ? dark(hair, 0.85f) : hair;
      if (hs == "long" && f == "back" && y >= 6) return hair;
    }
    if (L.snout && f == "front" && y >= 4 && x >= 1 && x <= 6) return y == 4 && (x == 2 || x == 5) ? P(vec3(30, 20, 18)) : scaleTint(x, y);
    if (L.tusks && f == "front" && y == 5 && (x == 1 || x == 6)) return white;
    if (L.ears == "long" && side && (y == 2 || y == 3) && x == (f == "right" ? 4 : 3)) return light(skin, 1.05f);
    if (in(L.beard, {"long", "braided"}) && f == "front" && y >= 6) return L.beard == "braided" && x % 3 == 1 ? H("#c8a040") : beardC;
    if (L.beard == "braided" && f == "bottom" && x >= 2 && x <= 5) return beardC;
    return NONE;
  });
  // ---- the body: the outfit ----
  const std::string& O = L.outfit;
  bool robe = in(O, {"vestments", "leafrobe", "silks", "pact", "archrobe", "dress", "gown"});
  bool bareArms = O == "furs" || O == "wraps";
  const std::string &bt = L.belt, &nk = L.neck, &gl = L.gloves, &sh = L.shoes;
  Px gold = H("#e0b838");
  auto mail = [&](int x, int y) { return (x + y) % 2 ? H("#8a8a94") : H("#a0a0aa"); };
  auto bodyFn = [&](std::string_view f, int x, int y, int, int h) -> Px {
    if (f == "top") return cMain;
    if (f == "bottom") return robe ? cMain : cLegs;
    if (O == "furs") {
      if (f == "front" && x >= 2 && x <= 5 && y < 8) return scaleTint(x, y);
      if (y == 8 && (bt == "-" || bt == "plain")) return bt == "plain" ? A(H("#3a2a1a")) : H("#3a2a1a");
      if (y != 8 || bt == "none" || robe) return R() < 0.3f ? dark(cMain, 0.85f) : cMain;
    }
    if (O == "wraps" && f == "front" && (x == y % 8 || x == 7 - (y % 8)) && y < 7) return cTrim;
    if (O == "smith" && f == "front" && (y >= 2 ? x >= 1 && x <= 6 : x == 1 || x == 6)) return y == 2 ? dark(cTrim, 0.8f) : cTrim;
    if (O == "tabard") { if ((f == "front" || f == "back") && x >= 1 && x <= 6) return f == "front" && (((x == 3 || x == 4) && y >= 2 && y <= 7) || (y == 4 && x >= 2 && x <= 5)) ? gold : cTrim; return mail(x, y); }
    if (O == "pirate" && (y == h - 4 || y == h - 5 || (f == "front" && x == 6 && y > h - 4))) return y == h - 5 ? dark(cTrim, 0.85f) : cTrim;
    if (bt == "bandolier" && f == "front" && x == std::min(7, y) && y < 8) return A(y % 3 == 1 ? H("#c8c8d0") : H("#4a3020"));
    if (y == h - 4 && !robe && bt != "none") {   // belt and buckle ('-' is your own skin's, so it isn't marked)
      if (bt == "sash") return A(cTrim);
      if (bt == "rope") return A(x % 2 ? H("#c8a870") : H("#a08050"));
      if (bt == "studded") return A(f == "front" && (x == 3 || x == 4) ? H("#c8c8d0") : x % 2 ? H("#b0b0b8") : H("#3a2a1a"));
      if (bt == "gold") return A(f == "front" && (x == 3 || x == 4) ? H("#3a8ae0") : gold);
      Px pb = f == "front" && (x == 3 || x == 4) ? H("#c8a040") : H("#3a2a1a");
      return bt == "-" ? pb : A(pb);
    }
    if (!robe && f == "front" && y > h - 4 && y < h - 1 && ((bt == "sash" && x == 6) || (bt == "pouch" && x >= 5 && x <= 6))) return A(bt == "sash" ? dark(cTrim, 0.9f) : y == h - 3 ? H("#7a5a32") : H("#6a4a2a"));
    if (y == 7 && robe && bt != "none") return bt == "rope" ? (x % 2 ? H("#c8a870") : H("#a08050")) : bt == "gold" ? gold : cTrim;   // robe sash
    if (O == "sailor" && y < h - 4) return y % 2 ? cTrim : cMain;
    if (O == "jester" && y != h - 4) return f == "front" || f == "back" ? ((x < 4) == (y < 6) ? cMain : cTrim) : f == "right" ? cMain : cTrim;
    if (O == "hunter" && y < h - 4 && (f != "front" || x <= 2 || x >= 5)) return f == "front" && (x == 2 || x == 5) ? dark(cTrim, 0.8f) : cTrim;
    if (f == "front") {
      if (y == 0 && (x == 3 || x == 4)) return O == "shadow" || O == "gambeson" || O == "ranger" ? dark(cMain, 0.8f) : scaleTint(x, y);
      if (O == "gambeson" && x >= 2 && x <= 5 && y >= 1 && y < 8) return y == 3 && (x == 3 || x == 4) ? H("#e8c040") : cTrim;
      if (O == "surcoat" && x >= 1 && x <= 6) return ((x == 3 || x == 4) && y >= 2 && y <= 5) || (y == 3 && x >= 2 && x <= 5) ? cTrim : cMain;
      if (O == "vestments" && (x == 3 || x == 4)) return cTrim;
      if (O == "vestments" && y == 3 && x >= 2 && x <= 5) return cTrim;
      if (O == "doublet" && (x == y - 1 || x == y) && y < 8) return cTrim;
      if (O == "archrobe" && R() < 0.06f) return H("#f0e8a0");
      if (O == "pact" && (x == 3 || x == 4) && y > 1) return cTrim;
      if (O == "silks" && x == 3 + (y % 2)) return cTrim;
      if (O == "noble" && (x == 3 || x == 4) && y % 2 == 0) return H("#e8c040");
      if (O == "ranger" && (x == 1 || x == 6)) return dark(cMain, 0.82f);
      if (O == "tunic" && ((y == 0 && (x == 2 || x == 5)) || y == h - 2)) return cTrim;
      if (O == "merchant" && (x == 2 || x == 5) && y < 5) return cTrim;
      if (O == "merchant" && x == 3 && y % 2 == 1 && y < 8) return gold;
      if (O == "pirate" && y <= 2 && (x == 3 || x == 4)) return y == 2 ? dark(skin, 0.9f) : scaleTint(x, y);
      if (O == "pirate" && y == 1 && (x == 2 || x == 5)) return dark(cMain, 0.8f);
    }
    if (f == "back" && L.cloak) return dark(cTrim, 0.9f);
    return y == h - 1 ? dark(cMain, 0.8f) : (O == "shadow" && R() < 0.1f ? dark(cMain, 0.8f) : cMain);
  };
  G.box(16, 16, 8, 12, 4, [&](std::string_view f, int x, int y, int w, int h) -> Px {
    Px c = bodyFn(f, x, y, w, h);
    bool sd = (x >= 1 && x <= 2) || (x >= 5 && x <= 6);
    if (fem && c.on && f == "front" && O != "furs" && sd) { if (y == 2) return light(c, 1.1f); if (y == 4) return dark(c, 0.8f); }
    if (O == "gown" && f == "front" && y == 1 && x >= 1 && x <= 6) return cTrim;
    if (nk == "scarf" && f != "bottom" && (f == "top" ? x >= 2 && x <= 5 : y <= 1 || (f == "front" && x == 5 && y <= 4))) return A(y == 1 && f != "top" ? dark(cTrim, 0.85f) : cTrim);
    if (nk == "collar" && f != "bottom" && (f == "top" || y == 0)) return A(R() < 0.4f ? H("#b8a888") : H("#d8ccb0"));
    if ((nk == "amulet" || nk == "pendant") && f == "front") { if (y <= 1 && (x == 2 || x == 5)) return A(gold); if (y == 2 && (x == 3 || x == 4)) return A(nk == "amulet" ? H("#c83030") : H("#3a7ae0")); }
    if (nk == "torc" && y == 0 && f != "top" && f != "bottom" && (f != "front" || x <= 2 || x >= 5)) return A(gold);
    return c;
  });
  // the jacket layer: a cloak, a robe's skirt over the belt, a tail
  G.box(16, 32, 8, 12, 4, [&](std::string_view f, int x, int y, int, int) -> Px {
    if (L.cloak && (f == "back" || ((f == "left" || f == "right") && x == (f == "right" ? 0 : 3)))) return y == 0 ? dark(cTrim, 0.85f) : cTrim;
    if (L.cloak && f == "top" && (y == 3 || x == 0 || x == 7)) return cTrim;
    if (L.tail && f == "back" && x >= 3 && x <= 4 && y >= 8) return scaleTint(x, y);
    if (O == "surcoat" && f == "front" && x >= 1 && x <= 6 && y >= 8) return cMain;
    if (O == "merchant" && f != "top" && f != "bottom" && y >= 8 && !(f == "front" && (x == 3 || x == 4))) return y == 11 ? dark(cMain, 0.8f) : cMain;
    return NONE;
  });
  // ---- arms ----
  int aw = slim ? 3 : 4;
  auto glove = [&](int x, int y) -> Px { return gl == "leather" ? (y == 9 ? H("#6a4628") : H("#5a3a20")) : gl == "gauntlets" ? (y == 8 ? H("#c8c8d4") : mail(x, y)) : gl == "silk" ? H("#ecece6") : NONE; };
  auto arm = [&](char sideC) {
    return [&, sideC](std::string_view f, int x, int y, int, int) -> Px {
      Px sc = O == "jester" && sideC == 'r' ? cTrim : cMain;
      if (f == "top") return bareArms ? skin : O == "tabard" ? mail(x, y) : sc;
      if (f == "bottom") { Px gv = glove(x, y); return gv.on ? A(gv) : skin; }
      if (gl != "none" && y >= (gl == "gauntlets" ? 8 : gl == "silk" || gl == "leather" ? 9 : gl == "bracers" ? 7 : 10)) {
        if (gl == "fingerless") return y == 10 ? A(cTrim) : scaleTint(x, y);
        if (gl == "bracers") return y <= 9 ? A(f == "front" && y == 8 ? gold : H("#6a4a2a")) : scaleTint(x, y);
        return A(glove(x, y));
      }
      if (O == "tabard" && y < 10) return mail(x, y);
      if (O == "smith" && y < 4) return y == 3 ? dark(cMain, 0.8f) : cMain;
      if (O == "jester" && y < 8) return y == 7 ? dark(sc, 0.8f) : sc;
      if (bareArms) return y >= 8 && y <= 10 ? (O == "furs" ? H("#5a3a20") : H(cs[1])) : scaleTint(x, y);
      int sleeve = robe ? 11 : O == "doublet" ? 9 : 8;
      if (y < sleeve) return O == "surcoat" && y > 3 ? H("#8a8a94") : O == "doublet" && y > 3 ? light(cMain, 1.12f) : y == sleeve - 1 ? dark(cMain, 0.8f) : cMain;
      if ((O == "ranger" || O == "shadow") && y < 11) return H("#5a3a20");   // bracers
      return scaleTint(x, y);
    };
  };
  G.box(40, 16, aw, 12, 4, arm('r')); G.box(32, 48, aw, 12, 4, arm('l'));
  if (O == "surcoat" || O == "archrobe" || O == "silks") {   // gauntlets and robe cuffs on the sleeve layer
    Px cuff = O == "surcoat" ? H("#a8a8b4") : cTrim;
    auto fn = [&](std::string_view f, int, int y, int, int) -> Px { return f != "top" && f != "bottom" && y == (O == "surcoat" ? 9 : 10) ? cuff : NONE; };
    G.box(40, 32, aw, 12, 4, fn); G.box(48, 48, aw, 12, 4, fn);
  }
  // ---- legs: footwear (boots, tall boots, buckled shoes, sandals, fur boots or none at all) ----
  Px LC = cLegs;
  auto leg = [&](std::string_view f, int x, int y, int, int) -> Px {
    if (f == "top") return robe ? cMain : LC;
    if (f == "bottom") return sh == "barefoot" || (O == "wraps" && sh == "boots") ? skin : A(sh == "sandals" ? dark(cBoots, 0.8f) : cBoots);
    if (sh == "boots" && y >= (O == "wraps" ? 11 : 9)) return O == "wraps" ? (y == 11 ? A(H("#8a6a3a")) : skin) : A(y == 9 ? light(cBoots, 1.15f) : cBoots);
    if (sh == "tall" && y >= 5) return A(y == 5 ? light(cBoots, 1.18f) : f == "front" && y == 6 && x % 2 ? dark(cBoots, 0.75f) : cBoots);
    if (sh == "shoes" && y >= 10) return A(y == 10 && f == "front" && (x == 1 || x == 2) ? H("#c8c8d0") : cBoots);
    if (sh == "sandals" && y >= 10) return y == 11 ? A(dark(cBoots, 0.85f)) : f == "front" && x % 2 == 0 ? A(cBoots) : scaleTint(x, y);
    if (sh == "fur" && y >= 7) return A(y == 7 ? H("#d8ccb0") : R() < 0.35f ? dark(cBoots, 0.85f) : cBoots);
    if (sh == "barefoot" && y >= 11) return scaleTint(x, y);
    if (robe) return y == 8 ? dark(cMain, 0.8f) : (O == "archrobe" && R() < 0.05f ? H("#f0e8a0") : cMain);
    if (O == "furs" && y < 4) return R() < 0.4f ? dark(cMain, 0.9f) : cMain;
    return LC;
  };
  G.box(0, 16, 4, 12, 4, leg); if (O == "jester") LC = cTrim; G.box(16, 48, 4, 12, 4, leg);
  if (robe) for (auto [u, v] : {std::array<int, 2>{0, 32}, {0, 48}})
    G.box(u, v, 4, 12, 4, [&](std::string_view f, int, int y, int, int) -> Px { return f != "top" && f != "bottom" && y < 8 ? (y == 7 ? dark(cMain, 0.82f) : cMain) : NONE; });
  out.slim = slim;
  // a 3D character carries its look in the skin's spare corner
  if (L.model3d) gearWrite(out.px.data(), ccGearLook(Lin));
}

GearLook ccGearLook(const CharLook& L) {
  static const std::map<std::string, std::string> HM = {{"bob", "short"}, {"pixie", "crop"}, {"afro", "curly"}, {"dreads", "braids"}, {"undercut", "sides"},
                                                       {"pigtails", "braids"}, {"spiky", "wild"}, {"slicked", "short"}, {"twinbuns", "bun"}};
  auto rgb = [](const std::string& h) { Px p = H(h); return p.c; };
  GearLook G;
  G.species = L.species; G.hair = HM.count(L.hair) ? HM.at(L.hair) : L.hair; G.beard = L.beard; G.horns = L.horns; G.ears = L.ears;
  G.outfit = L.outfit; G.hat = L.hat;
  G.hood = L.hood || L.hat == "hood"; G.cloak = L.cloak; G.tusks = L.tusks; G.snout = L.snout; G.tail = L.tail; G.scales = L.scales; G.nose = L.nose;
  G.slim = L.body == "slim";
  G.robe = L.outfit == "dress" || L.outfit == "gown"; G.apron = L.outfit == "smith"; G.tabard = L.outfit == "tabard"; G.helmHide = L.helm == "hide";
  G.gloves = !L.gloves.empty() && L.gloves != "none" && L.gloves != "-";
  G.beltPick = in(L.belt, {"studded", "sash", "rope", "gold", "pouch", "bandolier", "none"});
  std::array<std::string, 4> cs = L.cols;
  if (cs[0].empty()) if (auto* o = ccOutfit(L.outfit)) cs = o->cols;
  G.hairCol = rgb(L.hairCol); G.beardCol = rgb(L.beardCol.empty() ? L.hairCol : L.beardCol); G.hornCol = rgb(L.hornCol.empty() ? "#463c32" : L.hornCol); G.skin = rgb(L.skin);
  for (int i = 0; i < 4; i++) G.cols[i] = rgb(cs[i]);
  G.extra = G.cols[1]; G.hatCol = G.cols[0];
  return G;
}

void ccPaintOverOwn(const CharLook& L, const SkinImage& own, SkinImage& out) {
  // your own skin under a 3D character: only the spare corner holding the 3D look, and the accessories picked by hand
  CharLook K = L;
  if (!L.accSet.count("shoes")) K.shoes = "-";
  if (!L.accSet.count("belt")) K.belt = "-";
  if (!L.accSet.count("gloves")) K.gloves = "none";
  if (!L.accSet.count("neck")) K.neck = "none";
  if (!L.accSet.count("eyewear")) K.eyewear = "none";
  SkinImage painted; std::vector<u8> acc;
  ccPaintSkin(K, painted, &acc);
  out = own;
  for (int i = 0; i < 4096; i++) {
    int y = i >> 6, x = i & 63;
    if (acc[i] || (y >= 16 && y < 18 && x >= 56)) std::memcpy(out.px.data() + i * 4, painted.px.data() + i * 4, 4);
  }
}

} // namespace dq
