// assets/pack_models.cpp — see pack_models.h
#include "assets/pack_models.h"
#include "assets/pack_fs.h"
#include "assets/tileset.h"
#include <atomic>
#include <map>
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

const PackModel* packModel(const std::string& name) {
  auto m = std::atomic_load(&g_models);
  if (!m) return nullptr;
  auto it = m->find(name);
  return it == m->end() ? nullptr : &it->second;   // (the map lives till the next pack: the mesher's chunk is done long before)
}

} // namespace dq
