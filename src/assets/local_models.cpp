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

// Tactile's chest picture in one of our chests' own colour (its paint, the party chest's gold, the ender chest's dark): the
// wood keeps Tactile's grain and shading, tinted to the colour of our picture's painted part (its coloured pixels, the
// metal bands and black edges left out); the grey metal and the latch kept
void recolorWood(std::vector<u8>& px, const TileSet& ts, int ourTile) {
  if (ourTile < 0 || ourTile >= ts.count) return;
  const int T = ts.size;
  const u8* src = ts.tile(ourTile);
  auto lum = [](const u8* p) { return 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2]; };
  auto sat = [](const u8* p) { return std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]}); };
  vec3 sum(0); int n = 0;
  for (int pass = 0; pass < 2 && n < T * T / 8; pass++) {   // (the coloured pixels; a grey or black paint: all but the extremes)
    sum = vec3(0); n = 0;
    for (int i = 0; i < T * T; i++) {
      const u8* q = src + i * 4;
      if (q[3] < 128) continue;
      const float L = lum(q);
      if (pass == 0 ? sat(q) < 30 : (L < 12 || L > 245)) continue;
      sum += vec3(q[0], q[1], q[2]); n++;
    }
  }
  if (!n) return;
  const vec3 paint = sum / (float)n;
  auto woody = [](const u8* p) { const int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]}); return p[3] > 128 && mx - mn > 18 && p[0] >= p[2]; };
  float mean = 0; int m = 0;
  for (size_t i = 0; i < px.size(); i += 4) if (woody(&px[i])) { mean += lum(&px[i]); m++; }
  if (!m) return;
  mean /= m;
  for (size_t i = 0; i < px.size(); i += 4) {
    if (!woody(&px[i])) continue;
    const float k = lum(&px[i]) / mean;   // (Tactile's grain, light and dark, over the paint)
    for (int c = 0; c < 3; c++) px[i + c] = (u8)std::clamp((int)std::lround(paint[c] * k), 0, 255);
  }
}

// a picture from the import's folder as a tile (nearest: the pixels stay crisp), once; recolor: one of our tiles whose wood
// it takes (Tactile's chest in each of our chests' own colours)
int localTile(TileSet& ts, const fs::path& dir, const std::string& ref, int recolor = -1) {
  std::string r = ref;
  for (const char* p : {"minecraft:", "block/"}) if (r.rfind(p, 0) == 0) r = r.substr(std::string(p).size());
  if (r.rfind("tactile:block/", 0) == 0) {
    const std::string name = r.substr(14), key = "tactile:" + name + (recolor >= 0 && name == "coffre" ? "@" + std::to_string(recolor) : "");
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
    if (recolor >= 0 && name == "coffre") recolorWood(out, ts, recolor);
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
  if (key.size() > 13 && key.compare(key.size() - 13, 13, "painted_chest") == 0) return true;
  if (key == "ender_chest" || key == "party_chest" || key == "jukebox" || key == "enchanting_table") return true;   // (every painted chest: Tactile's in its paint)
  static const std::unordered_set<std::string> DONE = {"smithing_table", "cartography_table", "furnace", "lit_furnace", "smoker", "lit_smoker", "blast_furnace", "lit_blast_furnace", "anvil", "chipped_anvil", "damaged_anvil", "grindstone", "chest", "brewing_stand"};
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
      // a chest: Tactile's model in its own wood (its side picture's shades: tools/port/tactile_import.py maps every chest here)
      const BlockDef& bdef = reg().block(id);
      // a chest: Tactile's model wearing the chest's own pictures (the pack's, as its icon shows them): every face Tactile
      // painted from its chest picture takes our top, front or side, mapped over the block as a cube's faces are (the lock
      // where the front's lock is); the lid's buttons and their marks kept
      const bool chest = v.value("state", std::string()) == "coffre";
      int tTop = -1, tSide = -1, tFront = -1;
      if (chest && bdef.props.contains("tex") && bdef.props["tex"].is_object()) {
        auto tile = [&](const char* k) { auto it = ts.index.find(bdef.props["tex"].value(k, std::string())); return it == ts.index.end() ? -1 : it->second; };
        tTop = tile("top"); tSide = tile("side"); tFront = tile("front");
        if (tFront < 0) tFront = tSide;
      }
      const int coffreTile = chest ? localTile(ts, dir, "tactile:block/coffre") : -1;
      auto model = [&](const std::string& name) -> std::shared_ptr<PackModel> {
        const std::string ck = chest && tSide >= 0 ? name + "@" + key : name;
        auto it = S->models.find(ck);
        if (it != S->models.end()) return it->second;
        auto pm = std::make_shared<PackModel>();
        if (const auto mj = readJson(dir / "models" / (name + ".json"))) *pm = parseBlockModel(*mj, [&](const std::string& ref) { return localTile(ts, dir, ref); });
        if (chest && tSide >= 0 && tTop >= 0) for (PackModelBox& bx : pm->boxes) for (int f = 0; f < 6; f++) {
          PackModelFace& fc = bx.face[f];
          if (fc.tile != coffreTile) continue;
          // our picture over the block (a cube's mapping: x across, y down; the lid's, as it lies shut)
          const vec3 lo = glm::min(bx.from, bx.to), hi = glm::max(bx.from, bx.to);
          if (f < 2) fc.uv = f == 0 ? vec4(16 - hi.x, 16 - hi.y, 16 - lo.x, 16 - lo.y) : vec4(lo.x, 16 - hi.y, hi.x, 16 - lo.y);
          else if (f < 4) fc.uv = f == 2 ? vec4(lo.z, 16 - hi.y, hi.z, 16 - lo.y) : vec4(16 - hi.z, 16 - hi.y, 16 - lo.z, 16 - lo.y);
          else fc.uv = vec4(lo.x, lo.z, hi.x, hi.z);
          fc.uv = glm::clamp(fc.uv, vec4(0), vec4(16));
          fc.rot = 0;
          fc.tile = f >= 4 ? tTop : f == 0 && lo.z < 1.5f ? tFront : tSide;   // (the front picture on the chest's front, not inside the back wall)
        }
        return S->models[ck] = pm;
      };
      if (v.value("state", std::string()) == "coffre") for (const char* part : {"coffre_lid", "coffre_buttontri", "coffre_buttonswap"}) sm.parts[part] = model(part);
      if (v.contains("model") && v["model"].is_array()) for (int k = 0; k < 4 && k < (int)v["model"].size(); k++) sm.models[k] = model(v["model"][k].get<std::string>());
      else if (v.contains("model") && v["model"].is_string()) for (int k = 0; k < 4; k++) sm.models[k] = model(v["model"].get<std::string>());
      if (!sm.models[0] || sm.models[0]->boxes.empty()) continue;
      sm.model = *sm.models[0];
      if (v.value("state", std::string()) == "coffre") sm.closed = model("coffre_full");   // (shut: the whole chest; open: render/tactile_chests.cpp swings its lid)
      { const BlockDef& bd = reg().block(id);
        sm.facingKind = bd.props.contains("tex") && bd.props["tex"].is_object() && bd.props["tex"].contains("front") ? 1 : bd.has("facingModel") ? 2 : 0;
        sm.faceBack = key == "brewing_stand" || key == "enchanting_table"; }
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
