// assets/local_models.cpp — see local_models.h
#include "assets/local_models.h"
#include "assets/tileset.h"
#include "sim/station_work.h"
#include "world/world.h"
#include "common/common.h"
#include <atomic>
#include <unordered_set>
#include <memory>
#include <stb_image.h>

namespace dq {
namespace {
struct Stations { std::vector<u8> has; std::unordered_map<BlockId, StationModel> byId; std::unordered_map<std::string, std::shared_ptr<PackModel>> models; };
// read on every cube the mesher draws (its neighbours' faces): a plain pointer, the old ones kept (a reload's worth each)
std::atomic<const Stations*> g_st{nullptr};
std::vector<std::shared_ptr<const Stations>> g_keep;

// a picture from the import's folder as a tile (nearest: the pixels stay crisp), once
int localTile(TileSet& ts, const fs::path& dir, const std::string& ref) {
  std::string r = ref;
  for (const char* p : {"minecraft:", "block/"}) if (r.rfind(p, 0) == 0) r = r.substr(std::string(p).size());
  if (r.rfind("tactile:block/", 0) == 0) {
    const std::string name = r.substr(14), key = "tactile:" + name;
    if (auto it = ts.index.find(key); it != ts.index.end()) return it->second;
    int w = 0, h = 0, ch = 0;
    u8* px = stbi_load((dir / "textures" / (name + ".png")).string().c_str(), &w, &h, &ch, 4);
    if (!px) return -1;
    const int T = ts.size, fw = std::min(w, h);
    std::vector<u8> out((size_t)T * T * 4);
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
      const u8* sp = px + ((size_t)(y * fw / T) * w + (x * fw / T)) * 4;
      u8* d = &out[((size_t)y * T + x) * 4];
      d[0] = sp[0]; d[1] = sp[1]; d[2] = sp[2]; d[3] = sp[3] >= 250 ? 251 : sp[3];   // (below the special alphas)
    }
    stbi_image_free(px);
    const int idx = appendTile(ts, out.data());
    ts.res.back() = (u8)std::min(fw, T);
    ts.index[key] = idx;
    return idx;
  }
  if (r == "water_still") r = "water";   // (our own pictures by their names)
  auto it = ts.index.find(r);
  return it != ts.index.end() ? it->second : -1;
}
}  // namespace

bool tactilePorted(const std::string& key) {
  static const std::unordered_set<std::string> DONE = {"smithing_table", "cartography_table", "furnace", "lit_furnace", "smoker", "lit_smoker", "blast_furnace", "lit_blast_furnace", "anvil", "chipped_anvil", "damaged_anvil", "grindstone", "chest"};
  return DONE.count(key) > 0;
}
const PackModel* tactilePart(const std::string& name) {   // a part drawn on its own (a door, a lid, the wheel), loaded once
  const Stations* s = g_st.load(std::memory_order_acquire);
  if (!s) return nullptr;
  auto it = s->models.find(name);
  return it == s->models.end() || !it->second || it->second->boxes.empty() ? nullptr : it->second.get();
}
void loadStationModels(TileSet& ts) {
  auto S = std::make_shared<Stations>();
  const fs::path dir = dataDir() / "local_models" / "tactile";
  if (const auto man = readJson(dir / "stations.json"); man && man->is_object()) {
    for (auto& [key, v] : man->items()) {
      const BlockId id = reg().blockId(key);
      if (!id || !v.is_object()) continue;
      if (!tactilePorted(key)) continue;   // (a block is drawn from its Tactile model once its port is whole: docs/TACTILE_PORT.md)
      StationModel sm;
      auto model = [&](const std::string& name) -> std::shared_ptr<PackModel> {
        auto it = S->models.find(name);
        if (it != S->models.end()) return it->second;
        auto pm = std::make_shared<PackModel>();
        if (const auto mj = readJson(dir / "models" / (name + ".json"))) *pm = parseBlockModel(*mj, [&](const std::string& ref) { return localTile(ts, dir, ref); });
        return S->models[name] = pm;
      };
      if (v.contains("model") && v["model"].is_array()) for (int k = 0; k < 4 && k < (int)v["model"].size(); k++) sm.models[k] = model(v["model"][k].get<std::string>());
      else if (v.contains("model") && v["model"].is_string()) for (int k = 0; k < 4; k++) sm.models[k] = model(v["model"].get<std::string>());
      if (!sm.models[0] || sm.models[0]->boxes.empty()) continue;
      sm.model = *sm.models[0];
      if (key == "chest") sm.closed = model("coffre_full");   // (shut: the whole chest; open: render/tactile_chests.cpp swings its lid)
      if (key == "ender_chest") sm.closed = model("coffre_ender_full");
      { const BlockDef& bd = reg().block(id);
        sm.facingKind = bd.props.contains("tex") && bd.props["tex"].is_object() && bd.props["tex"].contains("front") ? 1 : bd.has("facingModel") ? 2 : 0; }
      if (v.contains("y") && v["y"].is_array()) for (int k = 0; k < 4 && k < (int)v["y"].size(); k++) sm.turn[k] = ((v["y"][k].get<int>() / 90) % 4 + 4) % 4;
      if ((size_t)id >= S->has.size()) S->has.resize((size_t)id + 1, 0);
      S->has[id] = 1;
      S->byId[id] = std::move(sm);
    }
  }
  if (!S->byId.empty()) {   // the parts drawn on their own (doors, lids, the wheel, the arm): every model of the import
    std::error_code ec;
    for (auto& f : fs::directory_iterator(dir / "models", ec)) {
      const std::string name = f.path().stem().string();
      if (f.path().extension() != ".json" || S->models.count(name)) continue;
      auto pm = std::make_shared<PackModel>();
      if (const auto mj = readJson(f.path())) *pm = parseBlockModel(*mj, [&](const std::string& ref) { return localTile(ts, dir, ref); });
      S->models[name] = pm;
    }
  }
  if (!S->byId.empty()) DQ_LOG("stations: %zu drawn from local models (data/local_models/tactile)", S->byId.size());
  g_keep.push_back(S);
  g_st.store(S.get());
}

const StationModel* stationModel(BlockId id) {
  const Stations* s = g_st.load(std::memory_order_acquire);
  if (!s || (size_t)id >= s->has.size() || !s->has[id]) return nullptr;
  auto it = s->byId.find(id);
  return it == s->byId.end() ? nullptr : &it->second;
}
bool stationTopAt(BlockId id, u8 meta, vec2 xz, float& top) {
  const StationModel* SM = stationModel(id);
  if (!SM) return false;
  // into the model's own frame: the mesher's turn (clockwise from above, about 8,8) undone
  vec3 o(xz.x * 16, 64, xz.y * 16);
  const int q = SM->turn[meta & 3];
  for (int k = 0; k < (4 - q) % 4; k++) { const float dx = o.x - 8, dz = o.z - 8; o.x = 8 - dz; o.z = 8 + dx; }
  bool any = false; float best = -1e9f;
  for (const PackModelBox& b : SM->model.boxes) {
    // the ray (straight down) into the box's frame: its rotation undone about its origin
    vec3 ro = o - b.rotOrigin, rd(0, -1, 0);
    if (b.rotAxis >= 0 && b.rotAngle != 0) {
      const float r = -glm::radians(b.rotAngle), cs = std::cos(r), sn = std::sin(r);
      auto rot = [&](vec3 d) {
        if (b.rotAxis == 0) return vec3(d.x, d.y * cs - d.z * sn, d.y * sn + d.z * cs);
        if (b.rotAxis == 1) return vec3(d.x * cs + d.z * sn, d.y, -d.x * sn + d.z * cs);
        return vec3(d.x * cs - d.y * sn, d.x * sn + d.y * cs, d.z);
      };
      ro = rot(ro); rd = rot(rd);
    }
    ro += b.rotOrigin;
    float t0 = -1e9f, t1 = 1e9f; bool miss = false;
    for (int a = 0; a < 3 && !miss; a++) {
      const float lo = std::min(b.from[a], b.to[a]), hi = std::max(b.from[a], b.to[a]);
      if (std::fabs(rd[a]) < 1e-6f) { if (ro[a] < lo || ro[a] > hi) miss = true; continue; }
      float ta = (lo - ro[a]) / rd[a], tb = (hi - ro[a]) / rd[a]; if (ta > tb) std::swap(ta, tb);
      t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    }
    if (miss || t0 > t1 || t1 < 0) continue;
    if (b.to.x - b.from.x < 1.5f && b.to.z - b.from.z < 1.5f) continue;   // (a thin upright: a tool's handle, not a place to lay things)
    any = true; best = std::max(best, 64 - t0);
  }
  if (any) top = best / 16.f;
  return any;
}
vec3 pairLieOnModel(World& w, ivec3 at, int slot) {
  vec3 p = pairLiePos(w, at, slot);
  float top;
  if (stationTopAt(w.getBlock(at.x, at.y, at.z), w.getMeta(at.x, at.y, at.z), vec2(p.x - at.x, p.z - at.z), top)) p.y = at.y + top + 0.004f;
  return p;
}
bool stationModelOpen(BlockId id) {
  const Stations* s = g_st.load(std::memory_order_acquire);
  return s && (size_t)id < s->has.size() && s->has[id];
}

} // namespace dq
