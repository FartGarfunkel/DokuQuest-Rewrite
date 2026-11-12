// render/weather_sprites.cpp — see weather_sprites.h
#include "render/weather_sprites.h"
#include "render/gl.h"
#include "assets/tileset.h"
#include "assets/pack_fs.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace dq {
namespace {

struct Img { int w = 0, h = 0; std::vector<u8> px; u8* at(int x, int y) { return px.data() + ((size_t)y * w + x) * 4; } };

struct Rng {   // small and deterministic (the same pictures every run)
  u32 s;
  explicit Rng(u32 seed) : s(seed * 2654435761u + 1) {}
  u32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  int range(int a, int b) { return a + (int)(next() % (u32)(b - a + 1)); }
  float f() { return (next() & 0xffffff) / 16777216.f; }
};

void put(Img& c, int x, int y, u8 r, u8 g, u8 b, u8 a) {
  if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
  u8* p = c.at(x, y);
  if (a > p[3]) { p[0] = r; p[1] = g; p[2] = b; p[3] = a; }   // highest alpha wins (as Particle Rain merges)
}
Img blank(int n) { Img c; c.w = c.h = n; c.px.assign((size_t)n * n * 4, 0); return c; }

// ---- drawn here when the pack has no weather pictures (the game's own art) ----
Img drawRain(int i, int n) {   // a few thin falling streaks, light grey (tinted per particle later)
  Img c = blank(n); Rng r(101 + i);
  const int streaks = 2 + i % 3;
  for (int s = 0; s < streaks; s++) {
    int x = r.range(2, n - 3), y0 = r.range(0, n / 2), len = r.range(n / 5, n * 2 / 5);
    for (int k = 0; k < len; k++) put(c, x, y0 + k, 214, 222, 234, (u8)(120 + 120 * k / std::max(1, len - 1)));   // the head is brightest
  }
  return c;
}
Img drawSnow(int i, int n) {   // a few pixel flakes: dots, small squares, little crosses
  Img c = blank(n); Rng r(303 + i);
  const int flakes = 2 + i % 4;
  for (int s = 0; s < flakes; s++) {
    int x = r.range(3, n - 4), y = r.range(3, n - 4), kind = r.range(0, 2);
    u8 a = (u8)r.range(200, 255);
    put(c, x, y, 255, 255, 255, a);
    if (kind >= 1) { put(c, x + 1, y, 245, 248, 255, a); put(c, x, y + 1, 245, 248, 255, a); put(c, x + 1, y + 1, 235, 240, 250, a); }
    if (kind == 2) { put(c, x - 1, y, 240, 244, 255, a / 2); put(c, x + 2, y, 240, 244, 255, a / 2); put(c, x, y - 1, 240, 244, 255, a / 2); put(c, x, y + 2, 240, 244, 255, a / 2); }
  }
  return c;
}
Img drawRipple(int i, int n) {   // Particle Rain's ring: a Bresenham circle, radius (n/2)/8 * (i+1)
  Img c = blank(n);
  int R = std::clamp((int)((n / 2.f) / 8.f * (i + 1)), 1, n / 2 - 1), cx = n / 2, cy = n / 2;
  auto oct = [&](int x, int y) {
    for (auto [dx, dy] : {std::pair{x, y}, {-x, y}, {x, -y}, {-x, -y}, {y, x}, {-y, x}, {y, -x}, {-y, -x}}) put(c, cx + dx, cy + dy, 255, 255, 255, 255);
  };
  int x = 0, y = R, d = 3 - 2 * R;
  oct(x, y);
  while (y >= x) { if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6; x++; oct(x, y); }
  return c;
}
Img drawStreak(int n) {   // a trickle of water down a wall: a thin wet line, a bead at its foot
  Img c = blank(n);
  int x = n / 2;
  for (int y = n / 8; y < n - n / 6; y++) { u8 a = (u8)(110 + 100 * (y - n / 8) / (n - n / 6 - n / 8)); put(c, x, y, 225, 232, 245, a); if (y % 5 == 0) put(c, x + 1, y, 225, 232, 245, a / 2); }
  for (int dy = 0; dy < 3; dy++) for (int dx = -1; dx <= 1; dx++) put(c, x + dx, n - n / 6 + dy, 235, 240, 250, (u8)(dx == 0 ? 240 : 170));
  return c;
}
Img drawFog(int n, bool upright, u32 seed) {   // a soft cloud of fog: round (lying flat) or with a flat foot (upright)
  Img c = blank(n); Rng r(seed);
  float noise[8][8]; for (auto& row : noise) for (float& v : row) v = 0.7f + 0.3f * r.f();
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
    float u = (x + 0.5f) / n * 2 - 1, v = (y + 0.5f) / n * 2 - 1;
    float d2 = upright ? u * u + std::max(0.f, -v) * std::max(0.f, -v) * 1.6f + (v > 0 ? v * v * 0.3f : 0.f) : u * u + v * v;
    if (d2 >= 1) continue;
    float a = (1 - d2) * (1 - d2);
    if (upright && v > 0.7f) a *= (1 - v) / 0.3f;   // fades out into the ground
    // smooth (bilinear) noise, so the fog has no blocky grid in it
    float gx = std::clamp((x + 0.5f) * 7.f / n, 0.f, 6.999f), gy = std::clamp((y + 0.5f) * 7.f / n, 0.f, 6.999f);
    int ix = (int)gx, iy = (int)gy; float fx = gx - ix, fy = gy - iy;
    float nz = (noise[iy][ix] * (1 - fx) + noise[iy][ix + 1] * fx) * (1 - fy) + (noise[iy + 1][ix] * (1 - fx) + noise[iy + 1][ix + 1] * fx) * fy;
    put(c, x, y, 255, 255, 255, (u8)std::clamp(a * nz * 235.f, 0.f, 255.f));
  }
  return c;
}
Img drawDust(int i, int n) {   // a soft speck of dust: 2, 3 or 4 pixels across
  Img c = blank(n);
  float R = 1.2f + i * 0.9f;
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
    float d = std::hypot(x + 0.5f - n / 2.f, y + 0.5f - n / 2.f);
    if (d < R) put(c, x, y, 255, 255, 255, (u8)(255 * std::min(1.f, (R - d) / 0.8f)));
  }
  return c;
}
Img fromTile(const TileSet& ts, const char* name, int n) {
  Img c = blank(n);
  auto it = ts.index.find(name);
  if (it == ts.index.end() || it->second < 0 || it->second >= ts.count || ts.size <= 0) return drawDust(2, n);
  const u8* src = ts.rgba.data() + (size_t)it->second * ts.size * ts.size * 4;
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
    const u8* p = src + ((size_t)(y * ts.size / n) * ts.size + x * ts.size / n) * 4;
    u8* q = c.at(x, y); q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = p[3];
  }
  return c;
}

// ---- from the resource pack (Particle Rain's SpriteLoaderMixin + TextureUtil) ----
bool loadPng(const std::string& rel, Img& out) {
  PackFS* pk = activePack();
  if (!pk || !pk->hasTextures()) return false;
  auto bytes = pk->read(pk->texRoot + rel);
  if (!bytes) return false;
  std::vector<u8> rgba; int w = 0, h = 0;
  if (!loadPngMem(bytes->data(), bytes->size(), w, h, rgba) || w < 2 || h < w / 2) return false;
  out.w = w; out.h = h; out.px = std::move(rgba);
  return true;
}
void boostAlpha(Img& s) {   // TextureUtil.boostAlpha: a faint sheet is made as opaque as its most opaque pixel allows
  int hi = 0; for (size_t i = 3; i < s.px.size(); i += 4) hi = std::max<int>(hi, s.px[i]);
  if (hi == 0 || hi == 255) return;
  int mul = 255 / hi; if (mul <= 1) return;
  for (size_t i = 3; i < s.px.size(); i += 4) s.px[i] = (u8)std::min(255, s.px[i] * mul);
}
void desaturate(Img& s) {   // TextureUtil.desaturate: grey = the brightest channel (the rain is tinted per particle)
  for (size_t i = 0; i < s.px.size(); i += 4) { u8 g = std::max({s.px[i], s.px[i + 1], s.px[i + 2]}); s.px[i] = s.px[i + 1] = s.px[i + 2] = g; }
}
// slice i of a 2-column sheet (column-major: row i % rows, column i / rows), flipped both ways like copyRect(..,true,true)
Img slice(Img& sheet, int i, int n) {
  const int size = sheet.w / 2, rows = sheet.h / size;
  Img c = blank(n);
  if (i >= rows * 2) return c;
  const int ox = size * (i / rows), oy = size * (i % rows);
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
    const int sx = ox + (n - 1 - x) * size / n, sy = oy + (n - 1 - y) * size / n;
    const u8* p = sheet.at(sx, sy); put(c, x, y, p[0], p[1], p[2], p[3]);
  }
  return c;
}
Img merge(const std::vector<Img>& v, int from, int count, int n) {
  Img c = blank(n);
  for (int i = from; i < from + count && i < (int)v.size(); i++)
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) { const u8* p = v[i].px.data() + ((size_t)y * n + x) * 4; put(c, x, y, p[0], p[1], p[2], p[3]); }
  return c;
}

} // namespace

WeatherSprites& weatherSprites() { static WeatherSprites S; return S; }

vec4 WeatherSprites::uv(int sprite) const {
  const float W = (float)cols * cell, H = (float)rows * cell;
  const int cx = sprite % cols, cy = sprite / cols;
  return vec4((cx * cell + 0.5f) / W, (cy * cell + 0.5f) / H, ((cx + 1) * cell - 0.5f) / W, ((cy + 1) * cell - 0.5f) / H);
}

void buildWeatherSprites(const TileSet& ts, bool upload) {
  WeatherSprites& S = weatherSprites();
  const int n = S.cell, W = S.cols * n, H = S.rows * n;
  S.rgba.assign((size_t)W * H * 4, 0);
  std::vector<Img> rain, snow;
  Img sheet;
  S.fromPack = false;
  if (loadPng("environment/rain.png", sheet)) { boostAlpha(sheet); desaturate(sheet); for (int i = 0; i < 16; i++) rain.push_back(slice(sheet, i, n)); S.fromPack = true; }
  if (loadPng("environment/snow.png", sheet)) { boostAlpha(sheet); for (int i = 0; i < 16; i++) snow.push_back(slice(sheet, i, n)); }
  else S.fromPack = false;
  if (rain.size() < 16) { rain.clear(); for (int i = 0; i < 16; i++) rain.push_back(drawRain(i, n)); }
  if (snow.size() < 16) { snow.clear(); for (int i = 0; i < 16; i++) snow.push_back(drawSnow(i, n)); }
  auto place = [&](int sprite, const Img& c) {
    const int cx = sprite % S.cols, cy = sprite / S.cols;
    for (int y = 0; y < n; y++) std::copy_n(c.px.data() + (size_t)y * n * 4, n * 4, S.rgba.data() + ((size_t)(cy * n + y) * W + cx * n) * 4);
  };
  for (int i = 0; i < 16; i++) { place((int)WSprite::Rain0 + i, rain[i]); place((int)WSprite::Snow0 + i, snow[i]); }
  for (int k = 0; k < 4; k++) { place((int)WSprite::HeavyRain0 + k, merge(rain, k * 4, 4, n)); place((int)WSprite::HeavySnow0 + k, merge(snow, k * 4, 4, n)); }
  for (int i = 0; i < 8; i++) place((int)WSprite::Ripple0 + i, drawRipple(i, n));
  place((int)WSprite::Streak, drawStreak(n));
  place((int)WSprite::FogFlat, drawFog(n, false, 7));
  place((int)WSprite::FogUpright, drawFog(n, true, 11));
  for (int i = 0; i < 3; i++) place((int)WSprite::Dust0 + i, drawDust(i, n));
  place((int)WSprite::Shrub, fromTile(ts, "dead_bush", n));
  DQ_LOG("weather sprites: %d (rain/snow %s)", (int)WSprite::Count, S.fromPack ? "from the resource pack" : "built in");
  if (const char* dump = std::getenv("DQ_WEATHER_ATLAS")) savePng(dump, W, H, S.rgba.data());   // (to look at it)
  if (!upload) return;
  if (!S.tex) glGenTextures(1, &S.tex);
  glBindTexture(GL_TEXTURE_2D, S.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, S.rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace dq
