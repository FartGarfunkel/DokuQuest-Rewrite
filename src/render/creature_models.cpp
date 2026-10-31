// render/creature_models.cpp — voxel creature bodies (see creature_models.h); render/packmobs.js port.
#include "render/creature_models.h"
#include "assets/voxel_paint.h"
#include "assets/tileset.h"
#include "assets/pack_fs.h"
#include "client/app.h"

namespace dq {

static constexpr int TILE = 32;
using Pic = std::vector<u32>;   // TILE*TILE voxel colours (0 = clear); empty = the face is left open

// ------------------------------------------------------------------ the original's tiles (models.js tilesOf)
static Pic tilePic(const TileSet& ts, const std::string& name) {
  auto it = ts.index.find(name);
  int t = it == ts.index.end() ? -1 : it->second;
  if (t < 0) { auto w = ts.index.find("white"); t = w == ts.index.end() ? 0 : w->second; }
  Pic p(TILE * TILE);
  int S = ts.size;
  for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) p[y * TILE + x] = tileVoxel(ts, t, x * S / TILE, y * S / TILE);
  return p;
}
static std::array<Pic, 6> tilesOf(const TileSet& ts, const json& spec) {
  std::array<Pic, 6> f;
  auto pic = [&](const json& n) { return n.is_string() ? tilePic(ts, n.get<std::string>()) : Pic(); };
  if (spec.is_string()) { Pic p = pic(spec); for (auto& x : f) x = p; }
  else if (spec.is_array()) { for (int i = 0; i < 6 && i < (int)spec.size(); i++) f[i] = pic(spec[i]); }
  else if (spec.is_object()) {
    json side = spec.contains("side") ? spec["side"] : spec.value("all", json("white"));
    Pic s = pic(side), top = spec.contains("top") ? pic(spec["top"]) : s, bot = spec.contains("bottom") ? pic(spec["bottom"]) : s;
    Pic front = spec.contains("front") ? (spec["front"] == false ? Pic() : pic(spec["front"])) : s, back = spec.contains("back") ? pic(spec["back"]) : s;
    f = {s, s, top, bot, back, front};
  } else { Pic p = tilePic(ts, "white"); for (auto& x : f) x = p; }
  return f;
}

// ------------------------------------------------------------------ resource-pack skins (packmobs.js)
struct PM { int u, v, w, h, d; };
struct PMo { bool upper = false, lower = false, whole = false, mirror = false, lying = false, wool = false, slimeFace = false; int legStrip = -1; bool hasOver = false; PM over{}; };
struct PMPart { std::string name; PM box; PMo o; };
struct PMSpec { std::vector<std::string> tex; int texW = 64; std::string overlay, wool, eyes; std::vector<std::string> hide; std::vector<std::string> hidePrefix; std::vector<PMPart> parts; };

static std::vector<PMPart> pmBiped(PM head, std::optional<PM> hat, PM body, PM arm, std::optional<PM> armL, PM leg, std::optional<PM> legL) {
  std::vector<PMPart> out;
  PMo ho; if (hat) { ho.hasOver = true; ho.over = *hat; }
  out.push_back({"head", head, ho}); out.push_back({"body", body, {}});
  for (int s = 0; s < 2; s++) {
    bool L = s == 1; std::string S = L ? "L" : "R";
    PM a = L && armL ? *armL : arm, lg = L && legL ? *legL : leg;
    PMo up; up.upper = true; up.mirror = L && !armL; PMo lo; lo.lower = true; lo.mirror = up.mirror;
    out.push_back({"arm" + S, a, up}); out.push_back({"fore" + S, a, lo});
    PMo lu; lu.upper = true; lu.mirror = L && !legL; PMo ll; ll.lower = true; ll.mirror = lu.mirror;
    out.push_back({"leg" + S, lg, lu}); out.push_back({"shin" + S, lg, ll});
  }
  return out;
}
static std::vector<PMPart> pmQuad(PM head, PM body, PM leg) {
  std::vector<PMPart> out; PMo ly; ly.lying = true;
  out.push_back({"head", head, {}}); out.push_back({"body", body, ly});
  for (const char* n : {"FR", "FL", "BR", "BL"}) { PMo u; u.upper = true; u.whole = true; PMo l; l.lower = true; out.push_back({std::string("leg") + n, leg, u}); out.push_back({std::string("shin") + n, leg, l}); }
  return out;
}
static std::vector<PMPart> pmSpider() {
  std::vector<PMPart> out = {{"head", {32, 4, 8, 8, 8}, {}}, {"thorax", {0, 0, 6, 6, 6}, {}}, {"abdomen", {0, 12, 10, 8, 12}, {}}};
  for (int i = 0; i < 4; i++) for (const char* S : {"R", "L"}) { PMo a; a.legStrip = 0; PMo b; b.legStrip = 1; out.push_back({std::string("sleg") + S + std::to_string(i), {18, 0, 16, 2, 2}, a}); out.push_back({std::string("sshin") + S + std::to_string(i), {18, 0, 16, 2, 2}, b}); }
  return out;
}
static const std::unordered_map<std::string, PMSpec>& packMobs() {
  static std::unordered_map<std::string, PMSpec> M;
  if (!M.empty()) return M;
  auto P = [](std::string n, PM b, PMo o = {}) { return PMPart{std::move(n), b, o}; };
  PMo up; up.upper = true; PMo lo; lo.lower = true; PMo upM = up; upM.mirror = true; PMo loM = lo; loM.mirror = true; PMo mir; mir.mirror = true; PMo ly; ly.lying = true;
  { PMSpec s; s.tex = {"entity/iron_golem/iron_golem", "entity/iron_golem"}; s.texW = 128; s.hide = {"brow", "shoulders"};
    s.parts = {P("head", {0, 0, 8, 10, 8}), P("nose", {24, 0, 2, 4, 2}), P("body", {0, 40, 18, 12, 11}), P("waist", {0, 70, 9, 5, 6}),
      P("armR", {60, 21, 4, 30, 6}, up), P("foreR", {60, 21, 4, 30, 6}, lo), P("armL", {60, 58, 4, 30, 6}, up), P("foreL", {60, 58, 4, 30, 6}, lo),
      P("legR", {37, 0, 6, 16, 5}, up), P("shinR", {37, 0, 6, 16, 5}, lo), P("legL", {60, 0, 6, 16, 5}, upM), P("shinL", {60, 0, 6, 16, 5}, loM)};
    M["iron_golem"] = s; }
  PM zh{0, 0, 8, 8, 8}, zhat{32, 0, 8, 8, 8}, zb{16, 16, 8, 12, 4}, za{40, 16, 4, 12, 4}, zl{0, 16, 4, 12, 4};
  { PMSpec s; s.tex = {"entity/zombie/zombie"}; s.parts = pmBiped(zh, zhat, zb, za, std::nullopt, zl, std::nullopt); M["ghoul"] = s; }
  { PMSpec s; s.tex = {"entity/zombie/husk"}; s.parts = pmBiped(zh, zhat, zb, za, std::nullopt, zl, std::nullopt); M["husk"] = s; }
  { PMSpec s; s.tex = {"entity/zombie/drowned"}; s.overlay = "entity/zombie/drowned_outer_layer"; s.hide = {"eyes"}; s.parts = pmBiped(zh, zhat, zb, za, PM{32, 48, 4, 12, 4}, zl, PM{16, 48, 4, 12, 4}); M["drowned"] = s; }
  PM sa{40, 16, 2, 12, 2}, sl{0, 16, 2, 12, 2};
  { PMSpec s; s.tex = {"entity/skeleton/skeleton"}; s.hide = {"spine"}; s.parts = pmBiped(zh, std::nullopt, zb, sa, std::nullopt, sl, std::nullopt); M["skeleton"] = s; }
  { PMSpec s; s.tex = {"entity/skeleton/stray"}; s.overlay = "entity/skeleton/stray_overlay"; s.hide = {"spine", "cloak"}; s.parts = pmBiped(zh, std::nullopt, zb, sa, std::nullopt, sl, std::nullopt); M["stray"] = s; }
  auto illager = [&](PM head, PM body, PM arm, PM leg, std::vector<PMPart> extra) {
    std::vector<PMPart> v = {P("head", head), P("body", body), P("armR", arm, up), P("foreR", arm, lo), P("armL", arm, upM), P("foreL", arm, loM),
      P("legR", leg, up), P("shinR", leg, lo), P("legL", leg, upM), P("shinL", leg, loM)};
    for (auto& e : extra) v.push_back(e);
    return v;
  };
  { PMSpec s; s.tex = {"entity/illager/vindicator"}; s.hide = {"hood"}; s.parts = illager({0, 0, 8, 10, 8}, {16, 20, 8, 12, 6}, {40, 46, 4, 12, 4}, {0, 22, 4, 12, 4}, {}); M["raider"] = s; }
  { PMSpec s; s.tex = {"entity/witch"}; s.hide = {"hood", "herbs"}; s.parts = illager({0, 0, 8, 10, 8}, {16, 20, 8, 12, 6}, {44, 22, 4, 8, 4}, {0, 22, 4, 12, 4}, {P("skirt", {0, 38, 8, 18, 6})}); M["witch"] = s; }
  { PMSpec s; s.tex = {"entity/creeper/creeper"}; s.hide = {"cracks", "slits", "jaw", "crown"}; s.hidePrefix = {"frond", "thorn"};
    s.parts = {P("head", {0, 0, 8, 8, 8}), P("body", {16, 16, 8, 12, 4})}; for (const char* n : {"FR", "FL", "BR", "BL"}) s.parts.push_back(P(std::string("leg") + n, {0, 16, 4, 6, 4}));
    M["fuseling"] = s; }
  { PMSpec s; s.tex = {"entity/enderman/enderman", "entity/enderman"};   // Minecraft's 64x32 enderman; the jaw is its headwear box
    s.parts = {P("head", {0, 0, 8, 8, 8}), P("jaw", {0, 16, 8, 8, 8}), P("body", {32, 16, 8, 12, 4}),
               P("armR", {56, 0, 2, 30, 2}, up), P("foreR", {56, 0, 2, 30, 2}, lo), P("armL", {56, 0, 2, 30, 2}, upM), P("foreL", {56, 0, 2, 30, 2}, loM),
               P("legR", {56, 0, 2, 30, 2}, up), P("shinR", {56, 0, 2, 30, 2}, lo), P("legL", {56, 0, 2, 30, 2}, upM), P("shinL", {56, 0, 2, 30, 2}, loM)};
    M["enderman"] = s; }
  { PMSpec s; s.tex = {"entity/ghast/ghast", "entity/ghast"}; s.parts = {P("body", {0, 0, 16, 16, 16})};
    for (int i = 0; i < 9; i++) s.parts.push_back(P("tent" + std::to_string(i), {0, 0, 2, 9, 2}));
    M["ghast"] = s;
    s.tex = {"entity/ghast/ghast_shooting", "entity/ghast_fire", "entity/ghast/ghast", "entity/ghast"}; M["ghast#shooting"] = s; }   // the open-mouthed face while it fires
  { PMSpec s; s.tex = {"entity/spider/spider"}; s.eyes = "entity/spider_eyes"; s.parts = pmSpider(); M["spider"] = s; }
  { PMSpec s; s.tex = {"entity/spider/cave_spider"}; s.eyes = "entity/spider_eyes"; s.parts = pmSpider(); M["cave_spider"] = s; }
  { PMSpec s; s.tex = {"entity/slime/slime"}; PMo sf; sf.slimeFace = true; s.parts = {P("cube", {0, 0, 8, 8, 8}), P("core", {0, 16, 6, 6, 6}, sf)}; M["slime"] = s; }
  { PMSpec s; s.tex = {"entity/blaze"}; s.parts = {P("core", {0, 0, 8, 8, 8})}; for (int i = 0; i < 6; i++) s.parts.push_back(P("rod" + std::to_string(i), {0, 16, 2, 8, 2})); M["cinder_wisp"] = s; }
  { PMSpec s; s.tex = {"entity/silverfish"}; s.parts = {P("head", {0, 0, 3, 2, 2}), P("seg1", {0, 4, 4, 3, 2}), P("seg2", {0, 9, 6, 4, 3}), P("seg3", {0, 16, 3, 3, 3}), P("seg4", {0, 22, 2, 2, 3}), P("seg5", {11, 0, 2, 1, 2})}; M["stone_mite"] = s; }
  { PMSpec s; s.tex = {"entity/cow/temperate_cow", "entity/cow/cow"}; s.parts = pmQuad({0, 0, 8, 8, 6}, {18, 4, 12, 18, 10}, {0, 16, 4, 12, 4});
    s.parts.push_back(P("hornR", {22, 0, 1, 3, 1})); s.parts.push_back(P("hornL", {22, 0, 1, 3, 1}, mir)); s.parts.push_back(P("udder", {52, 0, 4, 6, 1}, ly)); s.parts.push_back(P("tail", {0, 16, 4, 12, 4}));
    M["cow"] = s; }
  { PMSpec s; s.tex = {"entity/pig/temperate_pig", "entity/pig/pig"}; s.parts = pmQuad({0, 0, 8, 8, 8}, {28, 8, 10, 16, 8}, {0, 16, 4, 6, 4});
    s.parts.push_back(P("snout", {16, 16, 4, 3, 1})); s.parts.push_back(P("tail", {0, 16, 4, 6, 4})); M["pig"] = s; }
  { PMSpec s; s.tex = {"entity/sheep/sheep"}; s.wool = "entity/sheep/sheep_fur"; s.parts = pmQuad({0, 0, 6, 6, 8}, {28, 8, 8, 16, 6}, {0, 16, 4, 12, 4});
    PMo w = ly; w.wool = true; PMo w2; w2.wool = true;
    s.parts.push_back(P("tail", {0, 16, 4, 12, 4})); s.parts.push_back(P("wool", {28, 8, 8, 16, 6}, w)); s.parts.push_back(P("cap", {0, 0, 6, 6, 6}, w2));
    for (const char* n : {"FL", "FR", "BL", "BR"}) s.parts.push_back(P(std::string("legWool") + n, {0, 16, 4, 6, 4}, w2));
    M["sheep"] = s; }
  { PMSpec s; s.tex = {"entity/chicken/temperate_chicken", "entity/chicken"};
    s.parts = {P("head", {0, 0, 4, 6, 3}), P("beak", {14, 0, 4, 2, 2}), P("wattle", {14, 4, 2, 2, 2}), P("body", {0, 9, 6, 8, 6}, ly), P("legR", {26, 0, 3, 5, 3}), P("legL", {26, 0, 3, 5, 3}, mir), P("wingR", {24, 13, 1, 4, 6}), P("wingL", {24, 13, 1, 4, 6}, mir)};
    M["chicken"] = s; }
  { PMSpec s; s.tex = {"entity/wolf/wolf"}; s.parts = pmQuad({0, 0, 6, 6, 4}, {18, 14, 6, 9, 6}, {0, 18, 2, 8, 2});
    s.parts.push_back(P("mane", {21, 0, 8, 6, 7}, ly)); s.parts.push_back(P("snout", {0, 10, 3, 3, 4})); s.parts.push_back(P("earR", {16, 14, 2, 2, 1})); s.parts.push_back(P("earL", {16, 14, 2, 2, 1}, mir)); s.parts.push_back(P("tail", {9, 18, 2, 8, 2}));
    M["wolf"] = s; }
  return M;
}

struct Img { int w = 0, h = 0; std::vector<u8> px; bool ok() const { return w >= 16; } };
static Img loadPackPic(PackFS* pk, const std::string& rel) {
  Img im;
  if (!pk || rel.empty()) return im;
  std::string r = rel; if (r.rfind("entity/", 0) != 0) r = "entity/" + r;
  auto data = pk->read(pk->texRoot + r + ".png");
  if (!data || !loadPngMem(data->data(), data->size(), im.w, im.h, im.px)) im.w = 0;
  return im;
}
// a face's patch, as [u, v, w, h] in texels, for faces [+x, -x, top, bottom, back, front]
static std::array<std::array<int, 4>, 6> pmRegions(PM b) {
  return {{{b.u, b.v + b.d, b.d, b.h}, {b.u + b.d + b.w, b.v + b.d, b.d, b.h}, {b.u + b.d, b.v, b.w, b.d}, {b.u + b.d + b.w, b.v, b.w, b.d}, {b.u + b.d + b.w + b.d, b.v + b.d, b.w, b.h}, {b.u + b.d, b.v + b.d, b.w, b.h}}};
}
enum XF { ID, FLIPX, ROT180, CW, CCW, FLIPY };
static vec2 xform(XF f, float x, float y) {
  switch (f) { case FLIPX: return {1 - x, y}; case ROT180: return {1 - x, 1 - y}; case CW: return {y, 1 - x}; case CCW: return {1 - y, x}; case FLIPY: return {x, 1 - y}; default: return {x, y}; }
}
static void pmPatch(const Img& img, float k, std::array<int, 4> reg, XF xf, Pic& out, bool over, vec2 slice) {
  if (out.empty()) out.assign(TILE * TILE, 0);
  for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
    vec2 m = xform(xf, (x + 0.5f) / TILE, (y + 0.5f) / TILE);
    m.y = slice.x + m.y * (slice.y - slice.x);
    int sx = std::min(img.w - 1, (int)std::floor((reg[0] + m.x * reg[2]) * k)), sy = std::min(img.h - 1, (int)std::floor((reg[1] + m.y * reg[3]) * k));
    const u8* p = &img.px[((size_t)sy * img.w + sx) * 4];
    u32& o = out[y * TILE + x];
    if (over) { if (p[3] >= 128) o = voxRGBA(p[0], p[1], p[2]); }
    else o = p[3] >= 128 ? voxRGBA(p[0], p[1], p[2]) : 0;
  }
}
struct FaceSpec { std::array<int, 4> reg; XF xf; vec2 slice; };
static std::array<FaceSpec, 6> pmFaces(PM box, const PMo& o) {
  auto R = pmRegions(box);
  std::array<std::pair<std::array<int, 4>, XF>, 6> f;
  if (o.lying) f = {{{R[0], CW}, {R[1], CCW}, {R[4], ID}, {R[5], ID}, {R[3], ID}, {R[2], ID}}};
  else f = {{{R[0], ID}, {R[1], ID}, {R[2], ROT180}, {R[3], ID}, {R[4], ID}, {R[5], ID}}};
  if (o.mirror) { auto g = f; f = {{{g[1].first, FLIPX}, {g[0].first, FLIPX}, {g[2].first, g[2].second == ROT180 ? FLIPY : FLIPX}, {g[3].first, FLIPX}, {g[4].first, FLIPX}, {g[5].first, FLIPX}}}; }
  vec2 sl = o.upper && !o.whole ? vec2(0, 0.5f) : o.lower ? vec2(0.5f, 1) : vec2(0, 1);
  std::array<FaceSpec, 6> out;
  for (int i = 0; i < 6; i++) out[i] = {f[i].first, f[i].second, i == 2 || i == 3 ? vec2(0, 1) : sl};
  return out;
}
// The faces of every part the pack has a picture for; hidden = parts to put away.
static bool packSkin(const RigModel& M, const std::string& key, std::vector<std::array<Pic, 6>>& faces, std::vector<u8>& hidden, std::string& used) {
  PackFS* pk = activePack();
  if (!pk || !pk->hasTextures()) return false;
  auto it = packMobs().find(key);
  if (it == packMobs().end()) return false;
  const PMSpec& spec = it->second;
  Img img; std::string rel;
  for (auto& t : spec.tex) { img = loadPackPic(pk, t); if (img.ok()) { rel = t; break; } }
  if (!img.ok()) return false;
  Img over = loadPackPic(pk, spec.overlay), wool = loadPackPic(pk, spec.wool), eyes = loadPackPic(pk, spec.eyes);
  float k = img.w / (float)spec.texW;
  std::set<std::string> empty, done;
  for (const PMPart& pp : spec.parts) {
    int pi = M.find(pp.name);
    if (pi < 0) continue;
    const Img& src = pp.o.wool ? wool : img;
    if (!src.ok()) continue;
    float kk = src.w / (pp.o.wool ? 64.f : (float)spec.texW);
    PMo o = pp.o;
    if (o.whole && M.find("shin" + pp.name.substr(3)) >= 0) o.whole = false;   // a leg with a knee: the thigh takes the top half
    auto F = pmFaces(pp.box, o);
    std::array<Pic, 6> fs; bool any = false;
    for (int f = 0; f < 6; f++) {
      std::array<int, 4> reg = F[f].reg; XF xf = F[f].xf;
      if (o.legStrip >= 0) { reg = {pp.box.u + pp.box.d, pp.box.v + pp.box.d, pp.box.w, pp.box.h}; xf = o.legStrip ? CW : ID; }
      Pic px; pmPatch(src, kk, reg, xf, px, false, F[f].slice);
      if (o.hasOver) pmPatch(src, kk, pmFaces(o.over, o)[f].reg, xf, px, true, F[f].slice);
      if (over.ok() && !o.wool) pmPatch(over, over.w / 64.f, reg, xf, px, true, F[f].slice);
      if (o.slimeFace && f == 5) {   // the slime's eyes and mouth are little boxes of their own in front of the core
        const int E[3][6] = {{34, 2, 2, 2, 10, 18}, {34, 6, 2, 2, 60, 18}, {33, 9, 1, 1, 45, 62}};
        for (auto& e : E) {
          Pic tmp; pmPatch(src, kk, {e[0], e[1], e[2], e[3]}, ID, tmp, false, vec2(0, 1));
          int x0 = e[4] * TILE / 100, y0 = e[5] * TILE / 100, sz = e[2] * TILE / 6;
          for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) { u32 c = tmp[(y * TILE / sz) * TILE + x * TILE / sz]; if (c && y0 + y < TILE && x0 + x < TILE) px[(y0 + y) * TILE + x0 + x] = c; }
        }
      }
      bool a = false; for (u32 c : px) if (c) { a = true; break; }
      if (a) { fs[f] = std::move(px); any = true; }
    }
    if (any) { faces[pi] = std::move(fs); done.insert(pp.name); } else empty.insert(pp.name);
  }
  int eyesPart = M.find("eyes");
  if (eyesPart >= 0) {
    if (eyes.ok() && M.key.find("spider") != std::string::npos) {
      Pic px; pmPatch(eyes, eyes.w / 64.f, pmRegions({32, 4, 8, 8, 8})[5], ID, px, false, vec2(0, 1));
      for (u32& c : px) { int mx = std::max({(int)(c & 255), (int)((c >> 8) & 255), (int)((c >> 16) & 255)}); if (mx < 40) c = 0; }   // black adds nothing: let the face show
      faces[eyesPart] = std::array<Pic, 6>{}; faces[eyesPart][5] = px;
    } else empty.insert("eyes");
  }
  for (size_t i = 0; i < M.parts.size(); i++) {
    const RigPart& p = M.parts[i];
    bool h = empty.count(p.name) || std::find(spec.hide.begin(), spec.hide.end(), p.name) != spec.hide.end() || (p.parent >= 0 && hidden[p.parent]);
    for (auto& pre : spec.hidePrefix) if (p.name.rfind(pre, 0) == 0) h = true;
    if (h) hidden[i] = 1;
  }
  used = rel;
  return true;
}

// ------------------------------------------------------------------ voxelising a part
static float lum(u32 c) { return (c & 255) * 0.299f + ((c >> 8) & 255) * 0.587f + ((c >> 16) & 255) * 0.114f; }

static void voxelPart(VoxelGrid& g, ivec3 n, const std::array<Pic, 6>& F, bool glow, bool faceRelief) {
  g.resize(n);
  auto at = [&](int f, float s, float t) -> u32 { if (F[f].empty()) return 0; int x = clampv((int)(s * TILE), 0, TILE - 1), y = clampv((int)(t * TILE), 0, TILE - 1); return F[f][y * TILE + x]; };
  auto open = [&](int f) { return F[f].empty(); };
  u32 avg = 0; { u64 r = 0, gg = 0, b = 0, k = 0; for (auto& f : F) for (u32 c : f) if (c) { r += c & 255; gg += (c >> 8) & 255; b += (c >> 16) & 255; k++; } if (k) avg = voxRGBA((int)(r / k), (int)(gg / k), (int)(b / k)); }
  for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
    vec3 u((x + 0.5f) / n.x, (y + 0.5f) / n.y, (z + 0.5f) / n.z);
    u32 c[6] = {at(0, 1 - u.z, 1 - u.y), at(1, u.z, 1 - u.y), at(2, u.x, u.z), at(3, 1 - u.x, u.z), at(4, u.x, 1 - u.y), at(5, 1 - u.x, 1 - u.y)};
    // the visual hull: on every axis, one of the two pictures must be solid there (open faces don't carve)
    bool keep = true;
    for (int a = 0; a < 3 && keep; a++) if (!(c[a * 2] || c[a * 2 + 1] || (open(a * 2) && open(a * 2 + 1)))) keep = false;
    if (!keep) continue;
    float d[6] = {(float)(n.x - 1 - x), (float)x, (float)(n.y - 1 - y), (float)y, (float)(n.z - 1 - z), (float)z};
    int best = -1; for (int f = 0; f < 6; f++) if (c[f] && (best < 0 || d[f] < d[best])) best = f;
    u32 col = best >= 0 ? c[best] : avg;
    if (!col) col = voxRGBA(200, 200, 200);
    g.set(x, y, z, (col & 0xFFFFFF) | (u32)(glow ? VOXEL_GLOW : VOXEL_SOLID) << 24);
  }
  // rounded edges on the bigger boxes
  if (n.x >= 10 && n.y >= 10 && n.z >= 10) {
    for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
      int ext = (x == 0 || x == n.x - 1) + (y == 0 || y == n.y - 1) + (z == 0 || z == n.z - 1);
      if (ext >= 2) g.set(x, y, z, 0);
    }
  }
  // a face's dark features (eyes, nostrils, mouths) sit a voxel deep
  if (faceRelief && !F[5].empty() && n.z >= 6 && n.x >= 8 && n.y >= 6) {
    std::vector<float> ls; for (u32 c : F[5]) if (c) ls.push_back(lum(c));
    if (ls.empty()) return;
    std::nth_element(ls.begin(), ls.begin() + ls.size() / 2, ls.end());
    float med = ls[ls.size() / 2];
    if (med < 50) return;
    for (int y = 1; y < n.y - 1; y++) for (int x = 1; x < n.x - 1; x++) {
      u32 c = at(5, 1 - (x + 0.5f) / n.x, 1 - (y + 0.5f) / n.y);
      if (!c || lum(c) >= med * 0.55f || !g.solid(x, y, 0)) continue;
      u32 v = g.at(x, y, 0);
      g.set(x, y, 0, 0);
      if (g.solid(x, y, 1)) g.set(x, y, 1, v);
    }
  }
}

void buildTileBox(VoxelGrid& g, ivec3 n, const std::string& tile, bool glow) {
  std::array<Pic, 6> F; Pic p = tilePic(app().tiles, tile); for (auto& f : F) f = p;
  voxelPart(g, n, F, glow, false);
}

// ------------------------------------------------------------------ cache
static std::unordered_map<std::string, std::unique_ptr<CreatureModelGL>>& cache() { static auto* m = new std::unordered_map<std::string, std::unique_ptr<CreatureModelGL>>(); return *m; }
static std::string builtFor;
void creatureModelsReset() { cache().clear(); }

const CreatureModelGL* creatureModel(const std::string& key) {
  const TileSet& ts = app().tiles;
  PackFS* pk = activePack();
  std::string stamp = ts.packName + "#" + std::to_string(ts.count) + "#" + (pk ? pk->name() : "");
  if (stamp != builtFor) { cache().clear(); builtFor = stamp; }
  auto it = cache().find(key);
  if (it != cache().end()) return it->second.get();
  const RigModel* M = rigModel(key.substr(0, key.find('#')));   // "<model>#<state>": the same rig in another pack picture (a ghast firing)
  if (!M) return nullptr;
  if (key.find('#') != std::string::npos && !packMobs().count(key)) return nullptr;
  auto out = std::make_unique<CreatureModelGL>();
  out->key = key;
  std::vector<std::array<Pic, 6>> faces(M->parts.size());
  std::vector<u8> hidden(M->parts.size(), 0);
  bool pack = packSkin(*M, key, faces, hidden, out->packSkin);
  for (size_t i = 0; i < M->parts.size(); i++) out->parts.push_back(std::make_unique<CreaturePartMesh>());
  for (size_t i = 0; i < M->parts.size(); i++) {
    const RigPart& p = M->parts[i];
    CreaturePartMesh& pm = *out->parts[i];
    if (hidden[i]) { pm.hidden = true; continue; }
    std::array<Pic, 6> F = pack && std::any_of(faces[i].begin(), faces[i].end(), [](const Pic& x) { return !x.empty(); }) ? faces[i] : tilesOf(ts, p.tex);
    vec3 lo = p.min, size = glm::max(p.max - p.min, vec3(1e-3f));
    bool packed = pack && std::any_of(faces[i].begin(), faces[i].end(), [](const Pic& x) { return !x.empty(); });
    // birds' feet: the rig's legs are 1 px sticks, which loses the toes. With a pack skin the leg takes
    // Minecraft's 3x5x3 box (its picture has the toes; the clear texels carve the rest away); with the
    // original's tiles a foot of three toes forward and one back is added under the stick.
    bool bird = p.role == "bleg" && size.x <= 1.6f / 16;
    if (bird && packed) { lo = vec3(-1.5f, p.min.y * 16, -2.5f) / 16.f; size = vec3(3, size.y * 16, 3) / 16.f; }
    ivec3 n = glm::max(ivec3(glm::round(size * 32.f)), ivec3(1));
    VoxelGrid g;
    bool faceRelief = p.tex.is_object() && p.tex.contains("front") && !pack;
    voxelPart(g, n, F, p.glow || p.unlit, faceRelief);
    if (p.eyes && !packed)   // render/models.js: two eyes, not one bar (the gap between them is the bridge of the nose)
      for (int x = 0; x < n.x; x++) if (std::fabs(lo.x + (x + 0.5f) * size.x / n.x) < 0.065f) for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) g.set(x, y, z, 0);
    if (bird && !packed && n.y >= 4) {
      int sx = n.x, sz = n.z, T = std::max(1, sx);   // toe width = the stick's width
      ivec3 m(sx + 4 * T, n.y, sz + 4 * T);           // room for toes on every side
      VoxelGrid f; f.resize(m);
      for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) if (g.solid(x, y, z)) f.set(x + 2 * T, y, z + 2 * T, g.at(x, y, z));
      u32 c = 0; for (int y = 0; y < n.y && !c; y++) if (g.solid(0, y, 0)) c = g.at(0, y, 0);
      if (!c) c = voxRGBA(232, 160, 40) | (u32)VOXEL_SOLID << 24;
      u32 dark = voxRGBA((int)((c & 255) * 0.8f), (int)(((c >> 8) & 255) * 0.8f), (int)(((c >> 16) & 255) * 0.8f)) | (c & 0xFF000000u);
      int cx = 2 * T, cz = 2 * T;
      for (int t = 0; t < T; t++) for (int k = 0; k < sx; k++) {
        for (int d = 1; d <= 2 * T; d++) {   // three toes forward (models face -z), splayed
          f.set(cx + k, t, cz - d, dark);
          f.set(std::max(0, cx + k - (d + 1) / 2 - T / 2), t, cz - d, dark);
          f.set(std::min(m.x - 1, cx + k + (d + 1) / 2 + T / 2), t, cz - d, dark);
        }
        for (int d = 0; d < T; d++) f.set(cx + k, t, cz + sz + d, dark);   // and one back
      }
      g = std::move(f); n = m;
      lo = vec3(lo.x - 2 * T / 32.f, lo.y, lo.z - 2 * T / 32.f);
      size = vec3(m) / 32.f;
    }
    VoxelMeshData md; buildVoxelMesh(g, md);
    if (md.verts.empty()) { pm.hidden = true; continue; }
    pm.gl.upload(md);
    pm.local = glm::scale(glm::translate(mat4(1), lo), size / vec3(n));
  }
  auto* raw = out.get();
  cache()[key] = std::move(out);
  return raw;
}

} // namespace dq
