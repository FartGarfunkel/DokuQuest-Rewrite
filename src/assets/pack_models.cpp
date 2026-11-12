// assets/pack_models.cpp — see pack_models.h
#include "assets/pack_models.h"
#include "assets/pack_fs.h"
#include "assets/tileset.h"
#include <atomic>
#include <array>
#include <map>
#include <unordered_map>
#include <stb_image.h>

namespace dq {
namespace {
std::shared_ptr<const std::map<std::string, PackModel>> g_models;

// a tile for a model's texture ("block/torch_base", "minecraft:block/torch"): ours by that name, else the pack's picture
int tileFor(TileSet& ts, const PackFS& P, std::string ref) {
  if (ref.rfind("minecraft:", 0) == 0) ref = ref.substr(10);
  const std::string base = ref.substr(ref.rfind('/') + 1);
  const std::string key = "pack:" + ref;
  if (auto it = ts.index.find(key); it != ts.index.end()) return it->second;
  auto bytes = P.read(P.texRoot + ref + ".png");
  if (!bytes) { auto it = ts.index.find(base); return it != ts.index.end() ? it->second : -1; }
  int w = 0, h = 0, n = 0;
  u8* px = stbi_load_from_memory(bytes->data(), (int)bytes->size(), &w, &h, &n, 4);
  if (!px || w < 4) { if (px) stbi_image_free(px); auto it = ts.index.find(base); return it != ts.index.end() ? it->second : -1; }
  const int T = ts.size, fh = std::min(w, h);   // (an animated strip: its first frame)
  std::vector<u8> t((size_t)T * T * 4);
  for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
    const u8* s = px + ((size_t)(y * fh / T) * w + x * w / T) * 4;
    u8* d = &t[((size_t)y * T + x) * 4];
    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3] == 253 ? 252 : s[3];
  }
  stbi_image_free(px);
  const int idx = appendTile(ts, t.data());
  ts.res.back() = (u8)std::min(w, T);
  ts.index[key] = idx;
  return idx;
}
// a model and its parents' textures (one level of "parent" is enough for the packs' block models)
std::optional<json> readModel(const PackFS& P, const std::string& name) {
  const std::string root = P.texRoot.substr(0, P.texRoot.size() - std::string("textures/").size());   // assets/minecraft/
  return P.readJson(root + "models/block/" + name + ".json");
}
}  // namespace

void loadPackModels(TileSet& ts, const PackFS& P, const std::vector<std::string>& names) {
  auto M = std::make_shared<std::map<std::string, PackModel>>();
  for (const std::string& name : names) {
    auto j = readModel(P, name);
    if (!j || !j->contains("elements") || !(*j)["elements"].is_array()) continue;
    std::map<std::string, std::string> tex;
    if (j->contains("textures") && (*j)["textures"].is_object()) for (auto& [k, v] : (*j)["textures"].items()) if (v.is_string()) tex[k] = v.get<std::string>();
    auto resolve = [&](std::string r) { for (int i = 0; i < 4 && !r.empty() && r[0] == '#'; i++) { auto it = tex.find(r.substr(1)); if (it == tex.end()) return std::string(); r = it->second; } return r; };
    PackModel pm;
    static const char* FACES[6] = {"north", "south", "west", "east", "up", "down"};
    for (auto& e : (*j)["elements"]) {
      if (!e.contains("from") || !e.contains("to")) continue;
      PackModelBox b;
      b.from = vec3(e["from"][0].get<float>(), e["from"][1].get<float>(), e["from"][2].get<float>());
      b.to = vec3(e["to"][0].get<float>(), e["to"][1].get<float>(), e["to"][2].get<float>());
      b.shade = e.value("shade", true);
      if (e.contains("rotation") && e["rotation"].is_object()) {
        const json& r = e["rotation"];
        const std::string ax = r.value("axis", std::string("y"));
        b.rotAxis = ax == "x" ? 0 : ax == "y" ? 1 : 2; b.rotAngle = r.value("angle", 0.f);
        if (r.contains("origin")) b.rotOrigin = vec3(r["origin"][0].get<float>(), r["origin"][1].get<float>(), r["origin"][2].get<float>());
      }
      if (e.contains("faces")) for (int f = 0; f < 6; f++) {
        if (!e["faces"].contains(FACES[f])) continue;
        const json& fj = e["faces"][FACES[f]];
        const std::string ref = resolve(fj.value("texture", std::string()));
        if (ref.empty()) continue;
        b.face[f].tile = tileFor(ts, P, ref);
        if (fj.contains("uv")) b.face[f].uv = vec4(fj["uv"][0].get<float>(), fj["uv"][1].get<float>(), fj["uv"][2].get<float>(), fj["uv"][3].get<float>());
      }
      pm.boxes.push_back(b);
    }
    if (!pm.boxes.empty()) (*M)[name] = std::move(pm);
  }
  std::atomic_store(&g_models, std::shared_ptr<const std::map<std::string, PackModel>>(M));
  if (!M->empty()) DQ_LOG("pack models: %zu block models", M->size());
}

namespace { std::shared_ptr<const std::unordered_map<int, TileVariants>> g_vars; }
void loadTileVariants(TileSet& ts, const PackFS& P, const std::unordered_map<std::string, int>& baseTiles) {
  auto V = std::make_shared<std::unordered_map<int, TileVariants>>();
  const std::string root = P.texRoot.substr(0, P.texRoot.size() - std::string("minecraft/textures/").size());   // assets/
  if (auto j = P.readJson(root + "dokuquest/variants.json"); j && j->is_object()) for (auto& [base, list] : j->items()) {
    auto bt = baseTiles.find(base);
    if (bt == baseTiles.end() || !list.is_array()) continue;
    TileVariants tv;
    for (auto& e : list) {
      if (!e.is_array() || e.size() < 2) continue;
      const std::string nm = e[0].get<std::string>();
      const int t = nm == base ? bt->second : tileFor(ts, P, "block/" + nm);
      if (t < 0) continue;
      tv.total += std::max(1, e[1].get<int>()); tv.tiles.push_back(t); tv.cum.push_back(tv.total);
    }
    if (tv.tiles.size() > 1) (*V)[bt->second] = std::move(tv);
  }
  std::atomic_store(&g_vars, std::shared_ptr<const std::unordered_map<int, TileVariants>>(V));
  if (!V->empty()) DQ_LOG("pack variants: %zu pictures with random looks", V->size());
}
const TileVariants* tileVariants(int tile) {
  auto v = std::atomic_load(&g_vars);
  if (!v) return nullptr;
  auto it = v->find(tile);
  return it == v->end() ? nullptr : &it->second;
}

namespace { std::shared_ptr<const std::unordered_map<int, DoubleChestTiles>> g_dchest; }
void loadDoubleChests(TileSet& ts, const PackFS& P) {
  auto D = std::make_shared<std::unordered_map<int, DoubleChestTiles>>();
  const bool flip = !P.legacy && (P.packFormat == 0 || P.packFormat >= 5);   // (as packs.cpp packChests: the boxes' sides upside down)
  const int T = ts.size;
  static const vec3 FRONT[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};
  const char* kinds[2][2] = {{"normal", "chest"}, {"trapped", "trapped_chest"}};
  for (auto& k : kinds) {
    auto fi = ts.index.find(std::string(k[1]) + "_front");
    if (fi == ts.index.end()) continue;
    DoubleChestTiles dc;
    bool ok = true;
    for (int half = 0; half < 2 && ok; half++) {
      auto bytes = P.read(P.texRoot + "entity/chest/" + k[0] + (half ? "_right" : "_left") + ".png");
      int w = 0, h = 0, n = 0;
      u8* px = bytes ? stbi_load_from_memory(bytes->data(), (int)bytes->size(), &w, &h, &n, 4) : nullptr;
      if (!px || w < 64 || h < 64) { if (px) stbi_image_free(px); ok = false; break; }
      const double s = w / 64.0;
      auto get = [&](double u, double v) { const int sx = std::clamp((int)std::floor(u * s), 0, w - 1), sy = std::clamp((int)std::floor(v * s), 0, h - 1); const u8* p = px + ((size_t)sy * w + sx) * 4; return std::array<u8, 4>{p[0], p[1], p[2], p[3]}; };
      auto strip = [&](double u, double v, double sw, double sh, double fx, double fy) { return get(u + fx * sw, v + (flip ? 1 - fy : fy) * sh); };
      // a side of the half (front at u 14, back at u 43): the lid's 5 rows over the base's 10
      auto side = [&](double u0, double fx, double fy) { return fy < 5.0 / 15 ? strip(u0, 14, 15, 5, fx, fy * 3) : strip(u0, 33, 15, 10, fx, (fy - 5.0 / 15) * 1.5); };
      std::vector<u8> t((size_t)T * T * 4);
      auto make = [&](auto fn) {
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) { auto c = fn((x + 0.5) / T, (y + 0.5) / T); u8* o = &t[((size_t)y * T + x) * 4]; o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255; }
        const int idx = appendTile(ts, t.data()); ts.res.back() = (u8)T; return idx;
      };
      // (the halves' rows: an end at u 0, the back at u 14, the end the two meet at 29, the front — its lock split between the
      // halves at their meeting edges — at u 43)
      dc.front[half] = make([&](double fx, double fy) { return side(43, fx, fy); });
      dc.back[half] = make([&](double fx, double fy) { return side(14, fx, fy); });
      for (int f = 0; f < 4; f++) {   // the lid's top, for each facing: across it (left to right as you face the front) and from its back to its front
        const vec3 d = FRONT[f], r = glm::cross(-d, vec3(0, 1, 0));
        dc.top[half][f] = make([&](double fx, double fz) {
          const vec3 p((float)fx - 0.5f, 0, (float)fz - 0.5f);
          const double a = glm::dot(p, r) + 0.5, b = glm::dot(p, d) + 0.5;
          return get(14 + a * 15, (1 - b) * 14);
        });
      }
      stbi_image_free(px);
    }
    if (ok) (*D)[fi->second] = dc;
  }
  std::atomic_store(&g_dchest, std::shared_ptr<const std::unordered_map<int, DoubleChestTiles>>(D));
  if (!D->empty()) DQ_LOG("pack large chests: %zu kinds drawn as one", D->size());
}
const DoubleChestTiles* doubleChestFor(int t) {
  auto d = std::atomic_load(&g_dchest);
  if (!d) return nullptr;
  auto it = d->find(t);
  return it == d->end() ? nullptr : &it->second;
}

const PackModel* packModel(const std::string& name) {
  auto m = std::atomic_load(&g_models);
  if (!m) return nullptr;
  auto it = m->find(name);
  return it == m->end() ? nullptr : &it->second;   // (the map lives till the next pack: the mesher's chunk is done long before)
}

} // namespace dq
