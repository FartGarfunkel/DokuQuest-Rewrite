// render/creature_models.cpp — voxel creature bodies (see creature_models.h); render/packmobs.js port.
#include "render/creature_models.h"
#include "assets/voxel_paint.h"
#include "assets/tileset.h"
#include "assets/pack_fs.h"
#include "client/app.h"
#include "sim/creature.h"
#include <mutex>

namespace dq {

static constexpr int TILE = 32;
using Pic = std::vector<u32>;   // TILE*TILE voxel colours (0 = clear); empty = the face is left open

// ------------------------------------------------------------------ the original's tiles (models.js tilesOf)
// "@name": a creature look's own picture (tools/port/mob_variants.py), data/textures/entity/<name>.png, read here on the
// CPU only (the creatures are voxelised on the CPU), so the hundreds of looks' pictures take no room in the GPU's tile
// arrays (two pages of 2048 at most).
static Pic filePic(const std::string& name) {
  static std::mutex mu; static std::unordered_map<std::string, Pic> cache;
  std::lock_guard<std::mutex> lk(mu);
  if (auto it = cache.find(name); it != cache.end()) return it->second;
  Pic p(TILE * TILE, 0);
  int w = 0, h = 0; std::vector<u8> px;
  if (loadPng(dataDir() / "textures" / "entity" / (name + ".png"), w, h, px) && w > 0 && h > 0)
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
      const u8* q = &px[((size_t)(y * h / TILE) * w + (size_t)(x * w / TILE)) * 4];
      if (q[3] >= 128) p[y * TILE + x] = voxRGBA(q[0], q[1], q[2], VOXEL_SOLID);
    }
  else p.assign(TILE * TILE, voxRGBA(200, 200, 200, VOXEL_SOLID));
  return cache[name] = p;
}
static Pic tilePic(const TileSet& ts, const std::string& name) {
  if (!name.empty() && name[0] == '@') return filePic(name.substr(1));
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
struct PMo { bool upper = false, lower = false, whole = false, mirror = false, lying = false, wool = false, under = false, slimeFace = false; int legStrip = -1; bool hasOver = false; PM over{}; };
struct PMPart { std::string name; PM box; PMo o; };
struct PMSpec { std::vector<std::string> tex; int texW = 64; std::string overlay, eyes; std::vector<std::string> wool, under; std::vector<std::string> hide; std::vector<std::string> hidePrefix; std::vector<PMPart> parts;
  std::unordered_map<std::string, std::array<float, 6>> box;   // with the pack's skin, a part takes Minecraft's box: {lo x,y,z, size x,y,z} in px, model space (feet at 0)
};

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
    // Minecraft's creeper proportions (legs 4x6x4, body 8x12x4, head 8x8x8: 26 px tall) on the fuseling's pivots, so the skin isn't stretched
    s.box = {{"legFL", {-4, 0, -6, 4, 6, 4}}, {"legFR", {0, 0, -6, 4, 6, 4}}, {"legBL", {-4, 0, 2, 4, 6, 4}}, {"legBR", {0, 0, 2, 4, 6, 4}},
             {"body", {-4, 6, -2, 8, 12, 4}}, {"head", {-4, 18, -4, 8, 8, 8}}};
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
  { // the bee (the pack's entity/bee: Minecraft's 64x64 layout), plain, angry and carrying nectar
    PMSpec s; s.tex = {"entity/bee/bee", "entity/bee"};
    PMo wl; wl.mirror = true;
    s.parts = {P("body", {0, 0, 7, 7, 10}), P("sting", {26, 7, 1, 1, 2}), P("antL", {2, 0, 1, 2, 3}), P("antR", {2, 3, 1, 2, 3}),
               P("legs", {26, 1, 7, 2, 2}), P("wingR", {0, 18, 9, 0, 6}), P("wingL", {0, 18, 9, 0, 6}, wl)};
    M["bee"] = s;
    s.tex = {"entity/bee/bee_angry", "entity/bee/bee", "entity/bee"}; M["bee#angry"] = s;
    s.tex = {"entity/bee/bee_nectar", "entity/bee/bee", "entity/bee"}; M["bee#nectar"] = s;
    s.tex = {"entity/bee/bee_angry_nectar", "entity/bee/bee_angry", "entity/bee/bee", "entity/bee"}; M["bee#angry_nectar"] = s; }
  { PMSpec s; s.tex = {"entity/silverfish"}; s.parts = {P("head", {0, 0, 3, 2, 2}), P("seg1", {0, 4, 4, 3, 2}), P("seg2", {0, 9, 6, 4, 3}), P("seg3", {0, 16, 3, 3, 3}), P("seg4", {0, 22, 2, 2, 3}), P("seg5", {11, 0, 2, 1, 2})}; M["stone_mite"] = s; }
  { PMSpec s; s.tex = {"entity/cow/temperate_cow", "entity/cow/cow"}; s.parts = pmQuad({0, 0, 8, 8, 6}, {18, 4, 12, 18, 10}, {0, 16, 4, 12, 4});
    s.parts.push_back(P("hornR", {22, 0, 1, 3, 1})); s.parts.push_back(P("hornL", {22, 0, 1, 3, 1}, mir)); s.parts.push_back(P("udder", {52, 0, 4, 6, 1}, ly)); s.parts.push_back(P("tail", {0, 16, 4, 12, 4}));
    M["cow"] = s; }
  { PMSpec s; s.tex = {"entity/pig/temperate_pig", "entity/pig/pig"}; s.parts = pmQuad({0, 0, 8, 8, 8}, {28, 8, 10, 16, 8}, {0, 16, 4, 6, 4});
    s.parts.push_back(P("snout", {16, 16, 4, 3, 1})); s.parts.push_back(P("tail", {0, 16, 4, 6, 4})); M["pig"] = s; }
  { PMSpec s; s.tex = {"entity/sheep/sheep", "entity/sheep"};
    s.wool = {"entity/sheep/sheep_fur", "entity/sheep/sheep_wool", "entity/sheep_wool"};   // Minecraft's old name, 1.21.5's, a pack's own place
    s.under = {"entity/sheep/sheep_wool_undercoat", "entity/sheep_wool_undercoat"};          // 1.21.5's undercoat: what a sheared sheep keeps
    s.parts = pmQuad({0, 0, 6, 6, 8}, {28, 8, 8, 16, 6}, {0, 16, 4, 12, 4});
    PMo w = ly; w.wool = true; PMo w2; w2.wool = true;
    PMo u = ly; u.under = true; PMo u2; u2.under = true;
    s.parts.push_back(P("under", {28, 8, 8, 16, 6}, u)); s.parts.push_back(P("underCap", {0, 0, 6, 6, 6}, u2));
    for (const char* n : {"FL", "FR", "BL", "BR"}) s.parts.push_back(P(std::string("legUnder") + n, {0, 16, 4, 6, 4}, u2));
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
  // your own skin over the pack's (the Texture Studio's Creatures: userDir()/textures/entity/...)
  { std::error_code ec; const fs::path mine = userDir() / "textures" / (r + ".png"); if (fs::exists(mine, ec) && loadPng(mine, im.w, im.h, im.px)) return im; im.w = 0; }
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
// A variant's colours on its base's pack skin (RigModel::packTint): hue turn (degrees), saturation and value multipliers,
// colVal = a value multiplier for the coloured texels only (brown patches to black, white stays white), mix = [r,g,b,amount].
static void tintImg(Img& im, const json& t) {
  if (!im.ok() || !t.is_object()) return;
  const float hue = t.value("hue", 0.f), sat = t.value("sat", 1.f), val = t.value("val", 1.f), colVal = t.value("colVal", 1.f);
  vec4 mix(0); if (t.contains("mix") && t["mix"].is_array() && t["mix"].size() >= 4) for (int i = 0; i < 4; i++) mix[i] = t["mix"][i].get<float>();
  for (size_t i = 0; i + 3 < im.px.size(); i += 4) {
    if (im.px[i + 3] < 8) continue;
    float r = im.px[i] / 255.f, g = im.px[i + 1] / 255.f, b = im.px[i + 2] / 255.f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn, h = 0, s = mx > 0 ? d / mx : 0, v = mx;
    if (d > 1e-5f) { h = mx == r ? std::fmod((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4; h *= 60; if (h < 0) h += 360; }
    const float s0 = s;
    h = std::fmod(h + hue + 360, 360.f); s = clampv(s * sat, 0.f, 1.f); v = clampv(v * val * (1 + (colVal - 1) * std::min(1.f, s0 * 1.6f)), 0.f, 1.f);
    float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60, 2.f) - 1)), m = v - c, rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = c; gg = x; } else if (h < 120) { rr = x; gg = c; } else if (h < 180) { gg = c; bb = x; } else if (h < 240) { gg = x; bb = c; } else if (h < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
    rr += m; gg += m; bb += m;
    if (mix.w > 0) { rr += (mix.x - rr) * mix.w; gg += (mix.y - gg) * mix.w; bb += (mix.z - bb) * mix.w; }
    im.px[i] = (u8)clampv((int)(rr * 255 + 0.5f), 0, 255); im.px[i + 1] = (u8)clampv((int)(gg * 255 + 0.5f), 0, 255); im.px[i + 2] = (u8)clampv((int)(bb * 255 + 0.5f), 0, 255);
  }
}
// The faces of every part the pack has a picture for; hidden = parts to put away.
static bool packSkin(const RigModel& M, const std::string& key, std::vector<std::array<Pic, 6>>& faces, std::vector<u8>& hidden, std::string& used) {
  PackFS* pk = activePack();
  if (!pk || !pk->hasTextures()) return false;
  auto it = packMobs().find(key);
  if (it == packMobs().end() && !M.packBase.empty()) it = packMobs().find(M.packBase);   // a variant: its base's skin
  if (it == packMobs().end()) return false;
  const PMSpec& spec = it->second;
  Img img; std::string rel;
  for (auto& t : M.packTex) { img = loadPackPic(pk, t); if (img.ok()) { rel = t; break; } }   // the pack's own picture of this look
  const bool own = img.ok();
  if (!own) for (auto& t : spec.tex) { img = loadPackPic(pk, t); if (img.ok()) { rel = t; break; } }
  if (!img.ok()) return false;
  Img over = loadPackPic(pk, spec.overlay), eyes = loadPackPic(pk, spec.eyes), wool, under;
  for (auto& t : spec.wool) { wool = loadPackPic(pk, t); if (wool.ok()) break; }
  for (auto& t : spec.under) { under = loadPackPic(pk, t); if (under.ok()) break; }
  if (!own && !M.packBase.empty()) { tintImg(img, M.packTint); tintImg(over, M.packTint); }
  float k = img.w / (float)spec.texW;
  std::set<std::string> empty, done;
  for (const PMPart& pp : spec.parts) {
    int pi = M.find(pp.name);
    if (pi < 0) continue;
    const Img& src = pp.o.wool ? wool : pp.o.under ? under : img;
    if (!src.ok()) continue;
    float kk = src.w / (pp.o.wool || pp.o.under ? 64.f : (float)spec.texW);
    PMo o = pp.o;
    if (o.whole && M.find("shin" + pp.name.substr(3)) >= 0) o.whole = false;   // a leg with a knee: the thigh takes the top half
    auto F = pmFaces(pp.box, o);
    std::array<Pic, 6> fs; bool any = false;
    for (int f = 0; f < 6; f++) {
      std::array<int, 4> reg = F[f].reg; XF xf = F[f].xf;
      if (o.legStrip >= 0) { reg = {pp.box.u + pp.box.d, pp.box.v + pp.box.d, pp.box.w, pp.box.h}; xf = o.legStrip ? CW : ID; }
      Pic px; pmPatch(src, kk, reg, xf, px, false, F[f].slice);
      if (o.hasOver) pmPatch(src, kk, pmFaces(o.over, o)[f].reg, xf, px, true, F[f].slice);
      if (over.ok() && !o.wool && !o.under) pmPatch(over, over.w / 64.f, reg, xf, px, true, F[f].slice);
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
  // the skin check: a pack picture of the wrong layout (another mob's, an old 64x32 net where a 64x64 is read...) leaves
  // the head or the body empty — that would draw a headless or bodiless creature. Such a skin isn't used: the creature
  // keeps the game's own pictures, and the log says which skin was turned down (once each).
  for (const char* must : {"head", "body"}) if (M.find(must) >= 0 && empty.count(must) && !done.count(must)) {
    static std::set<std::string> told;
    if (told.insert(rel).second) DQ_WARN("creature skins: %s.png from the resource pack doesn't fit %s (its %s is empty); using the game's own", rel.c_str(), key.c_str(), must);
    for (auto& f : faces) f = std::array<Pic, 6>{};
    return false;
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
  int thinAxis = -1;
  { const int d[3] = {n.x, n.y, n.z}; for (int a = 0; a < 3; a++) if (d[a] <= 2 && d[(a + 1) % 3] >= 4 && d[(a + 2) % 3] >= 4) thinAxis = a; }
  u32 avg = 0; { u64 r = 0, gg = 0, b = 0, k = 0; for (auto& f : F) for (u32 c : f) if (c) { r += c & 255; gg += (c >> 8) & 255; b += (c >> 16) & 255; k++; } if (k) avg = voxRGBA((int)(r / k), (int)(gg / k), (int)(b / k)); }
  for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
    vec3 u((x + 0.5f) / n.x, (y + 0.5f) / n.y, (z + 0.5f) / n.z);
    u32 c[6] = {at(0, 1 - u.z, 1 - u.y), at(1, u.z, 1 - u.y), at(2, u.x, u.z), at(3, 1 - u.x, u.z), at(4, u.x, 1 - u.y), at(5, 1 - u.x, 1 - u.y)};
    // the visual hull: on every axis, one of the two pictures must be solid there (open faces don't carve). A thin
    // plane (a leaf, a fern, a cobweb, a coral fan, a wing) is cut by its face pictures only: its edges are a strip of
    // the picture squeezed to a voxel or two, and would carve the shape away.
    bool keep = true;
    for (int a = 0; a < 3 && keep; a++) {
      if (thinAxis >= 0 && a != thinAxis) continue;
      if (!(c[a * 2] || c[a * 2 + 1] || (open(a * 2) && open(a * 2 + 1)))) keep = false;
    }
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

// The enderman's jaw (Minecraft's headwear box): when it lifts its head in anger, the jaw's rim shows a crown of jagged
// teeth around a dark open mouth. The top voxels of the box are carved into points all round its edge, in the jaw's own colours.
static void toothCrown(VoxelGrid& g, ivec3 n) {
  int th = std::max(2, n.y * 2 / 5);   // the teeth: the top two fifths of the jaw
  if (n.y <= th + 1 || n.x < 6 || n.z < 6) return;
  static const int H[4] = {2, 4, 2, 0};   // a tooth every 4 voxels: rising to a point, then a gap
  for (int y = n.y - th; y < n.y; y++) {
    int k = y - (n.y - th);   // 0 = the gum line
    for (int z = 0; z < n.z; z++) for (int x = 0; x < n.x; x++) {
      bool rim = x == 0 || z == 0 || x == n.x - 1 || z == n.z - 1;
      int u = (z == 0 || z == n.z - 1) ? x : z;
      if (!rim || k >= H[u & 3] * th / 4) g.set(x, y, z, 0);   // the mouth is open; between the points, air
    }
  }
  u32 mouth = voxRGBA(18, 6, 22);
  for (int z = 1; z < n.z - 1; z++) for (int x = 1; x < n.x - 1; x++) if (g.solid(x, n.y - th - 1, z)) g.set(x, n.y - th - 1, z, mouth);   // the dark inside
}

void buildTileBox(VoxelGrid& g, ivec3 n, const std::string& tile, bool glow) {
  std::array<Pic, 6> F; Pic p = tilePic(app().tiles, tile); for (auto& f : F) f = p;
  voxelPart(g, n, F, glow, false);
}

// ------------------------------------------------------------------ cache
// one part of a creature as a voxel grid (CPU only: any thread), where it sits in the part's space (lo, size)
static void partGrid(const TileSet& ts, const RigModel& M, const std::string& key, size_t i, const std::vector<std::array<Pic, 6>>& faces, bool pack,
                     VoxelGrid& g, vec3& lo, vec3& size) {
  const RigPart& p = M.parts[i];
  g = VoxelGrid();
  std::array<Pic, 6> F = pack && std::any_of(faces[i].begin(), faces[i].end(), [](const Pic& x) { return !x.empty(); }) ? faces[i] : tilesOf(ts, p.tex);
  lo = p.min; size = glm::max(p.max - p.min, vec3(1e-3f));
  bool packed = pack && std::any_of(faces[i].begin(), faces[i].end(), [](const Pic& x) { return !x.empty(); });
  // birds' feet: the rig's legs are 1 px sticks, which loses the toes. With a pack skin the leg takes
  // Minecraft's 3x5x3 box (its picture has the toes; the clear texels carve the rest away); with the
  // original's tiles a foot of three toes forward and one back is added under the stick.
  bool bird = p.role == "bleg" && size.x <= 1.6f / 16;
  if (bird && packed) { lo = vec3(-1.5f, p.min.y * 16, -2.5f) / 16.f; size = vec3(3, size.y * 16, 3) / 16.f; }
  if (packed) if (auto si = packMobs().find(packMobs().count(key) || M.packBase.empty() ? key : M.packBase); si != packMobs().end())
    if (auto bi = si->second.box.find(p.name); bi != si->second.box.end()) {   // model space -> this part's own space (whatever rig the creature has)
      const auto& b = bi->second; vec3 piv(0); for (int q = (int)i; q >= 0; q = M.parts[q].parent) piv += M.parts[q].pivot;
      lo = vec3(b[0], b[1], b[2]) / 16.f - piv; size = vec3(b[3], b[4], b[5]) / 16.f;
    }
  ivec3 n = glm::max(ivec3(glm::round(size * 32.f)), ivec3(1));
  bool faceRelief = p.tex.is_object() && p.tex.contains("front") && !pack;
  voxelPart(g, n, F, p.glow || p.unlit, faceRelief);
  if (p.name == "jaw" && M.key == "enderman") toothCrown(g, n);
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
}

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
    CreaturePartMesh& pm = *out->parts[i];
    if (hidden[i]) { pm.hidden = true; continue; }
    VoxelGrid g; vec3 lo, size;
    partGrid(ts, *M, key, i, faces, pack, g, lo, size);
    VoxelMeshData md; buildVoxelMesh(g, md);
    if (md.verts.empty()) { pm.hidden = true; continue; }
    pm.gl.upload(md);
    pm.local = glm::scale(glm::translate(mat4(1), lo), size / vec3(g.size));
    pm.lo = lo; pm.size = size;
  }
  auto* raw = out.get();
  cache()[key] = std::move(out);
  return raw;
}

// ------------------------------------------------------------------ mob heads
bool creatureHeadParts(const std::string& mob, const CreatureModelGL*& CM, std::vector<std::pair<int, mat4>>& out) {
  out.clear(); CM = nullptr;
  const CreatureType* T = creatureType(mob);
  if (!T) return false;
  const RigModel* M = rigModel(T->model);
  CM = creatureModel(T->model);
  if (!M || !CM || CM->parts.size() != M->parts.size()) return false;
  int head = M->find("head");
  if (head < 0) for (size_t i = 0; i < M->parts.size() && head < 0; i++) { const RigPart& p = M->parts[i]; if (p.limb == "head" || p.role == "head" || p.role == "phead") head = (int)i; }
  if (head < 0 || CM->parts[head]->hidden) return false;
  const CreaturePartMesh& H = *CM->parts[head];
  float s = 1.f / std::max({H.size.x, H.size.y, H.size.z, 1e-3f});
  mat4 fit = glm::translate(mat4(1), vec3(0.5f, 0, 0.5f)) * glm::scale(mat4(1), vec3(s)) * glm::translate(mat4(1), -vec3(H.lo.x + H.size.x * 0.5f, H.lo.y, H.lo.z + H.size.z * 0.5f));
  std::vector<mat4> rel(M->parts.size(), mat4(0)); rel[head] = mat4(1);
  out.push_back({head, fit});
  for (size_t i = head + 1; i < M->parts.size(); i++) {   // parents come before their children
    const RigPart& p = M->parts[i];
    if (p.parent < 0 || rel[p.parent][3][3] == 0) continue;
    mat4 r = glm::translate(rel[p.parent], p.pivot);
    if (p.rest.y) r = glm::rotate(r, p.rest.y, vec3(0, 1, 0));
    if (p.rest.x) r = glm::rotate(r, p.rest.x, vec3(1, 0, 0));
    if (p.rest.z) r = glm::rotate(r, p.rest.z, vec3(0, 0, 1));
    rel[i] = r;
    if (!CM->parts[i]->hidden && p.alpha >= 1) out.push_back({(int)i, fit * r});
  }
  return true;
}

// A creature's head as ONE voxel grid (CPU only, any thread): the model of a mob head item, so a skull in your hand or
// inventory is that creature's skull (it was drawn as a helmet). The head part and what's fixed to it, fitted like
// creatureHeadParts but turned to face +z (the way block items show their front), sampled at 32 voxels a block.
bool creatureHeadGrid(const std::string& mob, const TileSet& ts, VoxelGrid& out, ivec3& lo32) {
  const CreatureType* T = creatureType(mob);
  if (!T) return false;
  const RigModel* M = rigModel(T->model);
  if (!M) return false;
  const std::string key = T->model;
  std::vector<std::array<Pic, 6>> faces(M->parts.size());
  std::vector<u8> hidden(M->parts.size(), 0);
  std::string used;
  const bool pack = packSkin(*M, key, faces, hidden, used);
  int head = M->find("head");
  if (head < 0) for (size_t i = 0; i < M->parts.size() && head < 0; i++) { const RigPart& p = M->parts[i]; if (p.limb == "head" || p.role == "head" || p.role == "phead") head = (int)i; }
  if (head < 0 || hidden[head]) return false;
  struct PG { VoxelGrid g; mat4 toGrid; };   // unit space -> the part's grid
  std::vector<PG> parts;
  vec3 hlo, hsize;
  { VoxelGrid g; partGrid(ts, *M, key, head, faces, pack, g, hlo, hsize); if (!g.size.x) return false; }
  const float s = 1.f / std::max({hsize.x, hsize.y, hsize.z, 1e-3f});
  const mat4 fit = glm::translate(mat4(1), vec3(0.5f, 0, 0.5f)) * glm::rotate(mat4(1), PI, vec3(0, 1, 0)) * glm::scale(mat4(1), vec3(s)) *
                   glm::translate(mat4(1), -vec3(hlo.x + hsize.x * 0.5f, hlo.y, hlo.z + hsize.z * 0.5f));
  std::vector<mat4> rel(M->parts.size(), mat4(0)); rel[head] = mat4(1);
  vec3 bmin(1e9f), bmax(-1e9f);
  for (size_t i = head; i < M->parts.size(); i++) {
    const RigPart& p = M->parts[i];
    if ((int)i != head) {
      if (p.parent < 0 || rel[p.parent][3][3] == 0) continue;
      mat4 r = glm::translate(rel[p.parent], p.pivot);
      if (p.rest.y) r = glm::rotate(r, p.rest.y, vec3(0, 1, 0));
      if (p.rest.x) r = glm::rotate(r, p.rest.x, vec3(1, 0, 0));
      if (p.rest.z) r = glm::rotate(r, p.rest.z, vec3(0, 0, 1));
      rel[i] = r;
      if (hidden[i] || p.alpha < 1) continue;
    }
    PG pg; vec3 lo, size;
    partGrid(ts, *M, key, i, faces, pack, pg.g, lo, size);
    if (!pg.g.size.x || !pg.g.count()) continue;
    const mat4 toUnit = fit * rel[i] * glm::scale(glm::translate(mat4(1), lo), size / vec3(pg.g.size));
    pg.toGrid = glm::inverse(toUnit);
    for (int c = 0; c < 8; c++) {
      vec3 q = vec3(toUnit * vec4((c & 1) ? pg.g.size.x : 0, (c & 2) ? pg.g.size.y : 0, (c & 4) ? pg.g.size.z : 0, 1));
      bmin = glm::min(bmin, q); bmax = glm::max(bmax, q);
    }
    parts.push_back(std::move(pg));
  }
  if (parts.empty()) return false;
  lo32 = ivec3(glm::floor(bmin * 32.f)); const ivec3 hi32 = ivec3(glm::ceil(bmax * 32.f));
  const ivec3 n = glm::clamp(hi32 - lo32, ivec3(1), ivec3(96));
  out.resize(n);
  for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
    const vec4 c((lo32.x + x + 0.5f) / 32.f, (lo32.y + y + 0.5f) / 32.f, (lo32.z + z + 0.5f) / 32.f, 1);
    for (const PG& pg : parts) {
      const vec3 q = vec3(pg.toGrid * c);
      const u32 v = pg.g.at(ifloor(q.x), ifloor(q.y), ifloor(q.z));
      if (v) { out.set(x, y, z, v); break; }
    }
  }
  return true;
}

} // namespace dq
